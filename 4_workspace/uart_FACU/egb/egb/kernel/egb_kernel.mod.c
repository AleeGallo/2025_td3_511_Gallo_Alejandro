#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x122c3a7e, "_printk" },
	{ 0x9db46ab2, "serdev_device_close" },
	{ 0xd30c3a4c, "serdev_device_open" },
	{ 0xbe2f7a74, "serdev_device_set_baudrate" },
	{ 0xb8b3d0cc, "serdev_device_set_flow_control" },
	{ 0xf0d3c14b, "serdev_device_write_buf" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xe67106b1, "_dev_info" },
	{ 0xe2964344, "__wake_up" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x59c02473, "class_create" },
	{ 0x2c9a4c10, "device_create" },
	{ 0xa01f13a6, "cdev_init" },
	{ 0x3a6d85d3, "cdev_add" },
	{ 0x62543cd7, "__serdev_device_driver_register" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x92893115, "driver_unregister" },
	{ 0x27271c6b, "cdev_del" },
	{ 0x8b970f46, "device_destroy" },
	{ 0x6775d5d3, "class_destroy" },
	{ 0xdcb764ad, "memset" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x1000e51, "schedule" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x476b165a, "sized_strscpy" },
	{ 0x7682ba4e, "__copy_overflow" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cfrankie,egb-uart");
MODULE_ALIAS("of:N*T*Cfrankie,egb-uartC*");

MODULE_INFO(srcversion, "D2374713040C082CF860021");
