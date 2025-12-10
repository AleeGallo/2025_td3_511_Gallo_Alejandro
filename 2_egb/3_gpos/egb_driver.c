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


// Autor del modulo
#define AUTHOR				"Alejandro"
#define DRIVER_NAME 		"egb_kernel"
#define DEVICE_NAME  		"egb"
#define CLASS_NAME   		"egb_class"
// Char device name
//#define CDEV_NAME	"egb_driver_uart"
// Cantidad maxima de bytes para el buffer de usuario
#define SHARED_BUFF_SIZE	128
#define BAUDRATE 			115200


static char rx_buffer[SHARED_BUFF_SIZE];
static size_t rx_len = 0;

static char tx_buffer[SHARED_BUFF_SIZE];

static DECLARE_WAIT_QUEUE_HEAD(rx_wq);
static int rx_ready = 0;

/* static char last_tx[SHARED_BUFF_SIZE];
static size_t last_tx_len = 0; */

static struct   class *egb_class;
static struct   cdev  egb_cdev;
static dev_t    dev_number;

// Puntero global para UART
static struct serdev_device *g_serdev = NULL;


/* ============================================================
 * SERDEV - UART RECEIVE (modificado)
 * ============================================================ */
static size_t egb_serdev_receive(struct serdev_device *serdev, const unsigned char *buf, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        char ch = buf[i];

        if (ch == '\n' || ch == '\r') {
            if (rx_len > 0) {
                rx_buffer[rx_len] = '\0';

                // Enviar buffer
                dev_info(&serdev->dev, "Linea completa recibida por UART: '%s'\n", rx_buffer);
                rx_ready = 1;
                wake_up_interruptible(&rx_wq);
                // Reinicio las variables
                rx_len = 0;
            }
        } else {
            if (rx_len < SHARED_BUFF_SIZE - 1)
                rx_buffer[rx_len++] = ch;
            // si el buffer se llena, lo truncamos (podés añadir manejo de overflow) 
        }
    }

    return count; 
}

static const struct serdev_device_ops egb_serdev_ops = {
    .receive_buf = egb_serdev_receive,
};


/* ============================================================
 *         CHAR DEVICE: READ (bloqueante hasta recibir UART)
 * ============================================================ */

static ssize_t egb_read(struct file *f, char __user *buf, size_t size, loff_t *offset) {
    int ret;
    ssize_t copied;
    size_t len;
    char line[SHARED_BUFF_SIZE + 2];

    // Esperar hasta que haya una linea completa desde UART
    ret = wait_event_interruptible(rx_wq, rx_ready == 1);
    if (ret < 0)
        return ret;


    // Copiamos la línea completa desde rx_buffer a 'line'
    copied = strscpy(line, rx_buffer, SHARED_BUFF_SIZE);
    if (copied < 0) {
        line[SHARED_BUFF_SIZE - 1] = '\0';
        len = SHARED_BUFF_SIZE - 1;
    } else {
        len = copied;
    }

    // Agregar '\n' para que "cat /dev/egb" se vea prolijo
    if (len < SHARED_BUFF_SIZE - 1) {
        line[len] = '\n'; 
        len++;
        line[len] = '\0';
    }

    if (len > size)
        len = size;

    // Copiar buffer al User Space
    if (copy_to_user(buf, line, len)) {
        return -EFAULT;
    }

    // Reset del estado: ahora que ya entregamos la línea la limpiamos
    rx_ready = 0;
    rx_len = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    /* Mensaje testigo para el kernel: usar 'line', no 'buf' */
    printk(KERN_INFO "%s: Leido sobre /dev/%s - %s\n", DRIVER_NAME, DEVICE_NAME, line);

    return len;
}

/* ============================================================
 *           CHAR DEVICE: WRITE (envía por UART)
 * ============================================================ */

static ssize_t egb_write(struct file *f, const char __user *buf, size_t size, loff_t *offset)
{
    size_t len = (size > SHARED_BUFF_SIZE - 2) ? SHARED_BUFF_SIZE - 2 : size;

    // Copia el buffer desde User Space
    if (copy_from_user(tx_buffer, buf, len))
        return -EFAULT;

    // Añadir "\n" si no existe
    if (len == 0 || (tx_buffer[len-1] != '\n' && tx_buffer[len-1] != '\r')) {
        tx_buffer[len++] = '\n';
        tx_buffer[len] = '\0';
    } else {
        tx_buffer[len] = '\0';
    }

    // Enviar por serdev al UART (Escribe directamente desde WRITE) 
    if (g_serdev) serdev_device_write_buf(g_serdev, tx_buffer, len);

    return len;
}


