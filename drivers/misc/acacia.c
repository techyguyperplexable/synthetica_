// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/pm_qos.h>

#define DRIVER_NAME "acacia"

static int boost_enabled = 1;
static struct pm_qos_request net_qos;
static struct pm_qos_request cpu_qos;

static void enable_boost(void)
{
	if (!pm_qos_request_active(&net_qos))
		pm_qos_add_request(&net_qos, PM_QOS_NETWORK_LATENCY, 0);

	if (!pm_qos_request_active(&cpu_qos))
		pm_qos_add_request(&cpu_qos, PM_QOS_CPU_DMA_LATENCY, 0);
}

static void disable_boost(void)
{
	if (pm_qos_request_active(&net_qos))
		pm_qos_remove_request(&net_qos);

	if (pm_qos_request_active(&cpu_qos))
		pm_qos_remove_request(&cpu_qos);
}

static ssize_t write_boost(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	char input;

	if (!count)
		return -EINVAL;

	if (copy_from_user(&input, buf, 1))
		return -EFAULT;

	switch (input) {
	case '1':
		if (!boost_enabled) {
			boost_enabled = 1;
			enable_boost();
		}
		break;
	case '0':
		if (boost_enabled) {
			boost_enabled = 0;
			disable_boost();
		}
		break;
	default:
		return -EINVAL;
	}

	return count;
}

/* FIX: Use proc_ops instead of file_operations */
static const struct proc_ops fops = {
	.proc_write = write_boost,
};

static int __init acacia_init(void)
{
	if (!proc_create(DRIVER_NAME, 0220, NULL, &fops))
		return -ENOMEM;

	enable_boost();
	return 0;
}

static void __exit acacia_exit(void)
{
	remove_proc_entry(DRIVER_NAME, NULL);
	disable_boost();
}

module_init(acacia_init);
module_exit(acacia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia Performance Driver");
