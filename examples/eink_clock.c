/*
 * eink_clock.c - Real-time clock display with 7-segment digits
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <time.h>
#include <signal.h>
#include "pamir-ai-eink.h"

static volatile int keep_running = 1;
static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;

/* 7-segment digit patterns (5x8 characters) */
static const uint8_t digits[10][8] = {
	{ 0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00 }, /* 0 */
	{ 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* 1 */
	{ 0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE, 0x00 }, /* 2 */
	{ 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00 }, /* 3 */
	{ 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00 }, /* 4 */
	{ 0xFE, 0xC0, 0xC0, 0xFC, 0x06, 0xC6, 0x7C, 0x00 }, /* 5 */
	{ 0x7C, 0xC6, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00 }, /* 6 */
	{ 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 }, /* 7 */
	{ 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00 }, /* 8 */
	{ 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, 0x7C, 0x00 } /* 9 */
};

static const uint8_t colon[8] = {
	0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 /* : */
};

static void signal_handler(int sig)
{
	keep_running = 0;
}

static int open_framebuffer(const char *device)
{
	fb_fd = open(device, O_RDWR);
	if (fb_fd < 0) {
		perror("open framebuffer");
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		perror("FBIOGET_VSCREENINFO");
		close(fb_fd);
		return -1;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		perror("FBIOGET_FSCREENINFO");
		close(fb_fd);
		return -1;
	}

	size_t fb_size = finfo.smem_len;
	fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd,
		      0);
	if (fb_mem == MAP_FAILED) {
		perror("mmap");
		close(fb_fd);
		return -1;
	}

	printf("Framebuffer: %dx%d, %d bpp, size=%zu\n", vinfo.xres, vinfo.yres,
	       vinfo.bits_per_pixel, fb_size);

	return 0;
}

static void close_framebuffer(void)
{
	if (fb_mem != MAP_FAILED) {
		munmap(fb_mem, finfo.smem_len);
	}
	if (fb_fd >= 0) {
		close(fb_fd);
	}
}

static void draw_digit(int x, int y, int digit)
{
	if (digit < 0 || digit > 9)
		return;

	int scale = 3;

	for (int row = 0; row < 8; row++) {
		for (int sy = 0; sy < scale; sy++) {
			int y_pos = y + row * scale + sy;
			if (y_pos >= vinfo.yres)
				continue;

			uint8_t pattern = digits[digit][row];
			for (int col = 0; col < 8; col++) {
				for (int sx = 0; sx < scale; sx++) {
					int x_pos = x + col * scale + sx;
					if (x_pos >= vinfo.xres)
						continue;

					int byte_offset =
						y_pos * finfo.line_length +
						(x_pos / 8);
					int bit_offset = 7 - (x_pos % 8);

					if (pattern & (0x80 >> col)) {
						fb_mem[byte_offset] &=
							~(1 << bit_offset);
					} else {
						fb_mem[byte_offset] |=
							(1 << bit_offset);
					}
				}
			}
		}
	}
}

static void draw_colon_char(int x, int y)
{
	int scale = 3;

	for (int row = 0; row < 8; row++) {
		for (int sy = 0; sy < scale; sy++) {
			int y_pos = y + row * scale + sy;
			if (y_pos >= vinfo.yres)
				continue;

			uint8_t pattern = colon[row];
			for (int col = 0; col < 8; col++) {
				for (int sx = 0; sx < scale; sx++) {
					int x_pos = x + col * scale + sx;
					if (x_pos >= vinfo.xres)
						continue;

					int byte_offset =
						y_pos * finfo.line_length +
						(x_pos / 8);
					int bit_offset = 7 - (x_pos % 8);

					if (pattern & (0x80 >> col)) {
						fb_mem[byte_offset] &=
							~(1 << bit_offset);
					} else {
						fb_mem[byte_offset] |=
							(1 << bit_offset);
					}
				}
			}
		}
	}
}

static void clear_area(int x, int y, int width, int height)
{
	for (int row = y; row < y + height && row < vinfo.yres; row++) {
		for (int col = x; col < x + width && col < vinfo.xres; col++) {
			int byte_offset = row * finfo.line_length + (col / 8);
			int bit_offset = 7 - (col % 8);
			fb_mem[byte_offset] |= (1 << bit_offset);
		}
	}
}

static void update_clock(void)
{
	time_t rawtime;
	struct tm *timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	int digit_width = 24;
	int digit_height = 24;
	int clock_width = digit_width * 8;

	int partial_width = (vinfo.xres < clock_width) ?
				    ((vinfo.xres / 8) * 8) :
				    clock_width;
	int partial_x = (vinfo.xres < clock_width) ?
				0 :
				((vinfo.xres - clock_width) / 2 / 8) * 8;

	clear_area(partial_x, 100, partial_width, digit_height);
	int x_pos = partial_x;
	draw_digit(x_pos, 100, timeinfo->tm_hour / 10);
	x_pos += digit_width;
	draw_digit(x_pos, 100, timeinfo->tm_hour % 10);
	x_pos += digit_width;
	draw_colon_char(x_pos, 100);
	x_pos += digit_width;
	draw_digit(x_pos, 100, timeinfo->tm_min / 10);
	x_pos += digit_width;
	draw_digit(x_pos, 100, timeinfo->tm_min % 10);
	x_pos += digit_width;
	draw_colon_char(x_pos, 100);
	x_pos += digit_width;
	draw_digit(x_pos, 100, timeinfo->tm_sec / 10);
	x_pos += digit_width;
	draw_digit(x_pos, 100, timeinfo->tm_sec % 10);

	struct epd_update_area area;
	area.x = (partial_x / 8) * 8;
	area.y = 100;
	area.width = (partial_width / 8) * 8;
	area.height = digit_height;

	if (ioctl(fb_fd, EPD_IOC_SET_PARTIAL_AREA, &area) < 0) {
		perror("EPD_IOC_SET_PARTIAL_AREA");
		int mode = EPD_MODE_FULL;
		ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode);
	}

	if (ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("EPD_IOC_UPDATE_DISPLAY");
	}
}

int main(int argc, char *argv[])
{
	const char *fb_device = "/dev/fb0";

	if (argc > 1) {
		fb_device = argv[1];
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	if (open_framebuffer(fb_device) < 0) {
		return 1;
	}

	memset(fb_mem, 0xFF, finfo.smem_len);
	int mode = EPD_MODE_FULL;
	if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("EPD_IOC_SET_UPDATE_MODE");
	}
	if (ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("EPD_IOC_UPDATE_DISPLAY");
	}

	/* Display title */
	printf("E-Ink Clock Display\n");
	printf("Display: %dx%d\n", vinfo.xres, vinfo.yres);
	printf("Press Ctrl+C to exit\n");

	mode = EPD_MODE_PARTIAL;
	if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("EPD_IOC_SET_UPDATE_MODE partial");
	}

	while (keep_running) {
		update_clock();
		sleep(1);
	}

	/* Clear display on exit */
	printf("\nClearing display...\n");
	if (ioctl(fb_fd, EPD_IOC_CLEAR_DISPLAY) < 0) {
		perror("EPD_IOC_CLEAR_DISPLAY");
	}

	printf("Cleaning up...\n");
	close_framebuffer();

	return 0;
}
