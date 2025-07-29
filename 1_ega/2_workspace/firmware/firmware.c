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

/* ----------------- CONSTANTES ---------------- */

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000
#define PIN_LCD_SDA 20      // GP20 (pin 26 físico)
#define PIN_LCD_SCL 21      // GP21 (pin 27 físico)

#define PIN_ENC_CLK 12      // GP12 (pin 16 físico)
#define PIN_ENC_DT  11      // GP11 (pin 15 físico)
#define PIN_ENC_SW  10      // GP10 (pin 14 físico)
#define LCD_DIR 0x27


#define DEBOUNCE_US 7000    // Tiempo antirrebote en microsegundos

/* ----------------- VARIABLES Y ESTRUCTURAS ----------------- */

// Variables internas
volatile uint64_t last_clk_time = 0;
volatile uint64_t last_sw_time = 0;

volatile bool last_clk_state = 1;  // estado anterior de CLK
volatile bool last_sw_state = 1;   // estado anterior de SW

typedef struct {
    char textoLCD[4][20];
} lcd_data_t;

/*------------- COLAS Y SEMAFOROS  -------------*/

QueueHandle_t Queue_EscribirLCD;
QueueHandle_t Queue_Setpoints;
QueueHandle_t Queue_ADC_Sensado;
SemaphoreHandle_t Sem_Bin_Select_Mas, Sem_Bin_Select_Menos, Sem_Bin_OK;     // Tiene que ser externo?
SemaphoreHandle_t Sem_Bin_Config, Sem_Bin_Memory;                           // Tiene que ser externo?


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
    char linea1[20];
    char linea2[20];
    lcd_data_t lcd_buffer;
    int resistencia_idx = 0;       // de 0 a 9 (RESISTENCIA 1 a 10)
    int valores[10] = {0};         // cada resistencia tiene un valor (6 dígitos)
    int digit_selected = 0;        // índice del dígito actual (0 = unidades, 5 = centenas de mil)
    const int potencias[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};

    TickType_t last_press_time = 0;
    bool pressed = false;
    int last_digit_selected = -1;

    TickType_t last_cursor_time = 0;
    bool cursor_visible = false;

    while (1) {
        // ==============================
        // Armo la línea 1: nombre
        snprintf(linea1, sizeof(linea1), "RESISTENCIA %2d", resistencia_idx + 1);

        // Armo la línea 2: valor con formato
        char valor_str[13];  // 7 dígitos + null
        sprintf(valor_str, "%07d  OHM", valores[resistencia_idx]);
        strncpy(linea2, valor_str, 12);
        linea2[12] = '\0';

        // Copio todo a la estructura de LCD
        snprintf(lcd_buffer.textoLCD[0], 20, "%-20s", linea1);
        snprintf(lcd_buffer.textoLCD[1], 20, "%-20s", linea2);
        lcd_buffer.textoLCD[2][0] = '\0';
        lcd_buffer.textoLCD[3][0] = '\0';
        // Envío estructura con las 2 primeras líneas
        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);

        // Después de que el LCD imprima el texto, colocamos el símbolo y el texto restante
      /*   int ohm_col = strlen(lcd_buffer.textoLCD[1]);
        lcd_set_cursor(1, ohm_col);
        lcd_put_custom_char(0);  // Muestra símbolo Ω */

        // 👉 Mover cursor físico al dígito seleccionado solo si cambió
        if (digit_selected != last_digit_selected) {
            int col = 6 - digit_selected; // de derecha a izquierda
            lcd_set_cursor(1, col);
            lcd_show_cursor(true, true);
            cursor_visible = true;
            last_cursor_time = xTaskGetTickCount();
            last_digit_selected = digit_selected;
        }

        // ==============================
        // ▶ Giro horario = incrementar
        if (xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE) {
            int factor = potencias[digit_selected];
            valores[resistencia_idx] += factor;
            if (valores[resistencia_idx] > 9999999)
                valores[resistencia_idx] = 9999999;
        }

        // ◀ Giro antihorario = decrementar
        if (xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE) {
            int factor = potencias[digit_selected];
            valores[resistencia_idx] -= factor;
            if (valores[resistencia_idx] < 0)
                valores[resistencia_idx] = 0;
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
                resistencia_idx = (resistencia_idx + 1) % 10;
                digit_selected = 0;
            } else if (elapsed >= pdMS_TO_TICKS(100)) {
                digit_selected = (digit_selected + 1) % 6;
            }
        }

        /* if (cursor_visible && (xTaskGetTickCount() - last_cursor_time > pdMS_TO_TICKS(2000))) {
            lcd_show_cursor(false, false);
            cursor_visible = false;
        }
 */
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
    //xTaskCreate(task_polling, "Polling", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}