/**
 * test_blocking.c - Blocking IO test
 *
 * Demonstrates blocking read on /dev/comp_drv.
 * Open the device in blocking mode (default) and read.
 * Run this, then from another terminal run "test_led_rw on"
 * to see the read complete.
 *
 * Also demonstrates non-blocking mode with O_NONBLOCK flag
 * (covers Ch49/52 concepts from userspace).
 *
 * Usage: ./test_blocking [nonblock]
 */

#include "test_common.h"

int main(int argc, char **argv)
{
    int flags = O_RDWR;
    int fd, ret;

    if (argc > 1 && strcmp(argv[1], "nonblock") == 0)
        flags |= O_NONBLOCK;

    fd = open(COMP_DRV_DEV, flags);
    if (fd < 0) {
        perror("open " COMP_DRV_DEV);
        return 1;
    }

    if (flags & O_NONBLOCK) {
        /* Ch52: non-blocking read - returns immediately with -EAGAIN if busy */
        printf("Non-blocking read test...\n");
        struct comp_drv_status st;
        ret = read(fd, &st, sizeof(st));
        if (ret < 0) {
            if (errno == EAGAIN) {
                printf("Got EAGAIN as expected (device busy or no data)\n");
            } else {
                perror("read");
            }
        } else {
            printf("Read %d bytes immediately (non-blocking)\n", ret);
        }
    } else {
        /* Ch49: blocking read - waits until data is available
         * (in this driver, read is always available, but demonstrates the pattern) */
        printf("Blocking read test (press Ctrl+C to stop)...\n");
        printf("In another terminal, run: test_led_rw on\n");

        while (1) {
            struct comp_drv_status st;
            ssize_t n = read(fd, &st, sizeof(st));
            if (n < 0) {
                perror("read");
                break;
            }
            printf("Got status: state=%d blink=%dms reads=%lu writes=%lu\n",
                   st.state, st.blink_period_ms,
                   st.read_count, st.write_count);
            sleep(1);
        }
    }

    close(fd);
    return 0;
}
