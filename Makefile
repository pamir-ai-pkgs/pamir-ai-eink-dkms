# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Pamir AI E-Ink Display Driver
#

obj-m += pamir-ai-eink.o

pamir-ai-eink-objs := pamir-ai-eink-core.o \
		      pamir-ai-eink-hw.o \
		      pamir-ai-eink-display.o \
		      pamir-ai-eink-fb.o \
		      pamir-ai-eink-sysfs.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -I$(src)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -a

.PHONY: all clean install