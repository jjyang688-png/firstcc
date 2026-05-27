/**
 * sys_monitor.c - System monitor misc device for i.MX6ULL
 *
 * Demonstrates:
 *
 * Ch42    - Misc device (lighter alternative to full CDEV)
 * Ch42    - misc_register() / misc_deregister()
 * Ch64    - module_param for device configuration
 *
 * Creates /dev/sys_monitor — a simple system status device.
 *
 *
 * Misc device vs Full CDEV comparison:
 *
 *   Full CDEV (comp_drv.c)               Misc device (this file)
 *   ──────────────────────               ─────────────────────
 *   alloc_chrdev_region(&dev_id)         (auto MISC_MAJOR + minor)
 *   cdev_init(&cdev, &fops)             misc_register(&miscdev) ← one call!
 *   cdev_add(&cdev, dev_id, 1)
 *   class_create()                       (class created automatically)
 *   device_create()                      (device node auto-created)
 *   ── ~20 lines ──                      ── ~5 lines ──
 *
 * Misc devices are ideal for:
 *   - Simple sensors, watchdog timers, system status
 *   - Any device that doesn't need a full class hierarchy
 *   - Debug/testing interfaces
 *   - Devices with only one instance (minor=0)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>

#define SYS_MON_NAME "sys_monitor"

/*
 * Module parameter: heartbeat interval in seconds.
 *
 * Usage:
 *   insmod sys_monitor.ko heartbeat_sec=5
 */
static int heartbeat_sec = 10;
module_param(heartbeat_sec, int, 0644);
MODULE_PARM_DESC(heartbeat_sec, "Heartbeat log interval in seconds");

/*
 * System status structure — returned to userspace on read().
 * Keep it small (< PAGE_SIZE) for simple read semantics.
 */
struct sys_mon_status {
    unsigned long uptime_sec;     /* System uptime (jiffies → seconds) */
    unsigned long read_count;     /* How many times this device was read */
    int            heartbeat;     /* Current heartbeat interval */
};

struct sys_mon_dev {
    struct miscdevice miscdev;    /* The misc device handle */
    struct mutex lock;
    atomic_t open_count;
    unsigned long read_count;
};

static struct sys_mon_dev *g_sys_dev;

/* ---- file_operations ---- */

static int sys_mon_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_sys_dev;
    atomic_inc(&g_sys_dev->open_count);
    return 0;
}

static int sys_mon_release(struct inode *inode, struct file *filp)
{
    atomic_dec(&g_sys_dev->open_count);
    return 0;
}

static ssize_t sys_mon_read(struct file *filp, char __user *buf,
                            size_t count, loff_t *ppos)
{
    struct sys_mon_dev *dev = filp->private_data;
    struct sys_mon_status st;

    mutex_lock(&dev->lock);

    memset(&st, 0, sizeof(st));
    st.uptime_sec = jiffies_to_msecs(get_jiffies_64()) / 1000;
    st.read_count = dev->read_count;
    st.heartbeat  = heartbeat_sec;

    dev->read_count++;
    mutex_unlock(&dev->lock);

    if (count > sizeof(st))
        count = sizeof(st);

    if (copy_to_user(buf, &st, count))
        return -EFAULT;

    return count;
}

static const struct file_operations sys_mon_fops = {
    .owner   = THIS_MODULE,
    .open    = sys_mon_open,
    .release = sys_mon_release,
    .read    = sys_mon_read,
};

/* ---- Platform probe —— misc device registration ---- */
static int sys_mon_probe(struct platform_device *pdev)
{
    struct device *d = &pdev->dev;
    struct sys_mon_dev *dev;
    int ret;

    dev_info(d, "sys_monitor probing...\n");

    dev = devm_kzalloc(d, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_sys_dev = dev;
    platform_set_drvdata(pdev, dev);

    mutex_init(&dev->lock);
    atomic_set(&dev->open_count, 0);

    /*
     * Misc device registration — one call replaces:
     *   alloc_chrdev_region + cdev_init + cdev_add
     *   + class_create + device_create
     *
     * struct miscdevice fields:
     *   .minor = MISC_DYNAMIC_MINOR  → kernel auto-assigns a minor number
     *   .name  = "sys_monitor"       → appears as /dev/sys_monitor
     *   .fops  = &sys_mon_fops       → file operations
     */
    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name  = SYS_MON_NAME;
    dev->miscdev.fops  = &sys_mon_fops;

    ret = misc_register(&dev->miscdev);
    if (ret) {
        dev_err(d, "misc_register failed: %d\n", ret);
        return ret;
    }

    dev_info(d, SYS_MON_NAME " probed ok (minor=%d, heartbeat=%ds)\n",
             dev->miscdev.minor, heartbeat_sec);
    return 0;
}

static int sys_mon_remove(struct platform_device *pdev)
{
    struct sys_mon_dev *dev = platform_get_drvdata(pdev);

    misc_deregister(&dev->miscdev);    /* One call to undo everything */

    dev_info(&pdev->dev, SYS_MON_NAME " removed\n");
    return 0;
}

/* Device tree match */
static const struct of_device_id sys_mon_of_match[] = {
    { .compatible = "alientek,sys-monitor" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sys_mon_of_match);

static struct platform_driver sys_mon_driver = {
    .probe  = sys_mon_probe,
    .remove = sys_mon_remove,
    .driver = {
        .name           = SYS_MON_NAME,
        .of_match_table = sys_mon_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(sys_mon_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("System monitor misc device for i.MX6ULL");
