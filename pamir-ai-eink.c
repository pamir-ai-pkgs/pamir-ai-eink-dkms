// SPDX-License-Identifier: GPL-2.0
/*
 * Framebuffer driver for Pamir AI E-Ink displays
 *
 * Copyright (C) 2024 Pamir AI <engineering@pamir.ai>
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

 #define DRIVER_NAME	"pamir-ai-eink"

/* EPD Commands */
 #define EPD_CMD_DRIVER_OUTPUT_CTRL	0x01
 #define EPD_CMD_DATA_ENTRY_MODE		0x11
 #define EPD_CMD_SW_RESET		0x12
 #define EPD_CMD_TEMP_SENSOR_READ	0x18
 #define EPD_CMD_ACTIVATE		0x20
 #define EPD_CMD_DISPLAY_UPDATE_CTRL1	0x21
 #define EPD_CMD_DISPLAY_UPDATE_CTRL2	0x22
 #define EPD_CMD_WRITE_RAM_BW		0x24
 #define EPD_CMD_BORDER_WAVEFORM	0x3C
 #define EPD_CMD_SET_RAM_X		0x44
 #define EPD_CMD_SET_RAM_Y		0x45
 #define EPD_CMD_SET_RAM_X_COUNT		0x4E
 #define EPD_CMD_SET_RAM_Y_COUNT		0x4F

/* Timing constants */
 #define EPD_RESET_DELAY_MS		10
 #define EPD_BUSY_TIMEOUT_INIT_MS	2000
 #define EPD_BUSY_TIMEOUT_UPDATE_MS	10000
 #define EPD_BUSY_POLL_INTERVAL_MS	5

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

		msleep(EPD_BUSY_POLL_INTERVAL_MS);
		elapsed += EPD_BUSY_POLL_INTERVAL_MS;
	}

	dev_warn(&epd->spi->dev, "Busy timeout after %u ms\n", timeout_ms);
	return -ETIMEDOUT;
}

static int epd_hw_init(struct epd_dev *epd)
{
	int ret;
	u8 data[4];

	/* Hardware reset */
	gpiod_set_value_cansleep(epd->reset_gpio, 0);
	msleep(EPD_RESET_DELAY_MS);
	gpiod_set_value_cansleep(epd->reset_gpio, 1);
	msleep(EPD_RESET_DELAY_MS);

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

static int epd_display_flush(struct epd_dev *epd)
{
	int ret;
	struct fb_info *info = epd->info;
	u8 *buf = info->screen_base;
	size_t len = epd->screensize;
	u8 data;

	if (!buf)
		return -ENOMEM;

	mutex_lock(&epd->lock);

	/* Write RAM for black/white */
	ret = epd_send_cmd(epd, EPD_CMD_WRITE_RAM_BW);
	if (ret)
		goto out;

	ret = epd_send_data_buf(epd, buf, len);
	if (ret)
		goto out;

	/* Trigger display update */
	ret = epd_send_cmd(epd, EPD_CMD_DISPLAY_UPDATE_CTRL2);
	if (ret)
		goto out;

	data = 0xF7;
	ret = epd_send_data_buf(epd, &data, 1);
	if (ret)
		goto out;

	ret = epd_send_cmd(epd, EPD_CMD_ACTIVATE);
	if (ret)
		goto out;

	ret = epd_wait_busy(epd, EPD_BUSY_TIMEOUT_UPDATE_MS);

out:
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
			dev_err(&epd->spi->dev,
				"Display flush failed: %d\n", ret);
	}

	return rc;
}

static const struct fb_ops epd_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= fb_sys_read,
	.fb_write	= epd_fb_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
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

	/* Get GPIOs */
	epd->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(epd->reset_gpio))
		return PTR_ERR(epd->reset_gpio);

	epd->dc_gpio = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(epd->dc_gpio))
		return PTR_ERR(epd->dc_gpio);

	epd->busy_gpio = devm_gpiod_get_optional(&spi->dev, "busy",
						  GPIOD_IN);
	if (IS_ERR(epd->busy_gpio))
		return PTR_ERR(epd->busy_gpio);

	/* Allocate framebuffer */
	info = framebuffer_alloc(0, &spi->dev);
	if (!info)
		return -ENOMEM;

	info->par = epd;
	epd->info = info;

	/* Setup framebuffer parameters */
	strscpy(info->fix.id, "EPD", sizeof(info->fix.id));
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

	/* Allocate screen buffer */
	info->screen_base = vzalloc(epd->screensize);
	if (!info->screen_base) {
		ret = -ENOMEM;
		goto err_fb_release;
	}

	info->fix.smem_start = (unsigned long)info->screen_base;
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

	dev_info(&spi->dev,
		 "Pamir AI E-Ink display registered: %ux%u pixels\n",
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

	if (info) {
		unregister_framebuffer(info);
		vfree(info->screen_base);
		framebuffer_release(info);
	}
}

static const struct of_device_id epd_of_match[] = {
	{ .compatible = "pamir-ai,eink-display" },
	{ }
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
