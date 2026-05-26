/**
 * comp_drv.c - Composite character device driver for i.MX6ULL
 *
 * Consolidates knowledge from Chapters 40-55 of the 正点原子
 * I.MX6U Embedded Linux Driver Development Guide:
 *
 * Ch40/42 - New-style CDEV: alloc_chrdev_region, cdev_init/add, class/device_create
 * Ch43/44 - Device Tree: of_match_table, OF functions, compatible matching
 * Ch45    - GPIO subsystem: devm_gpiod_get, gpiod_set_value
 * Ch46-48 - Concurrency: mutex, atomic operations
 * Ch49/52 - Blocking & Non-blocking IO: wait_queue, mutex_trylock
 * Ch52    - Poll/select: poll_wait
 * Ch51/53 - Async notification: fasync_helper, kill_fasync
 * Ch51    - Kernel timer: timer_setup, mod_timer
 * Ch54/55 - Platform driver: platform_driver, probe/remove
 *
 * Controls the onboard LED. Create /dev/comp_drv for user interaction.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include "comp_drv.h"

#define DRV_NAME         "comp_drv"
#define DEVICE_COUNT     1
#define DEFAULT_BLINK_MS 500

/* ---- Device structure ---- */
struct comp_dev {
    dev_t dev_id;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct gpio_desc *led_gpio;      /* Ch45: gpiod API */

    struct mutex lock;               /* Ch48: mutex protection */
    wait_queue_head_t read_wq;       /* Ch49: blocking IO wait queue */

    struct fasync_struct *fasync;    /* Ch51/53: async notification */
    struct timer_list blink_timer;   /* Ch51: kernel timer */

    enum led_state led_state;
    int blink_period_ms;
    int timer_period_ms;

    unsigned long read_count;
    unsigned long write_count;
    atomic_t open_count;             /* Ch47: atomic operations */
};

static struct comp_dev *g_comp_dev;

/* ---- Forward declarations ---- */
static int comp_drv_open(struct inode *inode, struct file *filp);
static int comp_drv_release(struct inode *inode, struct file *filp);
static ssize_t comp_drv_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *ppos);
static ssize_t comp_drv_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *ppos);
static long comp_drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static unsigned int comp_drv_poll(struct file *filp, poll_table *wait);
static int comp_drv_fasync(int fd, struct file *filp, int on);

/* ---- File operations ---- */
static const struct file_operations comp_drv_fops = {
    .owner          = THIS_MODULE,
    .open           = comp_drv_open,
    .release        = comp_drv_release,
    .read           = comp_drv_read,
    .write          = comp_drv_write,
    .unlocked_ioctl = comp_drv_ioctl,
    .poll           = comp_drv_poll,
    .fasync         = comp_drv_fasync,
};

/* ---- LED control ---- */
static void led_apply(struct comp_dev *dev, enum led_state state)
{
    del_timer_sync(&dev->blink_timer);

    switch (state) {
    case LED_OFF:
        gpiod_set_value(dev->led_gpio, 0);
        break;
    case LED_ON:
        gpiod_set_value(dev->led_gpio, 1);
        break;
    case LED_BLINK:
        mod_timer(&dev->blink_timer,
                  jiffies + msecs_to_jiffies(dev->timer_period_ms));
        break;
    }
    dev->led_state = state;
}

/* ---- Timer callback (compatible with Linux 4.1.x+) ---- */
static void blink_timer_callback(unsigned long data)
{
    struct comp_dev *dev = (struct comp_dev *)data;
    static int toggle;

    toggle = !toggle;
    gpiod_set_value(dev->led_gpio, toggle ? 1 : 0);

    mod_timer(&dev->blink_timer,
              jiffies + msecs_to_jiffies(dev->timer_period_ms));
}

/* ---- file_operations implementation ---- */

static int comp_drv_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_comp_dev;
    atomic_inc(&g_comp_dev->open_count);
    pr_info(DRV_NAME ": opened (open_count=%d)\n",
            atomic_read(&g_comp_dev->open_count));
    return 0;
}

static int comp_drv_release(struct inode *inode, struct file *filp)
{
    struct comp_dev *dev = filp->private_data;

    comp_drv_fasync(-1, filp, 0);
    atomic_dec(&dev->open_count);
    pr_info(DRV_NAME ": closed (open_count=%d)\n", atomic_read(&dev->open_count));
    return 0;
}

/* Ch49/52: supports both blocking and non-blocking read */
static ssize_t comp_drv_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *ppos)
{
    struct comp_dev *dev = filp->private_data;
    struct comp_drv_status status;
    int ret;

    if (filp->f_flags & O_NONBLOCK) {
        /* Ch52: non-blocking - trylock and return -EAGAIN if busy */
        if (!mutex_trylock(&dev->lock))
            return -EAGAIN;
    } else {
        /* Ch49: blocking - wait for mutex with interruptible sleep */
        ret = mutex_lock_interruptible(&dev->lock);
        if (ret)
            return ret;
    }

    memset(&status, 0, sizeof(status));
    status.state           = dev->led_state;
    status.blink_period_ms = dev->blink_period_ms;
    status.read_count      = dev->read_count;
    status.write_count     = dev->write_count;

    mutex_unlock(&dev->lock);

    if (count > sizeof(status))
        count = sizeof(status);

    if (copy_to_user(buf, &status, count))
        return -EFAULT;

    dev->read_count++;
    return count;
}

