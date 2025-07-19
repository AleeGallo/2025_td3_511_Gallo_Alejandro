
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "rtc.h"   // Librería del RTC DS3231
#include "lcd.h"  // Librería del LCD 4x20

// Semáforos
SemaphoreHandle_t sem_bin_config;   // Semaforo para la configuracion
SemaphoreHandle_t sem_bin_memoria;  // Semaforo para guardar la memoria
SemaphoreHandle_t sem_bin_readyToRead;
SemaphoreHandle_t sem_i2c0_mutex;   // Semaforo mutex para manejar el I2C

// Colas
QueueHandle_t queue_escribirLCD;    // Cola de datos para LCD
QueueHandle_t queue_seqPoints;      // Cola de los SetPoints
QueueHandle_t queue_adcSensado;     // Cola de valores sensados por el ADC
QueueHandle_t queue_pwm;            // Cola para el PWM de salida.

void vTask_Program_Init(void *pvParameters) 
{
    // Inicializar GPIOs

    gpio_init(9); // LED MIN
    gpio_set_dir(9, GPIO_OUT);

    gpio_init(10); // LED MAX
    gpio_set_dir(10, GPIO_OUT);

    gpio_init(11); // BOT 1 
    gpio_set_dir(11, GPIO_IN);
    gpio_pull_down(PIN_ENT_SIGNAL);          //  HABILITO RESISTENCIA PULL DOWN

    gpio_init(12); // BOT 2
    gpio_set_dir(12, GPIO_IN);
    gpio_pull_down(PIN_ENT_SIGNAL);          //  HABILITO RESISTENCIA PULL DOWN


    // Inicializar otros GPIOs(PWM, ADC, I2C)
    adc_init();
    adc_gpio_init(31); // ADC0
    adc_gpio_init(32); // ADC1

    // Configurar pines PWM (ejemplo)
    gpio_set_function(29, GPIO_FUNC_PWM); // PWM para control de carga

    // Inicializar I2C (para EEPROM, el LCD y el RTC)
    i2c_init(i2c0, 100 * 1000); // 100 kHz
    gpio_set_function(6, GPIO_FUNC_I2C); // SDA
    gpio_set_function(7, GPIO_FUNC_I2C); // SCL
    gpio_pull_up(6);
    gpio_pull_up(7);



    // Libero los semáforos para permitir arranque de tareas dependientes
    xSemaphoreGive(sem_bin_config);
    xSemaphoreGive(sem_bin_memoria);
    xSemaphoreGive(sem_bin_readyToRead);

    // Destruyo la tarea para que no se vuelva a iniciar.
    vTaskDelete(NULL);
}


void vTask_EEPROM_RTC(void *pvParameters) 
{
    int setpoints[4];
    float muestras[2]; // tensión y corriente

    // Esperar habilitación
    xSemaphoreTake(sem_bin_memoria, portMAX_DELAY);

    while (1) {
        // Tomar mutex para I2C
        xSemaphoreTake(sem_i2c0_mutex, portMAX_DELAY);

        // Obtener los valores de setpoints
        xQueuePeek(queue_seqPoints, &setpoints, portMAX_DELAY);

        // Obtener valores de medición actuales
        xQueuePeek(queue_adcSensado, &muestras, portMAX_DELAY);

        // Simular escritura en EEPROM e impresión
        printf("EEPROM => V: %f, I: %f | Set: %d %d %d %d\n",
               muestras[0], muestras[1],
               setpoints[0], setpoints[1], setpoints[2], setpoints[3]);

        // Liberar mutex
        xSemaphoreGive(sem_i2c0_mutex);

        // Dormir (simula ciclo de escritura lento)
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


void vTask_LCD(void *pvParameters) 
{
    float mediciones[4]; // Voltaje, Corriente, Potencia, Resistencia
    char linea[21];      // Cada línea del LCD (máx 20 + null)

    while (1) {
        // Espera nueva medición
        if (xQueueReceive(queue_escribirLCD, &mediciones, portMAX_DELAY) == pdPASS) {
            xSemaphoreTake(sem_i2c0_mutex, portMAX_DELAY);

            // Línea 0: VOLTAJE
            snprintf(linea, sizeof(linea), "Volt Load: %6.1f V", mediciones[0]);
            lcd_set_cursor(0, 0);
            lcd_write_string(linea);

            // Línea 1: CORRIENTE
            snprintf(linea, sizeof(linea), "Curr Load: %6.2f A", mediciones[1]);
            lcd_set_cursor(0, 1);
            lcd_write_string(linea);

            // Línea 2: POTENCIA
            snprintf(linea, sizeof(linea), "Pot Load:%7.1f W", mediciones[2]);
            lcd_set_cursor(0, 2);
            lcd_write_string(linea);

            // Línea 3: RESISTENCIA
            snprintf(linea, sizeof(linea), "Resist Load: %6.1f Ohm", mediciones[3]);
            lcd_set_cursor(0, 3);
            lcd_write_string(linea);

            xSemaphoreGive(sem_i2c0_mutex);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}