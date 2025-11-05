#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "gpio_driver.h"

// Etiqueta para el autor del modulo
#define AUTHOR	"Alejandro"

// Puntero para primer hilo
static struct task_struct *holaKernel;
// Puntero para segundo hilo
static struct task_struct *chauKernel;


static int holaKernel_function(void *params) {
    // Corre mientras no haya otros procesos que lo detengan
    while(!kthread_should_stop()) {
        // Mensaje para el Kernel
        printk(KERN_INFO "%s: Hola desde el kernel!\n", AUTHOR);
        // Demora de 1 segundo
        msleep(1000);
    }
    return 0;
}

static int chauKernel_function(void *params) {
    // Corre mientras no haya otros procesos que lo detengan
    msleep(500);
	while(!kthread_should_stop()) {
        // Mensaje para el Kernel
        printk(KERN_INFO "%s: Chau desde el kernel!\n", AUTHOR);
        // Demora de 2 segundos
        msleep(1000);
    }
    return 0;
}


static int __init kernel_module_init(void) {
	// Mensaje para el Kernel
	printk(KERN_INFO "%s: Insertando el modulo de kernel\n", AUTHOR);
	
    // Intento crear y correr el hilo
	holaKernel = kthread_run(
        holaKernel_function,  // Callback
        NULL,       // Sin datos
        "holaKernel"   // Nombre del hilo
    );
    // Verifico si hubo error al crearlo
    if (IS_ERR(holaKernel)) {
        printk(KERN_ERR "%s: Error al crear thread 1\n", AUTHOR);
        return -1;
    }

    // Intento crear y correr el hilo
    chauKernel = kthread_run(
        chauKernel_function,  // Callback
        NULL,       // Sin datos
        "chauKernel"   // Nombre del hilo
    );
    // Verifico si hubo error al crearlo
    if (IS_ERR(chauKernel)) {
        printk(KERN_ERR "%s: Error al crear thread 2\n", AUTHOR);
        // Elimino el hilo anterior
        kthread_stop(holaKernel);
        return -1;
    }
    return 0;
}



/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit kernel_module_exit(void) {
	// Mensaje para el Kernel
	pr_info("%s: Removiendo el modulo de kernel\n", AUTHOR);
    // Si se habia podido crear el hilo
	if (holaKernel) {
        // Detengo el hilo
        kthread_stop(holaKernel);
    }
    // Si se habia podido crear el hilo
    if (chauKernel) {
        // Detengo el hilo
        kthread_stop(chauKernel);
    }


}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");