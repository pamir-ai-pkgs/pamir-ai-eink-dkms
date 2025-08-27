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

/**
 * epd_send_cmd - Send command to display controller
 * @epd: EPD device
 * @cmd: Command byte to send
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_send_cmd(struct epd_dev *epd, u8 cmd)
{
	int ret;

	gpiod_set_value_cansleep(epd->dc_gpio, 0);
	ret = spi_write(epd->spi, &cmd, 1);
	if (ret)
		dev_err(&epd->spi->dev, "Failed to send command 0x%02x: %d\n",
			cmd, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(epd_send_cmd);

/**
 * epd_send_data_buf - Send data buffer to display controller
 * @epd: EPD device
 * @buf: Data buffer
 * @len: Buffer length
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_send_data_buf(struct epd_dev *epd, const u8 *buf, size_t len)
{
	int ret;

	if (!len)
		return 0;

	gpiod_set_value_cansleep(epd->dc_gpio, 1);
	ret = spi_write(epd->spi, buf, len);
	if (ret)
		dev_err(&epd->spi->dev, "Failed to send data: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(epd_send_data_buf);

/**
 * epd_wait_busy - Wait for display controller to become ready
 * @epd: EPD device
 * @timeout_ms: Maximum wait time in milliseconds
 *
 * Return: 0 when ready, -ETIMEDOUT on timeout
 */
int
epd_wait_busy(struct epd_dev *epd, unsigned int timeout_ms)
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
EXPORT_SYMBOL_GPL(epd_wait_busy);

/**
 * epd_set_ram_area - Set the RAM area for partial updates
 * @epd: EPD device
 * @x_start: Starting X position (byte-aligned)
 * @y_start: Starting Y position
 * @x_end: Ending X position (byte-aligned)
 * @y_end: Ending Y position
 *
 * Sets the RAM boundaries for partial update operations.
 * X coordinates must be byte-aligned (multiple of 8).
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_set_ram_area(struct epd_dev *epd, u16 x_start, u16 y_start,
		 u16 x_end, u16 y_end)
{
	u8 data[4];
	int ret;

	/* Convert X coordinates to bytes */
	x_start = x_start / 8;
	x_end = x_end / 8;

	/* Set RAM X boundaries */
	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X);
	if (ret)
		return ret;

	data[0] = x_start;
	data[1] = x_end;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	/* Set RAM Y boundaries */
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

	/* Set RAM pointer */
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
EXPORT_SYMBOL_GPL(epd_set_ram_area);

/**
 * epd_hw_init - Initialize display hardware
 * @epd: EPD device
 *
 * Performs hardware reset and initialization sequence for the display
 * controller.
 *
 * Return: 0 on success, negative error code on failure
 */
int
epd_hw_init(struct epd_dev *epd)
{
	u8 data[4];
	int ret;

	/* Hardware reset */
	gpiod_set_value_cansleep(epd->reset_gpio, 0);
	usleep_range(EPD_RESET_DELAY_MS * 1000,
		     EPD_RESET_DELAY_MS * 1000 + 1000);
	gpiod_set_value_cansleep(epd->reset_gpio, 1);
	usleep_range(EPD_RESET_DELAY_MS * 1000,
		     EPD_RESET_DELAY_MS * 1000 + 1000);

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
	if (ret)
		return ret;

	/* Software reset */
	ret = epd_send_cmd(epd, EPD_CMD_SW_RESET);
	if (ret)
		return ret;

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
	if (ret)
		return ret;

	/* Driver output control */
	ret = epd_send_cmd(epd, EPD_CMD_DRIVER_OUTPUT_CTRL);
	if (ret)
		return ret;

	data[0] = (epd->height - 1) & 0xff;
	data[1] = ((epd->height - 1) >> 8) & 0xff;
	data[2] = 0x00;
	ret = epd_send_data_buf(epd, data, 3);
	if (ret)
		return ret;

	/* Data entry mode */
	ret = epd_send_cmd(epd, EPD_CMD_DATA_ENTRY_MODE);
	if (ret)
		return ret;

	data[0] = 0x01;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	/* Set RAM X boundaries */
	ret = epd_send_cmd(epd, EPD_CMD_SET_RAM_X);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = (epd->width / 8) - 1;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	/* Set RAM Y boundaries */
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

	/* Border waveform */
	ret = epd_send_cmd(epd, EPD_CMD_BORDER_WAVEFORM);
	if (ret)
		return ret;

	data[0] = 0x05;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	/* Display update control */
	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL1);
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x80;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	/* Temperature sensor */
	ret = epd_send_cmd(epd, EPD_CMD_TEMP_SENSOR_READ);
	if (ret)
		return ret;

	data[0] = 0x80;
	ret = epd_send_data_buf(epd, data, 1);
	if (ret)
		return ret;

	/* Set RAM address counters */
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

	data[0] = (epd->height - 1) & 0xff;
	data[1] = ((epd->height - 1) >> 8) & 0xff;
	ret = epd_send_data_buf(epd, data, 2);
	if (ret)
		return ret;

	return epd_wait_busy(epd, EPD_BUSY_TIMEOUT_INIT_MS);
}
EXPORT_SYMBOL_GPL(epd_hw_init);
