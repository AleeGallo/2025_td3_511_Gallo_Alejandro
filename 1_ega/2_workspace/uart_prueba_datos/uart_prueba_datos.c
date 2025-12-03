#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define BAUD_RATE 115200
#define LED_PIN 25     // LED onboard
#define UART_BUFFER 128

int main() {
    stdio_init_all();

    // Init UART
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Init LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    int cantidad = 0;
    int qty10 = 0;
    

    char buffer[8][UART_BUFFER];

    snprintf(buffer[0], UART_BUFFER, "GET KP\n");
    snprintf(buffer[1], UART_BUFFER, "GET KI\n");
    snprintf(buffer[2], UART_BUFFER, "GET KD\n");
    snprintf(buffer[3], UART_BUFFER, "GET SP\n");
    snprintf(buffer[4], UART_BUFFER, "GET R \n");
    snprintf(buffer[5], UART_BUFFER, "GET VI\n");
    snprintf(buffer[6], UART_BUFFER, "GET I\n");
    snprintf(buffer[7], UART_BUFFER, "GET LOG \n");

    int idx = 0;  // índice del buffer a enviar

    while (true) {
        // Enviar un buffer
        uart_puts(UART_ID, buffer[idx]);
        printf("Enviado: %s", buffer[idx]);

        // Leer lo que llega por UART
        char buff_receiver[UART_BUFFER];
        int r_idx = 0;
        while (uart_is_readable(UART_ID) && r_idx < UART_BUFFER - 1) {
            buff_receiver[r_idx++] = uart_getc(UART_ID);
        }
        buff_receiver[r_idx] = '\0';

        if (r_idx > 0) {
            printf("Leido: %s\n", buff_receiver);
        }

        // Parpadeo LED
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);

        // Esperar 1 segundo antes del próximo envío
        sleep_ms(2000);

        // Pasar al siguiente buffer
        idx = (idx + 1) % 8;
    }
}