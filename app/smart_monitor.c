/**
 * smart_monitor.c - Unified sensor monitor application
 *
 * Uses epoll to simultaneously monitor all sensor devices,
 * demonstrating IO multiplexing at the application level.
 *
 * Devices monitored:
 *   - /dev/comp_drv       (LED status — poll/read)
 *   - /dev/input/eventX   (Key events — blocking read via epoll)
 *   - /dev/ap3216c        (ALS/PS/IR sensor)
 *   - /dev/icm20608       (Accel/Gyro/Temp)
 *   - /dev/uart_sensor    (Serial sensor data)
 *   - /dev/sys_monitor    (System status — misc device)
 *
 * Key knowledge points:
 *   - epoll_create / epoll_ctl / epoll_wait (> poll/select)
 *   - Non-blocking IO with O_NONBLOCK
 *   - stdin (terminal keyboard) in epoll loop ('q' to quit)
 *   - CSV data logging
 *   - Signal handling (SIGINT/SIGTERM graceful shutdown)
 *
 * Usage:
 *   ./smart_monitor [csv_output_file]
 *
 * Interactive keys:
 *   q - quit
 *   l - toggle LED on/off
 *   b - start LED blink
 *   s - show sensor status summary
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <time.h>

/* ioctl definitions — keep in sync with drivers */
#define COMP_DRV_MAGIC  'C'
#define COMP_DRV_GET_STATUS       _IOR(COMP_DRV_MAGIC, 2, unsigned long)
#define COMP_DRV_START_BLINK      _IO(COMP_DRV_MAGIC, 3)
#define COMP_DRV_STOP_BLINK       _IO(COMP_DRV_MAGIC, 4)

/* Device paths */
#define DEV_COMP_DRV   "/dev/comp_drv"
#define DEV_KEY_INPUT  "/dev/input/event0"
#define DEV_AP3216C    "/dev/ap3216c"
#define DEV_ICM20608   "/dev/icm20608"
#define DEV_UART       "/dev/uart_sensor"
#define DEV_SYS_MON    "/dev/sys_monitor"

/* Data structures — keep in sync with drivers */
struct comp_drv_status {
    int led_state;
    int blink_period_ms;
    unsigned long read_count;
    unsigned long write_count;
};

struct ap3216c_data {
    unsigned short ir;
    unsigned short als;
    unsigned short ps;
};

struct icm20608_full {
    short accel_x, accel_y, accel_z;
    short temp;
    short gyro_x, gyro_y, gyro_z;
};

struct sys_mon_status {
    unsigned long uptime_sec;
    unsigned long read_count;
    int heartbeat;
};

/* ---- Globals ---- */
static volatile sig_atomic_t g_running = 1;
static FILE *g_csv = NULL;
static int g_led_state = 0;

static void sig_handler(int signo)
{
    (void)signo;
    g_running = 0;
}

static void write_csv(const char *fmt, ...)
{
    va_list args;
    time_t now;
    struct tm *tm_info;
    char time_buf[32];

    if (!g_csv) return;

    time(&now);
    tm_info = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(g_csv, "%s,", time_buf);

    va_start(args, fmt);
    vfprintf(g_csv, fmt, args);
    va_end(args);

    fprintf(g_csv, "\n");
    fflush(g_csv);
}

/* ---- epoll helpers ---- */

static int add_epoll_fd(int epfd, int fd, const char *name)
{
    struct epoll_event ev;
    ev.events  = EPOLLIN;             /* Level-triggered (default) */
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        fprintf(stderr, "epoll_ctl add %s failed: %s\n",
                name, strerror(errno));
        return -1;
    }
    printf("[epoll] monitoring %s (fd=%d)\n", name, fd);
    return 0;
}

/* ---- Sensor read functions ---- */

static void read_comp_drv(int fd)
{
    struct comp_drv_status st;
    ssize_t n = read(fd, &st, sizeof(st));
    if (n == sizeof(st)) {
        g_led_state = st.led_state;
        const char *state_str = st.led_state == 1 ? "ON " :
                                st.led_state == 2 ? "BLK" : "OFF";
        printf("[LED  ] state=%s period=%dms r=%lu w=%lu\n",
               state_str, st.blink_period_ms,
               st.read_count, st.write_count);
    }
}

static void read_ap3216c(int fd)
{
    struct ap3216c_data data;
    ssize_t n = read(fd, &data, sizeof(data));
    if (n == sizeof(data)) {
        printf("[ALS  ] IR=%-5u ALS=%-5u PS=%-5u\n",
               data.ir, data.als, data.ps);
        write_csv("AP3216C,%u,%u,%u", data.ir, data.als, data.ps);
    }
}

static void read_icm20608(int fd)
{
    struct icm20608_full data;
    ssize_t n = read(fd, &data, sizeof(data));
    if (n == sizeof(data)) {
        printf("[6AXIS] AX=%6d AY=%6d AZ=%6d | "
               "GX=%6d GY=%6d GZ=%6d | T=%d\n",
               data.accel_x, data.accel_y, data.accel_z,
               data.gyro_x, data.gyro_y, data.gyro_z,
               data.temp);
        write_csv("ICM20608,%d,%d,%d,%d,%d,%d,%d",
                  data.accel_x, data.accel_y, data.accel_z,
                  data.temp,
                  data.gyro_x, data.gyro_y, data.gyro_z);
    }
}

static void read_uart(int fd)
{
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        /* Trim trailing newline */
        if (buf[n-1] == '\n') buf[n-1] = '\0';
        printf("[UART ] %s\n", buf);
        write_csv("UART,%s", buf);
    }
}

