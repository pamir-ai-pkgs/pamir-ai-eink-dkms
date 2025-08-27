// SPDX-License-Identifier: GPL-2.0
/*
 * Test program for Pamir AI E-Ink display update modes
 *
 * Copyright (C) 2024 Pamir AI <engineering@pamir.ai>
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/ioctl.h>
 #include <sys/mman.h>
 #include <string.h>
 #include <errno.h>
 #include <linux/fb.h>

 #include "pamir-ai-eink.h"

 #define FB_DEVICE "/dev/fb0"

static void draw_pattern(unsigned char *fb, int width, int height, int pattern)
{
	int x, y;
	int bytes_per_line = (width + 7) / 8;

	switch (pattern) {
	case 0: /* Clear screen (all white) */
		memset(fb, 0xFF, bytes_per_line * height);
		break;

	case 1: /* Fill screen (all black) */
		memset(fb, 0x00, bytes_per_line * height);
		break;

	case 2: /* Checkerboard pattern */
		for (y = 0; y < height; y++) {
			for (x = 0; x < bytes_per_line; x++) {
				fb[y * bytes_per_line + x] =
					((y / 8) % 2) ? 0xAA : 0x55;
			}
		}
		break;

	case 3: /* Horizontal stripes */
		for (y = 0; y < height; y++) {
			unsigned char val = ((y / 16) % 2) ? 0x00 : 0xFF;
			memset(&fb[y * bytes_per_line], val, bytes_per_line);
		}
		break;
	}
}

static void draw_rectangle(unsigned char *fb, int width, int height, int rx,
			   int ry, int rw, int rh, int fill)
{
	int x, y;
	int bytes_per_line = (width + 7) / 8;

	for (y = ry; y < ry + rh && y < height; y++) {
		for (x = rx; x < rx + rw && x < width; x++) {
			int byte_idx = y * bytes_per_line + x / 8;
			int bit_idx = 7 - (x % 8);

			if (fill) {
				/* Set pixel (black) */
				fb[byte_idx] &= ~(1 << bit_idx);
			} else {
				/* Clear pixel (white) */
				fb[byte_idx] |= (1 << bit_idx);
			}
		}
	}
}

static void test_full_update(int fd, unsigned char *fb, int width, int height)
{
	int mode = EPD_MODE_FULL;

	printf("Testing FULL update mode...\n");

	/* Set update mode to full */
	if (ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("Failed to set full update mode");
		return;
	}

	/* Draw pattern and update */
	draw_pattern(fb, width, height, 2); /* Checkerboard */
	if (ioctl(fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("Failed to trigger update");
	}

	printf("Full update completed\n");
	sleep(3);
}

static void test_partial_update(int fd, unsigned char *fb, int width,
				int height)
{
	int mode = EPD_MODE_PARTIAL;
	struct epd_update_area area;

	printf("Testing PARTIAL update mode...\n");

	/* Set update mode to partial */
	if (ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("Failed to set partial update mode");
		return;
	}

	/* Clear screen first with full update */
	mode = EPD_MODE_FULL;
	ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode);
	draw_pattern(fb, width, height, 0); /* Clear */
	ioctl(fd, EPD_IOC_UPDATE_DISPLAY);
	sleep(2);

	/* Switch to partial mode */
	mode = EPD_MODE_PARTIAL;
	ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode);

	/* Update a small area */
	area.x = 32; /* Must be multiple of 8 */
	area.y = 50;
	area.width = 64; /* Must be multiple of 8 */
	area.height = 64;

	if (ioctl(fd, EPD_IOC_SET_PARTIAL_AREA, &area) < 0) {
		perror("Failed to set partial area");
		return;
	}

	/* Draw a rectangle in the partial area */
	draw_rectangle(fb, width, height, area.x, area.y, area.width,
		       area.height, 1);

	if (ioctl(fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("Failed to trigger partial update");
	}

	printf("Partial update completed\n");
	sleep(2);
}

static void test_base_map_mode(int fd, unsigned char *fb, int width, int height)
{
	int mode;

	printf("Testing BASE MAP mode...\n");

	/* Set base map with a pattern */
	mode = EPD_MODE_BASE_MAP;
	if (ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("Failed to set base map mode");
		return;
	}

	/* Draw horizontal stripes as base */
	draw_pattern(fb, width, height, 3);

	if (ioctl(fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("Failed to update base map");
	}

	printf("Base map update completed\n");
	sleep(3);

	/* Now do partial updates on top */
	mode = EPD_MODE_PARTIAL;
	ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode);

	/* Draw some rectangles */
	draw_rectangle(fb, width, height, 16, 100, 96, 32, 1);
	ioctl(fd, EPD_IOC_UPDATE_DISPLAY);

	printf("Partial update over base map completed\n");
	sleep(2);
}

static void test_sysfs_interface(void)
{
	FILE *fp;
	char buf[256];

	printf("\nTesting sysfs interface...\n");

	/* Try to read current update mode */
	fp = fopen("/sys/bus/spi/devices/spi0.0/update_mode", "r");
	if (fp) {
		if (fgets(buf, sizeof(buf), fp)) {
			printf("Current update mode: %s", buf);
		}
		fclose(fp);
	}

	/* Try to set update mode via sysfs */
	fp = fopen("/sys/bus/spi/devices/spi0.0/update_mode", "w");
	if (fp) {
		fprintf(fp, "partial\n");
		fclose(fp);
		printf("Set update mode to partial via sysfs\n");
	}
}

int main(int argc, char *argv[])
{
	int fd;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	unsigned char *fb;
	size_t screensize;
	int test = -1;

	if (argc > 1) {
		test = atoi(argv[1]);
	}

	/* Open framebuffer device */
	fd = open(FB_DEVICE, O_RDWR);
	if (fd < 0) {
		perror("Failed to open framebuffer");
		return 1;
	}

	/* Get screen info */
	if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		perror("Failed to get variable screen info");
		close(fd);
		return 1;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		perror("Failed to get fixed screen info");
		close(fd);
		return 1;
	}

	printf("E-Ink Display: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres,
	       vinfo.bits_per_pixel);

	/* Map framebuffer to memory */
	screensize = finfo.line_length * vinfo.yres;
	fb = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (fb == MAP_FAILED) {
		perror("Failed to map framebuffer");
		close(fd);
		return 1;
	}

	/* Run tests */
	if (test == -1 || test == 0) {
		test_full_update(fd, fb, vinfo.xres, vinfo.yres);
	}

	if (test == -1 || test == 1) {
		test_partial_update(fd, fb, vinfo.xres, vinfo.yres);
	}

	if (test == -1 || test == 2) {
		test_base_map_mode(fd, fb, vinfo.xres, vinfo.yres);
	}

	if (test == -1 || test == 3) {
		test_sysfs_interface();
	}

	/* Put display to deep sleep */
	printf("\nEntering deep sleep mode...\n");
	if (ioctl(fd, EPD_IOC_DEEP_SLEEP) < 0) {
		perror("Failed to enter deep sleep");
	}

	/* Cleanup */
	munmap(fb, screensize);
	close(fd);

	return 0;
}

