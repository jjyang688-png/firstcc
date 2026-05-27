#ifndef _ICM20608_H
#define _ICM20608_H

#include <linux/ioctl.h>

#define ICM20608_MAGIC 'I'

/* IOCTL commands */
#define ICM20608_GET_ACCEL  _IOR(ICM20608_MAGIC, 1, struct icm20608_data)
#define ICM20608_GET_GYRO   _IOR(ICM20608_MAGIC, 2, struct icm20608_data)
#define ICM20608_GET_TEMP   _IOR(ICM20608_MAGIC, 3, int)
#define ICM20608_GET_ALL    _IOR(ICM20608_MAGIC, 4, struct icm20608_full)

/* 3-axis data (accelerometer or gyroscope) */
struct icm20608_data {
    short x;
    short y;
    short z;
};

/* Full sensor data: accel + gyro + temp */
struct icm20608_full {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

#endif /* _ICM20608_H */
