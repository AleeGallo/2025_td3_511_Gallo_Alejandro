#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include "bmp280.h"
#include "lcd.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000
#define IN_GPIO     14
#define OUT_GPIO    15
#define PIN_SDA 0  // GPIO0 (pin 1 físico)
#define PIN_SCL 1  // GPIO1 (pin 2 físico)
#define LCD_DIR 0x27

typedef struct {
    float temperatura;
    int32_t presion;
} bmp280_data_t;

// Cola para datos del sensor
QueueHandle_t cola_lectura;
// Semaforo Mutex
SemaphoreHandle_t semaforo_mutex;

void task_init(void *params) {
    // Inicializacion de GPIO
    gpio_init(IN_GPIO);
    gpio_init(OUT_GPIO);
    gpio_set_dir(IN_GPIO, false);  //Set como entrada
    gpio_set_dir(OUT_GPIO, true);  //Set como salida


    // Inicialización I2C a 100KHz
    i2c_init(i2c0, 10000);
    // Configurar los pines con función I2C
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    //Resistencias de pull up
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    /* INICIALIZACIÓN LCD */
    // Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
    lcd_init(i2c0, LCD_DIR);
    // Limpia la pantalla
    lcd_clear();
    // Imprimo mensaje fijo
    lcd_string("Temp y presion");

    /* INICIALIZACIÓN BMP280 */
    // Inicializa el BMP280 usando el I2C0
    bmp280_init(i2c0);
    
    /* Creación de semáforos */
    //semphr_counting = xSemaphoreCreateCounting(MAX_COUNT, 0);
    cola_lectura = xQueueCreate(1, sizeof(bmp280_data_t));
    semaforo_mutex = xSemaphoreCreateMutex();

    // Entrego semaforo Mutex
    xSemaphoreGive(semaforo_mutex);

    // Elimino la tarea para liberar recursos
    vTaskDelete(NULL);
}

void task_sensor(void *params) {
    struct bmp280_calib_param parameters;
    bmp280_get_calib_params(&parameters);

    int32_t raw_temperature, raw_pressure;
    bmp280_data_t lectura_bmp280;
    while(1){
        // Tomo el semafoto Mutex
        xSemaphoreTake(semaforo_mutex, portMAX_DELAY);

        // Obtiene parámetros de compensación
        // bmp280_get_calib_params(&parameters);

        // Obtiene valores sin compensar
        bmp280_read_raw(&raw_temperature, &raw_pressure);

        // Obtiene los valores compensados de temperatura y presión
        lectura_bmp280.temperatura = bmp280_convert_temp(raw_temperature, &parameters);
        lectura_bmp280.presion = bmp280_convert_pressure(raw_pressure, raw_temperature, &parameters);
        
        // Envio la cola
        xQueueOverwrite(cola_lectura, &lectura_bmp280);

        // Entrego semaforo Mutex
        xSemaphoreGive(semaforo_mutex);
    }
}

void task_print(void *params) {
    bmp280_data_t valor_lectura_bmp280;
    char str[8];
    while(1){
        // Tomo el semafoto Mutex y recibo la cola
        xSemaphoreTake(semaforo_mutex, portMAX_DELAY);
        xQueuePeek (cola_lectura, &valor_lectura_bmp280, portMAX_DELAY);

        // Escribo en LCD
        lcd_clear();

        sprintf(str, "Temp: %.2f C", valor_lectura_bmp280.temperatura);
        lcd_set_cursor(0, 0);
        lcd_string(str);

        sprintf(str, "Presion: %d kPa", valor_lectura_bmp280.temperatura);
        lcd_set_cursor(1, 0);
        lcd_string(str);

        // Entrego semaforo Mutex
        xSemaphoreGive(semaforo_mutex);
    }
}

int main()
{
    stdio_init_all();

    /* Creacion de tareas */ 
    xTaskCreate(task_init, "Init", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_print, "Print", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_sensor, "Sensor", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}
