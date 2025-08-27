// SPDX-License-Identifier: GPL-2.0
/*
 * Framebuffer driver for Pamir AI E-Ink display
 *
 * Copyright (C) 2025 Pamir AI
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/string.h>

#define DRIVER_NAME "pamir-ai-eink"

/* EPD Commands */
#define EPD_CMD_DRIVER_OUTPUT_CTRL 0x01
#define EPD_CMD_DEEP_SLEEP_MODE 0x10
#define EPD_CMD_DATA_ENTRY_MODE 0x11
#define EPD_CMD_SW_RESET 0x12
#define EPD_CMD_TEMP_SENSOR_READ 0x18
#define EPD_CMD_ACTIVATE 0x20
#define EPD_CMD_DISPLAY_UPDATE_CTRL1 0x21
#define EPD_CMD_DISPLAY_UPDATE_CTRL2 0x22
#define EPD_CMD_WRITE_RAM_BW 0x24
#define EPD_CMD_WRITE_RAM_RED 0x26 /* Used for base map mode */
#define EPD_CMD_BORDER_WAVEFORM 0x3C
#define EPD_CMD_SET_RAM_X 0x44
#define EPD_CMD_SET_RAM_Y 0x45
#define EPD_CMD_SET_RAM_X_COUNT 0x4E
#define EPD_CMD_SET_RAM_Y_COUNT 0x4F

/* Update modes for SSD1681 controller */
#define EPD_UPDATE_MODE_FULL 0xF7 /* Full refresh  */
#define EPD_UPDATE_MODE_PARTIAL 0xFF /* Fast partial refresh */

/* Border waveform control */
#define EPD_BORDER_NORMAL 0x05 /* Normal border for full update */
#define EPD_BORDER_PARTIAL 0x80 /* Locked border for partial update */

/* Timing constants */
#define EPD_RESET_DELAY_MS 10
#define EPD_BUSY_TIMEOUT_INIT_MS 2000
#define EPD_BUSY_TIMEOUT_UPDATE_MS 10000
#define EPD_BUSY_POLL_INTERVAL_MS 5

/* IOCTL commands for e-ink control */
#define EPD_IOC_MAGIC 'E'
#define EPD_IOC_SET_UPDATE_MODE _IOW(EPD_IOC_MAGIC, 1, int)
#define EPD_IOC_GET_UPDATE_MODE _IOR(EPD_IOC_MAGIC, 2, int)
#define EPD_IOC_SET_PARTIAL_AREA _IOW(EPD_IOC_MAGIC, 3, struct epd_update_area)
#define EPD_IOC_UPDATE_DISPLAY _IO(EPD_IOC_MAGIC, 4)
#define EPD_IOC_DEEP_SLEEP _IO(EPD_IOC_MAGIC, 5)
#define EPD_IOC_SET_BASE_MAP _IOW(EPD_IOC_MAGIC, 6, void *)

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
 */
struct epd_update_area {
	u16 x;
	u16 y;
	u16 width;
	u16 height;
};

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
	struct mutex lock;

	/* Update mode control */
	enum epd_update_mode update_mode;
	struct epd_update_area partial_area;
	bool partial_area_set;
	u8 *base_map_buffer; /* Buffer for base map mode */
};

static inline int epd_send_cmd(struct epd_dev *epd, u8 cmd)
{
	int ret;

	gpiod_set_value_cansleep(epd->dc_gpio, 0);
	ret = spi_write(epd->spi, &cmd, 1);
	if (ret)
		dev_err(&epd->spi->dev, "Failed to send command 0x%02x: %d\n",
			cmd, ret);

	return ret;
}

static inline int epd_send_data_buf(struct epd_dev *epd, const u8 *buf,
				    size_t len)
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

static int epd_wait_busy(struct epd_dev *epd, unsigned int timeout_ms)
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
 */
