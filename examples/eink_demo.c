/* SPDX-License-Identifier: GPL-2.0 */
/*
 * eink_demo.c - Demonstration program for Pamir AI E-Ink display driver
 *
 * Copyright (C) 2025 Pamir AI
 *
 * Compile with:
 *   gcc -o eink_demo eink_demo.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include "../pamir-ai-eink.h"

/* Simple font - 8x8 bitmap for characters */
static const unsigned char font8x8_basic[128][8] = {
	[' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	['H'] = {0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0x81},
	['E'] = {0xFF, 0x80, 0x80, 0xFC, 0x80, 0x80, 0x80, 0xFF},
	['L'] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF},
	['O'] = {0x7E, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7E},
};

static void
draw_char(unsigned char *fb, int fb_width, int x, int y, char c)
{
	int i, j;
	const unsigned char *glyph = font8x8_basic[(int)c];
	
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			int px = x + j;
			int py = y + i;
			int byte_offset = py * (fb_width / 8) + (px / 8);
			int bit_offset = 7 - (px % 8);
			
			if (glyph[i] & (1 << (7 - j))) {
				/* Black pixel */
				fb[byte_offset] &= ~(1 << bit_offset);
			}
		}
	}
}

static void
draw_string(unsigned char *fb, int fb_width, int x, int y, const char *str)
{
	int i;
	for (i = 0; str[i]; i++) {
		draw_char(fb, fb_width, x + i * 8, y, str[i]);
	}
}

static void
draw_rectangle(unsigned char *fb, int fb_width, int x, int y,
	       int width, int height, int filled)
{
	int i, j;
	
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			if (filled || i == 0 || i == height - 1 ||
			    j == 0 || j == width - 1) {
				int px = x + j;
				int py = y + i;
				int byte_offset = py * (fb_width / 8) + (px / 8);
				int bit_offset = 7 - (px % 8);
				
				/* Black pixel */
				fb[byte_offset] &= ~(1 << bit_offset);
			}
		}
	}
}

int
main(int argc, char *argv[])
{
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	struct epd_update_area area;
	unsigned char *fbp;
	size_t screensize;
	int fbfd;
	int mode;
	int ret;
	
	/* Open framebuffer device */
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0) {
		perror("Error opening framebuffer device");
		return 1;
	}
	
	/* Get fixed screen information */
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
		perror("Error reading fixed information");
		close(fbfd);
		return 1;
	}
	
	/* Get variable screen information */
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		perror("Error reading variable information");
		close(fbfd);
		return 1;
	}
	
	printf("E-Ink Display Information:\n");
	printf("  Resolution: %dx%d\n", vinfo.xres, vinfo.yres);
	printf("  Bits per pixel: %d\n", vinfo.bits_per_pixel);
	printf("  Line length: %d bytes\n", finfo.line_length);
	
	/* Calculate screen size */
	screensize = vinfo.yres_virtual * finfo.line_length;
	
	/* Memory map the framebuffer */
	fbp = mmap(0, screensize, PROT_READ | PROT_WRITE,
		   MAP_SHARED, fbfd, 0);
	if ((intptr_t)fbp == -1) {
		perror("Error mapping framebuffer device to memory");
		close(fbfd);
		return 1;
	}
	
	printf("\n=== Demo 1: Full Update Mode ===\n");
	
	/* Set full update mode */
	mode = EPD_MODE_FULL;
	if (ioctl(fbfd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("Error setting update mode");
		goto cleanup;
	}
	
	/* Clear screen (white) */
	memset(fbp, 0xFF, screensize);
	
	/* Draw border */
	draw_rectangle(fbp, vinfo.xres, 2, 2, vinfo.xres - 4, vinfo.yres - 4, 0);
	
	/* Draw title */
	draw_string(fbp, vinfo.xres, 10, 10, "HELLO E-INK");
	
	/* Draw some shapes */
	draw_rectangle(fbp, vinfo.xres, 10, 30, 50, 30, 1);  /* Filled */
	draw_rectangle(fbp, vinfo.xres, 70, 30, 50, 30, 0);  /* Outline */
	
	/* Trigger update (happens automatically on write, but explicit for demo) */
	if (ioctl(fbfd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("Error updating display");
	}
	
	printf("Full update complete. Press Enter to continue...");
	getchar();
	
	printf("\n=== Demo 2: Partial Update Mode ===\n");
	
	/* Switch to partial update mode */
	mode = EPD_MODE_PARTIAL;
	if (ioctl(fbfd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("Error setting partial mode");
		goto cleanup;
	}
	
	/* Define partial area (must be byte-aligned) */
	area.x = 128;  /* Multiple of 8 */
	area.y = 30;
	area.width = 80;   /* Multiple of 8 */
	area.height = 40;
	
	if (ioctl(fbfd, EPD_IOC_SET_PARTIAL_AREA, &area) < 0) {
		perror("Error setting partial area");
		goto cleanup;
	}
	
	/* Update only the partial area */
	draw_rectangle(fbp, vinfo.xres, 130, 32, 76, 36, 1);
	
	if (ioctl(fbfd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("Error updating partial area");
	}
	
	printf("Partial update complete. Press Enter to continue...");
	getchar();
	
	printf("\n=== Demo 3: Base Map Mode ===\n");
	
	/* Set base map mode (sets both buffers) */
	if (ioctl(fbfd, EPD_IOC_SET_BASE_MAP, NULL) < 0) {
		perror("Error setting base map");
		goto cleanup;
	}
	
	printf("Base map set. Press Enter to enter deep sleep...");
	getchar();
	
	/* Enter deep sleep mode */
	printf("\n=== Entering Deep Sleep Mode ===\n");
	if (ioctl(fbfd, EPD_IOC_DEEP_SLEEP) < 0) {
		perror("Error entering deep sleep");
	}
	
	printf("Display is now in deep sleep mode.\n");
	
cleanup:
	munmap(fbp, screensize);
	close(fbfd);
	return 0;
}