/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal header for Pamir AI E-Ink driver
 *
 * Copyright (C) 2025 Pamir AI
 */

#ifndef _PAMIR_AI_EINK_INTERNAL_H
#define _PAMIR_AI_EINK_INTERNAL_H

#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include "pamir-ai-eink.h"

#define DRIVER_NAME "pamir-ai-eink"

#define EPD_CMD_DRIVER_OUTPUT_CTRL 0x01
#define EPD_CMD_DEEP_SLEEP_MODE 0x10
#define EPD_CMD_DATA_ENTRY_MODE 0x11
#define EPD_CMD_SW_RESET 0x12
#define EPD_CMD_TEMP_SENSOR_READ 0x18
#define EPD_CMD_ACTIVATE 0x20
#define EPD_CMD_DISPLAY_UPDATE_CTRL1 0x21
#define EPD_CMD_DISPLAY_UPDATE_CTRL2 0x22
#define EPD_CMD_WRITE_RAM_BW 0x24
#define EPD_CMD_WRITE_RAM_RED 0x26
#define EPD_CMD_BORDER_WAVEFORM 0x3C
#define EPD_CMD_SET_RAM_X 0x44
#define EPD_CMD_SET_RAM_Y 0x45
#define EPD_CMD_SET_RAM_X_COUNT 0x4E
#define EPD_CMD_SET_RAM_Y_COUNT 0x4F

#define EPD_UPDATE_MODE_FULL 0xF7
#define EPD_UPDATE_MODE_PARTIAL 0xFF

#define EPD_BORDER_NORMAL 0x05
#define EPD_BORDER_PARTIAL 0x80

#define EPD_RESET_PULSE_US 200 /* per SSD1680 datasheet */
#define EPD_RESET_INIT_MS 10
#define EPD_BUSY_TIMEOUT_INIT_MS 2000
#define EPD_BUSY_TIMEOUT_UPDATE_MS 10000
#define EPD_BUSY_POLL_INTERVAL_MS 5

struct epd_dev {
	struct spi_device *spi;
	struct fb_info *info;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *dc_gpio;
	struct gpio_desc *busy_gpio;
	u32 width;
	u32 height;
	u32 bytes_per_line;
	size_t screensize;
	size_t alloc_size;
	struct mutex lock;
	enum epd_update_mode update_mode;
	struct epd_update_area partial_area;
	bool partial_area_set;
	bool initialized;
};

int epd_send_cmd(struct epd_dev *epd, u8 cmd);
int epd_send_data_buf(struct epd_dev *epd, const u8 *buf, size_t len);
int epd_wait_busy(struct epd_dev *epd, unsigned int timeout_ms);
int epd_hw_init(struct epd_dev *epd);
int epd_set_ram_area(struct epd_dev *epd, u16 x_start, u16 y_start, u16 x_end,
		     u16 y_end);

int epd_full_update(struct epd_dev *epd);
int epd_partial_update(struct epd_dev *epd);
int epd_base_map_update(struct epd_dev *epd);
int epd_display_flush(struct epd_dev *epd);
int epd_clear_display(struct epd_dev *epd);
int epd_deep_sleep(struct epd_dev *epd);

extern const struct fb_ops epd_fb_ops;

extern const struct attribute_group epd_attr_group;

#endif /* _PAMIR_AI_EINK_INTERNAL_H */