static void read_sys_mon(int fd)
{
    struct sys_mon_status st;
    ssize_t n = read(fd, &st, sizeof(st));
    if (n == sizeof(st)) {
        unsigned long h = st.uptime_sec / 3600;
        unsigned long m = (st.uptime_sec % 3600) / 60;
        unsigned long s = st.uptime_sec % 60;
        printf("[SYS  ] uptime=%luh%02lum%02lus reads=%lu heartbeat=%ds\n",
               h, m, s, st.read_count, st.heartbeat);
    }
}

/* ---- LED control ---- */

static void led_on(int fd)
{
    write(fd, "on", 2);
    printf("[CTRL ] LED → ON\n");
    g_led_state = 1;
}

static void led_off(int fd)
{
    write(fd, "off", 3);
    printf("[CTRL ] LED → OFF\n");
    g_led_state = 0;
}

static void led_blink(int fd)
{
    ioctl(fd, COMP_DRV_SET_BLINK_PERIOD, 500);
    write(fd, "blink", 5);
    printf("[CTRL ] LED → BLINK (500ms)\n");
    g_led_state = 2;
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    const char *csv_path = (argc > 1) ? argv[1] : NULL;
    struct epoll_event events[8];
    int epfd, fd_led, fd_input, fd_als, fd_imu, fd_uart, fd_sys;
    int fd_stdin;
    int nfds, i;

    /* Setup signal handlers for graceful shutdown */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Open CSV log file */
    if (csv_path) {
        g_csv = fopen(csv_path, "a");
        if (g_csv)
            printf("[LOG  ] CSV output: %s\n", csv_path);
        else
            perror("fopen csv");
    }

    /* Open all devices (non-blocking for epoll) */
    fd_led   = open(DEV_COMP_DRV,  O_RDWR  | O_NONBLOCK);
    fd_input = open(DEV_KEY_INPUT, O_RDONLY | O_NONBLOCK);
    fd_als   = open(DEV_AP3216C,   O_RDONLY | O_NONBLOCK);
    fd_imu   = open(DEV_ICM20608,  O_RDONLY | O_NONBLOCK);
    fd_uart  = open(DEV_UART,      O_RDWR  | O_NONBLOCK);
    fd_sys   = open(DEV_SYS_MON,   O_RDONLY | O_NONBLOCK);

    /* Create epoll instance */
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }

    /* Register all fds with epoll */
    add_epoll_fd(epfd, STDIN_FILENO, "stdin");
    add_epoll_fd(epfd, fd_led,       "comp_drv");
    add_epoll_fd(epfd, fd_input,     "key_input");
    if (fd_als > 0)
        add_epoll_fd(epfd, fd_als,   "ap3216c");
    if (fd_imu > 0)
        add_epoll_fd(epfd, fd_imu,   "icm20608");
    if (fd_uart > 0)
        add_epoll_fd(epfd, fd_uart,  "uart_sensor");
    if (fd_sys > 0)
        add_epoll_fd(epfd, fd_sys,   "sys_monitor");

    printf("\n=== Smart Environment Monitor ===\n");
    printf("Commands: q=quit  l=toggle LED  b=blink  s=status  "
           "o=LED off\n\n");

    /* Epoll event loop */
    while (g_running) {
        nfds = epoll_wait(epfd, events, 8, 2000);  /* 2s timeout */

        for (i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == STDIN_FILENO) {
                char c;
                if (read(STDIN_FILENO, &c, 1) <= 0) continue;
                switch (c) {
                case 'q':
                    printf("\n[CTRL ] Shutting down...\n");
                    g_running = 0;
                    break;
                case 'l':
                    if (g_led_state == 1)
                        led_off(fd_led);
                    else
                        led_on(fd_led);
                    break;
                case 'b':
                    led_blink(fd_led);
                    break;
                case 'o':
                    led_off(fd_led);
                    break;
                case 's':
                    read_comp_drv(fd_led);
                    read_sys_mon(fd_sys);
                    break;
                }
            } else if (fd == fd_led) {
                read_comp_drv(fd);
            } else if (fd == fd_input) {
                /* Key event — just acknowledge */
                struct input_event {
                    struct timeval { long tv_sec, tv_usec; } time;
                    unsigned short type, code;
                    int value;
                } ev;
                ssize_t n = read(fd, &ev, sizeof(ev));
                if (n == sizeof(ev) && ev.type == 1) { /* EV_KEY */
                    printf("[KEY  ] code=%d %s\n",
                           ev.code, ev.value ? "PRESSED" : "RELEASED");
                    write_csv("KEY,%d,%d", ev.code, ev.value);
                }
            } else if (fd == fd_als) {
                read_ap3216c(fd);
            } else if (fd == fd_imu) {
                read_icm20608(fd);
            } else if (fd == fd_uart) {
                read_uart(fd);
            } else if (fd == fd_sys) {
                read_sys_mon(fd);
            }
        }

        if (!g_running) break;
    }

    /* Cleanup */
    printf("\n[CTRL ] Cleaning up...\n");
    if (g_csv) fclose(g_csv);
    close(fd_led);
    close(fd_input);
    if (fd_als  > 0) close(fd_als);
    if (fd_imu  > 0) close(fd_imu);
    if (fd_uart > 0) close(fd_uart);
    if (fd_sys  > 0) close(fd_sys);
    close(epfd);

    printf("[CTRL ] Goodbye.\n");
    return 0;
}
