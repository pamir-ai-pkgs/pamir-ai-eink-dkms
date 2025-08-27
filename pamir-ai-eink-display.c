// SPDX-License-Identifier: GPL-2.0
/*
 * Display operations for Pamir AI E-Ink display
 *
 * Copyright (C) 2025 Pamir AI
 */

 #include <linux/kernel.h>
 #include <linux/fb.h>
 #include <linux/delay.h>
 #include <linux/gpio/consumer.h>

 #include "pamir-ai-eink-internal.h"

/**
 * epd_full_update - Perform a full screen update
 * @epd: EPD device
 *
 * Performs a complete screen refresh with best image quality.
 * This mode clears ghosting but is slower than partial update.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_full_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;
	int ret;

	if (!buf)
		return -ENOMEM;

	/* Set full screen boundaries */
	ret = epd_set_ram_area(epd, 0, epd->height - 1, epd->width - 1, 0);
	if (ret)
		return ret;

	/* Write RAM for black/white */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

	/* Set border waveform for full update */
	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data = EPD_BORDER_NORMAL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	/* Trigger full display update */
	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		return ret;

	data = EPD_UPDATE_MODE_FULL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);
}
EXPORT_SYMBOL_GPL(epd_full_update);

/**
 * epd_partial_update - Perform a partial screen update
 * @epd: EPD device
 *
 * Performs a fast partial update on the specified region.
 * May cause ghosting but is much faster than full update.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_partial_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	struct epd_update_area *area = &epd->partial_area;
	u8 *buf;
	u8 data;
	u32 x_bytes, y;
	int ret;

	if (!epd->partial_area_set) {
		/* If no area set, do full screen partial update */
		area->x = 0;
		area->y = 0;
		area->width = epd->width;
		area->height = epd->height;
	}

	/* Validate and align coordinates */
	if (area->x % 8 != 0 || area->width % 8 != 0) {
		dev_err(&epd->spi->dev,
			"Partial update X coordinates must be byte-aligned\n");
		return -EINVAL;
	}

	if (area->x + area->width > epd->width ||
	    area->y + area->height > epd->height) {
		dev_err(&epd->spi->dev,
			"Partial update area exceeds display bounds\n");
		return -EINVAL;
	}

	/* Hardware reset to prevent background color change */
	gpiod_set_value_cansleep(epd->reset_gpio, 0);
	usleep_range(EPD_RESET_DELAY_MS * 1000,
		     EPD_RESET_DELAY_MS * 1000 + 1000);
	gpiod_set_value_cansleep(epd->reset_gpio, 1);
	usleep_range(EPD_RESET_DELAY_MS * 1000,
		     EPD_RESET_DELAY_MS * 1000 + 1000);

	/* Lock border to prevent flashing */
	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data = EPD_BORDER_PARTIAL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	/* Set RAM boundaries for partial area */
	ret = epd_set_ram_area(epd, area->x, area->y + area->height - 1,
			       area->x + area->width - 1, area->y);
	if (ret)
		return ret;

	/* Write partial area data */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	/* Calculate buffer location and size */
	x_bytes = area->width / 8;
	buf = info->screen_base;

	/* Send only the partial area data */
	for (y = area->y; y < area->y + area->height; y++) {
		size_t offset = y * epd->bytes_per_line + (area->x / 8);

		ret = epd_send_data_buf(epd, buf + offset, x_bytes);
		if (ret)
			return ret;
	}

	/* Trigger partial display update */
	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		return ret;

	data = EPD_UPDATE_MODE_PARTIAL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);
}
EXPORT_SYMBOL_GPL(epd_partial_update);

/**
 * epd_base_map_update - Perform base map mode update
 * @epd: EPD device
 *
 * Updates both RAM buffers (0x24 and 0x26) with the same data.
 * Used for setting a base image for subsequent partial updates.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_base_map_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;
	int ret;

	if (!buf)
		return -ENOMEM;

	/* Write to first RAM (0x24) */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

	/* Write to second RAM (0x26) for base map */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_RED);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

	/* Trigger full display update */
	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		return ret;

	data = EPD_UPDATE_MODE_FULL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);
}
EXPORT_SYMBOL_GPL(epd_base_map_update);

/**
 * epd_display_flush - Flush framebuffer to display
 * @epd: EPD device
 *
 * Updates the display according to the current update mode.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_display_flush(struct epd_dev *epd)
{
	int ret;

	mutex_lock(&epd->lock);

	switch (epd->update_mode) {
	case EPD_MODE_FULL:
		ret = epd_full_update(epd);
		break;
	case EPD_MODE_PARTIAL:
		ret = epd_partial_update(epd);
		break;
	case EPD_MODE_BASE_MAP:
		ret = epd_base_map_update(epd);
		break;
	default:
		dev_err(&epd->spi->dev, "Invalid update mode %d\n",
			epd->update_mode);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&epd->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(epd_display_flush);

/**
 * epd_deep_sleep - Put display into deep sleep mode
 * @epd: EPD device
 *
 * Enters deep sleep mode to save power. Display must be
 * re-initialized before next use.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_deep_sleep(struct epd_dev *epd)
{
	u8 data = 0x01;
	int ret;

	mutex_lock(&epd->lock);

	ret = epd_send_cmd(epd, EPD_CMD_DEEP_SLEEP_MODE);
	if (!ret)
		ret = epd_send_data_buf(epd, &data, 1);

	mutex_unlock(&epd->lock);

	if (!ret)
		usleep_range(10000, 11000);

	return ret;
}
EXPORT_SYMBOL_GPL(epd_deep_sleep);
