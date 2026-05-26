# Makefile for i.MX6ULL driver test applications
#
# Cross-compile for ARM:
#   make CC=arm-linux-gnueabihf-gcc
#
# Compile natively on the board:
#   make CC=gcc

CC      ?= gcc
CFLAGS  ?= -Wall -O2
TARGETS := test_led_rw test_blocking test_poll test_async test_key test_ap3216c

.PHONY: all clean

all: $(TARGETS)

test_led_rw: test_led_rw.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

test_blocking: test_blocking.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

test_poll: test_poll.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

test_async: test_async.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

test_key: test_key.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

test_ap3216c: test_ap3216c.c test_common.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
