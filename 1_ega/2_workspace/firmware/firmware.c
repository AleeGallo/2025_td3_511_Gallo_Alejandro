#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lcd.h"
#include "eeprom.h"
#include "ds3231.h"


/* ----------------- CONSTANTES ---------------- */

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000
#define PIN_LCD_SDA 20      // GP20 (pin 26 físico)
#define PIN_LCD_SCL 21      // GP21 (pin 27 físico)

#define PIN_ENC_CLK 12      // GP12 (pin 16 físico)
#define PIN_ENC_DT  11      // GP11 (pin 15 físico)
#define PIN_ENC_SW  10      // GP10 (pin 14 físico)
#define LCD_DIR     0x27
#define RTC_DIR     0x68
#define EEPROM_DIR  0x57

#define DEBOUNCE_US 7000    // Tiempo antirrebote en microsegundos

/* ----------------- VARIABLES Y ESTRUCTURAS ----------------- */

// Variables internas
volatile uint64_t last_clk_time = 0;
volatile uint64_t last_sw_time = 0;

volatile bool last_clk_state = 1;  // estado anterior de CLK (Endoder)
volatile bool last_sw_state = 1;   // estado anterior de SW (Endoder)

ds3231_rtc_t rtc;   // Variable global del RTC

typedef struct {
    char textoLCD[4][20];
} lcd_data_t;


// REVISAR
typedef struct {
    int tipo_Dato;              // Valores de tipo de dato (1.Setpoint / 2.Vmax superado / 3.Imax superado 
                                //                          4.Vmin superado / 5.Imin superado )
    int resistencia_valor[10]; // Valores de las 10 resistencias
    float V_max;               // Tensión máxima
    int I_max;               // Corriente máxima
} setpoint_data_t;

typedef struct {
    eeprom_data_type_t tipo_dato;    // Setpoint o Alarma
    eeprom_data_id_t id;  // Vmax, Imax, R1-R10, etc.
    ds3231_datetime_t timestamp;     // Fecha y hora del evento
    float valor;                     // Valor del setpoint o de la alarma
} eeprom_data_t;

/*------------- COLAS Y SEMAFOROS  -------------*/

QueueHandle_t Queue_EEPROM;
QueueHandle_t Queue_EscribirLCD;
QueueHandle_t Queue_Setpoints;
QueueHandle_t Queue_ADC_Sensado;
SemaphoreHandle_t Sem_Bin_Select_Mas, Sem_Bin_Select_Menos, Sem_Bin_OK;     // Tiene que ser externo?
SemaphoreHandle_t Sem_Bin_Config, Sem_Bin_Memory;                           // Tiene que ser externo?
SemaphoreHandle_t Sem_I2C0_Mutex;

/*------------- INTERRUPCIONES  -------------*/

