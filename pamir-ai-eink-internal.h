/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal header for Pamir AI E-Ink driver
 *
 * Copyright (C) 2025 Pamir AI
 */

 #ifndef _PAMIR_AI_EINK_INTERNAL_H
 #define _PAMIR_AI_EINK_INTERNAL_H

 #include <linux/fb.h>
 #include <linux/mutex.h>
 #include <linux/spi/spi.h>
 #include <linux/gpio/consumer.h>

 #define DRIVER_NAME	"pamir-ai-eink"

/* EPD Commands */
 #define EPD_CMD_DRIVER_OUTPUT_CTRL	0x01
 #define EPD_CMD_DEEP_SLEEP_MODE		0x10
 #define EPD_CMD_DATA_ENTRY_MODE		0x11
 #define EPD_CMD_SW_RESET		0x12
 #define EPD_CMD_TEMP_SENSOR_READ	0x18
 #define EPD_CMD_ACTIVATE		0x20
 #define EPD_CMD_DISPLAY_UPDATE_CTRL1	0x21
 #define EPD_CMD_DISPLAY_UPDATE_CTRL2	0x22
 #define EPD_CMD_WRITE_RAM_BW		0x24
 #define EPD_CMD_WRITE_RAM_RED		0x26	/* Used for base map mode */
 #define EPD_CMD_BORDER_WAVEFORM		0x3C
 #define EPD_CMD_SET_RAM_X		0x44
 #define EPD_CMD_SET_RAM_Y		0x45
 #define EPD_CMD_SET_RAM_X_COUNT		0x4E
 #define EPD_CMD_SET_RAM_Y_COUNT		0x4F

/* Update modes for SSD1681 controller */
 #define EPD_UPDATE_MODE_FULL		0xF7	/* Full refresh  */
 #define EPD_UPDATE_MODE_PARTIAL		0xFF	/* Fast partial refresh */

/* Border waveform control */
 #define EPD_BORDER_NORMAL		0x05	/* Normal border for full update */
 #define EPD_BORDER_PARTIAL		0x80	/* Locked border for partial update */

/* Timing constants */
 #define EPD_RESET_DELAY_MS		10
 #define EPD_BUSY_TIMEOUT_INIT_MS	2000
 #define EPD_BUSY_TIMEOUT_UPDATE_MS	10000
 #define EPD_BUSY_POLL_INTERVAL_MS	5

/* IOCTL commands for e-ink control */
 #define EPD_IOC_MAGIC			'E'
 #define EPD_IOC_SET_UPDATE_MODE		_IOW(EPD_IOC_MAGIC, 1, int)
 #define EPD_IOC_GET_UPDATE_MODE		_IOR(EPD_IOC_MAGIC, 2, int)
 #define EPD_IOC_SET_PARTIAL_AREA	_IOW(EPD_IOC_MAGIC, 3, struct epd_update_area)
 #define EPD_IOC_UPDATE_DISPLAY		_IO(EPD_IOC_MAGIC, 4)
 #define EPD_IOC_DEEP_SLEEP		_IO(EPD_IOC_MAGIC, 5)
 #define EPD_IOC_SET_BASE_MAP		_IOW(EPD_IOC_MAGIC, 6, void *)
 #define EPD_IOC_RESET			_IO(EPD_IOC_MAGIC, 7)

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

/**
 * struct epd_dev - Main device structure for e-ink display
 * @spi: SPI device pointer
 * @info: Framebuffer info structure
 * @reset_gpio: GPIO descriptor for reset pin
 * @dc_gpio: GPIO descriptor for data/command pin
 * @busy_gpio: GPIO descriptor for busy pin
 * @width: Display width in pixels
 * @height: Display height in pixels
 * @bytes_per_line: Number of bytes per display line
 * @screensize: Total size of screen buffer in bytes
 * @lock: Mutex for protecting device state
 * @update_mode: Current display update mode
 * @partial_area: Current partial update area
 * @partial_area_set: Flag indicating if partial area is set
 * @base_map_buffer: Buffer for base map mode
 */
struct epd_dev {
	struct spi_device	*spi;
	struct fb_info		*info;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*dc_gpio;
	struct gpio_desc	*busy_gpio;
	u32			width;
	u32			height;
	u32			bytes_per_line;
	size_t			screensize;
	struct mutex		lock;

	/* Update mode control */
	enum epd_update_mode	update_mode;
	struct epd_update_area	partial_area;
	bool			partial_area_set;
	bool			initialized;		/* Display initialized flag */
	u8			*base_map_buffer;	/* Buffer for base map mode */
};

/* Hardware control functions (pamir-ai-eink-hw.c) */
int epd_send_cmd(struct epd_dev *epd, u8 cmd);
int epd_send_data_buf(struct epd_dev *epd, const u8 *buf, size_t len);
int epd_wait_busy(struct epd_dev *epd, unsigned int timeout_ms);
int epd_hw_init(struct epd_dev *epd);
int epd_set_ram_area(struct epd_dev *epd, u16 x_start, u16 y_start,
		     u16 x_end, u16 y_end);

/* Display operation functions (pamir-ai-eink-display.c) */
int epd_full_update(struct epd_dev *epd);
int epd_partial_update(struct epd_dev *epd);
int epd_base_map_update(struct epd_dev *epd);
int epd_display_flush(struct epd_dev *epd);
int epd_deep_sleep(struct epd_dev *epd);

/* Framebuffer operation functions (pamir-ai-eink-fb.c) */
extern const struct fb_ops epd_fb_ops;

/* Sysfs interface functions (pamir-ai-eink-sysfs.c) */
extern const struct attribute_group epd_attr_group;

 #endif /* _PAMIR_AI_EINK_INTERNAL_H */
