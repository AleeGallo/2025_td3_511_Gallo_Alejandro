#include <stdio.h>
#include <string.h>
#include <math.h>
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
#include "mcp4725.h"

/* ----------------- CONSTANTES ---------------- */

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT       10000
#define PIN_LCD_SDA     20      // GP20 (pin 26 físico)
#define PIN_LCD_SCL     21      // GP21 (pin 27 físico)
#define PIN_RTC_SDA     20
#define PIN_RTC_SCL     21  
#define PIN_ENC_CLK     12      // GP12 (pin 16 físico)
#define PIN_ENC_DT      11      // GP11 (pin 15 físico)
#define PIN_ENC_SW      10      // GP10 (pin 14 físico)
#define PIN_BTN_CONFIG  9
#define PIN_LED_MAX     8
#define PIN_LED_MIN     7

#define LCD_DIR     0x27
#define RTC_DIR     0x68
#define EEPROM_DIR  0x57
#define DAC_DIR     0x60

// Constantes del sistema
#define R_SHUNT     0.1f    // Ohms
#define ADC_MAX     4095.0f // Resolución ADC de 12 bits
#define VREF        3.3f    // Voltaje de referencia ADC

#define DEBOUNCE_US 7000    // Tiempo antirrebote en microsegundos

/* ----------------- VARIABLES Y ESTRUCTURAS ----------------- */

// Variables de antirrebote
volatile uint64_t last_clk_time = 0;
volatile uint64_t last_sw_time = 0;
volatile bool last_clk_state = 1;  // estado anterior de CLK (Encoder)
volatile bool last_sw_state = 1;   // estado anterior de SW (Encoder)
static uint64_t last_config_time = 0;
static bool last_config_state = 1;  // Se asume que el botón está en reposo (pull-up -> alto)

ds3231_rtc_t rtc;       // Variable global del RTC
ds3231_datetime_t dt;   // Datetime global del RTC
mcp4725_t dac;

// PID variables
float Kp = 1.0f, Ki = 0.1f, Kd = 0.05f;
float setpoint_R = 10.0f; // Resistencia deseada en Ohms


typedef struct {
    char textoLCD[4][20];
} lcd_data_t;

typedef struct {
    eeprom_data_type_t tipo_dato;    // Setpoint o Alarma
    eeprom_data_id_t id;  // Vmax, Imax, R1-R10, etc.
    ds3231_datetime_t timestamp;     // Fecha y hora del evento
    float valor;                     // Valor del setpoint o de la alarma
} eeprom_data_t;

typedef struct {
    float Vin;      // Voltaje en la carga
    float Vshunt;   // Voltaje en la resistencia shunt
    float Iload;    // Corriente en la carga
} sensado_data_t;

/*------------- COLAS Y SEMAFOROS  -------------*/

