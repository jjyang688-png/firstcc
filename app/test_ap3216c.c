/**
 * test_ap3216c.c - Read AP3216C sensor data
 *
 * Demonstrates reading I2C sensor data via character device
 * (covers Ch61 concepts from userspace).
 *
 * Usage: ./test_ap3216c [count]
 */

#include "test_common.h"

struct ap3216c_data {
    unsigned short ir;   /* Infrared */
    unsigned short als;  /* Ambient light */
    unsigned short ps;   /* Proximity */
};

int main(int argc, char **argv)
{
    int count = (argc > 1) ? atoi(argv[1]) : 5;
    int fd, i;

    fd = open("/dev/ap3216c", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/ap3216c");
        printf("Is the ap3216c driver loaded?\n");
        return 1;
    }

    printf("Reading AP3216C sensor %d times...\n", count);
    printf("%-8s %-8s %-8s\n", "IR", "ALS", "PS");
    printf("-------- -------- --------\n");

    for (i = 0; i < count; i++) {
        struct ap3216c_data data;
        ssize_t n = read(fd, &data, sizeof(data));
        if (n < 0) {
            perror("read");
            break;
        }
        printf("%-8u %-8u %-8u\n", data.ir, data.als, data.ps);
        sleep(1);
    }

    close(fd);
    return 0;
}
