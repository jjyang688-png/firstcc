#ifndef _COMP_DRV_H
#define _COMP_DRV_H

#include <linux/ioctl.h>

#define COMP_DRV_MAGIC 'C'

/* IOCTL commands */
#define COMP_DRV_SET_BLINK_PERIOD _IOW(COMP_DRV_MAGIC, 1, unsigned long)
#define COMP_DRV_GET_STATUS       _IOR(COMP_DRV_MAGIC, 2, unsigned long)
#define COMP_DRV_START_BLINK      _IO(COMP_DRV_MAGIC, 3)
#define COMP_DRV_STOP_BLINK       _IO(COMP_DRV_MAGIC, 4)

enum led_state {
    LED_OFF = 0,
    LED_ON = 1,
    LED_BLINK = 2,
};

/* Status structure shared with userspace */
struct comp_drv_status {
    enum led_state state;
    int blink_period_ms;
    unsigned long read_count;
    unsigned long write_count;
};

#endif /* _COMP_DRV_H */
