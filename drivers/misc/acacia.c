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

void enable_boost(void) {
    pm_qos_add_request(&net_qos, PM_QOS_NETWORK_LATENCY, 0);
    pm_qos_add_request(&cpu_qos, PM_QOS_CPU_DMA_LATENCY, 0);
}

void disable_boost(void) {
    pm_qos_remove_request(&net_qos);
    pm_qos_remove_request(&cpu_qos);
}

static ssize_t write_boost(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    char input;
    if (copy_from_user(&input, buf, 1)) return -EFAULT;
    
    if (input == '1' && !boost_enabled) {
        boost_enabled = 1;
        enable_boost();
    } else if (input == '0' && boost_enabled) {
        boost_enabled = 0;
        disable_boost();
    }
    return count;
}

/* FIX: Use proc_ops instead of file_operations */
static const struct proc_ops fops = {
    .proc_write = write_boost,
};

static int __init acacia_init(void) {
    /* FIX: Cast fops to void* if the kernel is extremely picky, but usually standard struct works */
    proc_create(DRIVER_NAME, 0666, NULL, &fops);
    enable_boost();
    return 0;
}

static void __exit acacia_exit(void) {
    remove_proc_entry(DRIVER_NAME, NULL);
    if (boost_enabled) disable_boost();
}

module_init(acacia_init);
module_exit(acacia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("techyguyperplexable <objecting@objecting.org>");
MODULE_DESCRIPTION("Acacia Performance Driver");
