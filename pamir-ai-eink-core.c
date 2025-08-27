// SPDX-License-Identifier: GPL-2.0
/*
 * Core driver for Pamir AI E-Ink display
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
 #include <linux/string.h>

 #include "pamir-ai-eink-internal.h"

static int
epd_probe(struct spi_device *spi)
{
	struct device_node *np = spi->dev.of_node;
	struct epd_dev *epd;
	struct fb_info *info;
	u32 width = 0, height = 0;
	int ret;

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
	epd->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						   GPIOD_OUT_HIGH);
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
		dev_err(&spi->dev, "Failed to register framebuffer: %d\n",
			ret);
		goto err_free_screen;
	}

	spi_set_drvdata(spi, epd);

	/* Initialize display hardware */
	ret = epd_hw_init(epd);
	if (ret) {
		dev_err(&spi->dev, "Hardware initialization failed: %d\n",
			ret);
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

static void
epd_remove(struct spi_device *spi)
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

MODULE_AUTHOR("Pamir AI Engineering <support@pamir.ai>");
MODULE_DESCRIPTION("Pamir AI E-Ink Display Driver");
MODULE_LICENSE("GPL");
