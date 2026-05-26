/**
 * key_input.c - Key input driver for i.MX6ULL
 *
 * Consolidates knowledge from:
 *
 * Ch43/44 - Device Tree: of_match_table, gpiod_get, of_property_read_u32
 * Ch45    - GPIO subsystem: gpiod API, gpiod_to_irq
 * Ch47    - Spinlock: protecting data shared with hardirq context
 * Ch56    - Interrupts: request_irq, free_irq, irq_handler_t
 * Ch59    - Bottom-half tasklet (demonstrated conceptually)
 * Ch60    - Bottom-half workqueue: INIT_DELAYED_WORK, schedule_delayed_work
 * Ch58    - Input subsystem: input_allocate_device, input_register_device,
 *           input_report_key, input_sync
 *
 * Reports key events via /dev/input/eventX. Supports blocking read,
 * poll, and async notification through the input subsystem.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#define KEY_DRV_NAME "key_input"
#define DEFAULT_DEBOUNCE_MS 20

/* ---- Device structure ---- */
struct key_dev {
    struct gpio_desc *key_gpio;
    int irq;
    int key_code;                     /* e.g. KEY_ENTER */
    int debounce_ms;

    struct input_dev *input;          /* Ch58: input device */

    spinlock_t lock;                  /* Ch47: protect data shared with irq */
    struct delayed_work work;         /* Ch60: bottom-half debounce work */
    bool last_state;                  /* Track to detect edges */
};

static struct key_dev *g_key_dev;

/* ---- Bottom-half: delayed work for debounce (Ch60) ---- */
static void key_work_func(struct work_struct *ws)
{
    struct delayed_work *dwork = to_delayed_work(ws);
    struct key_dev *dev = container_of(dwork, struct key_dev, work);
    unsigned long flags;
    int val;

    /* Read stable GPIO value after debounce delay */
    val = gpiod_get_value(dev->key_gpio);

    spin_lock_irqsave(&dev->lock, flags);

    /* Only report on edge (press) */
    if (val != dev->last_state) {
        dev->last_state = val;
        spin_unlock_irqrestore(&dev->lock, flags);

        /* Report to input subsystem */
        input_report_key(dev->input, dev->key_code, val ? 0 : 1);
        input_sync(dev->input);

        pr_debug(KEY_DRV_NAME ": key %s (code=%d)\n",
                 val ? "released" : "pressed", dev->key_code);
    } else {
        spin_unlock_irqrestore(&dev->lock, flags);
    }
}

/* ---- Top-half: interrupt handler (Ch56) ---- */
static irqreturn_t key_irq_handler(int irq, void *data)
{
    struct key_dev *dev = data;

    /* Schedule debounce work - bottom half (Ch60) */
    schedule_delayed_work(&dev->work,
                          msecs_to_jiffies(dev->debounce_ms));

    return IRQ_HANDLED;
}

/* ---- Platform probe ---- */
static int key_input_probe(struct platform_device *pdev)
{
    struct device *d = &pdev->dev;
    struct input_dev *input;
    struct key_dev *dev;
    int ret;
    u32 key_code;

    dev_info(d, "key_input probing...\n");

    dev = devm_kzalloc(d, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_key_dev = dev;
    platform_set_drvdata(pdev, dev);

    /* Ch45: get GPIO from device tree */
    dev->key_gpio = devm_gpiod_get(d, "key", GPIOD_IN);
    if (IS_ERR(dev->key_gpio)) {
        ret = PTR_ERR(dev->key_gpio);
        dev_err(d, "failed to get key-gpios: %d\n", ret);
        return ret;
    }

    /* Read key code from device tree, default to KEY_ENTER */
    ret = of_property_read_u32(d->of_node, "key-code", &key_code);
    if (ret)
        key_code = KEY_ENTER;
    dev->key_code = key_code;

    /* Read debounce time from device tree */
    ret = of_property_read_u32(d->of_node, "debounce-interval",
                               &dev->debounce_ms);
    if (ret)
        dev->debounce_ms = DEFAULT_DEBOUNCE_MS;

    dev->last_state = gpiod_get_value(dev->key_gpio);

    /* Initialize spinlock */
    spin_lock_init(&dev->lock);

    /* Ch60: initialize bottom-half delayed work */
    INIT_DELAYED_WORK(&dev->work, key_work_func);

    /* Ch58: allocate and register input device */
    input = devm_input_allocate_device(d);
    if (!input)
        return -ENOMEM;

    input->name = KEY_DRV_NAME;
    input->phys = "key_input/input0";
    input->id.bustype = BUS_HOST;
    input->id.vendor  = 0x0001;
    input->id.product = 0x0001;
    input->id.version = 0x0100;

    __set_bit(EV_KEY, input->evbit);
    __set_bit(dev->key_code, input->keybit);

    ret = input_register_device(input);
    if (ret) {
        dev_err(d, "input_register_device failed: %d\n", ret);
        return ret;
    }
    dev->input = input;

    /* Ch56: request GPIO interrupt */
    dev->irq = gpiod_to_irq(dev->key_gpio);
    if (dev->irq < 0) {
        dev_err(d, "gpiod_to_irq failed: %d\n", dev->irq);
        return dev->irq;
    }

    ret = devm_request_irq(d, dev->irq, key_irq_handler,
                           IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                           KEY_DRV_NAME, dev);
    if (ret) {
        dev_err(d, "request_irq failed: %d\n", ret);
        return ret;
    }

    dev_info(d, KEY_DRV_NAME " probed ok (irq=%d code=%d)\n",
             dev->irq, dev->key_code);
    return 0;
}

/* ---- Platform remove ---- */
static int key_input_remove(struct platform_device *pdev)
{
    struct key_dev *dev = platform_get_drvdata(pdev);

    cancel_delayed_work_sync(&dev->work);

    dev_info(&pdev->dev, KEY_DRV_NAME " removed\n");
    return 0;
}

/* Ch43/44: device tree match table */
static const struct of_device_id key_input_of_match[] = {
    { .compatible = "alientek,key-input" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, key_input_of_match);

/* Ch54/55: platform driver */
static struct platform_driver key_input_platform_driver = {
    .probe  = key_input_probe,
    .remove = key_input_remove,
    .driver = {
        .name           = KEY_DRV_NAME,
        .of_match_table = key_input_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(key_input_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("Key input driver for i.MX6ULL (Ch58/60 demo)");
