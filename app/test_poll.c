/**
 * test_poll.c - Poll/Select IO multiplexing test
 *
 * Demonstrates poll() and select() usage on /dev/comp_drv
 * (covers Ch52 concepts from userspace).
 *
 * Usage: ./test_poll [poll|select] [timeout_ms]
 */

#include "test_common.h"
#include <poll.h>
#include <sys/time.h>

static void test_poll(int fd, int timeout_ms)
{
    struct pollfd pfd;
    int ret;

    pfd.fd     = fd;
    pfd.events = POLLIN | POLLOUT;
    pfd.revents = 0;

    printf("poll() with %dms timeout...\n", timeout_ms);

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        perror("poll");
        return;
    }

    printf("poll returned %d, revents=0x%x", ret, pfd.revents);

    if (pfd.revents & POLLIN)
        printf(" [readable]");
    if (pfd.revents & POLLOUT)
        printf(" [writable]");
    if (pfd.revents & POLLERR)
        printf(" [error]");
    if (pfd.revents & POLLHUP)
        printf(" [hungup]");
    printf("\n");
}

static void test_select(int fd, int timeout_ms)
{
    fd_set rfds, wfds;
    struct timeval tv, *ptv;
    int ret;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(fd, &rfds);
    FD_SET(fd, &wfds);

    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    } else {
        ptv = NULL; /* block forever */
    }

    printf("select() with %dms timeout...\n", timeout_ms);

    ret = select(fd + 1, &rfds, &wfds, NULL, ptv);
    if (ret < 0) {
        perror("select");
        return;
    }

    printf("select returned %d", ret);
    if (FD_ISSET(fd, &rfds))
        printf(" [readable]");
    if (FD_ISSET(fd, &wfds))
        printf(" [writable]");
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *method   = (argc > 1) ? argv[1] : "poll";
    int timeout_ms      = (argc > 2) ? atoi(argv[2]) : 1000;
    int fd;

    fd = open(COMP_DRV_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " COMP_DRV_DEV);
        return 1;
    }

    if (strcmp(method, "select") == 0)
        test_select(fd, timeout_ms);
    else
        test_poll(fd, timeout_ms);

    close(fd);
    return 0;
}
