#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include "helper.h"
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


// Cola para mandar numero de frecuencia a mostrar  
QueueHandle_t q_frecuencia;
// Semaforo counting
SemaphoreHandle_t semphr_counting;

/**
 * @brief Callback para interrupcion por GPIO
 */
void irq_callback(uint gpio, uint32_t event_mask) {
    // Incremento la cuenta
    BaseType_t to_higher_priority_task = false;
    xSemaphoreGiveFromISR(semphr_counting, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}


/**
 * Tareas
 */


/* void task_polling(void *params) {
/* Tarea para agregar el semaforo counting cada pulso 
    int variable;
    while (1)
    {
        while (!gpio_get(IN_GPIO)){
            if (variable){
                xSemaphoreGive(semphr_counting);
                variable=0;
            }
        }
        variable=1;
    }
} */

void task_print(void *params) {
/* Tarea para calcular la frecuencia mediante una cola counting cada 1 segundo */    
    // Aseguro que sea consistente el bloqueo
    TickType_t tick = xTaskGetTickCount();
    UBaseType_t frecuencia;
    char str[8];
    while (1)
    {
        /* Recibo semaforo counting*/
        //printf("Frecuencia: %d Hz\n", uxSemaphoreGetCount(semphr_counting));
        sprintf(str, "%lu Hz", uxSemaphoreGetCount(semphr_counting));
        // Muevo el cursor al comienzo de la segunda fila
        lcd_set_cursor(1, 0);
        // Imprimo el mensaje
        lcd_string(str);
        xQueueReset(semphr_counting);
        // Bloqueo por un segundo para contar
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(1000));
    }
}

void task_init(void *params) {
    // Inicializacion de GPIO
    gpio_init(IN_GPIO);
    gpio_init(OUT_GPIO);
    gpio_set_dir(IN_GPIO, false);  //Set como entrada
    gpio_set_dir(OUT_GPIO, true);  //Set como salida

    // PWM de 5KHz en GPIO OUT
    pwm_user_init(OUT_GPIO, 5000);

    // Inicialización I2C a 100KHz
    i2c_init(i2c0, 100000);
    // Configurar los pines con función I2C
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    //Resistencias de pull up
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

    // Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
    lcd_init(i2c0, LCD_DIR);
    // Limpia la pantalla
    lcd_clear();
    // Imprimo mensaje fijo
    lcd_string("Frecuencia");

    // Agrego interrupcion por flanco descendente
    gpio_set_irq_enabled_with_callback(IN_GPIO, GPIO_IRQ_EDGE_RISE, true, irq_callback);

    /* Creación de semáforos */
    semphr_counting = xSemaphoreCreateCounting(MAX_COUNT, 0);

    // Elimino la tarea para liberar recursos
    vTaskDelete(NULL);
}

int main()
{
    stdio_init_all();

    /* Creacion de tareas */ 
    xTaskCreate(task_init, "Init", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_print, "Print", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    //xTaskCreate(task_polling, "Polling", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}