static int epd_set_ram_area(struct epd_dev *epd, u16 x_start, u16 y_start,
			    u16 x_end, u16 y_end)
{
	int ret;
	u8 data[4];

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

/**
 * epd_deep_sleep - Put display into deep sleep mode
 * @epd: EPD device
 *
 * Enters deep sleep mode to save power. Display must be
 * re-initialized before next use.
 */
static int epd_deep_sleep(struct epd_dev *epd)
{
	int ret;
	u8 data = 0x01;

	mutex_lock(&epd->lock);

	ret = epd_send_cmd(epd, EPD_CMD_DEEP_SLEEP_MODE);
	if (!ret)
		ret = epd_send_data_buf(epd, &data, 1);

	mutex_unlock(&epd->lock);

	if (!ret)
		usleep_range(10000, 11000);

	return ret;
}

static int epd_hw_init(struct epd_dev *epd)
{
	int ret;
	u8 data[4];

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

/**
 * epd_full_update - Perform a full screen update
 * @epd: EPD device
 *
 * Performs a complete screen refresh with best image quality.
 * This mode clears ghosting but is slower than partial update.
 */
static int epd_full_update(struct epd_dev *epd)
{
	int ret;
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;

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

/**
 * epd_partial_update - Perform a partial screen update
 * @epd: EPD device
 *
 * Performs a fast partial update on the specified region.
 * May cause ghosting but is much faster than full update.
 */
static int epd_partial_update(struct epd_dev *epd)
{
	int ret;
	struct fb_info *info = epd->info;
	struct epd_update_area *area = &epd->partial_area;
	u8 data;
	u8 *buf;
	u32 x_bytes, y;

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

/**
 * epd_base_map_update - Perform base map mode update
 * @epd: EPD device
 *
 * Updates both RAM buffers (0x24 and 0x26) with the same data.
 * Used for setting a base image for subsequent partial updates.
 */
static int epd_base_map_update(struct epd_dev *epd)
{
	int ret;
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;

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

/**
 * epd_display_flush - Flush framebuffer to display
 * @epd: EPD device
 *
 * Updates the display according to the current update mode.
 */
static int epd_display_flush(struct epd_dev *epd)
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

static ssize_t epd_fb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	ssize_t rc;
	struct epd_dev *epd = info->par;

	rc = fb_sys_write(info, buf, count, ppos);
	if (rc > 0) {
		int ret = epd_display_flush(epd);

		if (ret)
			dev_err(&epd->spi->dev, "Display flush failed: %d\n",
				ret);
	}

	return rc;
}

/**
 * epd_fb_ioctl - Handle ioctl commands for e-ink control
 * @info: Framebuffer info
 * @cmd: IOCTL command
 * @arg: Command argument
 *
 * Provides userspace control over update modes and display operations.
 */
static int epd_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct epd_dev *epd = info->par;
	struct epd_update_area area;
	void __user *argp = (void __user *)arg;
	int mode;
	int ret = 0;

	switch (cmd) {
	case EPD_IOC_SET_UPDATE_MODE:
		if (get_user(mode, (int __user *)argp))
			return -EFAULT;

		if (mode < EPD_MODE_FULL || mode > EPD_MODE_BASE_MAP)
			return -EINVAL;

		mutex_lock(&epd->lock);
		epd->update_mode = mode;
		mutex_unlock(&epd->lock);
		break;

	case EPD_IOC_GET_UPDATE_MODE:
		if (put_user(epd->update_mode, (int __user *)argp))
			return -EFAULT;
		break;

	case EPD_IOC_SET_PARTIAL_AREA:
		if (copy_from_user(&area, argp, sizeof(area)))
			return -EFAULT;

		/* Validate alignment */
		if (area.x % 8 != 0 || area.width % 8 != 0) {
			dev_err(&epd->spi->dev,
				"X coordinates must be byte-aligned\n");
			return -EINVAL;
		}

		if (area.x + area.width > epd->width ||
		    area.y + area.height > epd->height) {
			dev_err(&epd->spi->dev,
				"Update area exceeds display bounds\n");
			return -EINVAL;
		}

		mutex_lock(&epd->lock);
		epd->partial_area = area;
		epd->partial_area_set = true;
		mutex_unlock(&epd->lock);
		break;

	case EPD_IOC_UPDATE_DISPLAY:
		ret = epd_display_flush(epd);
		break;

	case EPD_IOC_DEEP_SLEEP:
		ret = epd_deep_sleep(epd);
		break;

	case EPD_IOC_SET_BASE_MAP:
		/* Set update mode to base map and trigger update */
		mutex_lock(&epd->lock);
		epd->update_mode = EPD_MODE_BASE_MAP;
		mutex_unlock(&epd->lock);
		ret = epd_display_flush(epd);
		break;

	default:
		return -ENOTTY;
	}

	return ret;
}

/**
 * epd_fb_mmap - Map framebuffer memory to userspace
 * @info: Framebuffer info
 * @vma: Virtual memory area descriptor
 *
 * Maps the vmalloc_user allocated framebuffer memory to userspace.
 * The memory was allocated with VM_USERMAP flag set, making it
 * suitable for userspace mapping.
 */
static int epd_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	/* Map the vmalloc_user allocated memory to userspace */
	return remap_vmalloc_range(vma, info->screen_base, 0);
}

static const struct fb_ops epd_fb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = epd_fb_write,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_ioctl = epd_fb_ioctl,
	.fb_mmap = epd_fb_mmap,
};

/* Sysfs attributes for runtime configuration */

/**
 * update_mode_show - Show current update mode
 */
static ssize_t update_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	const char *mode_str;

	switch (epd->update_mode) {
	case EPD_MODE_FULL:
		mode_str = "full";
		break;
	case EPD_MODE_PARTIAL:
		mode_str = "partial";
		break;
	case EPD_MODE_BASE_MAP:
		mode_str = "base_map";
		break;
	default:
		mode_str = "unknown";
		break;
	}

	return sysfs_emit(buf, "%s\n", mode_str);
}

