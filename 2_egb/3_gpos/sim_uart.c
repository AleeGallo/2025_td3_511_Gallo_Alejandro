#define _POSIX_C_SOURCE 200809L
#include "sim_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>

static int sim_fd = -1;
static struct termios saved_tio;

// Internal: configure terminal for raw mode
static int make_raw_fd(int fd, int baudrate)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;
    memcpy(&saved_tio, &tio, sizeof(tio));

    cfmakeraw(&tio);
    // set speed
    speed_t speed = B115200;
    switch (baudrate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B115200; break;
    }
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    // non canonical, no echo etc.
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) return -1;
    return 0;
}

int sim_uart_open_device(const char *dev_path)
{
    if (dev_path == NULL) {
        // use stdin/out by duplicating fd 0 (stdin)
        sim_fd = dup(STDIN_FILENO);
        if (sim_fd < 0) return -1;
        return make_raw_fd(sim_fd, 115200) == 0 ? 0 : -1;
    }

    // Open device read/write, no controlling tty
    sim_fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (sim_fd < 0) return -1;

    // Put blocking mode for reads (we will use select)
    int flags = fcntl(sim_fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(sim_fd, F_SETFL, flags);

    if (make_raw_fd(sim_fd, 115200) != 0) {
        close(sim_fd);
        sim_fd = -1;
        return -1;
    }
    return 0;
}

void sim_uart_close(void)
{
    if (sim_fd >= 0) {
        // restore saved attrs if possible
        tcsetattr(sim_fd, TCSANOW, &saved_tio);
        close(sim_fd);
        sim_fd = -1;
    }
}

/* --------- API (compatible con llamadas en tu firmware) --------- */

// uart pointer param is ignored; kept for signature compatibility
int uart_init(void *uart, int baudrate)
{
    (void)uart;
    (void)baudrate;
    if (sim_fd < 0) {
        // try to auto-open stdin as fallback
        return sim_uart_open_device(NULL);
    }
    return 0;
}

int uart_is_readable(void *uart)
{
    (void)uart;
    if (sim_fd < 0) return 0;

    fd_set rfds;
    struct timeval tv = {0, 0}; // non-blocking check
    FD_ZERO(&rfds);
    FD_SET(sim_fd, &rfds);
    int ret = select(sim_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(sim_fd, &rfds)) return 1;
    return 0;
}

char uart_getc(void *uart)
{
    (void)uart;
    if (sim_fd < 0) return 0;
    char c = 0;
    ssize_t r;
    // blocking read of one byte
    do {
        r = read(sim_fd, &c, 1);
    } while (r == 0 || (r < 0 && errno == EINTR));
    if (r <= 0) return 0;
    return c;
}

void uart_puts(void *uart, const char *s)
{
    (void)uart;
    if (sim_fd < 0) {
        // fallback to stdout
        fputs(s, stdout);
        fflush(stdout);
        return;
    }
    size_t len = strlen(s);
    ssize_t w = write(sim_fd, s, len);
    (void)w;
    fsync(sim_fd);
}

void uart_set_format(void *uart, int data_bits, int stop_bits, int parity)
{
    (void)uart; (void)data_bits; (void)stop_bits; (void)parity;
    // No-op in simulation
}

void uart_set_fifo_enabled(void *uart, bool enabled)
{
    (void)uart; (void)enabled;
    // No-op in simulation
}
