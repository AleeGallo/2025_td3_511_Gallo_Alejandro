#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define DRIVER_NAME  "egb_kernel"
#define DEVICE_NAME  "egb"
#define CLASS_NAME   "egb_class"

#define RX_BUF_SIZE 256
static char rx_buffer[RX_BUF_SIZE];
static size_t rx_len = 0;

static char tx_buffer[RX_BUF_SIZE];

static DEFINE_MUTEX(rx_mutex);
static DECLARE_WAIT_QUEUE_HEAD(rx_wq);
static int rx_ready = 0;

static struct class *egb_class;
static struct cdev egb_cdev;
static dev_t dev_number;

static struct serdev_device *g_serdev = NULL;

/* ============================================================
 *                   SERDEV - UART RX CALLBACK
 * ============================================================ */
static size_t egb_serdev_receive(struct serdev_device *serdev,
                                 const unsigned char *buf,
                                 size_t count)
{
    size_t i;

    mutex_lock(&rx_mutex);

    for (i = 0; i < count; i++) {
        char ch = buf[i];

        if (ch == '\n' || ch == '\r') {

            if (rx_len > 0) {

                rx_buffer[rx_len] = '\0';

                dev_info(&serdev->dev,
                         "Linea completa recibida por UART: '%s'\n",
                         rx_buffer);

                rx_ready = 1;
                wake_up_interruptible(&rx_wq);

                rx_len = 0;
            }

        } else {
            if (rx_len < RX_BUF_SIZE - 1)
                rx_buffer[rx_len++] = ch;
        }
    }

    mutex_unlock(&rx_mutex);
    return count;
}

static const struct serdev_device_ops egb_serdev_ops = {
    .receive_buf = egb_serdev_receive,
};

/* ============================================================
 *         CHAR DEVICE: READ (bloqueante hasta recibir UART)
 *         Versión sin usar *offset, permite múltiples lecturas
 * ============================================================ */
static ssize_t egb_read(struct file *f,
                        char __user *buf,
                        size_t size,
                        loff_t *offset)
{
    int ret;
    ssize_t copied;
    size_t len;
    char line[RX_BUF_SIZE + 2];

    /* Esperar hasta que haya una linea completa desde UART */
    ret = wait_event_interruptible(rx_wq, rx_ready == 1);
    if (ret < 0)
        return ret;

    mutex_lock(&rx_mutex);
    rx_ready = 0;

    /* Copiar desde rx_buffer a 'line' */
    copied = strscpy(line, rx_buffer, RX_BUF_SIZE);
    if (copied < 0) {
        line[RX_BUF_SIZE - 1] = '\0';
        len = RX_BUF_SIZE - 1;
    } else {
        len = copied;
    }

    /* Agregar '\n' para que cat /dev/egb se vea prolijo */
    if (len < RX_BUF_SIZE - 1) {
        line[len] = '\n';
        len++;
        line[len] = '\0';
    }

    if (len > size)
        len = size;

    if (copy_to_user(buf, line, len)) {
        mutex_unlock(&rx_mutex);
        return -EFAULT;
    }

    mutex_unlock(&rx_mutex);

    return len;
}

/* ============================================================
 *           CHAR DEVICE: WRITE (envía por UART)
 * ============================================================ */
static ssize_t egb_write(struct file *f,
                         const char __user *buf,
                         size_t size,
                         loff_t *offset)
{
    size_t len;

    if (size > RX_BUF_SIZE - 1)
        len = RX_BUF_SIZE - 1;
    else
        len = size;

    if (copy_from_user(tx_buffer, buf, len))
        return -EFAULT;

    tx_buffer[len] = '\0';

    if (g_serdev) {
        serdev_device_write_buf(g_serdev, tx_buffer, len);
    }

    return size;
}

static const struct file_operations egb_fops = {
    .owner = THIS_MODULE,
    .read  = egb_read,
    .write = egb_write,
};

/* ============================================================
 *                        SERDEV PROBE
 * ============================================================ */
static int egb_serdev_probe(struct serdev_device *serdev)
{
    int ret;
    const char hello_msg[] = "EGB kernel module online!\r\n";

    pr_info("egb_kernel: serdev probe OK\n");

    g_serdev = serdev;

    serdev_device_set_client_ops(serdev, &egb_serdev_ops);

    ret = serdev_device_open(serdev);
    if (ret)
        return ret;

    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);

    serdev_device_write_buf(serdev, hello_msg, strlen(hello_msg));

    return 0;
}

static void egb_serdev_remove(struct serdev_device *serdev)
{
    pr_info("egb_kernel: serdev remove\n");
    serdev_device_close(serdev);
}

/* ============================================================
 *      DEVICE TREE MATCH TABLE (para el overlay del UART)
 * ============================================================ */
static const struct of_device_id egb_of_match[] = {
    { .compatible = "frankie,egb-uart" },
    {},
};
MODULE_DEVICE_TABLE(of, egb_of_match);

static struct serdev_device_driver egb_serdev_driver = {
    .probe  = egb_serdev_probe,
    .remove = egb_serdev_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = of_match_ptr(egb_of_match),
    },
};

/* ============================================================
 *                  INIT / EXIT DEL MÓDULO
 * ============================================================ */
static int __init egb_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    egb_class = class_create(CLASS_NAME);
    if (IS_ERR(egb_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(egb_class);
    }

    device_create(egb_class, NULL, dev_number, NULL, DEVICE_NAME);

    cdev_init(&egb_cdev, &egb_fops);
    ret = cdev_add(&egb_cdev, dev_number, 1);
    if (ret < 0) return ret;

    ret = serdev_device_driver_register(&egb_serdev_driver);
    if (ret < 0)
        return ret;

    pr_info("egb_kernel: módulo listo (/dev/egb + UART)\n");
    return 0;
}

static void __exit egb_exit(void)
{
    serdev_device_driver_unregister(&egb_serdev_driver);

    cdev_del(&egb_cdev);
    device_destroy(egb_class, dev_number);
    class_destroy(egb_class);
    unregister_chrdev_region(dev_number, 1);

    pr_info("egb_kernel: módulo removido\n");
}

module_init(egb_init);
module_exit(egb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Facu");
MODULE_DESCRIPTION("EGB: char device + serdev UART integrados");
