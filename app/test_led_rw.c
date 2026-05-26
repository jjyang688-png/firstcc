/**
 * test_led_rw.c - Basic LED read/write test
 *
 * Demonstrates basic open/read/write/ioctl/close operations
 * on /dev/comp_drv (covers Ch40-42 concepts from userspace).
 *
 * Usage: ./test_led_rw [on|off|blink|status]
 */

#include "test_common.h"

static void print_status(int fd)
{
    struct comp_drv_status st;
    if (ioctl(fd, COMP_DRV_GET_STATUS, &st) < 0) {
        perror("ioctl GET_STATUS");
        return;
    }
    printf("state=%d  blink_period=%dms  reads=%lu  writes=%lu\n",
           st.state, st.blink_period_ms, st.read_count, st.write_count);
}

static void do_read(int fd)
{
    struct comp_drv_status st;
    ssize_t n = read(fd, &st, sizeof(st));
    if (n < 0) {
        perror("read");
        return;
    }
    printf("read %zd bytes: state=%d blink=%dms reads=%lu writes=%lu\n",
           n, st.state, st.blink_period_ms, st.read_count, st.write_count);
}

int main(int argc, char **argv)
{
    const char *cmd = (argc > 1) ? argv[1] : "status";
    int fd;

    fd = open(COMP_DRV_DEV, O_RDWR);
    if (fd < 0) {
        perror("open " COMP_DRV_DEV);
        return 1;
    }

    if (strcmp(cmd, "on") == 0) {
        write(fd, "on", 2);
        printf("LED turned ON\n");
    } else if (strcmp(cmd, "off") == 0) {
        write(fd, "off", 3);
        printf("LED turned OFF\n");
    } else if (strcmp(cmd, "blink") == 0) {
        /* Set 500ms blink period */
        ioctl(fd, COMP_DRV_SET_BLINK_PERIOD, 500);
        write(fd, "blink", 5);
        printf("LED blinking (500ms period)\n");
    } else if (strcmp(cmd, "status") == 0) {
        do_read(fd);
    } else if (strcmp(cmd, "ioctl_status") == 0) {
        print_status(fd);
    } else {
        printf("Usage: %s [on|off|blink|status|ioctl_status]\n", argv[0]);
    }

    close(fd);
    return 0;
}
