// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware control for Pamir AI E-Ink display
 *
 * Copyright (C) 2025 Pamir AI
 */

#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include "pamir-ai-eink-internal.h"

int epd_send_cmd(struct epd_dev *epd, u8 cmd)
{
	int ret;

	gpiod_set_value_cansleep(epd->dc_gpio, 0);
	ret = spi_write(epd->spi, &cmd, 1);
	if (ret)
		dev_err(&epd->spi->dev, "Failed to send command 0x%02x: %d\n",
			cmd, ret);

	return ret;
}

int epd_send_data_buf(struct epd_dev *epd, const u8 *buf, size_t len)
{
	int ret;

	if (!len)
		return 0;

	gpiod_set_value_cansleep(epd->dc_gpio, 1);
	ret = spi_write(epd->spi, buf, len);
	if (ret)
		dev_err(&epd->spi->dev, "SPI write failed (%zu bytes): %d\n",
			len, ret);

	return ret;
}

int epd_wait_busy(struct epd_dev *epd, unsigned int timeout_ms)
{
	unsigned int elapsed = 0;

	if (!epd->busy_gpio)
		return 0;

	while (elapsed < timeout_ms) {
		if (gpiod_get_value_cansleep(epd->busy_gpio) == 0)
			return 0;

		usleep_range(EPD_BUSY_POLL_INTERVAL_MS * 1000,
			     EPD_BUSY_POLL_INTERVAL_MS * 1000 + 1000);
		elapsed += EPD_BUSY_POLL_INTERVAL_MS;
	}

	dev_warn(&epd->spi->dev, "Busy timeout after %u ms\n", timeout_ms);
	return -ETIMEDOUT;
}

int epd_set_ram_area(struct epd_dev *epd, u16 x_start, u16 y_start, u16 x_end,
		     u16 y_end)
{
	u8 data[4];
	int ret;

	x_start = x_start / 8;
	x_end = x_end / 8;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X);
	if (ret)
		return ret;

	data[0] = x_start;
	data[1] = x_end;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_Y);
	if (ret)
		return ret;

	data[0] = y_start & 0xff;
	data[1] = (y_start >> 8) & 0xff;
	data[2] = y_end & 0xff;
	data[3] = (y_end >> 8) & 0xff;
	ret = epd_send_data_buf(epd, data, 4);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X_COUNT);
	if (ret)
		return ret;

	data[0] = x_start;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_Y_COUNT);
	if (ret)
		return ret;

	data[0] = y_start & 0xff;
	data[1] = (y_start >> 8) & 0xff;
	ret = epd_send_data_buf(epd, data, 2);

	return ret;
}

int epd_hw_init(struct epd_dev *epd)
{
	u8 data[4];
	int ret;

	/* Try deep sleep command first to recover from stuck state */
	/* This doesn't require busy wait and can help unstick the controller */
	epd_send_cmd(epd, EPD_CMD_DEEP_SLEEP_MODE);
	usleep_range(10000, 15000);

	/* SSD1680 datasheet timing */
	gpiod_set_value_cansleep(epd->reset_gpio, 0);
	udelay(EPD_RESET_PULSE_US);
	gpiod_set_value_cansleep(epd->reset_gpio, 1);
	usleep_range(10000, 15000);

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SW_RESET);
	if (ret)
		return ret;

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_DRIVER_OUTPUT_CTRL);
	if (ret)
		return ret;

	data[0] = (epd->height - 1) & 0xff;
	data[1] = ((epd->height - 1) >> 8) & 0xff;
	data[2] = 0x00;
	ret = epd_send_data_buf(epd, data, 3);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_DATA_ENTRY_MODE);
	if (ret)
		return ret;

	data[0] = 0x03; /* X-increment, Y-increment */
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = (epd->width / 8) - 1;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_Y);
	if (ret)
		return ret;

	data[0] = (epd->height - 1) & 0xff;
	data[1] = ((epd->height - 1) >> 8) & 0xff;
	data[2] = 0x00;
	data[3] = 0x00;
	ret = epd_send_data_buf(epd, data, 4);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data[0] = 0x05;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL1);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x80;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_TEMP_SENSOR_READ);
	if (ret)
		return ret;

	data[0] = 0x80;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X_COUNT);
	if (ret)
		return ret;

	data[0] = 0x00;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_Y_COUNT);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x00;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
}