// Estructura para implementacion de operaciones con archivos
static const struct file_operations egb_fops = {
    .owner = THIS_MODULE,
    .read  = egb_read,
    .write = egb_write,
};


/* ============================================================
 *                   SERDEV - PROBE y REMOVE
 * ============================================================ */

static int egb_serdev_probe(struct serdev_device *serdev)
{
    pr_info("EGB Driver conectado");
    pr_info("%s: dispositivo encontrado\n", DRIVER_NAME);

	// Registro las operaciones al SERDEV
    serdev_device_set_client_ops(serdev, &egb_serdev_ops);
    // Intento abrir el puerto UART
	if(serdev_device_open(serdev)) {
		printk(KERN_ERR "%s: Error abriendo el puerto UART\n", DRIVER_NAME);
		return -1;
	}

	// UART - Setup
    serdev_device_set_baudrate(serdev, BAUDRATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

    // Guardo el puntero
	g_serdev = serdev;
	if(g_serdev == NULL) {
		printk(KERN_ERR "%s: Algo salio mal con el puerto UART\n", DRIVER_NAME);
		return -1;
	}

    return 0;
}

static void egb_serdev_remove(struct serdev_device *serdev)
{
	pr_info("%s: Cerrando dispositivo\n", DRIVER_NAME);
    // Cierro el dispositivo
    serdev_device_close(serdev);
}


/* ============================================================
 *      DEVICE TREE (overlay UART)
 * ============================================================ */

// Identificador del SERDEV (Serial Device) dentro del DEV-TREE
static const struct of_device_id egb_of_match[] = {
    { .compatible = "frankie,egb-driver" },
    {},
};
MODULE_DEVICE_TABLE(of, egb_of_match);

// Estructura principal del driver SERDEV (Serial Device)
static struct serdev_device_driver egb_serdev_driver = {
	.probe = egb_serdev_probe,
	.remove = egb_serdev_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(egb_of_match),
	},
};


/* ============================================================
 *                  INIT / EXIT DEL MÓDULO
 * ============================================================ */

static int __init egb_driver_init(void)
{
	int ret;

	// Reservar major/minor dinámicamente
    // 0 -> Busca el MAJOR libre
    if(alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME) < 0) {
		printk(KERN_ERR "%s: No se pudo crear el char device\n", DRIVER_NAME);
		return -1;
	}

	// Inicializar cdev
    // Registro las operaciones al char device
    cdev_init(&egb_cdev, &egb_fops);
    // Lo asocio con el char device
	if(cdev_add(&egb_cdev, dev_number, 1) < 0) {
		// Error
		unregister_chrdev_region(dev_number, 1);
		printk(KERN_ERR "%s: No se pudo crear el char device\n", DRIVER_NAME);
		return -1;
	}

	// Crear clase
    egb_class = class_create(CLASS_NAME);
    if (IS_ERR(egb_class)) {
        // Error
		printk(KERN_ERR "%s: No se pudo crear la clase del char device\n", DRIVER_NAME);
        class_destroy(egb_class);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(egb_class);
    }

	// Crear archivo del char device "/dev/egb"
	if(IS_ERR(device_create(egb_class, NULL, dev_number, NULL, DEVICE_NAME))) {
		// Error
		printk(KERN_ERR "%s: No se pudo crear el archivo del char device\n", DRIVER_NAME);
		class_destroy(egb_class);
		unregister_chrdev_region(dev_number, 1);
		return -1;
	}
	printk(KERN_INFO "%s: Archivo de char device creado!\n", DRIVER_NAME);

    // Intento registrar el driver para el UART
    ret = serdev_device_driver_register(&egb_serdev_driver);
    if (ret < 0)
        return ret;

    pr_info("%s: Driver cargado (/dev/egb + UART)\n", DRIVER_NAME);
    return 0;
}

static void __exit egb_driver_exit(void)
{
	// Elimino el device
    device_destroy(egb_class, dev_number);
    // Elimino la clase
    class_destroy(egb_class);
    // Libero la region
    unregister_chrdev_region(dev_number, 1);
    // Elimino el char device
    cdev_del(&egb_cdev);
    // Elimino el driver de UART
    serdev_device_driver_unregister(&egb_serdev_driver);

    pr_info("%s: Driver deshabilitado\n", DRIVER_NAME);
}
0

// Registro funciones de inicializacion y saiida del driver
module_init(egb_driver_init);
module_exit(egb_driver_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Kernel Space EGB: CDEV + SERDEV");
