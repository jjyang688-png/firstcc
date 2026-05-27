/**
 * uart_sensor.c - UART serial communication driver for i.MX6ULL
 *
 * Consolidates knowledge from:
 *
 * Ch63    - UART / Serial driver: uart_driver, uart_ops,
 *           uart_port, TTY layer, serial core framework
 * Ch43/44 - Device Tree: of_match_table, UART node binding
 * Ch42    - New-style CDEV: alloc_chrdev_region, cdev_init
 * Ch48    - Mutex: protecting serial buffers
 * Ch49    - Blocking IO: wait_queue for RX data
 * Ch52    - Poll: poll_wait for serial port readiness
 * Ch56    - Interrupts: serial RX interrupt handling
 *
 * Creates /dev/uart_sensor — a character device for reading/writing
 * serial data from a UART-connected peripheral (e.g. GPS, PM2.5 sensor).
 *
 *
 * Linux TTY/Serial Architecture Overview:
 *
 *   Userspace                 Kernel Space
 *   ─────────                 ────────────
 *
 *   /dev/ttyS0                ┌─────────────────────┐
 *   /dev/ttymxc0  ← open() → │  TTY Layer           │
 *                             │  ├─ tty_driver       │  ← Line discipline (N_TTY)
 *                             │  ├─ tty_port         │     handles: \n→\r\n, echo, ^C...
 *                             │  └─ tty_ldisc        │
 *                             └─────────┬───────────┘
 *                                       │
 *                             ┌─────────▼───────────┐
 *                             │  Serial Core         │
 *                             │  ├─ uart_driver      │  ← uart_ops callback table:
 *                             │  ├─ uart_port        │     startup/shutdown/tx_empty
 *                             │  └─ uart_ops         │     set_mctrl/stop_tx/start_tx...
 *                             └─────────┬───────────┘
 *                                       │
 *                             ┌─────────▼───────────┐
 *                             │  Hardware Driver     │
 *                             │  (imx.c)             │  ← Register-level UART control
 *                             │  ├─ TX FIFO          │     UARTx->UTXD, UARTx->URXD
 *                             │  ├─ RX FIFO          │     UARTx->UCR1~UCR4...
 *                             │  └─ Baud rate gen    │
 *                             └─────────────────────┘
 *
 *   This driver demonstrates UART concepts by creating a
 *   platform driver that manages a serial-connected device.
 *   For production use, drivers for UART-connected sensors
 *   typically use the serdev subsystem (drivers/tty/serdev/).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/atomic.h>
#include <linux/kfifo.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>

#define UART_SENSOR_NAME  "uart_sensor"
#define DEVICE_COUNT      1

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug output");

/* Default UART configuration */
#define DEFAULT_BAUD_RATE    115200
#define DEFAULT_DATA_BITS    8
#define DEFAULT_STOP_BITS    1
#define DEFAULT_PARITY       'n'    /* 'n'=none, 'e'=even, 'o'=odd */

/*
 * RX ring buffer size: must be power of 2 for kfifo.
 * 4096 bytes is enough for typical sensor messages
 * (GPS NMEA ~80 bytes, PM2.5 ~32 bytes).
 */
#define RX_BUF_SIZE  4096

/* UART frame format constants */
enum uart_parity {
    PARITY_NONE = 0,
    PARITY_EVEN = 1,
    PARITY_ODD  = 2,
};

enum uart_stop_bits {
    STOP_BITS_1 = 0,
    STOP_BITS_2 = 1,
};

/* ---- Device structure ---- */
struct uart_sensor_dev {
    dev_t dev_id;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    /* UART configuration (from device tree or defaults) */
    int baud_rate;
    int data_bits;       /* 7 or 8 */
    int stop_bits;       /* 1 or 2 */
    int parity;          /* PARITY_NONE/EVEN/ODD */

    /*
     * RX data buffer: incoming serial data is stored here.
     * Userspace reads consume data from the buffer.
     *
     * kfifo provides lock-free single-reader/single-writer FIFO.
     */
    DECLARE_KFIFO(rx_fifo, u8, RX_BUF_SIZE);

    struct mutex lock;              /* Protect config and device state */
    wait_queue_head_t rx_wq;        /* Blocking read wait queue */

    atomic_t open_count;
    unsigned long rx_bytes;         /* Total bytes received (stats) */
    unsigned long tx_bytes;         /* Total bytes sent (stats) */
    bool device_opened;             /* Track if serial device is active */
};

static struct uart_sensor_dev *g_uart_dev;

/*
 * Simulated serial data generation.
 *
 * In a real driver, serial data would arrive via:
 *   - UART RX interrupt → tty_flip_buffer_push() → tty_ldisc
 *   - serdev callback:  serdev_device_write() / receive_buf()
 *   - Direct UART register reads
 *
 * Here we simulate periodic sensor data to demonstrate the
 * full RX pipeline without depending on real hardware.
 */
