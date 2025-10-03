/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace API header for Pamir AI E-Ink display driver
 *
 * Copyright (C) 2025 Pamir AI
 */

#ifndef _UAPI_PAMIR_AI_EINK_H
#define _UAPI_PAMIR_AI_EINK_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define EPD_IOC_MAGIC 'E'
#define EPD_IOC_SET_UPDATE_MODE _IOW(EPD_IOC_MAGIC, 1, int)
#define EPD_IOC_GET_UPDATE_MODE _IOR(EPD_IOC_MAGIC, 2, int)
#define EPD_IOC_SET_PARTIAL_AREA _IOW(EPD_IOC_MAGIC, 3, struct epd_update_area)
#define EPD_IOC_UPDATE_DISPLAY _IO(EPD_IOC_MAGIC, 4)
#define EPD_IOC_DEEP_SLEEP _IO(EPD_IOC_MAGIC, 5)
#define EPD_IOC_SET_BASE_MAP _IOW(EPD_IOC_MAGIC, 6, void *)
#define EPD_IOC_RESET _IO(EPD_IOC_MAGIC, 7)
#define EPD_IOC_CLEAR_DISPLAY _IO(EPD_IOC_MAGIC, 8)

enum epd_update_mode {
	EPD_MODE_FULL = 0,
	EPD_MODE_PARTIAL,
	EPD_MODE_BASE_MAP,
};

struct epd_update_area {
	__u16 x;
	__u16 y;
	__u16 width;
	__u16 height;
};

#endif /* _UAPI_PAMIR_AI_EINK_H */
