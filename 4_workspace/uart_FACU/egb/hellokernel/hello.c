#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Facu");
MODULE_DESCRIPTION("Modulo simple Hello World para Raspberry Pi 4B");

static int __init hello_init(void)
{
    pr_info("hello: Hola mundo desde el kernel en la Raspberry Pi 4B!\n");
    return 0; // 0 = OK
}

static void __exit hello_exit(void)
{
    pr_info("hello: Chau mundo, me desregistran del kernel!\n");
}

module_init(hello_init);
module_exit(hello_exit);
