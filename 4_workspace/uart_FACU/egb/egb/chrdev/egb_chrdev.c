#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "egb"
#define CLASS_NAME  "egb_class"

static dev_t dev_number;
static struct class *egb_class;
static struct cdev egb_cdev;

/* Buffer interno del driver */
#define BUF_SIZE 128
static char kernel_buffer[BUF_SIZE];
static size_t kernel_buffer_len = 0;

/* --- OPERACIÓN WRITE --- */
/* Lo que escriba el usuario en /dev/egb queda guardado en kernel_buffer */
static ssize_t egb_write(struct file *filep,
                         const char __user *buf,
                         size_t len,
                         loff_t *offset)
{
    size_t copy_len = min(len, (size_t)(BUF_SIZE - 1));

    if (copy_from_user(kernel_buffer, buf, copy_len))
        return -EFAULT;

    kernel_buffer[copy_len] = '\0';
    kernel_buffer_len = copy_len;

    pr_info("egb_chrdev: recibido desde user: '%s'\n", kernel_buffer);

    return len; // Devolvemos la cantidad original
}

/* --- OPERACIÓN READ --- */
/* Devuelve al usuario lo último que escribió */
static ssize_t egb_read(struct file *filep,
                        char __user *buf,
                        size_t len,
                        loff_t *offset)
{
    if (*offset > 0)
        return 0; // indicar EOF

    if (kernel_buffer_len == 0)
        return 0;

    if (copy_to_user(buf, kernel_buffer, kernel_buffer_len))
        return -EFAULT;

    *offset = kernel_buffer_len;
    return kernel_buffer_len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = egb_write,
    .read  = egb_read,
};

/* --- INIT MODULE --- */
static int __init egb_init(void)
{
    int ret;

    pr_info("egb_chrdev: inicializando\n");

    /* Reservar major/minor dinámicamente */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("egb_chrdev: no pudo reservar major/minor\n");
        return ret;
    }

    /* Crear clase */
    egb_class = class_create(CLASS_NAME);
    if (IS_ERR(egb_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(egb_class);
    }

    /* Crear /dev/egb */
    if (IS_ERR(device_create(egb_class, NULL, dev_number, NULL, DEVICE_NAME))) {
        class_destroy(egb_class);
        unregister_chrdev_region(dev_number, 1);
        return -1;
    }

    /* Inicializar cdev */
    cdev_init(&egb_cdev, &fops);
    ret = cdev_add(&egb_cdev, dev_number, 1);
    if (ret < 0) {
        device_destroy(egb_class, dev_number);
        class_destroy(egb_class);
        unregister_chrdev_region(dev_number, 1);
        return ret;
    }

    pr_info("egb_chrdev: listo! /dev/egb creado\n");
    return 0;
}

/* --- EXIT MODULE --- */
static void __exit egb_exit(void)
{
    pr_info("egb_chrdev: removiendo\n");

    cdev_del(&egb_cdev);
    device_destroy(egb_class, dev_number);
    class_destroy(egb_class);
    unregister_chrdev_region(dev_number, 1);
}

module_init(egb_init);
module_exit(egb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Facu");
MODULE_DESCRIPTION("EGB: char device basico (sin UART)");
