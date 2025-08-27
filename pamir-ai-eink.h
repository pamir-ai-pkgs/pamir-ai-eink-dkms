/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace API for Pamir AI E-Ink Display Driver
 *
 * Copyright (C) 2025 Pamir AI
 */

#ifndef _UAPI_PAMIR_AI_EINK_H
#define _UAPI_PAMIR_AI_EINK_H

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * enum epd_update_mode - Display update modes for SSD1681 controller
 * @EPD_MODE_FULL: Full screen refresh with best quality
 *                 (slower, ~2-3 seconds). Clears ghosting and provides
 *                 best image quality
 * @EPD_MODE_PARTIAL: Fast partial update (faster, ~500ms)
 *                    May cause ghosting but suitable for real-time updates
 * @EPD_MODE_BASE_MAP: Dual-buffer mode using both RAM buffers
 *                     Sets base image for subsequent partial updates
 */
enum epd_update_mode {
	EPD_MODE_FULL = 0,
	EPD_MODE_PARTIAL,
	EPD_MODE_BASE_MAP,
};

/**
 * struct epd_update_area - Defines a rectangular area for partial update
 * @x: X coordinate of top-left corner
 *     (must be byte-aligned, i.e., multiple of 8)
 * @y: Y coordinate of top-left corner
 * @width: Width of update area in pixels (must be multiple of 8)
 * @height: Height of update area in pixels
 *
 * Note: X coordinates and width must be multiples of 8 due to the
 * display controller's byte-oriented memory organization.
 */
struct epd_update_area {
	__u16 x;
	__u16 y;
	__u16 width;
	__u16 height;
};

/* IOCTL commands for e-ink control */
#define EPD_IOC_MAGIC 'E'

/**
 * EPD_IOC_SET_UPDATE_MODE - Set the display update mode
 * arg: pointer to int containing epd_update_mode value
 */
#define EPD_IOC_SET_UPDATE_MODE _IOW(EPD_IOC_MAGIC, 1, int)

/**
 * EPD_IOC_GET_UPDATE_MODE - Get the current display update mode
 * arg: pointer to int to receive epd_update_mode value
 */
#define EPD_IOC_GET_UPDATE_MODE _IOR(EPD_IOC_MAGIC, 2, int)

/**
 * EPD_IOC_SET_PARTIAL_AREA - Set the area for partial updates
 * arg: pointer to struct epd_update_area
 */
#define EPD_IOC_SET_PARTIAL_AREA _IOW(EPD_IOC_MAGIC, 3, struct epd_update_area)

/**
 * EPD_IOC_UPDATE_DISPLAY - Trigger a display update immediately
 * No arguments
 */
#define EPD_IOC_UPDATE_DISPLAY _IO(EPD_IOC_MAGIC, 4)

/**
 * EPD_IOC_DEEP_SLEEP - Put display into deep sleep mode
 * No arguments
 * Note: Display must be re-initialized after deep sleep
 */
#define EPD_IOC_DEEP_SLEEP _IO(EPD_IOC_MAGIC, 5)

/**
 * EPD_IOC_SET_BASE_MAP - Set base map mode and update display
 * arg: optional pointer to buffer (if NULL, uses current framebuffer)
 */
#define EPD_IOC_SET_BASE_MAP _IOW(EPD_IOC_MAGIC, 6, void *)

/*
 * Sysfs Interface
 * ===============
 * The driver also provides sysfs attributes for runtime configuration:
 *
 * /sys/bus/spi/devices/spiX.Y/update_mode
 *   - Read/write current update mode ("full", "partial", "base_map")
 *
 * /sys/bus/spi/devices/spiX.Y/partial_area
 *   - Read/write partial update area as "x,y,width,height"
 *   - Read returns "not set" if no area is configured
 *
 * /sys/bus/spi/devices/spiX.Y/trigger_update
 *   - Write "1" to trigger a display update
 *
 * /sys/bus/spi/devices/spiX.Y/deep_sleep
 *   - Write "1" to enter deep sleep mode
 */

#endif /* _UAPI_PAMIR_AI_EINK_H */
