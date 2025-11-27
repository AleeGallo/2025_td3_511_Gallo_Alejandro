#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

#include <linux/property.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

// Autor del modulo
#define AUTHOR		"Alejandro"
#define DRIVER_NAME "egb_driver"
// Char device name
#define CDEV_NAME	"egb_driver_uart"
// Minor number del char device
#define CDEV_MINOR	50
// Cantidad de devices para reservar
#define CDEV_COUNT	1
// Cantidad maxima de bytes para el buffer de usuario
#define SHARED_BUFF_SIZE	64



/* -------------- SERDEV -----------------*/

// IDs de serial devices
static struct of_device_id serdev_ids[] = {
	{ .compatible = "brightlight,egb_driver", },
	{ }
};
MODULE_DEVICE_TABLE(of, serdev_ids);

// Estructura de implementacion del driver
static struct serdev_device_driver egb_driver_uart = {
	.probe = egb_uart_probe,
	.remove = egb_uart_remove,
	.driver = {
		.name = "egb_driver_uart",
		.of_match_table = serdev_ids,
	},
};


// Estructura para manejar el char device
typedef struct {
	struct cdev cdev;			// Guarda el char device
	dev_t cdev_number;			// Guarda el major y minor number
	unsigned int cdev_major;	// Numero mayor
	struct class *cdev_class;	// Clase del char device
} egb_cdev_t;

// Variable para mi char device
egb_cdev_t egb_cdev;

// Puntero global para UART
static struct serdev_device *g_serdev = NULL;

static int major;
static char shared_buffer[SHARED_BUFF_SIZE];

/* --------------------- READ / WRITE -----------------------*/

/**
 * @brief Se llama cuando se lee el archivo
*/
static ssize_t cdev_echo_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
	// Variables para cantidad de bytes escritos
	int not_copied, delta, to_copy = (len + *off) < SHARED_BUFF_SIZE ? len : SHARED_BUFF_SIZE - *off;
	// Veo si se puede copiar
	if(*off >= SHARED_BUFF_SIZE) { return 0; }
	// Copio al user
	not_copied = copy_to_user(buff, &shared_buffer[*off], to_copy);
	delta = to_copy - not_copied;
	// Mensaje testigo para el kernel
	printk(KERN_INFO "%s: Leido sobre /dev/%s - %s\n", AUTHOR, CDEV_NAME, shared_buffer);

	if (not_copied)
		pr_warn("%s: Solo se pudo copiar %d bytes\n", AUTHOR, delta);
	// *off += delta;
	// Devuelvo lo que falta
	return delta;
}

/**
 * @brief Se llama cuando se escribe el archivo
*/
static ssize_t cdev_echo_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
	// Variables para cantidad de bytes escritos
	int not_copied, delta, to_copy = (len + *off) < SHARED_BUFF_SIZE ? len : SHARED_BUFF_SIZE - *off;
	// Veo si se puede copiar
	if(*off >= SHARED_BUFF_SIZE) { return 0; }
	// Copio al user
	not_copied = copy_to_user(&shared_buffer[*off], buff, to_copy);
	delta = to_copy - not_copied;
	// Mensaje testigo para el kernel
	printk(KERN_INFO "%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CDEV_NAME, shared_buffer);

	if (not_copied)
		pr_warn("%s: Solo se pudo escribir %d bytes\n", AUTHOR, delta);
	// *off += delta;
	// Devuelvo lo que falta
	return delta;
}

/* ----------------------- PROBE / REMOVE ---------------------*/

static int egb_uart_probe(struct serdev_device *serdev)
{
    int ret, my_value;
	const char *label;
    const char msg[] = "HELLO desde kernel via serdev!\r\n";
	struct device *dev = &serdev->dev;

	printk("%s - Ahora estoy en funcion probe.", AUTHOR);

	/* Check for device properties */
	if(!device_property_present(dev, "label")) {
		printk("dt_probe - Error! Device property 'label' not found!\n");
		return -1;
	}
	if(!device_property_present(dev, "my_value")) {
		printk("dt_probe - Error! Device property 'my_value' not found!\n");
		return -1;
	}

	/* Read device properties */
	ret = device_property_read_string(dev, "label", &label);
	if(ret) {
		printk("dt_probe - Error! Could not read 'label'\n");
		return -1;
	}
	printk("dt_probe - label: %s\n", label);
	ret = device_property_read_u32(dev, "my_value", &my_value);
	if(ret) {
		printk("dt_probe - Error! Could not read 'my_value'\n");
		return -1;
	}
	printk("dt_probe - my_value: %d\n", my_value);

	
    return 0;
}

static int egb_uart_remove (struct serdev_device *serdev)
{
    dev_info(&serdev->dev, "%s: cerrando dispositivo\n", AUTHOR);
    serdev_device_close(serdev);
	return 0;
}


/* -------------- SERDEV -----------------*/

// Estructura de implementacion del driver
/* static struct serdev_device_driver egb_driver_uart = {
	.probe = egb_uart_probe,
	.remove = egb_uart_remove,
	.driver = {
		.name = "egb_driver_uart",
		.of_match_table = serdev_ids,
	},
}; */

static struct platform_driver egb_driver_uart = {
	.probe = egb_uart_probe,
	.remove = egb_uart_remove,
	.driver = {
		.name = "egb_driver_uart",
		.of_match_table = serdev_ids,
	},
};

// Estructura para manejar el char device
typedef struct {
	struct cdev cdev;			// Guarda el char device
	dev_t cdev_number;			// Guarda el major y minor number
	unsigned int cdev_major;	// Numero mayor
	struct class *cdev_class;	// Clase del char device
} egb_cdev_t;

// Variable para mi char device
egb_cdev_t egb_cdev;


// Estructura para implementacion de operaciones con archivos
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = cdev_echo_read,
	.write = cdev_echo_write
};

/* -------------- INIT / EXIT -----------------*/

static int __init td3_uart_init(void)
{
	/* major = register_chrdev(0, "egb_driver_uart", &fops);
	if (major < 0){
		pr_err("%s - Error registering chrdev\n", AUTHOR);
		return major;
	} 
	printk("%s - Major Device Number: %d\n", AUTHOR, major); */

	printk("%s - Cargando el driver...\n", AUTHOR);
	if(platform_driver_register(&egb_driver_uart)) {
		printk("dt_probe - Error! Could not load driver\n");
		return -1;
	}
	return 0;
}

static void __exit td3_uart_exit(void)
{
	/* pr_info("%s - Se cierra la prueba", AUTHOR);
	unregister_chrdev(major, "egb_driver_uart"); */
	printk("%s - Sacar driver", AUTHOR);
	platform_driver_unregister(&egb_driver_uart);
}


// Registro funciones de inicializacion y saiida del driver
module_init(td3_uart_init);
module_exit(td3_uart_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo que inicializa y registra un char device");
