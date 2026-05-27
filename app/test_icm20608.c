/**
 * test_icm20608.c - ICM20608 SPI sensor test
 *
 * Demonstrates reading SPI sensor data via character device
 * (covers Ch62 concepts from userspace).
 *
 * Usage: ./test_icm20608 [count]
 *        ./test_icm20608 accel     (read accelerometer only)
 *        ./test_icm20608 gyro      (read gyroscope only)
 *        ./test_icm20608 temp      (read temperature only)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

/* Keep in sync with driver/icm20608.h */
#define ICM20608_MAGIC 'I'
#define ICM20608_GET_ACCEL  _IOR(ICM20608_MAGIC, 1, struct icm20608_data)
#define ICM20608_GET_GYRO   _IOR(ICM20608_MAGIC, 2, struct icm20608_data)
#define ICM20608_GET_TEMP   _IOR(ICM20608_MAGIC, 3, int)
#define ICM20608_GET_ALL    _IOR(ICM20608_MAGIC, 4, struct icm20608_full)

struct icm20608_data {
    short x;
    short y;
    short z;
};

struct icm20608_full {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

static void print_accel(const char *prefix, struct icm20608_data *d)
{
    printf("%s  AX=%6d  AY=%6d  AZ=%6d\n", prefix, d->x, d->y, d->z);
}

static void print_gyro(const char *prefix, struct icm20608_data *d)
{
    printf("%s  GX=%6d  GY=%6d  GZ=%6d\n", prefix, d->x, d->y, d->z);
}

int main(int argc, char **argv)
{
    const char *mode  = (argc > 1) ? argv[1] : "all";
    int count = (argc > 2) ? atoi(argv[2]) : 10;
    int fd, i;

    fd = open("/dev/icm20608", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/icm20608");
        printf("Is the icm20608 driver loaded?\n");
        return 1;
    }

    if (strcmp(mode, "accel") == 0) {
        printf("Reading accelerometer %d times...\n", count);
        printf("%-8s %-8s %-8s\n", "AX", "AY", "AZ");
        for (i = 0; i < count; i++) {
            struct icm20608_data data;
            if (ioctl(fd, ICM20608_GET_ACCEL, &data) < 0) {
                perror("ioctl GET_ACCEL");
                break;
            }
            printf("%-8d %-8d %-8d\n", data.x, data.y, data.z);
            usleep(100000);
        }
    } else if (strcmp(mode, "gyro") == 0) {
        printf("Reading gyroscope %d times...\n", count);
        printf("%-8s %-8s %-8s\n", "GX", "GY", "GZ");
        for (i = 0; i < count; i++) {
            struct icm20608_data data;
            if (ioctl(fd, ICM20608_GET_GYRO, &data) < 0) {
                perror("ioctl GET_GYRO");
                break;
            }
            printf("%-8d %-8d %-8d\n", data.x, data.y, data.z);
            usleep(100000);
        }
    } else if (strcmp(mode, "temp") == 0) {
        printf("Reading temperature %d times...\n", count);
        for (i = 0; i < count; i++) {
            short temp;
            if (ioctl(fd, ICM20608_GET_TEMP, &temp) < 0) {
                perror("ioctl GET_TEMP");
                break;
            }
            printf("Temp = %.2f C\n", temp / 340.0 + 36.53);
            usleep(500000);
        }
    } else {
        printf("Reading all sensor data %d times...\n\n", count);
        printf("%-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
               "AX", "AY", "AZ", "TEMP", "GX", "GY", "GZ");
        printf("-------- -------- -------- -------- "
               "-------- -------- --------\n");

        for (i = 0; i < count; i++) {
            struct icm20608_full all;
            ssize_t n = read(fd, &all, sizeof(all));
            if (n < 0) {
                perror("read");
                break;
            }
            printf("%-8d %-8d %-8d %-8d %-8d %-8d %-8d\n",
                   all.accel_x, all.accel_y, all.accel_z,
                   all.temp,
                   all.gyro_x, all.gyro_y, all.gyro_z);
            usleep(100000);
        }
    }

    close(fd);
    return 0;
}
