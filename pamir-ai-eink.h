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

/* IOCTL commands for e-ink control */
#define EPD_IOC_MAGIC			'E'
#define EPD_IOC_SET_UPDATE_MODE		_IOW(EPD_IOC_MAGIC, 1, int)
#define EPD_IOC_GET_UPDATE_MODE		_IOR(EPD_IOC_MAGIC, 2, int)
#define EPD_IOC_SET_PARTIAL_AREA	_IOW(EPD_IOC_MAGIC, 3, struct epd_update_area)
#define EPD_IOC_UPDATE_DISPLAY		_IO(EPD_IOC_MAGIC, 4)
#define EPD_IOC_DEEP_SLEEP		_IO(EPD_IOC_MAGIC, 5)
#define EPD_IOC_SET_BASE_MAP		_IOW(EPD_IOC_MAGIC, 6, void *)

/**
 * enum epd_update_mode - Display update modes
 * @EPD_MODE_FULL: Full screen refresh with best quality (slower)
 * @EPD_MODE_PARTIAL: Fast partial update (ghosting may occur)
 * @EPD_MODE_BASE_MAP: Dual-buffer mode using both RAM buffers
 */
enum epd_update_mode {
	EPD_MODE_FULL = 0,
	EPD_MODE_PARTIAL,
	EPD_MODE_BASE_MAP,
};

/**
 * struct epd_update_area - Defines a rectangular area for partial update
 * @x: X coordinate of top-left corner (must be byte-aligned, i.e., multiple of 8)
 * @y: Y coordinate of top-left corner
 * @width: Width of update area in pixels (must be multiple of 8)
 * @height: Height of update area in pixels
 *
 * Note: X coordinate and width must be multiples of 8 due to the display's
 * byte-aligned memory organization.
 */
struct epd_update_area {
	__u16 x;
	__u16 y;
	__u16 width;
	__u16 height;
};

#endif /* _UAPI_PAMIR_AI_EINK_H */
