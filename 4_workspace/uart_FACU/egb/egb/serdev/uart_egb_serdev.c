#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>

#define DRIVER_NAME  "egb_uart_serdev"
#define AUTHOR       "Facu"

/* Buffer para acumular una línea completa hasta '\n' o '\r' */
#define RX_LINE_BUF_SIZE 128
static char  rx_line_buf[RX_LINE_BUF_SIZE];
static size_t rx_line_len;

/*
 * Callback de recepción: vamos acumulando carácter por carácter
 * hasta encontrar '\n' o '\r'. Recién ahí imprimimos la línea completa.
 */
static size_t egb_uart_receive(struct serdev_device *serdev,
                               const unsigned char *buf,
                               size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        unsigned char ch = buf[i];

        if (ch == '\n' || ch == '\r') {
            /* Cerrar string */
            if (rx_line_len < RX_LINE_BUF_SIZE)
                rx_line_buf[rx_line_len] = '\0';
            else
                rx_line_buf[RX_LINE_BUF_SIZE - 1] = '\0';

            /* Si termina en '\r', ya lo manejamos con la condición de arriba */

            dev_info(&serdev->dev,
                     "Linea completa recibida por UART: '%s'\n",
                     rx_line_buf);

            /* Reseteamos para la próxima línea */
            rx_line_len = 0;
        } else {
            /* Acumular mientras haya espacio */
            if (rx_line_len < RX_LINE_BUF_SIZE - 1) {
                rx_line_buf[rx_line_len++] = ch;
            } else {
                /* Si se llena, descartamos lo que sobra */
            }
        }
    }

    /* Indicamos que consumimos todos los bytes */
    return size;
}

static const struct serdev_device_ops egb_uart_ops = {
    .receive_buf = egb_uart_receive,
};

static int egb_uart_probe(struct serdev_device *serdev)
{
    int ret;
    const char msg[] = "HELLO desde EGB kernel via serdev!\r\n";

    rx_line_len = 0;

    dev_info(&serdev->dev, "egb_uart_probe: dispositivo encontrado\n");

    serdev_device_set_client_ops(serdev, &egb_uart_ops);

    ret = serdev_device_open(serdev);
    if (ret) {
        dev_err(&serdev->dev,
                "No se pudo abrir el dispositivo serdev: %d\n", ret);
        return ret;
    }

    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);

    dev_info(&serdev->dev,
             "Enviando mensaje inicial por UART (write_buf): \"%s\"\n", msg);

    ret = serdev_device_write_buf(serdev, msg, strlen(msg));
    if (ret < 0) {
        dev_err(&serdev->dev,
                "Error al enviar datos por UART (write_buf): %d\n", ret);
        serdev_device_close(serdev);
        return ret;
    } else {
        dev_info(&serdev->dev,
                 "Se escribieron %d bytes por UART\n", ret);
    }

    dev_info(&serdev->dev,
             "egb_uart_serdev listo. Lineas terminadas en \\n o \\r se mostraran completas en dmesg.\n");

    return 0;
}

static void egb_uart_remove(struct serdev_device *serdev)
{
    dev_info(&serdev->dev, "egb_uart_remove: cerrando dispositivo\n");
    serdev_device_close(serdev);
}

static const struct of_device_id egb_uart_of_match[] = {
    { .compatible = "frankie,egb-uart" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, egb_uart_of_match);

static struct serdev_device_driver egb_uart_driver = {
    .probe  = egb_uart_probe,
    .remove = egb_uart_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = of_match_ptr(egb_uart_of_match),
    },
};

module_serdev_device_driver(egb_uart_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("EGB: driver serdev UART base (sin char device todavia)");