/**
 * update_mode_store - Set update mode
 */
static ssize_t update_mode_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	enum epd_update_mode mode;

	if (sysfs_streq(buf, "full"))
		mode = EPD_MODE_FULL;
	else if (sysfs_streq(buf, "partial"))
		mode = EPD_MODE_PARTIAL;
	else if (sysfs_streq(buf, "base_map"))
		mode = EPD_MODE_BASE_MAP;
	else
		return -EINVAL;

	mutex_lock(&epd->lock);
	epd->update_mode = mode;
	mutex_unlock(&epd->lock);

	return count;
}

static DEVICE_ATTR_RW(update_mode);

/**
 * partial_area_show - Show current partial update area
 */
static ssize_t partial_area_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);

	if (!epd->partial_area_set)
		return sysfs_emit(buf, "not set\n");

	return sysfs_emit(buf, "%u,%u,%u,%u\n", epd->partial_area.x,
			  epd->partial_area.y, epd->partial_area.width,
			  epd->partial_area.height);
}

/**
 * partial_area_store - Set partial update area
 * Format: "x,y,width,height" (all values in pixels)
 */
static ssize_t partial_area_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	struct epd_update_area area;
	int ret;

	ret = sscanf(buf, "%hu,%hu,%hu,%hu", &area.x, &area.y, &area.width,
		     &area.height);
	if (ret != 4)
		return -EINVAL;

	/* Validate alignment */
	if (area.x % 8 != 0 || area.width % 8 != 0) {
		dev_err(dev,
			"X coordinates must be byte-aligned (multiple of 8)\n");
		return -EINVAL;
	}

	if (area.x + area.width > epd->width ||
	    area.y + area.height > epd->height) {
		dev_err(dev, "Update area exceeds display bounds\n");
		return -EINVAL;
	}

	mutex_lock(&epd->lock);
	epd->partial_area = area;
	epd->partial_area_set = true;
	mutex_unlock(&epd->lock);

	return count;
}

static DEVICE_ATTR_RW(partial_area);

/**
 * trigger_update_store - Trigger a display update
 */
static ssize_t trigger_update_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	int ret;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	ret = epd_display_flush(epd);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_WO(trigger_update);

/**
 * deep_sleep_store - Enter deep sleep mode
 */
static ssize_t deep_sleep_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	int ret;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	ret = epd_deep_sleep(epd);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_WO(deep_sleep);

static struct attribute *epd_attrs[] = {
	&dev_attr_update_mode.attr,
	&dev_attr_partial_area.attr,
	&dev_attr_trigger_update.attr,
	&dev_attr_deep_sleep.attr,
	NULL,
};

static const struct attribute_group epd_attr_group = {
	.attrs = epd_attrs,
};

