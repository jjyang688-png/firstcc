/**
 * test_uart.c - UART serial sensor test
 *
 * Demonstrates reading/writing via a UART-connected device
 * through /dev/uart_sensor (covers Ch63 concepts from userspace).
 *
 * This is analogous to reading from a real serial port like
 * /dev/ttymxc0, but through the kernel driver interface.
 *
 * Usage: ./test_uart [read|write|poll] [timeout_ms]
 *
 *   read  - Blocking read, print incoming serial data
 *   write - Send a command and read the response
 *   poll  - Wait for data with poll() (non-blocking wait)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define UART_DEV "/dev/uart_sensor"

static void do_read(int fd, int timeout_ms)
{
    struct pollfd pfd;
    char buf[256];
    int ret;

    printf("Waiting for serial data from UART device...\n");
    printf("(Press Ctrl+C to stop)\n\n");

    while (1) {
        pfd.fd     = fd;
        pfd.events = POLLIN;

        ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            perror("poll");
            break;
        }
        if (ret == 0) {
            printf("."); fflush(stdout);
            continue;
        }

        if (pfd.revents & POLLIN) {
            memset(buf, 0, sizeof(buf));
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n > 0 && buf[n-1] == '\n')
                buf[n-1] = '\0';  /* strip newline for clean output */
            printf("UART RX [%zd bytes]: %s\n", n, buf);
        }

        if (pfd.revents & (POLLERR | POLLHUP)) {
            printf("UART error/hangup detected!\n");
            break;
        }
    }
}

static void do_write_read(int fd)
{
    char buf[256];
    ssize_t n;

    /* Send a command via UART TX */
    const char *cmd = "AT+STATUS\r\n";
    n = write(fd, cmd, strlen(cmd));
    if (n < 0) {
        perror("write");
        return;
    }
    printf("UART TX [%zd bytes]: %s", n, cmd);

    /* Wait a bit for the device to respond */
    usleep(500000);

    /* Read response */
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        if (errno == EAGAIN)
            printf("No data available (would block)\n");
        else
            perror("read");
        return;
    }
    printf("UART RX [%zd bytes]: %s\n", n, buf);
}

static void do_nonblock_test(int fd)
{
    char buf[256];
    ssize_t n;

    /* Set non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    printf("Non-blocking UART read test:\n");

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0 && errno == EAGAIN)
        printf("  Got EAGAIN (no data yet) — as expected\n");
    else if (n > 0)
        printf("  Got %zd bytes immediately!\n", n);
    else
        perror("  read");

    /* Restore blocking mode */
    fcntl(fd, F_SETFL, flags);
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "read";
    int timeout_ms  = (argc > 2) ? atoi(argv[2]) : 2000;
    int fd;

    fd = open(UART_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " UART_DEV);
        printf("Is the uart_sensor driver loaded?\n");
        return 1;
    }

    printf("Connected to %s\n\n", UART_DEV);

    if (strcmp(mode, "write") == 0) {
        do_write_read(fd);
    } else if (strcmp(mode, "nonblock") == 0) {
        do_nonblock_test(fd);
    } else if (strcmp(mode, "poll") == 0) {
        do_read(fd, timeout_ms);
    } else {
        /* Default: blocking read */
        do_read(fd, timeout_ms);
    }

    close(fd);
    return 0;
}
