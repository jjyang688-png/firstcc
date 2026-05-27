/**
 * icm20608.c - ICM20608 6-axis SPI sensor driver for i.MX6ULL
 *
 * Consolidates knowledge from:
 *
 * Ch62    - SPI subsystem: spi_driver, spi_write, spi_read,
 *           spi_write_then_read, spi_message, spi_transfer
 * Ch43/44 - Device Tree: of_match_table, SPI node binding
 * Ch42    - New-style CDEV: alloc_chrdev_region, cdev_init,
 *           class_create, device_create
 * Ch48    - Mutex: protecting SPI bus access
 * Ch47    - Atomic operations: open_count
 *
 * ICM20608: 6-axis MEMS sensor (3-axis accel + 3-axis gyro + temp)
 * Interface: SPI (Mode 0/3), max 10 MHz
 * Device file: /dev/icm20608
 *
 * SPI vs I2C key differences demonstrated here:
 *
 *   I2C (ap3216c.c)            SPI (this file)
 *   ─────────────               ────────────
 *   i2c_driver                  spi_driver
 *   reg = <0x1e> (slave addr)  reg = <0> (chip select)
 *   i2c_smbus_read_byte_data   spi_write_then_read (combined)
 *   i2c_transfer (msgs)        spi_sync / spi_message
 *   2-wire (SCL + SDA)         4-wire (CS, CLK, MOSI, MISO)
 *   open-drain, slow             push-pull, fast
 *   addressed by slave addr      addressed by chip select line
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include "icm20608.h"

#define ICM20608_NAME  "icm20608"
#define DEVICE_COUNT   1

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug output");

/* ICM20608 registers */
#define REG_WHO_AM_I      0x75   /* Should return 0x98 (ICM20608G) or 0xAF */
#define REG_USER_CTRL     0x6A
#define REG_PWR_MGMT_1    0x6B   /* Power management */
#define REG_PWR_MGMT_2    0x6C
#define REG_FIFO_EN       0x23

/* Accelerometer registers (X/Y/Z, 16-bit each) */
#define REG_ACCEL_XOUT_H  0x3B
#define REG_ACCEL_XOUT_L  0x3C
#define REG_ACCEL_YOUT_H  0x3D
#define REG_ACCEL_YOUT_L  0x3E
#define REG_ACCEL_ZOUT_H  0x3F
#define REG_ACCEL_ZOUT_L  0x40

/* Temperature registers */
#define REG_TEMP_OUT_H    0x41
#define REG_TEMP_OUT_L    0x42

/* Gyroscope registers (X/Y/Z, 16-bit each) */
#define REG_GYRO_XOUT_H   0x43
#define REG_GYRO_XOUT_L   0x44
#define REG_GYRO_YOUT_H   0x45
#define REG_GYRO_YOUT_L   0x46
#define REG_GYRO_ZOUT_H   0x47
#define REG_GYRO_ZOUT_L   0x48

/* Power management bits */
#define PWR_MGMT1_DEVICE_RESET  (1 << 7)
#define PWR_MGMT1_SLEEP         (1 << 6)
#define PWR_MGMT1_CLKSEL_AUTO   0x01

/* ---- Device structure ---- */
struct icm20608_dev {
    dev_t dev_id;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct spi_device *spi;

    struct mutex lock;           /* Protect SPI bus access */
    atomic_t open_count;         /* Device open counter */

    struct icm20608_full data;   /* Latest sensor readings */
};

static struct icm20608_dev *g_icm20608;

/* ---- SPI helper: read a single register ---- */
static int icm20608_read_reg(struct icm20608_dev *dev, u8 reg, u8 *val)
{
    int ret;

    /*
     * SPI write-then-read pattern:
     *   1. Send register address (MSB=1 indicates READ in many SPI devices,
     *      ICM20608 needs | 0x80 for read)
     *   2. Read the data byte
     *
     * spi_write_then_read() performs both operations in a single SPI
     * transaction (CS active throughout), which is essential for
     * register-based SPI devices.
     */
    u8 tx_buf = reg | 0x80;  /* Set R/W bit: 1 = read */

    ret = spi_write_then_read(dev->spi, &tx_buf, 1, val, 1);
    if (ret < 0)
        dev_err(&dev->spi->dev,
                "SPI read reg 0x%02x failed: %d\n", reg, ret);

    return ret;
}

/* ---- SPI helper: write a single register ---- */
static int icm20608_write_reg(struct icm20608_dev *dev, u8 reg, u8 val)
{
    /*
     * spi_write() sends data on MOSI line.
     * For ICM20608, first byte = register address (MSB=0 for write),
     * second byte = data to write.
     */
    u8 tx_buf[2] = { reg & 0x7F, val };  /* Clear R/W bit: 0 = write */

    return spi_write(dev->spi, tx_buf, sizeof(tx_buf));
}

