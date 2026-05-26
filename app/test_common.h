#ifndef _TEST_COMMON_H
#define _TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>

/* Shared ioctl definitions - keep in sync with comp_drv.h */
#define COMP_DRV_MAGIC 'C'
#define COMP_DRV_SET_BLINK_PERIOD _IOW(COMP_DRV_MAGIC, 1, unsigned long)
#define COMP_DRV_GET_STATUS       _IOR(COMP_DRV_MAGIC, 2, unsigned long)
#define COMP_DRV_START_BLINK      _IO(COMP_DRV_MAGIC, 3)
#define COMP_DRV_STOP_BLINK       _IO(COMP_DRV_MAGIC, 4)

enum led_state {
    LED_OFF = 0,
    LED_ON = 1,
    LED_BLINK = 2,
};

struct comp_drv_status {
    enum led_state state;
    int blink_period_ms;
    unsigned long read_count;
    unsigned long write_count;
};

/* Default device paths */
#define COMP_DRV_DEV   "/dev/comp_drv"
#define KEY_INPUT_DEV  "/dev/input/event0"

#endif /* _TEST_COMMON_H */
