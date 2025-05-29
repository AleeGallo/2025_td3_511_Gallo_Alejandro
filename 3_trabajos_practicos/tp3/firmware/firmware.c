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
#define IN_GPIO     11
// Cola para mandar numero de frecuencia a mostrar  
QueueHandle_t q_frecuencia;
// Semaforo counting
SemaphoreHandle_t semphr_counting;


/**
 * Tareas
 */

void task_polling(void *params) {
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
}

void task_print(void *params) {
/* Tarea para calcular la frecuencia mediante una cola counting cada 1 segundo */    
    // Aseguro que sea consistente el bloqueo
    TickType_t tick = xTaskGetTickCount();

    while (1)
    {
        /* Recibo semaforo counting*/
        printf("Frecuencia: %d\n", uxSemaphoreGetCount(semphr_counting));
        xQueueReset(semphr_counting);
        // Bloqueo por un segundo para contar
        // vTaskDelay(1000);
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(1000));
    }
}

void task_init(void *params) {
    // Inicializacion de GPIO
    gpio_init(IN_GPIO);
    gpio_set_dir(IN_GPIO, false);  //Set como entrada

    // PWM de 5KHz en GPIO 12
    pwm_user_init(12, 5000);

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
    xTaskCreate(task_polling, "Polling", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}
