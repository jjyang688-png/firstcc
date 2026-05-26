/**
 * test_key.c - Key input test (reads /dev/input/eventX)
 *
 * Demonstrates reading input events from the key_input driver
 * via the input subsystem (covers Ch58 concepts from userspace).
 *
 * The input subsystem provides:
 * - Blocking read: read() blocks until an event is available
 * - Poll/select: poll() works on input event devices
 * - Standard input_event struct for all input events
 *
 * Usage: ./test_key [device_path]
 *   Default: /dev/input/event0
 */

#include "test_common.h"
#include <linux/input.h>
#include <poll.h>

int main(int argc, char **argv)
{
    const char *dev_path = (argc > 1) ? argv[1] : KEY_INPUT_DEV;
    struct input_event ev;
    int fd;
    ssize_t n;

    fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        printf("Hint: specify device path, e.g. %s /dev/input/event1\n",
               argv[0]);
        return 1;
    }

    printf("Reading key events from %s (press Ctrl+C to stop)...\n",
           dev_path);

    /* Also demonstrate poll on input device */
    {
        struct pollfd pfd;
        pfd.fd     = fd;
        pfd.events = POLLIN;

        printf("Waiting for key press (poll with 5s timeout)...\n");
        int ret = poll(&pfd, 1, 5000);
        if (ret < 0) {
            perror("poll");
        } else if (ret == 0) {
            printf("No key press within 5 seconds, switching to blocking read...\n");
        } else {
            printf("Key event available!\n");
        }
    }

    printf("Now in blocking read mode - press keys on the board...\n");

    while (1) {
        n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            perror("read");
            break;
        }
        if (n != sizeof(ev)) {
            printf("Short read: %zd bytes\n", n);
            continue;
        }

        printf("Event: type=%d code=%d value=%d time=%ld.%06ld\n",
               ev.type, ev.code, ev.value,
               ev.time.tv_sec, ev.time.tv_usec);

        if (ev.type == EV_KEY) {
            printf("  -> Key %d %s\n", ev.code,
                   ev.value ? "PRESSED" :
                   ev.value == 0 ? "RELEASED" : "REPEAT");
        }
    }

    close(fd);
    return 0;
}