static const char *demo_messages[] = {
    "$GPS,1245.6789,N,01234.5678,E,1,08,1.2,100.5,M,,,,",
    "$SENSOR,PM25=035,PM10=062,TEMP=23.5,HUMI=58",
    "$DATA,V=3.30,A=0.125,P=0.412,T=25.0",
    "$STATUS,OK,UPTIME=3600,ERRORS=0,RX_BYTES=4096",
};

/*
 * Feed simulated data into the RX FIFO.
 * In real hardware, this would be called from an IRQ handler
 * when the UART RX register has new data.
 */
static void uart_sensor_feed_rx(struct uart_sensor_dev *dev)
{
    static int msg_idx;
    const char *msg = demo_messages[msg_idx % ARRAY_SIZE(demo_messages)];
    int len = strlen(msg);
    char newline = '\n';
    int ret;

    /*
     * kfifo_in: non-blocking write to FIFO.
     * If FIFO is full, data is silently dropped (real UARTs
     * also have FIFO overrun behavior).
     */
    ret = kfifo_in(&dev->rx_fifo, msg, len);
    if (ret > 0) {
        kfifo_in(&dev->rx_fifo, &newline, 1);
        dev->rx_bytes += ret + 1;
    }

    msg_idx++;

    /* Wake up any blocked readers */
    wake_up_interruptible(&dev->rx_wq);

    pr_debug(UART_SENSOR_NAME ": RX fed %d bytes, total=%lu\n",
             ret + 1, dev->rx_bytes);
}

/* ---- file_operations ---- */

static int uart_sensor_open(struct inode *inode, struct file *filp)
{
    struct uart_sensor_dev *dev = g_uart_dev;

    filp->private_data = dev;
    atomic_inc(&dev->open_count);

    mutex_lock(&dev->lock);
    if (!dev->device_opened) {
        dev->device_opened = true;
        pr_info(UART_SENSOR_NAME ": opened (%d-%d-%c%d, device active)\n",
                dev->baud_rate, dev->data_bits,
                dev->parity == PARITY_NONE  ? 'N' :
                dev->parity == PARITY_EVEN ? 'E' : 'O',
                dev->stop_bits);
    }
    mutex_unlock(&dev->lock);

    return 0;
}

static int uart_sensor_release(struct inode *inode, struct file *filp)
{
    struct uart_sensor_dev *dev = filp->private_data;

    mutex_lock(&dev->lock);
    dev->device_opened = false;
    mutex_unlock(&dev->lock);

    atomic_dec(&dev->open_count);
    return 0;
}

/*
 * Read serial data from RX buffer.
 *
 * Demonstrates blocking vs non-blocking serial read:
 *   - Default (blocking): wait until data is available in RX FIFO
 *   - O_NONBLOCK: return immediately with -EAGAIN if FIFO is empty
 *
 * This is the same pattern as a real serial port read().
 */
static ssize_t uart_sensor_read(struct file *filp, char __user *buf,
                                size_t count, loff_t *ppos)
{
    struct uart_sensor_dev *dev = filp->private_data;
    unsigned int copied;
    int ret;

    if (filp->f_flags & O_NONBLOCK) {
        /* Non-blocking: check FIFO and return immediately */
        if (kfifo_is_empty(&dev->rx_fifo))
            return -EAGAIN;
    } else {
        /*
         * Blocking mode: wait until data arrives in RX FIFO.
         *
         * In a real UART driver, this wait happens at the TTY layer,
         * which blocks on tty->read_wait until the line discipline
         * pushes data to the read buffer.
         */
        ret = wait_event_interruptible(dev->rx_wq,
                                       !kfifo_is_empty(&dev->rx_fifo));
        if (ret)
            return ret;  /* Signal interrupted */
    }

    /* Read from FIFO into userspace buffer (up to count bytes) */
    ret = kfifo_to_user(&dev->rx_fifo, buf, count, &copied);

    if (ret)
        return ret;

    return copied;
}

