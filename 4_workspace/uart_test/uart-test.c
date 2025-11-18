#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
// Headers de FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define LED_RUN_PIN     25

#define UART_ID uart0
#define UART_TX_PIN 16
#define UART_RX_PIN 17
#define UART_BAUDRATE 115200
#define UART_BUFFER_SIZE 128

// Queues de manejo de uart
QueueHandle_t q_uart_rx = NULL;
QueueHandle_t q_uart_tx = NULL;

/* ISR de recepción UART */
void ISR_uart_rx() {
    static char uart_rx_buffer[UART_BUFFER_SIZE];
    static uint16_t uart_rx_index = 0;

    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);

        if (c == '\r' || c == '\n') {
            if (uart_rx_index > 0) {
                uart_rx_buffer[uart_rx_index] = '\0';
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(q_uart_rx, uart_rx_buffer, &xHigherPriorityTaskWoken);
                uart_rx_index = 0;
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        } else if (uart_rx_index < UART_BUFFER_SIZE - 1) {
            uart_rx_buffer[uart_rx_index++] = c;
        }
    }
}

/* Tarea RX de la UART */
void task_UART_RX(void *pvParams) {
    // BUFFER RX
    char rx_buffer[UART_BUFFER_SIZE];

    for (;;) {
        if (xQueueReceive(q_uart_rx, rx_buffer, portMAX_DELAY) == pdTRUE) {
            // DATOS RECIBIDOS
            // Mandar a una cola, variable global, etc
            else printf("[UART] Datos recibidos: %s\n", rx_buffer);
        }
    }
}

/* Tarea TX de la UART */
void task_UART_TX(void *pvParams) {
    // BUFFER TX
    char tx_buffer[UART_BUFFER_SIZE];

    for (;;) {
        // Espera un mensaje en la cola para enviar
        if (xQueueReceive(q_uart_tx, tx_buffer, portMAX_DELAY) == pdTRUE) {
            // Aseguro terminacion de linea
            tx_buffer[UART_BUFFER_SIZE - 1] = '\0';
            // Mando string a la uart
            uart_puts(UART_ID, tx_buffer);
        }
    }
}

void task_LedRun(void *pvParams)
{
    for(;;){
        gpio_put(LED_RUN_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(LED_RUN_PIN, false);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main()
{
    stdio_init_all();

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, true);

    // INIT UART0 
    uart_init(UART_ID, UART_BAUDRATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, false);
    // IRQ UART RX
    irq_set_exclusive_handler(UART0_IRQ, ISR_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    // Creo queues de UART
    q_uart_rx = xQueueCreate(5, UART_BUFFER_SIZE);
    q_uart_tx = xQueueCreate(10, UART_BUFFER_SIZE);

    // Creo tareas
    xTaskCreate(task_LedRun, "RUN", 128, NULL, 1, NULL);
    xTaskCreate(task_UART_RX, "UART-RX", 512, NULL, 2, NULL);
    xTaskCreate(task_UART_TX, "UART-TX", 128, NULL, 1, NULL);

    // START SCHEDULER
    vTaskStartScheduler();
    while (true);
}