static int epd_probe(struct spi_device *spi)
{
	struct device_node *np = spi->dev.of_node;
	struct epd_dev *epd;
	struct fb_info *info;
	int ret;
	u32 width = 0, height = 0;

	if (!np) {
		dev_err(&spi->dev, "Device tree node required\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "width", &width);
	if (ret) {
		dev_err(&spi->dev, "Missing 'width' property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "height", &height);
	if (ret) {
		dev_err(&spi->dev, "Missing 'height' property\n");
		return ret;
	}

	epd = devm_kzalloc(&spi->dev, sizeof(*epd), GFP_KERNEL);
	if (!epd)
		return -ENOMEM;

	epd->spi = spi;
	epd->width = width;
	epd->height = height;
	epd->bytes_per_line = DIV_ROUND_UP(width, 8);
	epd->screensize = epd->bytes_per_line * epd->height;
	mutex_init(&epd->lock);

	/* Initialize update mode to full refresh */
	epd->update_mode = EPD_MODE_FULL;
	epd->partial_area_set = false;

	/* Get GPIOs */
	epd->reset_gpio =
		devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(epd->reset_gpio))
		return PTR_ERR(epd->reset_gpio);

	epd->dc_gpio = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(epd->dc_gpio))
		return PTR_ERR(epd->dc_gpio);

	epd->busy_gpio = devm_gpiod_get_optional(&spi->dev, "busy", GPIOD_IN);
	if (IS_ERR(epd->busy_gpio))
		return PTR_ERR(epd->busy_gpio);

	/* Allocate framebuffer */
	info = framebuffer_alloc(0, &spi->dev);
	if (!info)
		return -ENOMEM;

	info->par = epd;
	epd->info = info;

	/* Setup framebuffer parameters */
	strscpy(info->fix.id, "PamirAI", sizeof(info->fix.id));
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_MONO01;
	info->fix.line_length = epd->bytes_per_line;

	info->var.xres = epd->width;
	info->var.yres = epd->height;
	info->var.xres_virtual = epd->width;
	info->var.yres_virtual = epd->height;
	info->var.bits_per_pixel = 1;
	info->var.red.length = 0;
	info->var.green.length = 0;
	info->var.blue.length = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	info->fbops = &epd_fb_ops;

	/* Allocate screen buffer suitable for userspace mapping */
	info->screen_base = vmalloc_user(epd->screensize);
	if (!info->screen_base) {
		ret = -ENOMEM;
		goto err_fb_release;
	}
	/* Clear the allocated memory */
	memset(info->screen_base, 0, epd->screensize);

	/* No physical address for vmalloc'd memory */
	info->fix.smem_start = 0;
	info->fix.smem_len = epd->screensize;

	/* Register framebuffer */
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to register framebuffer: %d\n", ret);
		goto err_free_screen;
	}

	spi_set_drvdata(spi, epd);

	/* Initialize display hardware */
	ret = epd_hw_init(epd);
	if (ret) {
		dev_err(&spi->dev, "Hardware initialization failed: %d\n", ret);
		goto err_unregister_fb;
	}

	/* Create sysfs attributes */
	ret = sysfs_create_group(&spi->dev.kobj, &epd_attr_group);
	if (ret) {
		dev_err(&spi->dev, "Failed to create sysfs attributes: %d\n",
			ret);
		goto err_unregister_fb;
	}

	dev_info(&spi->dev, "Pamir AI E-Ink display registered: %ux%u pixels\n",
		 epd->width, epd->height);

	return 0;

err_unregister_fb:
	unregister_framebuffer(info);
err_free_screen:
	vfree(info->screen_base);
err_fb_release:
	framebuffer_release(info);
	return ret;
}

static void epd_remove(struct spi_device *spi)
{
	struct epd_dev *epd = spi_get_drvdata(spi);
	struct fb_info *info = epd->info;

	sysfs_remove_group(&spi->dev.kobj, &epd_attr_group);

	if (info) {
		unregister_framebuffer(info);
		vfree(info->screen_base);
		if (epd->base_map_buffer)
			vfree(epd->base_map_buffer);
		framebuffer_release(info);
	}
}

static const struct of_device_id epd_of_match[] = {
	{ .compatible = "pamir-ai,eink-display" },
	{}
};
MODULE_DEVICE_TABLE(of, epd_of_match);

static struct spi_driver epd_spi_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= epd_of_match,
	},
	.probe	= epd_probe,
	.remove	= epd_remove,
};

module_spi_driver(epd_spi_driver);

MODULE_AUTHOR("Pamir AI Engineering <engineering@pamir.ai>");
MODULE_DESCRIPTION("Pamir AI E-Ink Display Driver");
MODULE_LICENSE("GPL v2");
