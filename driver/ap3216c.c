/**
 * ap3216c.c - I2C ambient light & proximity sensor driver for i.MX6ULL
 *
 * Consolidates knowledge from:
 *
 * Ch43/44 - Device Tree: of_match_table, compatible string
 * Ch61    - I2C subsystem: i2c_add_driver, i2c_probe,
 *           i2c_transfer, i2c_smbus_read_byte_data
 * Ch42    - New-style CDEV: alloc_chrdev_region, cdev_init,
 *           class_create, device_create
 * Ch48    - Mutex: protecting I2C bus access
 * Ch46-47 - Atomic operations
 *
 * AP3216C sensor: ambient light (ALS), proximity (PS), IR
 * I2C address: 0x1E
 * Device file: /dev/ap3216c
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/atomic.h>

#define AP3216C_NAME  "ap3216c"
#define AP3216C_ADDR  0x1E
#define DEVICE_COUNT  1

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug output");

/* AP3216C registers */
#define REG_SYS_CONF       0x00
#define REG_IR_DATA_LOW    0x0A
#define REG_IR_DATA_HIGH   0x0B
#define REG_ALS_DATA_LOW   0x0C
#define REG_ALS_DATA_HIGH  0x0D
#define REG_PS_DATA_LOW    0x0E
#define REG_PS_DATA_HIGH   0x0F

/* System configuration bits */
#define SYS_CONF_ALS_ON    (1 << 0)
#define SYS_CONF_PS_ON     (1 << 1)
#define SYS_CONF_RESET     (1 << 7)

struct ap3216c_data {
    u16 ir;
    u16 als;
    u16 ps;
};

/* ---- Device structure ---- */
struct ap3216c_dev {
    dev_t dev_id;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct i2c_client *client;

    struct mutex lock;               /* Ch48: protect I2C access */
    atomic_t open_count;             /* Ch47: atomic counter */

    struct ap3216c_data data;        /* Latest sensor readings */
};

static struct ap3216c_dev *g_ap3216c;

/* Ch61: read a single register via I2C */
static int ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg, u8 *val)
{
    struct i2c_client *client = dev->client;
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "i2c read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }

    *val = (u8)ret;
    return 0;
}

/* Ch61: write a single register via I2C */
static int ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 val)
{
    struct i2c_client *client = dev->client;
    int ret;

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0)
        dev_err(&client->dev, "i2c write reg 0x%02x failed: %d\n", reg, ret);

    return ret;
}

/* Read all sensor data */
static int ap3216c_read_data(struct ap3216c_dev *dev)
{
    u8 reg_addr = REG_IR_DATA_LOW;
    u8 buf[6];
    int ret;

    /* AP3216C allows burst read: read 6 bytes starting from IR_DATA_LOW */
    struct i2c_msg msgs[2] = {
        { /* First: write register address */
            .addr   = dev->client->addr,
            .flags  = 0,
            .len    = 1,
            .buf    = &reg_addr,
        },
        { /* Second: read 6 bytes of data */
            .addr   = dev->client->addr,
            .flags  = I2C_M_RD,
            .len    = 6,
            .buf    = buf,
        },
    };

    ret = i2c_transfer(dev->client->adapter, msgs, 2);
    if (ret < 0) {
        dev_err(&dev->client->dev, "i2c_transfer failed: %d\n", ret);
        return ret;
    }

    mutex_lock(&dev->lock);
    dev->data.ir  = ((u16)buf[1] << 8) | buf[0];
    dev->data.als = ((u16)buf[3] << 8) | buf[2];
    dev->data.ps  = ((u16)buf[5] << 8) | buf[4];
    mutex_unlock(&dev->lock);

    return 0;
}

