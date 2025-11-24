#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

// Autor del modulo
#define AUTHOR		"Alejandro"
// Char device name
#define CDEV_NAME	"egb_driver_uart"
// Minor number del char device
#define CDEV_MINOR	50
// Cantidad de devices para reservar
#define CDEV_COUNT	1
// Cantidad maxima de bytes para el buffer de usuario
#define SHARED_BUFF_SIZE	64

static int major;
static char shared_buffer[SHARED_BUFF_SIZE];

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
	*off += delta;
	// Devuelvo lo que falta
	return delta;
}

/**
 * @brief Se llama cuando se escribe el archivo
*/
static ssize_t cdev_echo_write(struct file *f, char __user *buff, size_t size, loff_t *off) {
	// Variables para cantidad de bytes escritos
	int not_copied, delta, to_copy = (len + *off) < SHARED_BUFF_SIZE ? len : SHARED_BUFF_SIZE - *off;
	// Veo si se puede copiar
	if(*off >= SHARED_BUFF_SIZE) { return 0; }
	// Copio al user
	not_copied = copy_to_user(buff, &shared_buffer[*off], to_copy);
	delta = to_copy - not_copied;
	// Mensaje testigo para el kernel
	printk(KERN_INFO "%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CDEV_NAME, shared_buffer);

	if (not_copied)
		pr_warn("%s: Solo se pudo escribir %d bytes\n", AUTHOR, delta);
	*off += delta;
	// Devuelvo lo que falta
	return delta;
}

// Estructura para implementacion de operaciones con archivos
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = cdev_echo_read,
	.write = cdev_echo_write
};

static int __init td3_uart_init(void)
{
	major = register_chrdev(0, "egb_driver_uart", &fops);
	if (major < 0){
		pr_err("%s - Error registering chrdev\n", CDEV_NAME);
		return major;
	}
	printk("%s - Major Device Number: %d\n", CDEV_NAME, major);
	return 0;
}

static void __exit td3_uart_exit(void)
{
	unregister_chrdev(major, "egb_driver_uart");
}


// Registro funciones de inicializacion y saiida del driver
module_init(td3_uart_init);
module_exit(td3_uart_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo que inicializa y registra un char device");
