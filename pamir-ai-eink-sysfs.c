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

	if (area.x % 8 != 0 || area.width % 8 != 0)
		return -EINVAL;

	if (area.x > epd->width || area.y > epd->height ||
	    area.width > epd->width || area.height > epd->height ||
	    (u32)area.x + (u32)area.width > epd->width ||
	    (u32)area.y + (u32)area.height > epd->height)
		return -EINVAL;

	mutex_lock(&epd->lock);
	epd->partial_area = area;
	epd->partial_area_set = true;
	mutex_unlock(&epd->lock);

	return count;
}

static DEVICE_ATTR_RW(partial_area);

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
	return ret ? ret : count;
}

static DEVICE_ATTR_WO(deep_sleep);

static ssize_t force_reset_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct epd_dev *epd = spi_get_drvdata(spi);
	int ret;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	dev_warn(dev, "Force reset requested - attempting recovery\n");

	ret = epd_hw_init(epd);
	if (ret) {
		dev_err(dev, "Force reset failed: %d\n", ret);
		epd->initialized = true;
		return ret;
	}

	mutex_lock(&epd->lock);
	epd->partial_area_set = false;
	epd->update_mode = EPD_MODE_FULL;
	epd->initialized = true;
	mutex_unlock(&epd->lock);

	dev_info(dev, "Force reset completed successfully\n");
	return count;
}

static DEVICE_ATTR_WO(force_reset);

static struct attribute *epd_attrs[] = {
	&dev_attr_update_mode.attr,    &dev_attr_partial_area.attr,
	&dev_attr_trigger_update.attr, &dev_attr_deep_sleep.attr,
	&dev_attr_force_reset.attr,    NULL,
};

const struct attribute_group epd_attr_group = {
	.attrs = epd_attrs,
};