/* Write data to the serial TX line. */
static ssize_t uart_sensor_write(struct file *filp, const char __user *buf,
                                 size_t count, loff_t *ppos)
{
    struct uart_sensor_dev *dev = filp->private_data;
    char *kbuf;

    if (count == 0)
        return 0;

    kbuf = kmalloc(count + 1, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    /*
     * In a real UART driver, this would:
     * 1. Write bytes to UART TX FIFO register
     * 2. Wait for TX FIFO to drain (or use TX empty interrupt)
     * 3. Return the number of bytes actually sent
     *
     * The kernel serial core handles this via uart_ops->start_tx()
     * and the TX interrupt handler.
     */

    dev->tx_bytes += count;
    kfree(kbuf);

    /*
     * After writing, simulate the device responding by feeding
     * data into the RX buffer. In real hardware, the response
     * would come via the UART RX interrupt independently.
     */
    uart_sensor_feed_rx(dev);

    return count;
}

/*
 * Poll support for serial port.
 *
 * Real serial ports support:
 *   POLLIN  — data available in RX buffer
 *   POLLOUT — TX buffer has space (almost always true)
 */
static unsigned int uart_sensor_poll(struct file *filp, poll_table *wait)
{
    struct uart_sensor_dev *dev = filp->private_data;
    unsigned int mask = 0;

    poll_wait(filp, &dev->rx_wq, wait);

    if (!kfifo_is_empty(&dev->rx_fifo))
        mask |= POLLIN | POLLRDNORM;    /* Data available */

    mask |= POLLOUT | POLLWRNORM;        /* Always writable */

    return mask;
}

static const struct file_operations uart_sensor_fops = {
    .owner   = THIS_MODULE,
    .open    = uart_sensor_open,
    .release = uart_sensor_release,
    .read    = uart_sensor_read,
    .write   = uart_sensor_write,
    .poll    = uart_sensor_poll,
};

/* ---- Platform driver probe ---- */
static int uart_sensor_probe(struct platform_device *pdev)
{
    struct device *d = &pdev->dev;
    struct uart_sensor_dev *dev;
    u32 val;
    int ret;

    dev_info(d, "uart_sensor probing...\n");

    dev = devm_kzalloc(d, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    g_uart_dev = dev;
    platform_set_drvdata(pdev, dev);

    /*
     * Read UART configuration from device tree.
     *
     * Standard UART DT properties (Documentation/devicetree/bindings/serial/):
     *   - current-speed / baud = baud rate
     *   - data-bits = 7 or 8
     *   - stop-bits = 1 or 2
     *   - parity = "none" / "even" / "odd"
     */
    ret = of_property_read_u32(d->of_node, "current-speed", &val);
    if (ret == 0)
        dev->baud_rate = val;
    else {
        ret = of_property_read_u32(d->of_node, "baud", &val);
        dev->baud_rate = (ret == 0) ? val : DEFAULT_BAUD_RATE;
    }

    dev->data_bits = DEFAULT_DATA_BITS;
    dev->stop_bits = DEFAULT_STOP_BITS;
    dev->parity    = PARITY_NONE;

    /* Initialize kernel FIFO (must be called before use) */
    INIT_KFIFO(dev->rx_fifo);

    mutex_init(&dev->lock);
    init_waitqueue_head(&dev->rx_wq);
    atomic_set(&dev->open_count, 0);

    /* Ch42: Create /dev/uart_sensor */
    ret = alloc_chrdev_region(&dev->dev_id, 0, DEVICE_COUNT,
                              UART_SENSOR_NAME);
    if (ret < 0) {
        dev_err(d, "alloc_chrdev_region: %d\n", ret);
        return ret;
    }

    cdev_init(&dev->cdev, &uart_sensor_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->dev_id, DEVICE_COUNT);
    if (ret) {
        dev_err(d, "cdev_add: %d\n", ret);
        goto err_cdev;
    }

    dev->class = class_create(THIS_MODULE, UART_SENSOR_NAME);
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        goto err_class;
    }

    dev->device = device_create(dev->class, d, dev->dev_id,
                                NULL, UART_SENSOR_NAME);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_device;
    }

    dev_info(d, UART_SENSOR_NAME
             " probed ok (major=%d, baud=%d, %d%c%d)\n",
             MAJOR(dev->dev_id), dev->baud_rate,
             dev->data_bits,
             dev->parity == PARITY_NONE  ? 'N' :
             dev->parity == PARITY_EVEN ? 'E' : 'O',
             dev->stop_bits);

    return 0;

err_device:
    class_destroy(dev->class);
err_class:
    cdev_del(&dev->cdev);
err_cdev:
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);
    return ret;
}

static int uart_sensor_remove(struct platform_device *pdev)
{
    struct uart_sensor_dev *dev = platform_get_drvdata(pdev);

    device_destroy(dev->class, dev->dev_id);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_id, DEVICE_COUNT);

    dev_info(&pdev->dev, UART_SENSOR_NAME
             " removed (rx=%lu bytes, tx=%lu bytes)\n",
             dev->rx_bytes, dev->tx_bytes);
    return 0;
}

/* Device tree match */
static const struct of_device_id uart_sensor_of_match[] = {
    { .compatible = "alientek,uart-sensor" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uart_sensor_of_match);

static struct platform_driver uart_sensor_driver = {
    .probe  = uart_sensor_probe,
    .remove = uart_sensor_remove,
    .driver = {
        .name           = UART_SENSOR_NAME,
        .of_match_table = uart_sensor_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(uart_sensor_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Project");
MODULE_DESCRIPTION("UART serial sensor driver for i.MX6ULL (Ch63 demo)");
