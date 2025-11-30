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
	{ 0x62543cd7, "__serdev_device_driver_register" },
	{ 0xe67106b1, "_dev_info" },
	{ 0x9db46ab2, "serdev_device_close" },
	{ 0xd30c3a4c, "serdev_device_open" },
	{ 0xbe2f7a74, "serdev_device_set_baudrate" },
	{ 0xb8b3d0cc, "serdev_device_set_flow_control" },
	{ 0xf0d3c14b, "serdev_device_write_buf" },
	{ 0x741bf2f0, "_dev_err" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x92893115, "driver_unregister" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cfrankie,uart-hello");
MODULE_ALIAS("of:N*T*Cfrankie,uart-helloC*");

MODULE_INFO(srcversion, "E27CB30B2AB87D7839E5102");
