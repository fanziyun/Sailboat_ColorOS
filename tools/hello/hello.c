#include <linux/module.h>

static int __init sailboat_hello_init(void)
{
	pr_info("sailboat_hello: loaded\n");
	return 0;
}

static void __exit sailboat_hello_exit(void)
{
	pr_info("sailboat_hello: unloaded\n");
}

module_init(sailboat_hello_init);
module_exit(sailboat_hello_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("fanziyun");
MODULE_DESCRIPTION("modversions smoke test");
