/**
 * test_async.c - Async notification test
 *
 * Demonstrates async IO (SIGIO) notification from /dev/comp_drv.
 * When another process writes to the device, this process receives
 * SIGIO signal with POLL_IN flag.
 *
 * Covers Ch51/53 concepts from userspace:
 * - fcntl F_SETOWN to set PID for SIGIO delivery
 * - fcntl F_SETFL with FASYNC to enable async notifications
 * - sigaction to register SIGIO handler
 * - si_fd and si_band in siginfo to identify the source
 *
 * Usage: ./test_async
 *   Then in another terminal: test_led_rw on
 */

#include "test_common.h"

static int sigio_count = 0;

static void sigio_handler(int signo, siginfo_t *info, void *ctx)
{
    (void)ctx;
    sigio_count++;

    printf("\n[SIGIO #%d] fd=%d band=0x%lx",
           sigio_count, info->si_fd, info->si_band);

    if (info->si_band & POLLIN)
        printf(" [POLL_IN]");
    if (info->si_band & POLLOUT)
        printf(" [POLL_OUT]");

    printf("\n");
}

int main(void)
{
    struct sigaction sa;
    int fd, flags, ret;

    fd = open(COMP_DRV_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " COMP_DRV_DEV);
        return 1;
    }

    /* Register SIGIO handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigio_handler;
    sa.sa_flags     = SA_SIGINFO;
    ret = sigaction(SIGIO, &sa, NULL);
    if (ret < 0) {
        perror("sigaction");
        close(fd);
        return 1;
    }

    /* Set this process as the owner for SIGIO delivery */
    ret = fcntl(fd, F_SETOWN, getpid());
    if (ret < 0) {
        perror("fcntl F_SETOWN");
        close(fd);
        return 1;
    }

    /* Enable async notifications */
    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        perror("fcntl F_GETFL");
        close(fd);
        return 1;
    }

    ret = fcntl(fd, F_SETFL, flags | FASYNC);
    if (ret < 0) {
        perror("fcntl F_SETFL FASYNC");
        close(fd);
        return 1;
    }

    printf("Async notification enabled on fd=%d, pid=%d\n", fd, getpid());
    printf("Waiting for SIGIO events... (run 'test_led_rw on' in another terminal)\n");
    printf("Press Ctrl+C to stop.\n");

    /* Wait for signals */
    while (1)
        pause();

    close(fd);
    return 0;
}