/* Accepts: "on"/"1", "off"/"0", or "blink" */
static ssize_t comp_drv_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct comp_dev *dev = filp->private_data;
    char kbuf[16];

    if (count > sizeof(kbuf) - 1)
        count = sizeof(kbuf) - 1;
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;
    kbuf[count] = '\0';

    mutex_lock(&dev->lock);

    if (strncmp(kbuf, "on", 2) == 0 || kbuf[0] == '1')
        led_apply(dev, LED_ON);
    else if (strncmp(kbuf, "off", 3) == 0 || kbuf[0] == '0')
        led_apply(dev, LED_OFF);
    else if (strncmp(kbuf, "blink", 5) == 0)
        led_apply(dev, LED_BLINK);
    else {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    dev->write_count++;
    mutex_unlock(&dev->lock);

    /* Ch49: wake up blocking readers */
    wake_up_interruptible(&dev->read_wq);

    /* Ch51/53: send async notification */
    if (dev->fasync)
        kill_fasync(&dev->fasync, SIGIO, POLL_IN);

    return count;
}

/* Ch52: poll support for select/poll/epoll */
static unsigned int comp_drv_poll(struct file *filp, poll_table *wait)
{
    struct comp_dev *dev = filp->private_data;
    unsigned int mask = 0;

    poll_wait(filp, &dev->read_wq, wait);

    mask |= POLLOUT | POLLWRNORM;  /* always writable */
    mask |= POLLIN  | POLLRDNORM;  /* always readable for status */

    return mask;
}

/* Ch51/53: async notification */
static int comp_drv_fasync(int fd, struct file *filp, int on)
{
    struct comp_dev *dev = filp->private_data;
    return fasync_helper(fd, filp, on, &dev->fasync);
}

/* ---- ioctl ---- */
static long comp_drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct comp_dev *dev = filp->private_data;
    int ret = 0;

    mutex_lock(&dev->lock);

    switch (cmd) {
    case COMP_DRV_SET_BLINK_PERIOD:
        if (arg == 0) {
            ret = -EINVAL;
            break;
        }
        dev->blink_period_ms = arg;
        dev->timer_period_ms = arg / 2;
        pr_info(DRV_NAME ": blink period = %d ms\n", dev->blink_period_ms);
        break;

    case COMP_DRV_GET_STATUS: {
        struct comp_drv_status status;
        status.state           = dev->led_state;
        status.blink_period_ms = dev->blink_period_ms;
        status.read_count      = dev->read_count;
        status.write_count     = dev->write_count;
        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            ret = -EFAULT;
        break;
    }

    case COMP_DRV_START_BLINK:
        led_apply(dev, LED_BLINK);
        break;

    case COMP_DRV_STOP_BLINK:
        led_apply(dev, LED_OFF);
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    mutex_unlock(&dev->lock);
    return ret;
}

/* ---- Platform driver probe (Ch54/55) ---- */
static int comp_drv_probe(struct platform_device *pdev)
{
    struct device *d = &pdev->dev;
    struct comp_dev *dev;
    int ret;

    dev_info(d, "comp_drv probing...\n");

    dev = devm_kzalloc(d, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_comp_dev = dev;
    platform_set_drvdata(pdev, dev);

    /* Ch45: get GPIO from device tree via gpio subsystem */
    dev->led_gpio = devm_gpiod_get(d, "led", GPIOD_OUT_LOW);
    if (IS_ERR(dev->led_gpio)) {
        ret = PTR_ERR(dev->led_gpio);
        dev_err(d, "failed to get led-gpios: %d\n", ret);
        return ret;
    }

    /* Initialize concurrency primitives */
    mutex_init(&dev->lock);
    init_waitqueue_head(&dev->read_wq);
    atomic_set(&dev->open_count, 0);

    /* Ch51: initialize kernel timer (setup_timer for 4.1.x compat) */
    setup_timer(&dev->blink_timer, blink_timer_callback, (unsigned long)dev);
    dev->blink_period_ms = DEFAULT_BLINK_MS;
    dev->timer_period_ms = DEFAULT_BLINK_MS / 2;
    dev->led_state       = LED_OFF;

    /* Ch42: allocate device number and add cdev */
    ret = alloc_chrdev_region(&dev->dev_id, 0, DEVICE_COUNT, DRV_NAME);
    if (ret < 0) {
        dev_err(d, "alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&dev->cdev, &comp_drv_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->dev_id, DEVICE_COUNT);
    if (ret) {
        dev_err(d, "cdev_add failed: %d\n", ret);
        goto err_cdev;
    }

    /* Ch42: create device class and node */
    dev->class = class_create(THIS_MODULE, DRV_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        dev_err(d, "class_create failed: %d\n", ret);
        goto err_class;
    }

    dev->device = device_create(dev->class, d, dev->dev_id, NULL, DRV_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        dev_err(d, "device_create failed: %d\n", ret);
        goto err_device;
    }

    dev_info(d, DRV_NAME " probed ok (major=%d)\n", MAJOR(dev->dev_id));
    return 0;

err_device:
    class_destroy(dev->class);
err_class:
    cdev_del(&dev->cdev);
err_cdev:
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);
    return ret;
}

/* ---- Platform driver remove ---- */
static int comp_drv_remove(struct platform_device *pdev)
{
    struct comp_dev *dev = platform_get_drvdata(pdev);

    del_timer_sync(&dev->blink_timer);
    gpiod_set_value(dev->led_gpio, 0);

    device_destroy(dev->class, dev->dev_id);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);

    dev_info(&pdev->dev, DRV_NAME " removed\n");
    return 0;
}

/* Ch43/44: device tree match table */
static const struct of_device_id comp_drv_of_match[] = {
    { .compatible = "alientek,comp-drv" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, comp_drv_of_match);

/* Ch54/55: platform driver */
static struct platform_driver comp_drv_platform_driver = {
    .probe  = comp_drv_probe,
    .remove = comp_drv_remove,
    .driver = {
        .name           = DRV_NAME,
        .of_match_table = comp_drv_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(comp_drv_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("Composite LED driver for i.MX6ULL (Ch40-55 demo)");
