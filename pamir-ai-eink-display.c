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

static int epd_trigger_update(struct epd_dev *epd, u8 mode)
{
	u8 data;
	int ret;

	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		return ret;

	data = mode;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);
}

int epd_full_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;
	int ret;

	if (!buf)
		return -ENOMEM;

	/* Y-increment mode */
	ret = epd_set_ram_area(epd, 0, 0, epd->width - 1, epd->height - 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

	/* Clear residual data in red RAM */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_RED);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data = EPD_BORDER_NORMAL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	return epd_trigger_update(epd, EPD_UPDATE_MODE_FULL);
}

int epd_partial_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	struct epd_update_area *area = &epd->partial_area;
	u8 *buf;
	u8 data;
	u32 x_bytes, y;
	int ret;

	if (!epd->initialized) {
		dev_err(&epd->spi->dev,
			"Display not initialized, cannot perform partial update\n");
		return -ENODEV;
	}

	if (!epd->partial_area_set) {
		area->x = 0;
		area->y = 0;
		area->width = epd->width;
		area->height = epd->height;
	}

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

	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data = EPD_BORDER_PARTIAL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	ret = epd_set_ram_area(epd, area->x, area->y, area->x + area->width - 1,
			       area->y + area->height - 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	x_bytes = area->width / 8;
	buf = info->screen_base;

	for (y = area->y; y < area->y + area->height; y++) {
		size_t offset = y * epd->bytes_per_line + (area->x / 8);

		ret = epd_send_data_buf(epd, buf + offset, x_bytes);
		if (ret)
			return ret;
	}

	ret = epd_trigger_update(epd, EPD_UPDATE_MODE_PARTIAL);

	return ret;
}

int epd_base_map_update(struct epd_dev *epd)
{
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;
	int ret;

	if (!buf)
		return -ENOMEM;

	ret = epd_set_ram_area(epd, 0, 0, epd->width - 1, epd->height - 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		return ret;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		return ret;

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

int epd_display_flush(struct epd_dev *epd)
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

int epd_clear_display(struct epd_dev *epd)
{
	size_t len = epd->screensize;
	u8 *clear_buf;
	u8 data;
	int ret;

	// ret = epd_hw_init(epd);
	// if (ret) {
	//	dev_warn(&epd->spi->dev, "Failed to reinit hardware: %d\n", ret);
	// }

	/* Y-decrement mode needed for proper clear */
	data = 0x01; /* X-increment, Y-decrement - matches height-1 to 0 coords */
	ret = epd_send_cmd(epd, EPD_CMD_DATA_ENTRY_MODE);
	if (ret)
		return ret;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		return ret;

	clear_buf = kmalloc(len, GFP_KERNEL);
	if (!clear_buf)
		return -ENOMEM;
	memset(clear_buf, 0xFF, len);

	/* Y-increment mode */
	ret = epd_set_ram_area(epd, 0, 0, epd->width - 1, epd->height - 1);
	if (ret)
		goto out_free;

	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		goto out_free;

	ret = epd_send_data_buf(epd, clear_buf, len);
	if (ret)
		goto out_free;

	/* Prevent ghosting */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_RED);
	if (ret)
		goto out_free;

	ret = epd_send_data_buf(epd, clear_buf, len);
	if (ret)
		goto out_free;

	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		goto out_free;

	data = EPD_UPDATE_MODE_FULL;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		goto out_free;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		goto out_free;

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);
	if (ret)
		goto out_free;

	data = 0x03; /* X-increment, Y-increment for text display */
	ret = epd_send_cmd(epd, EPD_CMD_DATA_ENTRY_MODE);
	if (ret)
		goto out_free;
	ret = epd_send_data_buf(epd, &data, 1);

out_free:
	kfree(clear_buf);
	return ret;
}

int epd_deep_sleep(struct epd_dev *epd)
{
	u8 data = 0x11; /* Mode 2: Deep Sleep without RAM retention */
	int ret;

	mutex_lock(&epd->lock);

	ret = epd_send_cmd(epd, EPD_CMD_DEEP_SLEEP_MODE);
	if (!ret)
		ret = epd_send_data_buf(epd, &data, 1);

	if (!ret)
		epd->initialized = false;

	mutex_unlock(&epd->lock);

	if (!ret)
		usleep_range(10000, 11000);

	return ret;
}