// Interrupcion de giro encoder
void gpio_callback(uint gpio, uint32_t events) {
    uint64_t now = time_us_64();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (gpio == PIN_ENC_CLK) {
        bool clk = gpio_get(PIN_ENC_CLK);
        bool dt  = gpio_get(PIN_ENC_DT);

        // Detectar flanco y hacer antirrebote por tiempo
        if (clk == 1 && last_clk_state == 0 && (now - last_clk_time) > 3000) {
            last_clk_time = now;

            if (dt != clk) {
                xSemaphoreGiveFromISR(Sem_Bin_Select_Mas, &xHigherPriorityTaskWoken);
            } else {
                xSemaphoreGiveFromISR(Sem_Bin_Select_Menos, &xHigherPriorityTaskWoken);
            }
        }

        last_clk_state = clk;
    }

    if (gpio == PIN_ENC_SW) {
        bool sw = gpio_get(PIN_ENC_SW);

        // Detectar flanco de bajada con antirrebote
        if (sw == 0 && last_sw_state == 1 && (now - last_sw_time) > 10000) {
            last_sw_time = now;
            xSemaphoreGiveFromISR(Sem_Bin_OK, &xHigherPriorityTaskWoken);
        }

        last_sw_state = sw;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}



/*--------------FUNCIONES------------*/

/* void enviar_setpoints_a_eeprom(const setpoint_data_t *setpoints, ds3231_rtc_t *rtc) {
    eeprom_data_t data;

    // Timestamp actual
    ds3231_get_datetime(&data.timestamp, rtc);

    // Vmax
    data.tipo_dato = EEPROM_DATA_SETPOINT;
    data.id = ID_VMAX;
    data.valor = setpoints->Vmax;
    xQueueSend(Queue_EEPROM, &data, portMAX_DELAY);

    // Imax
    data.id = ID_IMAX;
    data.valor = setpoints->Imax;
    xQueueSend(Queue_EEPROM, &data, portMAX_DELAY);

    // R1 a R10
    for (int i = 0; i < 10; i++) {
        data.id = (eeprom_data_id_t)(ID_R1 + i);  // ID_R1, ID_R2, ..., ID_R10
        data.valor = setpoints->R[i];
        xQueueSend(Queue_EEPROM, &data, portMAX_DELAY);
    }
} */


/*------------- TAREAS -------------*/

void task_LCD (void *params) {
    lcd_data_t data_print_LCD;
    lcd_data_t prev_data = {0}; // Inicializo vacío
    while (1) {
        // Recibo colas con estructura
        xQueueReceive (Queue_EscribirLCD, &data_print_LCD, portMAX_DELAY);
        
        for (int i = 0; i < 4; i++) {
            if (strncmp(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 20) != 0) {
                lcd_set_cursor(i, 0);
                lcd_string(data_print_LCD.textoLCD[i]);
                strncpy(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 20);
            }
        }
    }
}

void task_Config(void *params) {
    char linea_aux[4][20];
    lcd_data_t lcd_buffer;
    int resistencia_idx = 0;       // de 0 a 9 (RESISTENCIA 1 a 10)
    int V_max_mV = 0;              // Vmax en decenas de milivoltios (ej: 123 = 12.3V)
    int I_max_mA = 0;
    int valores[10] = {0};         // cada resistencia tiene un valor (6 dígitos)
    int digit_selected = 0;        // índice del dígito actual (0 = unidades, 5 = centenas de mil)
    const int potencias[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    const int potencias_V[2] = {1, 10}; // para incrementar en 0.1 V y 1 V (en decenas de mV)
    const int potencias_I[3] = {1, 10, 100};

    int pantalla_actual = 0;       // 0 = Vmax, 1 = Imax, 2–11 = resistencias 1–10
    int last_digit_selected = -1;
    TickType_t last_press_time = 0;
    bool pressed = false;
    TickType_t last_cursor_time = 0;
    bool cursor_visible = false;

    eeprom_data_t dato_eeprom;
    dato_eeprom.tipo_dato = EEPROM_DATA_SETPOINT;

    while (1) {
        
        // Tomo semaforo de estado Configuracion
        // 


        int res_idx = pantalla_actual - 2;  // numero de resistencia a guardar

        // --- Lógica de armado de pantallas ---
         switch (pantalla_actual) {
            case 0:
                // Formateo manual V_max_mV a "XX.X V"
                int entero = V_max_mV / 10;
                int decimal = V_max_mV % 10;
                snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG TENSION MAX");
                snprintf(linea_aux[1], sizeof(linea_aux[1]), "%2d.%1d V", entero, decimal);
                linea_aux[2][0] = '\0';
                linea_aux[3][0] = '\0';
                break;

            case 1:
                snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG CORRIENTE MAX");
                //snprintf(linea_aux[1], sizeof(linea_aux[1]), "I MAX: %3d mA", I_max_mA);
                snprintf(linea_aux[1], sizeof(linea_aux[1]), "%03d mA", I_max_mA);
                linea_aux[2][0] = '\0';
                linea_aux[3][0] = '\0';
                //snprintf(linea_aux[3], sizeof(linea_aux[3]), "PULSAR PARA OK");
                break;

            default:
                snprintf(linea_aux[0], sizeof(linea_aux[0]), "RESISTENCIA %2d", res_idx + 1);
                snprintf(linea_aux[1], sizeof(linea_aux[1]), "%07d  OHM", valores[res_idx]);
                linea_aux[2][0] = '\0';
                linea_aux[3][0] = '\0';
                break;
        }
        
        
        
        // Copio todo a la estructura de LCD
        for (int i=0;i<4;i++){
            snprintf(lcd_buffer.textoLCD[i], 20, "%-20s", linea_aux[i]);
        }
    
        // Envío estructura con las 2 primeras líneas
        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);


        // 👉 Mover cursor físico al dígito seleccionado solo si cambió
        if (digit_selected != last_digit_selected) {
            int col = 0;
            int row = 1;

            if (pantalla_actual == 0) // Vmax: XX.X
                col = 3 - 2 * digit_selected;  // 0 = décima, 2 = unidad
            else if (pantalla_actual == 1) // Imax: XXX
                col = 2 - digit_selected;  // 0 = unidades, 2 = centenares
            else
                col = 6 - digit_selected;  // Resistencia: 7 dígitos, de derecha a izquierda

            lcd_set_cursor(row, col);
            lcd_show_cursor(true, true);
            cursor_visible = true;
            last_cursor_time = xTaskGetTickCount();
            last_digit_selected = digit_selected;
        }

        // ==============================
        // ▶ Giro horario = incrementar
        if (xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE) {
            if (pantalla_actual == 0) {
                int factor = potencias_V[digit_selected];
                V_max_mV += factor;
                if (V_max_mV > 120) V_max_mV = 120; // max 12.0 V  
            } else if (pantalla_actual == 1) {
                int factor = potencias_I[digit_selected];
                I_max_mA += factor;
                if (I_max_mA > 250) I_max_mA = 250;
            } else {
                int factor = potencias[digit_selected];
                valores[res_idx] += factor;
                if (valores[res_idx] > 9999999) valores[res_idx] = 9999999;
            }
        }

        // ◀ Giro antihorario = decrementar
        if (xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE) {
            if (pantalla_actual == 0) {
                int factor = potencias_V[digit_selected];
                V_max_mV -= factor;
                if (V_max_mV < 0) V_max_mV = 0;
            } else if (pantalla_actual == 1) {
                int factor = potencias_I[digit_selected];
                I_max_mA -= factor;
                if (I_max_mA < 0) I_max_mA = 0;
            } else {
                int factor = potencias[digit_selected];
                valores[res_idx] -= factor;
                if (valores[res_idx] < 0) valores[res_idx] = 0;
            }
        }

        // ==============================
        // 🔘 Pulsación del botón del encoder
        if (gpio_get(PIN_ENC_SW) == 0 && !pressed) {
            pressed = true;
            last_press_time = xTaskGetTickCount();
        }

        if (gpio_get(PIN_ENC_SW) == 1 && pressed) {
            pressed = false;
            TickType_t elapsed = xTaskGetTickCount() - last_press_time;

            if (elapsed >= pdMS_TO_TICKS(1000)) {
                // Mantengo apretado, confirmo y paso al siguiente dato
                ds3231_get_datetime(&dato_eeprom.timestamp, &rtc);  // Obtener fecha/hora actual

                switch (pantalla_actual) {
                    case 0:
                        dato_eeprom.id = ID_VMAX;
                        dato_eeprom.valor = V_max_mV / 10.0f;
                        break;
                    case 1:
                        dato_eeprom.id = ID_IMAX;
                        dato_eeprom.valor = (float)I_max_mA;
                        break;
                    default:
                        dato_eeprom.id = ID_R1 + res_idx;  // ID_R1 debe ser base de ID_R1 a ID_R10
                        dato_eeprom.valor = (float)valores[res_idx];
                        break;
                }

                // Mando cola para guardar el dato
                xQueueSend(Queue_EEPROM, &dato_eeprom, portMAX_DELAY);
                
                pantalla_actual = (pantalla_actual + 1) % 12;
                digit_selected = 0;
            } else if (elapsed >= pdMS_TO_TICKS(100)) {
                if (pantalla_actual == 0)      digit_selected = (digit_selected + 1) % 2;
                else if (pantalla_actual == 1) digit_selected = (digit_selected + 1) % 3;
                else                           digit_selected = (digit_selected + 1) % 7;
            }
        }


    /*  ENVIO DE COLA UNA VEZ SE COMPLETAN LOS 12 CAMPOS. SE ELIMINA PARA ENVIAR DE A 1 DATO A MEDIDA QUE SE CONFIRMA   
        if (pantalla_actual == 11 && pressed && (xTaskGetTickCount() - last_press_time >= pdMS_TO_TICKS(1000))) {
            setpoint_data_t data;
            memcpy(data.resistencia_valor, valores, sizeof(valores));
            data.V_max = V_max_mV / 10.0f;   // Convertir a float para enviar
            data.I_max = I_max_mA;

            xQueueSend(Queue_Setpoints, &data, portMAX_DELAY);
        } */

        /* APAGADO DE CURSOR 
        if (cursor_visible && (xTaskGetTickCount() - last_cursor_time > pdMS_TO_TICKS(2000))) {
            lcd_show_cursor(false, false);
            cursor_visible = false;
        }
 */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_EEPROM_RTC(void *params) {
    setpoint_data_t received_data;
    lcd_data_t lcd_buffer;
    static bool recibido = false;
    static int idx_mostrar = 0;
    TickType_t last_update = 0;

    while (1) {
        // Esperar estructura completa de los 12 bloques
        if (!recibido && xQueueReceive(Queue_Setpoints, &received_data, portMAX_DELAY) == pdTRUE) {
            recibido = true;
            idx_mostrar = 0;
            last_update = xTaskGetTickCount();
        }

        // Si ya recibimos, mostramos los 12 valores de a uno cada 2 segundos
        if (recibido && (xTaskGetTickCount() - last_update >= pdMS_TO_TICKS(2000))) {
            last_update = xTaskGetTickCount();

            if (idx_mostrar == 0) {
                int V_entero = (int)(received_data.V_max * 10.0f);
                int V_int = V_entero / 10;
                int V_dec = V_entero % 10;

                snprintf(lcd_buffer.textoLCD[0], 20, "VALORES MAXIMOS");
                lcd_buffer.textoLCD[1][0] = '\0';
                snprintf(lcd_buffer.textoLCD[2], 20, "V_MAX = %2d.%1d V", V_int, V_dec);
                snprintf(lcd_buffer.textoLCD[3], 20, "I_MAX = %3d mA", (int)(received_data.I_max * 1000.0f));
            }
            else if (idx_mostrar >= 1 && idx_mostrar <= 10) {
                int r_idx = idx_mostrar - 1;

                snprintf(lcd_buffer.textoLCD[0], 20, "VALORES RESISTENCIA");
                lcd_buffer.textoLCD[1][0] = '\0';
                snprintf(lcd_buffer.textoLCD[2], 20, "RESISTENCIA %02d", r_idx + 1);
                snprintf(lcd_buffer.textoLCD[3], 20, "VALOR = %07d OHM", received_data.resistencia_valor[r_idx]);
            }
            else if (idx_mostrar == 11) {
                snprintf(lcd_buffer.textoLCD[0], 20, "CONFIG GUARDADA");
                lcd_buffer.textoLCD[1][0] = '\0';
                lcd_buffer.textoLCD[2][0] = '\0';
                lcd_buffer.textoLCD[3][0] = '\0';
            }

            xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);
            idx_mostrar++;

            if (idx_mostrar > 11) {
                recibido = false;  // Terminamos, esperamos nuevo set
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void task_Init(void *params) {

    lcd_data_t info_LCD;
    // Inicializacion de GPIO
    // Inicialización ENCODER
    // CLK
    gpio_init(PIN_ENC_CLK);
    gpio_set_dir(PIN_ENC_CLK, GPIO_IN);
    gpio_pull_up(PIN_ENC_CLK);
    // DT
    gpio_init(PIN_ENC_DT);
    gpio_set_dir(PIN_ENC_DT, GPIO_IN);
    gpio_pull_up(PIN_ENC_DT);
    // SW
    gpio_init(PIN_ENC_SW);
    gpio_set_dir(PIN_ENC_SW, GPIO_IN);
    gpio_pull_up(PIN_ENC_SW);

    // IRQs
    gpio_set_irq_enabled_with_callback(PIN_ENC_CLK, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_ENC_SW, GPIO_IRQ_EDGE_FALL, true);

    // Inicialización I2C a 100KHz
    i2c_init(i2c0, 100000);
    // Configurar los pines con función I2C
    gpio_set_function(PIN_LCD_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LCD_SCL, GPIO_FUNC_I2C);
    //Resistencias de pull up
    gpio_pull_up(PIN_LCD_SDA);
    gpio_pull_up(PIN_LCD_SCL);


    // Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
    lcd_init(i2c0, LCD_DIR);
    // Limpia la pantalla
    lcd_clear();
    
    // Definición del símbolo Ω en slot 0
  /*   uint8_t ohm_char[8] = {
        0b00000,  
        0b01110, 
        0b10001,  
        0b10001,  
        0b10001,  
        0b01010,  
        0b01010,   
        0b11011    
    };
    lcd_create_char(0, ohm_char); */

    // Imprimo mensaje fijo
    lcd_set_cursor(0, 0);
    
    // Creación de colas y semaforos
    Queue_EscribirLCD = xQueueCreate(1, sizeof(lcd_data_t));
    Queue_Setpoints = xQueueCreate(1, sizeof(setpoint_data_t));
    Queue_EEPROM = xQueueCreate(12, sizeof(eeprom_data_t));
    Sem_Bin_Select_Mas   = xSemaphoreCreateBinary();
    Sem_Bin_Select_Menos = xSemaphoreCreateBinary();
    Sem_Bin_OK           = xSemaphoreCreateBinary();

// -----------------  PRUEBA DE ENVIO DE DATOS ------------------- 
// Envio la cola
/*
    snprintf(info_LCD.textoLCD[0], 20, "PRUEBA 1");
    snprintf(info_LCD.textoLCD[1], 20, "PRUEBA 2");
    snprintf(info_LCD.textoLCD[2], 20, "PRUEBA 3");
    snprintf(info_LCD.textoLCD[3], 20, "PRUEBA 4");
    xQueueSend (Queue_EscribirLCD, &info_LCD, portMAX_DELAY);
*/
// -----------------------------------------------------------------

    // Elimino la tarea para liberar recursos
    vTaskDelete(NULL);
}





int main()
{
    stdio_init_all();

    /* Creacion de tareas */ 
    xTaskCreate(task_Init, "Init", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(task_LCD, "LCD", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_Config, "Config", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_EEPROM_RTC, "Eeprom", 2 * configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    //xTaskCreate(task_polling, "Polling", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}