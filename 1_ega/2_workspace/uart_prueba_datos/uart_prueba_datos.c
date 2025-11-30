#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define BAUD_RATE 115200
#define LED_PIN 25     // LED onboard


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

    while (true) {

        cantidad += 1;
        qty10 += 10;

        char buffer[64], buff_receiver[64];
        snprintf(buffer, sizeof(buffer), "%d\n", cantidad);
        printf (buffer);
        uart_puts(UART_ID, buffer);

        // Leer lo que llega por UART
        int idx = 0;
        while (uart_is_readable(UART_ID) && idx < sizeof(buff_receiver)-1) {
            buff_receiver[idx++] = uart_getc(UART_ID);
        }
        buff_receiver[idx] = '\0'; // cerrar string

        // Mostrar lo leído
        if (idx > 0) {
            printf("Leido: %s\n", buff_receiver);
        }
        // Toggle LED
        gpio_put(LED_PIN, 1);
        sleep_ms(500);
        gpio_put(LED_PIN, 0);
        sleep_ms(500); // Total ~500 ms por loop
    }
}