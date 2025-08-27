// SPDX-License-Identifier: GPL-2.0
/*
 * Framebuffer operations for Pamir AI E-Ink display
 *
 * Copyright (C) 2025 Pamir AI
 */

 #include <linux/kernel.h>
 #include <linux/fb.h>
 #include <linux/uaccess.h>
 #include <linux/mm.h>
 #include <linux/vmalloc.h>

 #include "pamir-ai-eink-internal.h"

static ssize_t
epd_fb_write(struct fb_info *info, const char __user *buf,
	     size_t count, loff_t *ppos)
{
	struct epd_dev *epd = info->par;
	ssize_t rc;

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
 *
 * Return: 0 on success, negative error code on failure
 */
static int
epd_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
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
 *
 * Return: 0 on success, negative error code on failure
 */
static int
epd_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	/* Map the vmalloc_user allocated memory to userspace */
	return remap_vmalloc_range(vma, info->screen_base, 0);
}

const struct fb_ops epd_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= fb_sys_read,
	.fb_write	= epd_fb_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_ioctl	= epd_fb_ioctl,
	.fb_mmap	= epd_fb_mmap,
};
EXPORT_SYMBOL_GPL(epd_fb_ops);