/* ---- SPI burst read: read multiple consecutive registers ---- */
static int icm20608_read_burst(struct icm20608_dev *dev, u8 start_reg,
                                u8 *buf, int len)
{
    u8 tx_buf = start_reg | 0x80;
    int ret;

    /*
     * SPI burst read: send the starting register address once,
     * then read len bytes in a single transaction.
     * The ICM20608 auto-increments the register address.
     *
     * Alternative approach using spi_message + spi_transfer:
     *
     *   struct spi_message msg;
     *   struct spi_transfer xfer[2] = { ... };
     *   spi_message_init(&msg);
     *   spi_message_add_tail(&xfer[0], &msg);
     *   spi_message_add_tail(&xfer[1], &msg);
     *   ret = spi_sync(dev->spi, &msg);
     */
    ret = spi_write_then_read(dev->spi, &tx_buf, 1, buf, len);
    if (ret < 0)
        dev_err(&dev->spi->dev,
                "SPI burst read from 0x%02x failed: %d\n", start_reg, ret);

    return ret;
}

/* Read all sensor data (14 bytes: accel 6 + temp 2 + gyro 6) */
static int icm20608_read_all(struct icm20608_dev *dev)
{
    u8 buf[14];
    int ret;

    ret = icm20608_read_burst(dev, REG_ACCEL_XOUT_H, buf, sizeof(buf));
    if (ret < 0)
        return ret;

    mutex_lock(&dev->lock);

    /* ICM20608 data is big-endian: MSB first, LSB second */
    dev->data.accel_x = ((s16)buf[0]  << 8) | buf[1];
    dev->data.accel_y = ((s16)buf[2]  << 8) | buf[3];
    dev->data.accel_z = ((s16)buf[4]  << 8) | buf[5];
    dev->data.temp    = ((s16)buf[6]  << 8) | buf[7];
    dev->data.gyro_x  = ((s16)buf[8]  << 8) | buf[9];
    dev->data.gyro_y  = ((s16)buf[10] << 8) | buf[11];
    dev->data.gyro_z  = ((s16)buf[12] << 8) | buf[13];

    mutex_unlock(&dev->lock);
    return 0;
}

/* ---- file_operations ---- */
static int icm20608_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_icm20608;
    atomic_inc(&g_icm20608->open_count);
    pr_info(ICM20608_NAME ": opened (open_count=%d)\n",
            atomic_read(&g_icm20608->open_count));
    return 0;
}

static int icm20608_release(struct inode *inode, struct file *filp)
{
    atomic_dec(&g_icm20608->open_count);
    return 0;
}