/* ---- file_operations ---- */
static int ap3216c_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_ap3216c;
    atomic_inc(&g_ap3216c->open_count);
    return 0;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    atomic_dec(&g_ap3216c->open_count);
    return 0;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf,
                            size_t count, loff_t *ppos)
{
    struct ap3216c_dev *dev = filp->private_data;
    struct ap3216c_data data;
    int ret;

    ret = ap3216c_read_data(dev);
    if (ret)
        return ret;

    mutex_lock(&dev->lock);
    memcpy(&data, &dev->data, sizeof(data));
    mutex_unlock(&dev->lock);

    if (count > sizeof(data))
        count = sizeof(data);

    if (copy_to_user(buf, &data, count))
        return -EFAULT;

    return count;
}

static const struct file_operations ap3216c_fops = {
    .owner   = THIS_MODULE,
    .open    = ap3216c_open,
    .release = ap3216c_release,
    .read    = ap3216c_read,
};

/* ---- I2C driver probe (Ch61) ---- */
static int ap3216c_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct ap3216c_dev *dev;
    int ret;
    u8 val;

    dev_info(&client->dev, "ap3216c probing at addr 0x%02x...\n", client->addr);

    /* Check if device is present by reading system config register */
    ret = i2c_smbus_read_byte_data(client, REG_SYS_CONF);
    if (ret < 0) {
        dev_err(&client->dev, "failed to read chip ID: %d\n", ret);
        return -ENODEV;
    }

    dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_ap3216c = dev;
    dev->client = client;
    i2c_set_clientdata(client, dev);

    mutex_init(&dev->lock);
    atomic_set(&dev->open_count, 0);

    /* Enable ALS and PS functions */
    ap3216c_write_reg(dev, REG_SYS_CONF, SYS_CONF_ALS_ON | SYS_CONF_PS_ON);
    msleep(10);  /* Wait for sensor to stabilize */

    /* Verify by reading a register */
    ret = ap3216c_read_reg(dev, REG_SYS_CONF, &val);
    if (ret) {
        dev_err(&client->dev, "verification read failed\n");
        return ret;
    }
    dev_info(&client->dev, "SYS_CONF = 0x%02x\n", val);

    /* Ch42: allocate device number & create device node */
    ret = alloc_chrdev_region(&dev->dev_id, 0, DEVICE_COUNT, AP3216C_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "alloc_chrdev_region: %d\n", ret);
        return ret;
    }

    cdev_init(&dev->cdev, &ap3216c_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->dev_id, DEVICE_COUNT);
    if (ret) {
        dev_err(&client->dev, "cdev_add: %d\n", ret);
        goto err_cdev;
    }

    dev->class = class_create(THIS_MODULE, AP3216C_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        goto err_class;
    }

    dev->device = device_create(dev->class, &client->dev,
                                dev->dev_id, NULL, AP3216C_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_device;
    }

    dev_info(&client->dev, "ap3216c probed ok (major=%d)\n",
             MAJOR(dev->dev_id));
    return 0;

err_device:
    class_destroy(dev->class);
err_class:
    cdev_del(&dev->cdev);
err_cdev:
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);
    return ret;
}

static void ap3216c_remove(struct i2c_client *client)
{
    struct ap3216c_dev *dev = i2c_get_clientdata(client);

    /* Power down sensor */
    ap3216c_write_reg(dev, REG_SYS_CONF, 0);

    device_destroy(dev->class, dev->dev_id);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);

    dev_info(&client->dev, "ap3216c removed\n");
}

/* Ch43/44: device tree match */
static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = "alientek,ap3216c" },
    { .compatible = "liteon,ap3216c" },  /* also match mainline binding */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ap3216c_of_match);

/* I2C device ID for non-DT matching */
static const struct i2c_device_id ap3216c_id[] = {
    { "ap3216c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ap3216c_id);

/* Ch61: I2C driver */
static struct i2c_driver ap3216c_driver = {
    .probe    = ap3216c_probe,
    .remove   = ap3216c_remove,
    .id_table = ap3216c_id,
    .driver   = {
        .name           = AP3216C_NAME,
        .of_match_table = ap3216c_of_match,
        .owner          = THIS_MODULE,
    },
};

module_i2c_driver(ap3216c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("AP3216C I2C sensor driver for i.MX6ULL (Ch61 demo)");
