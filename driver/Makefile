# Makefile for i.MX6ULL driver modules
#
# Usage:
#   make KERNEL_DIR=/path/to/kernel ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
#
# The KERNEL_DIR should point to the compiled i.MX6ULL kernel source tree,
# e.g. /home/alientek/linux/atk-imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
#
# If cross-compiling on x86, set:
#   ARCH=arm
#   CROSS_COMPILE=arm-linux-gnueabihf-

KERNEL_DIR ?= /home/alientek/linux/imx6ull

obj-m := comp_drv.o key_input.o ap3216c.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) clean
	rm -f modules.order Module.symvers