static ssize_t icm20608_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *ppos)
{
    struct icm20608_dev *dev = filp->private_data;
    struct icm20608_full data;
    int ret;

    ret = icm20608_read_all(dev);
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

static long icm20608_ioctl(struct file *filp, unsigned int cmd,
                            unsigned long arg)
{
    struct icm20608_dev *dev = filp->private_data;
    struct icm20608_full full;
    struct icm20608_data axis;
    int ret = 0;

    /* Read fresh data before each ioctl */
    ret = icm20608_read_all(dev);
    if (ret)
        return ret;

    mutex_lock(&dev->lock);
    memcpy(&full, &dev->data, sizeof(full));
    mutex_unlock(&dev->lock);

    switch (cmd) {
    case ICM20608_GET_ACCEL:
        axis.x = full.accel_x;
        axis.y = full.accel_y;
        axis.z = full.accel_z;
        if (copy_to_user((void __user *)arg, &axis, sizeof(axis)))
            ret = -EFAULT;
        break;

    case ICM20608_GET_GYRO:
        axis.x = full.gyro_x;
        axis.y = full.gyro_y;
        axis.z = full.gyro_z;
        if (copy_to_user((void __user *)arg, &axis, sizeof(axis)))
            ret = -EFAULT;
        break;

    case ICM20608_GET_TEMP:
        if (copy_to_user((void __user *)arg, &full.temp, sizeof(short)))
            ret = -EFAULT;
        break;

    case ICM20608_GET_ALL:
        if (copy_to_user((void __user *)arg, &full, sizeof(full)))
            ret = -EFAULT;
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

static const struct file_operations icm20608_fops = {
    .owner          = THIS_MODULE,
    .open           = icm20608_open,
    .release        = icm20608_release,
    .read           = icm20608_read,
    .unlocked_ioctl = icm20608_ioctl,
};

/* ---- SPI probe (Ch62) ---- */
static int icm20608_probe(struct spi_device *spi)
{
    struct icm20608_dev *dev;
    int ret;
    u8 whoami;

    dev_info(&spi->dev, "icm20608 probing (CS=%d, max_speed=%u Hz)...\n",
             spi->chip_select, spi->max_speed_hz);

    /*
     * SPI mode configuration is set in device tree:
     *   spi-cpha / spi-cpol properties
     * The SPI core applies these before probe().
     *
     * Common SPI modes:
     *   Mode 0: CPOL=0, CPHA=0 (default, used by ICM20608)
     *   Mode 1: CPOL=0, CPHA=1
     *   Mode 2: CPOL=1, CPHA=0
     *   Mode 3: CPOL=1, CPHA=1
     */

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_icm20608 = dev;
    dev->spi = spi;
    spi_set_drvdata(spi, dev);   /* Store for remove / suspend */

    mutex_init(&dev->lock);
    atomic_set(&dev->open_count, 0);

    /* Verify chip presence via WHO_AM_I register */
    ret = icm20608_read_reg(dev, REG_WHO_AM_I, &whoami);
    if (ret) {
        dev_err(&spi->dev, "failed to read WHO_AM_I: %d\n", ret);
        return -ENODEV;
    }
    dev_info(&spi->dev, "WHO_AM_I = 0x%02x\n", whoami);

    /* Wake the device and select auto clock source */
    icm20608_write_reg(dev, REG_PWR_MGMT_1, PWR_MGMT1_CLKSEL_AUTO);
    msleep(10);

    /*
     * Ch42: Create character device /dev/icm20608.
     *
     * Note: SPI/I2C drivers that create CDEVs follow the same pattern:
     *   alloc_chrdev_region → cdev_init → cdev_add
     *   → class_create → device_create
     *
     * This is identical to what ap3216c.c does for I2C.
     */
    ret = alloc_chrdev_region(&dev->dev_id, 0, DEVICE_COUNT, ICM20608_NAME);
    if (ret < 0) {
        dev_err(&spi->dev, "alloc_chrdev_region: %d\n", ret);
        return ret;
    }

    cdev_init(&dev->cdev, &icm20608_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->dev_id, DEVICE_COUNT);
    if (ret) {
        dev_err(&spi->dev, "cdev_add: %d\n", ret);
        goto err_cdev;
    }

    dev->class = class_create(THIS_MODULE, ICM20608_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        dev_err(&spi->dev, "class_create: %d\n", ret);
        goto err_class;
    }

    dev->device = device_create(dev->class, &spi->dev,
                                dev->dev_id, NULL, ICM20608_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_device;
    }

    dev_info(&spi->dev, "icm20608 probed ok (major=%d, mode=%d)\n",
             MAJOR(dev->dev_id), spi->mode);
    return 0;

err_device:
    class_destroy(dev->class);
err_class:
    cdev_del(&dev->cdev);
err_cdev:
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);
    return ret;
}

static void icm20608_remove(struct spi_device *spi)
{
    struct icm20608_dev *dev = spi_get_drvdata(spi);

    /* Put device to sleep */
    icm20608_write_reg(dev, REG_PWR_MGMT_1, PWR_MGMT1_SLEEP);

    device_destroy(dev->class, dev->dev_id);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);

    dev_info(&spi->dev, "icm20608 removed\n");
}

/* Ch43/44: device tree match */
static const struct of_device_id icm20608_of_match[] = {
    { .compatible = "alientek,icm20608" },
    { .compatible = "invensense,icm20608" },  /* also match mainline */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, icm20608_of_match);

/* Traditional SPI device ID (non-DT matching fallback) */
static const struct spi_device_id icm20608_id[] = {
    { "icm20608", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, icm20608_id);

/*
 * Ch62: SPI driver — the core SPI structure.
 *
 * Compare with i2c_driver (ap3216c.c):
 *   - i2c_driver  →  spi_driver
 *   - .id_table   →  .id_table (same purpose, different type)
 *   - probe(client, id)  →  probe(spi)  — one param instead of two
 */
static struct spi_driver icm20608_spi_driver = {
    .probe    = icm20608_probe,
    .remove   = icm20608_remove,
    .id_table = icm20608_id,
    .driver   = {
        .name           = ICM20608_NAME,
        .of_match_table = icm20608_of_match,
        .owner          = THIS_MODULE,
    },
};

module_spi_driver(icm20608_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("ICM20608 SPI 6-axis sensor driver for i.MX6ULL (Ch62 demo)");
