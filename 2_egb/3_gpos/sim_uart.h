#ifndef SIM_UART_H
#define SIM_UART_H

#include <stdbool.h>

// Prototipos que imitan la API del Pico (parámetros ignorados en la simulación)
int uart_init(void *uart, int baudrate);
int uart_is_readable(void *uart);
char uart_getc(void *uart);
void uart_puts(void *uart, const char *s);
void uart_set_format(void *uart, int data_bits, int stop_bits, int parity);
void uart_set_fifo_enabled(void *uart, bool enabled);

// Helpers de simulación
// Si DEV==NULL usa stdin/stdout (útil para pruebas rápidas)
int sim_uart_open_device(const char *dev_path);
void sim_uart_close(void);

#endif // SIM_UART_H