//QueueHandle_t Queue_Setpoints;
QueueHandle_t Queue_EEPROM;
QueueHandle_t Queue_EscribirLCD;
QueueHandle_t Queue_Sensado;
QueueHandle_t Queue_DAC;
SemaphoreHandle_t Sem_Bin_Select_Mas, Sem_Bin_Select_Menos, Sem_Bin_OK;     // Tiene que ser externo?
SemaphoreHandle_t Sem_Bin_Config, Sem_Bin_Memory, Sem_Bin_ReadyToRead;      // Tiene que ser externo?
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

    if (gpio == PIN_BTN_CONFIG) {
        uint64_t now = time_us_64();
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        bool config = gpio_get(PIN_BTN_CONFIG);  // Leer estado actual

        // Detectar flanco de bajada con antirrebote
        if (!config && last_config_state && (now - last_config_time) > 20000) {
            last_config_time = now;
            xSemaphoreGiveFromISR(Sem_Bin_Config, &xHigherPriorityTaskWoken);
        }

        last_config_state = config;
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

void mostrar_ultimos_setpoints(void) {
    lcd_data_t lcd_buffer;
    eeprom_data_t dato;
    uint8_t buffer[sizeof(eeprom_data_t)];
    uint16_t addr = EEPROM_ADDR_SETPOINTS;

    for (int i = 0; i < 12; i++) {
        eeprom_read_data(i2c_default, addr, buffer, sizeof(buffer));
        memcpy(&dato, buffer, sizeof(dato));

        // Mostrar en LCD
        snprintf(lcd_buffer.textoLCD[0], 20, "ID: %d Tipo: %d", dato.id, dato.tipo_dato);
        snprintf(lcd_buffer.textoLCD[1], 20, "Valor: %.2f", dato.valor);
        snprintf(lcd_buffer.textoLCD[2], 20, "%02d/%02d/%04d", dato.timestamp.day, dato.timestamp.month, dato.timestamp.year);
        snprintf(lcd_buffer.textoLCD[3], 20, "%02d:%02d:%02d", dato.timestamp.hour, dato.timestamp.minutes, dato.timestamp.seconds);

        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 segundos por pantalla

        addr += sizeof(eeprom_data_t);
    }
}
/* 
void mostrar_dato_eeprom(uint16_t addr) {
    lcd_data_t lcd_buffer;
    eeprom_data_t dato;
    uint8_t buffer[sizeof(eeprom_data_t)];

    // Leer dato desde EEPROM
    if (xSemaphoreTake(Sem_I2C0_Mutex, portMAX_DELAY) == pdTRUE) {
        eeprom_read_data(i2c_default, addr, buffer, sizeof(buffer));
        xSemaphoreGive(Sem_I2C0_Mutex); // Liberar el mutex
    } 
    memcpy(&dato, buffer, sizeof(dato));

    // Preparar texto para LCD
    snprintf(lcd_buffer.textoLCD[0], 20, "ID: %d Tipo: %d", dato.id, dato.tipo_dato);
    snprintf(lcd_buffer.textoLCD[1], 20, "Valor: %.2f", dato.valor);
    snprintf(lcd_buffer.textoLCD[2], 20, "%02d/%02d/%04d", dato.timestamp.day, dato.timestamp.month, dato.timestamp.year);
    snprintf(lcd_buffer.textoLCD[3], 20, "%02d:%02d:%02d", dato.timestamp.hour, dato.timestamp.minutes, dato.timestamp.seconds);

    // Enviar a cola para mostrar en LCD
    xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);
} */

/*------------- TAREAS -------------*/

void task_LCD (void *params) {
    lcd_data_t data_print_LCD;
    lcd_data_t prev_data = {0}; // Inicializo vacío
    while (1) {
        // Recibo colas con estructura
        if (xQueueReceive(Queue_EscribirLCD, &data_print_LCD, portMAX_DELAY) == pdTRUE) {
             
            // Toma el mutex antes de escribir en I2C (LCD)
            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                for (int i = 0; i < 4; i++) {
                    if (strncmp(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 20) != 0) {
                        lcd_set_cursor(i, 0);
                        lcd_string(data_print_LCD.textoLCD[i]);
                        strncpy(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 20);
                    }
                    xSemaphoreGive(Sem_I2C0_Mutex);  // ✅ Liberar el mutex
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // pequeña pausa para evitar saturar
    }
}

void task_Sensado(void *pvParameters) {
    sensado_data_t lectura;

    while(1) {
        // Leer ADC Vsens
        //uint16_t adc_vin = adc_read_channel(0); // Ejemplo canal 0
        uint16_t adc_vin;
        lectura.Vin = (adc_vin / ADC_MAX) * VREF;

        // Leer ADC Vin
        //uint16_t adc_vshunt = adc_read_channel(1); // Ejemplo canal 1
        uint16_t adc_vshunt;
        lectura.Vshunt = (adc_vshunt / ADC_MAX) * VREF;

        // Calcular corriente
        lectura.Iload = lectura.Vshunt / R_SHUNT;

        // Mandar datos al controlador
        xQueueSend(Queue_Sensado, &lectura, 0);

        vTaskDelay(pdMS_TO_TICKS(100)); // Cada 100 ms --> Determina cada cuanto se va a leer ADC
    }
}

// REVISAR
/* void task_Control(void *pvParameters) {
    sensado_data_t datos;
    float pwm_out;
    float error, prev_error = 0, integral = 0;

    for(;;) {
        if (xQueueReceive(Queue_Sensado, &datos, portMAX_DELAY) == pdTRUE) {
            // Calcular resistencia real
            float R_actual = (datos.Iload > 0.001f) ? datos.Vin / datos.Iload : 9999.0f;

            // Calcular error
            error = setpoint_R - R_actual;
            integral += error;
            float derivative = error - prev_error;

            // Control PID
            float control = (Kp * error) + (Ki * integral) + (Kd * derivative);

            // Saturar duty
            if (control > 100.0f)   control = 100.0f;
            if (control < 0.0f)     control = 0.0f;

            pwm_out = control;

            // Enviar a PWM
            xQueueSend(Queue_DAC, &pwm_out, 0);

            prev_error = error;
        }
    }
} */

void task_DAC(void *pvParameters) {
    float dac_in;

    while(1) {
        if (xQueueReceive(Queue_DAC, &dac_in, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(Sem_I2C0_Mutex, portMAX_DELAY);
            mcp4725_set_voltage(&dac, dac_in, MCP4725_RegisterMode, MCP4725_PowerDown_OFF);
            xSemaphoreGive(Sem_I2C0_Mutex);
        }
    }
}

void task_Config(void *params) {
    char linea_aux[4][20];
    lcd_data_t lcd_buffer;
    int resistencia_idx = 0;       // de 0 a 9 (RESISTENCIA 1 a 10)
    int V_max_mV = 0;              // Vmax en decenas de milivoltios (ej: 123 = 12.3V)
    int I_max_mA = 0;
    int V_min_mV = 0;              // Vmax en decenas de milivoltios (ej: 123 = 12.3V)
    int I_min_mA = 0;
    int tiempo_seg = 0;
    int valores[10] = {0};         // cada resistencia tiene un valor (6 dígitos)
    int digit_selected = 0;        // índice del dígito actual (0 = unidades, 5 = centenas de mil)
    const int potencias[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    //const int potencias_V[2] = {1, 10}; // para incrementar en 0.1 V y 1 V (en decenas de mV)
    //const int potencias_I[3] = {1, 10, 100};

    int pantalla_actual = 0;       // 0=Vmax, 1=Imax, 2=Vmin, 3=Imin, 4=Tiempo, 5–14 = resistencias 1–10
    int last_digit_selected = -1;
    TickType_t last_press_time = 0;
    bool pressed = false;
    TickType_t last_cursor_time = 0;
    bool cursor_visible = false;

    eeprom_data_t dato_eeprom;
    dato_eeprom.tipo_dato = EEPROM_DATA_SETPOINT;

    while (1) {
        
        // Tomo semaforo de estado Configuracion
        if (xSemaphoreTake(Sem_Bin_Config, portMAX_DELAY) == pdTRUE){

            // Tomo semaforo ReadyToRead, trabajando como si fuese un Mutex
            //xSemaphoreTake(Sem_Bin_ReadyToRead, portMAX_DELAY);

          /*   for (int i=1;i<11;i++){
                float valorM;
                valorM = i * 0.5;
                xQueueSend(Queue_DAC, &valorM, portMAX_DELAY);
                vTaskDelay(2000);
            } */

            int res_idx = pantalla_actual - 2;  // numero de resistencia a guardar
            // --- Lógica de armado de pantallas ---
            switch (pantalla_actual) {
                case 0:
                    // Formateo manual V_max_mV a "XX.X V"
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG V MAX");
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%2d.%1d V", V_max_mV/10, V_max_mV%10);
                    //linea_aux[2][0] = '\0';
                    //linea_aux[3][0] = '\0';
                    break;
                    
                case 1:
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG I MAX");
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%03d mA", I_max_mA);
                    //linea_aux[2][0] = '\0';
                    //linea_aux[3][0] = '\0';
                    break;

                case 2:
                    // Formateo manual V_max_mV a "XX.X V"
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG V MIN");
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%2d.%1d V", V_min_mV/10, V_min_mV%10);
                    //linea_aux[2][0] = '\0';
                    //linea_aux[3][0] = '\0';
                    break;

                case 3:
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG I MIN");
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%03d mA", I_min_mA);
                    //linea_aux[2][0] = '\0';
                    //linea_aux[3][0] = '\0';
                    //snprintf(linea_aux[3], sizeof(linea_aux[3]), "PULSAR PARA OK");
                    break;

                 case 4:
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG TIEMPO");
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%03d seg", tiempo_seg);
                    break;

                default:
                    int res_idx = pantalla_actual - 5;
                    snprintf(linea_aux[0], sizeof(linea_aux[0]), "CONFIG  R%2d", res_idx + 1);
                    snprintf(linea_aux[1], sizeof(linea_aux[1]), "%07d  OHM", valores[res_idx]);
                    //linea_aux[2][0] = '\0';
                    //linea_aux[3][0] = '\0';
                    break;
            }
            linea_aux[2][0] = '\0';
            linea_aux[3][0] = '\0';

            // Copio todo a la estructura de LCD
            for (int i=0;i<4;i++){
                snprintf(lcd_buffer.textoLCD[i], 20, "%-20s", linea_aux[i]);
            }
        
            // Envío estructura con las 2 primeras líneas
            xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);


            // Mover cursor físico al dígito seleccionado solo si cambió
            if (digit_selected != last_digit_selected) {
                int col = 0;
                int row = 1;

                if (pantalla_actual == 0 || pantalla_actual == 2) // Vmax: XX.X
                    col = 3 - 2 * digit_selected;  // 0 = décima, 2 = unidad
                else if (pantalla_actual == 1 || pantalla_actual == 3) // Imax: XXX
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
                int factor = potencias[digit_selected];
                if (pantalla_actual == 0) {
                    //int factor = potencias[digit_selected];
                    V_max_mV += factor;
                    if (V_max_mV > 120) V_max_mV = 120; // max 12.0 V  
                } else if (pantalla_actual == 1) {
                    //int factor = potencias[digit_selected];
                    I_max_mA += factor;
                    if (I_max_mA > 250) I_max_mA = 250;
                } else if (pantalla_actual == 2) {
                    //int factor = potencias[digit_selected];
                    V_min_mV += factor;
                    if (V_min_mV > 120) V_min_mV = 120; // max 12.0 V  
                } else if (pantalla_actual == 3) {
                    //int factor = potencias[digit_selected];
                    I_min_mA += factor;
                    if (I_min_mA > 250) I_min_mA = 250;
                } else if (pantalla_actual == 4) {
                    //int factor = potencias[digit_selected];
                    tiempo_seg += factor;
                    if (tiempo_seg > 120) I_min_mA = 120; // 2 minutos
                } else {
                    //int factor = potencias[digit_selected];
                    valores[res_idx] += factor;
                    if (valores[res_idx] > 9999999) valores[res_idx] = 9999999;
                }
            }

            // ◀ Giro antihorario = decrementar
            if (xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE) {
                int factor = potencias[digit_selected];
                if (pantalla_actual == 0) {
                    //int factor = potencias[digit_selected];
                    V_max_mV -= factor;
                    if (V_max_mV < 0) V_max_mV = 0;
                } else if (pantalla_actual == 1) {
                    //int factor = potencias[digit_selected];
                    I_max_mA -= factor;
                    if (I_max_mA < 0) I_max_mA = 0;
                }else if (pantalla_actual == 2) {
                    //int factor = potencias[digit_selected];
                    V_min_mV -= factor;
                    if (V_min_mV < 0) V_min_mV = 0; // max 12.0 V  
                } else if (pantalla_actual == 3) {
                    //int factor = potencias[digit_selected];
                    I_min_mA -= factor;
                    if (I_min_mA < 0) I_min_mA = 0;
                } else if (pantalla_actual == 4) {
                    //int factor = potencias[digit_selected];
                    tiempo_seg -= factor;
                    if (tiempo_seg < 0) I_min_mA = 0; // 2 minutos
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

            // ----> Confirmo valor
            if (gpio_get(PIN_ENC_SW) == 1 && pressed) {
                pressed = false;
                TickType_t elapsed = xTaskGetTickCount() - last_press_time;

                if (elapsed >= pdMS_TO_TICKS(1000)) {
                    // Mantengo apretado, confirmo y paso al siguiente dato
                    if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        ds3231_get_datetime(&dt, &rtc);  // Obtener fecha/hora actual
                        dato_eeprom.timestamp = dt;
                        xSemaphoreGive(Sem_I2C0_Mutex);
                    }

                    switch (pantalla_actual) {
                        case 0:
                            dato_eeprom.id = ID_VMAX;
                            dato_eeprom.valor = V_max_mV / 10.0f;
                            break;
                        case 1:
                            dato_eeprom.id = ID_IMAX;
                            dato_eeprom.valor = (float)I_max_mA;
                            break;
                        case 2:
                            dato_eeprom.id = ID_VMIN;
                            dato_eeprom.valor = V_min_mV / 10.0f;
                            break;
                        case 3:
                            dato_eeprom.id = ID_IMIN;
                            dato_eeprom.valor = (float)I_min_mA;
                            break;
                        case 4:
                            dato_eeprom.id = ID_TIEMPO;
                            dato_eeprom.valor = (float)tiempo_seg;
                            break;
                        default:
                            dato_eeprom.id = ID_R1 + (pantalla_actual - 5);
                            dato_eeprom.valor = (float)valores[pantalla_actual - 5];
                            break;
                    }
                    
                    // Mando cola para guardar el dato y espero un segundo
                    xQueueSend(Queue_EEPROM, &dato_eeprom, portMAX_DELAY);
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    // Si es el último parámetro, mostrar los 12 setpoints guardados
                    if (pantalla_actual == 11) {
                        snprintf(lcd_buffer.textoLCD[0], 20, "CONFIG GUARDADA");
                        lcd_buffer.textoLCD[1][0] = '\0';
                        lcd_buffer.textoLCD[2][0] = '\0';
                        lcd_buffer.textoLCD[3][0] = '\0';
                        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        mostrar_ultimos_setpoints();
                        
                        // Da semaforo para empezar a medir
                        //xSemaphoreGive(Sem_Bin_ReadyToRead);
                    }
                    // Avanza al siguiente parámetro
                    pantalla_actual = (pantalla_actual + 1) % 15;
                    digit_selected = 0;

                } else if (elapsed >= pdMS_TO_TICKS(100)) {
                    if ((pantalla_actual == 0)||(pantalla_actual == 2))         digit_selected = (digit_selected + 1) % 2;
                    else if ((pantalla_actual == 1)||(pantalla_actual == 3))    digit_selected = (digit_selected + 1) % 3;
                    else if (pantalla_actual == 4)      digit_selected = (digit_selected + 1) % 2;
                    else                                digit_selected = (digit_selected + 1) % 7;
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
            xSemaphoreGive(Sem_Bin_Config);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

 void task_EEPROM(void *params) {
    eeprom_data_t dato;
    lcd_data_t lcd_text;
    static uint16_t addr_setpoints = EEPROM_ADDR_SETPOINTS;
    static uint16_t addr_alarmas   = EEPROM_ADDR_ALARMAS;
    static uint16_t addr_lecturas  = EEPROM_ADDR_LECTURAS;

    while (1) {
        //ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)); // Si no llega nada en 5s, se desbloquea
        if (xQueueReceive(Queue_EEPROM, &dato, portMAX_DELAY) == pdTRUE) {
            uint8_t buffer[sizeof(eeprom_data_t)];
            uint8_t buffer_lectura[sizeof(eeprom_data_t)];
            memcpy(buffer, &dato, sizeof(eeprom_data_t));

            uint16_t *addr_ptr = NULL;
            uint16_t addr_max = 0;
            uint16_t addr_min = 0;

            switch (dato.tipo_dato) {
                case EEPROM_DATA_SETPOINT:
                    addr_ptr = &addr_setpoints;
                    addr_min = EEPROM_ADDR_SETPOINTS;
                    addr_max = EEPROM_ADDR_SETPOINTS + EEPROM_SIZE_SETPOINTS;
                    break;

                case EEPROM_DATA_ALARMA:
                    addr_ptr = &addr_alarmas;
                    addr_min = EEPROM_ADDR_ALARMAS;
                    addr_max = EEPROM_ADDR_ALARMAS + EEPROM_SIZE_ALARMAS;
                    break;

                case EEPROM_DATA_LECTURA:
                    addr_ptr = &addr_lecturas;
                    addr_min = EEPROM_ADDR_LECTURAS;
                    addr_max = EEPROM_ADDR_LECTURAS + EEPROM_SIZE_LECTURAS;
                    break;

                default:
                    continue;  // Tipo desconocido, no escribir
            }

            // Reinicio circular por zona
            if (*addr_ptr + sizeof(eeprom_data_t) > addr_max) {
                *addr_ptr = addr_min;  // reiniciar a comienzo de la zona
            }

            // Toma Semaforo Mutex para escribir EEPROM
            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                //Escribe EEPROM
                eeprom_write_data(i2c_default, *addr_ptr, buffer, sizeof(buffer));
                //Lee EEPROM para ver si leyo OK
                eeprom_read_data(i2c_default, *addr_ptr, buffer_lectura, sizeof(buffer));
                // Entrega Semaforo Mutex
                xSemaphoreGive(Sem_I2C0_Mutex);      
                *addr_ptr += sizeof(eeprom_data_t);
            } 
            
            memcpy(&dato, buffer_lectura, sizeof(dato));

            // Armar el contenido a mostrar
            snprintf(lcd_text.textoLCD[0], 20, "GUARDADO - ID: %d  ", dato.id, dato.tipo_dato);
            snprintf(lcd_text.textoLCD[1], 20, "Valor: %.2f", dato.valor);
            snprintf(lcd_text.textoLCD[2], 20, "%02d/%02d/%04d", dato.timestamp.day, dato.timestamp.month, dato.timestamp.year);
            snprintf(lcd_text.textoLCD[3], 20, "%02d:%02d:%02d", dato.timestamp.hour, dato.timestamp.minutes, dato.timestamp.seconds);

            xQueueSend(Queue_EscribirLCD, &lcd_text, portMAX_DELAY);

        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


void task_Init(void *params) {

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

    // Inicialización LEDs
    gpio_init(PIN_LED_MAX);
    gpio_set_dir(PIN_LED_MAX, GPIO_OUT);
    gpio_put(PIN_LED_MAX, 0);  // Apagar al inicio

    gpio_init(PIN_LED_MIN);
    gpio_set_dir(PIN_LED_MIN, GPIO_OUT);
    gpio_put(PIN_LED_MIN, 0);  // Apagar al inicio

    // Inicialización BOTON
    gpio_init(PIN_BTN_CONFIG);
    gpio_set_dir(PIN_BTN_CONFIG, GPIO_IN);
    gpio_pull_up(PIN_BTN_CONFIG);

    // IRQs
    gpio_set_irq_enabled_with_callback(PIN_ENC_CLK, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_ENC_SW, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_BTN_CONFIG, GPIO_IRQ_EDGE_FALL, true);

    // Inicialización I2C a 100KHz
    i2c_init(i2c0, 100000);
    adc_init();
    //pwm_init_channel(0);

    // Inicialización LCD
    gpio_set_function(PIN_LCD_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LCD_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_LCD_SDA);
    gpio_pull_up(PIN_LCD_SCL);
    // Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
    lcd_init(i2c0, LCD_DIR);

    // Inicialización DAC
    //dac.i2c_port = i2c0;
    //dac.i2c_addr = DAC_DIR;

    mcp4725_begin(&dac, DAC_DIR, i2c0, 100, PIN_LCD_SDA, PIN_LCD_SCL, 1000);

    mcp4725_set_reference_voltage(&dac, 5.0);
    mcp4725_set_voltage(&dac, 4.8, MCP4725_RegisterMode, MCP4725_PowerDown_OFF);
    
    // Inicialización RTC */
    //ds3231_init(i2c0, PIN_RTC_SDA, PIN_RTC_SCL, &rtc);
    rtc.i2c_port = i2c0;
    rtc.i2c_addr = DS3231_I2C_ADDRESS;
    ds3231_get_datetime(&dt, &rtc);

    if (dt.year <= 2002){
        dt.seconds = 0;
        dt.minutes = 0;
        dt.hour = 21;
        dt.day = 12;
        dt.month = 8;
        dt.year = 2025;
        dt.dotw = 1;
        ds3231_set_datetime(&dt, &rtc);
    }

    
    // Creación de colas y semaforos
    Queue_EscribirLCD   = xQueueCreate(1, sizeof(lcd_data_t));
    //Queue_Setpoints     = xQueueCreate(1, sizeof(setpoint_data_t));
    Queue_EEPROM        = xQueueCreate(1, sizeof(eeprom_data_t));
    Queue_Sensado       = xQueueCreate(5, sizeof(sensado_data_t));
    Queue_DAC           = xQueueCreate(5, sizeof(float));
    Sem_Bin_Select_Mas   = xSemaphoreCreateBinary();
    Sem_Bin_Select_Menos = xSemaphoreCreateBinary();
    Sem_Bin_OK           = xSemaphoreCreateBinary();
    Sem_Bin_Config       = xSemaphoreCreateBinary();
    Sem_Bin_ReadyToRead  = xSemaphoreCreateBinary();
    Sem_I2C0_Mutex       = xSemaphoreCreateMutex();


    xSemaphoreGive(Sem_Bin_Config);

    // Elimino la tarea para liberar recursos
    vTaskDelete(NULL);
}


int main()
{
    stdio_init_all();
    // Creacion de tareas
    xTaskCreate(task_Init, "Init", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(task_LCD, "LCD", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_Config, "Config", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_EEPROM, "Eeprom", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    // Revisar prioridad y memoria
    xTaskCreate(task_Sensado, "Sensado", 1024, NULL, 2, NULL);
    //xTaskCreate(task_Control, "Control", 1024, NULL, 2, NULL);
    xTaskCreate(task_DAC, "DAC", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}