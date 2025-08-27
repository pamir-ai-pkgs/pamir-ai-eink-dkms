// SPDX-License-Identifier: GPL-2.0
/*
 * Sysfs interface for Pamir AI E-Ink display
 *
 * Copyright (C) 2025 Pamir AI
 */

 #include <linux/kernel.h>
 #include <linux/device.h>
 #include <linux/spi/spi.h>
 #include <linux/sysfs.h>
 #include <linux/string.h>

 #include "pamir-ai-eink-internal.h"

/**
 * update_mode_show - Show current update mode
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Output buffer
 *
 * Return: Number of bytes written to buffer
 */
static ssize_t
update_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
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
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Input buffer
 * @count: Number of bytes to write
 *
 * Return: Number of bytes written or negative error code
 */
static ssize_t
update_mode_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
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
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Output buffer
 *
 * Return: Number of bytes written to buffer
 */
static ssize_t
partial_area_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);

	if (!epd->partial_area_set)
		return sysfs_emit(buf, "not set\n");

	return sysfs_emit(buf, "%u,%u,%u,%u\n",
			  epd->partial_area.x,
			  epd->partial_area.y,
			  epd->partial_area.width,
			  epd->partial_area.height);
}

/**
 * partial_area_store - Set partial update area
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Input buffer containing "x,y,width,height" (all values in pixels)
 * @count: Number of bytes to write
 *
 * Return: Number of bytes written or negative error code
 */
static ssize_t
partial_area_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	struct epd_update_area area;
	int ret;

	ret = sscanf(buf, "%hu,%hu,%hu,%hu",
		     &area.x, &area.y, &area.width, &area.height);
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
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Input buffer (must contain "1")
 * @count: Number of bytes to write
 *
 * Return: Number of bytes written or negative error code
 */
static ssize_t
trigger_update_store(struct device *dev, struct device_attribute *attr,
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
 * @dev: Device pointer
 * @attr: Device attribute pointer
 * @buf: Input buffer (must contain "1")
 * @count: Number of bytes to write
 *
 * Return: Number of bytes written or negative error code
 */
static ssize_t
deep_sleep_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
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

const struct attribute_group epd_attr_group = {
	.attrs = epd_attrs,
};
EXPORT_SYMBOL_GPL(epd_attr_group);
