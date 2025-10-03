/*
 * eink_monitor.c - Enhanced system resource monitor with improved UI
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <linux/fb.h>
#include <signal.h>
#include <time.h>
#include "pamir-ai-eink.h"

static volatile int keep_running = 1;
static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;

#define HISTORY_SIZE 50
static int cpu_history[HISTORY_SIZE] = { 0 };
static int mem_history[HISTORY_SIZE] = { 0 };
static int history_index = 0;

/* 5x7 bitmap font for digits and essential characters */
static const uint8_t font_5x7[][7] = {
	/* 0 */ { 0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C },
	/* 1 */ { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E },
	/* 2 */ { 0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE },
	/* 3 */ { 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C },
	/* 4 */ { 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C },
	/* 5 */ { 0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C },
	/* 6 */ { 0x7C, 0xC6, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C },
	/* 7 */ { 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30 },
	/* 8 */ { 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C },
	/* 9 */ { 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0xC6, 0x7C },
	/* % */ { 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00 },
	/* : */ { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00 },
	/* - */ { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00 },
	/* . */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18 },
	/* space */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

/* Letters for labels */
static const uint8_t font_letters[][7] = {
	/* C */ { 0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C },
	/* P */ { 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0 },
	/* U */ { 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C },
	/* M */ { 0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6 },
	/* e */ { 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0xC6, 0x7C },
	/* m */ { 0x00, 0xDC, 0xEE, 0xD6, 0xD6, 0xD6, 0xC6 },
	/* D */ { 0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8 },
	/* i */ { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C },
	/* s */ { 0x00, 0x7C, 0xC6, 0x70, 0x1C, 0xC6, 0x7C },
	/* k */ { 0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC },
};

/* Simple icons (8x8) */
static const uint8_t icon_cpu[8] = { 0x3C, 0x42, 0x99, 0xBD,
				     0xBD, 0x99, 0x42, 0x3C };

static const uint8_t icon_mem[8] = { 0xFF, 0x81, 0xBD, 0xA5,
				     0xA5, 0xBD, 0x81, 0xFF };

static const uint8_t icon_disk[8] = { 0x7E, 0xFF, 0xFF, 0xFF,
				      0xE7, 0xC3, 0x81, 0x7E };

/* Dithering patterns for gradient effects */
static const uint8_t dither_patterns[4] = {
	0x00, /* 0% - all white */
	0x55, /* 25% - checkerboard */
	0xAA, /* 50% - inverse checkerboard */
	0xFF /* 100% - all black */
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

	printf("Framebuffer: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres,
	       vinfo.bits_per_pixel);

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

static void set_pixel(int x, int y, int value)
{
	if (x < 0 || x >= vinfo.xres || y < 0 || y >= vinfo.yres)
		return;

	int byte_offset = y * finfo.line_length + (x / 8);
	int bit_offset = 7 - (x % 8);

	if (value) {
		fb_mem[byte_offset] &= ~(1 << bit_offset);
	} else {
		fb_mem[byte_offset] |= (1 << bit_offset);
	}
}

static void draw_char_5x7(int x, int y, char c)
{
	const uint8_t *bitmap = NULL;

	if (c >= '0' && c <= '9') {
		bitmap = font_5x7[c - '0'];
	} else if (c == '%') {
		bitmap = font_5x7[10];
	} else if (c == ':') {
		bitmap = font_5x7[11];
	} else if (c == '-') {
		bitmap = font_5x7[12];
	} else if (c == '.') {
		bitmap = font_5x7[13];
	} else if (c == ' ') {
		bitmap = font_5x7[14];
	} else {
		return;
	}

	for (int row = 0; row < 7; row++) {
		uint8_t line = bitmap[row];
		for (int col = 0; col < 6; col++) {
			if (line & (0x80 >> col)) {
				set_pixel(x + col, y + row, 1);
			}
		}
	}
}

static void draw_string(int x, int y, const char *str)
{
	int pos = 0;
	while (*str) {
		draw_char_5x7(x + pos * 6, y, *str);
		str++;
		pos++;
	}
}

static void draw_icon(int x, int y, const uint8_t *icon)
{
	for (int row = 0; row < 8; row++) {
		uint8_t line = icon[row];
		for (int col = 0; col < 8; col++) {
			if (line & (0x80 >> col)) {
				set_pixel(x + col, y + row, 1);
			}
		}
	}
}

static void draw_rect(int x, int y, int width, int height, int filled)
{
	if (filled) {
		for (int row = y; row < y + height; row++) {
			for (int col = x; col < x + width; col++) {
				set_pixel(col, row, 1);
			}
		}
	} else {
		/* Top and bottom edges */
		for (int col = x; col < x + width; col++) {
			set_pixel(col, y, 1);
			set_pixel(col, y + height - 1, 1);
		}
		/* Left and right edges */
		for (int row = y; row < y + height; row++) {
			set_pixel(x, row, 1);
			set_pixel(x + width - 1, row, 1);
		}
	}
}

static void draw_dithered_rect(int x, int y, int width, int height, int level)
{
	uint8_t pattern = dither_patterns[level % 4];

	for (int row = y; row < y + height; row++) {
		for (int col = x; col < x + width; col++) {
			int bit = ((row & 1) << 1) | (col & 1);
			if (pattern & (1 << bit)) {
				set_pixel(col, row, 1);
			}
		}
	}
}

static void clear_area(int x, int y, int width, int height)
{
	for (int row = y; row < y + height && row < vinfo.yres; row++) {
		for (int col = x; col < x + width && col < vinfo.xres; col++) {
			set_pixel(col, row, 0);
		}
	}
}

static void draw_horizontal_line(int x, int y, int width, int dashed)
{
	for (int i = 0; i < width; i++) {
		if (!dashed || (i % 4 < 2)) {
			set_pixel(x + i, y, 1);
		}
	}
}

static void draw_vertical_line(int x, int y, int height, int dashed)
{
	for (int i = 0; i < height; i++) {
		if (!dashed || (i % 4 < 2)) {
			set_pixel(x, y + i, 1);
		}
	}
}

static int get_cpu_usage(void)
{
	static long prev_idle = 0, prev_total = 0;
	FILE *fp = fopen("/proc/stat", "r");
	if (!fp)
		return 0;

	char buffer[256];
	fgets(buffer, sizeof(buffer), fp);
	fclose(fp);

	long user, nice, system, idle, iowait, irq, softirq;
	sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system,
	       &idle, &iowait, &irq, &softirq);

	long total = user + nice + system + idle + iowait + irq + softirq;
	long total_diff = total - prev_total;
	long idle_diff = idle - prev_idle;

	int usage = 0;
	if (total_diff > 0) {
		usage = 100 * (total_diff - idle_diff) / total_diff;
	}

	prev_idle = idle;
	prev_total = total;

	return usage;
}

static int get_memory_usage(void)
{
	FILE *fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return 0;

	char buffer[256];
	long total = 0, available = 0;

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (sscanf(buffer, "MemTotal: %ld", &total) == 1)
			continue;
		if (sscanf(buffer, "MemAvailable: %ld", &available) == 1)
			break;
	}
	fclose(fp);

	if (total > 0) {
		return 100 * (total - available) / total;
	}
	return 0;
}

static int get_disk_usage(void)
{
	struct statvfs stat;
	if (statvfs("/", &stat) != 0)
		return 0;

	unsigned long total = stat.f_blocks * stat.f_frsize;
	unsigned long available = stat.f_bavail * stat.f_frsize;

	if (total > 0) {
		return 100 * (total - available) / total;
	}
	return 0;
}

static void draw_enhanced_graph(int x, int y, int width, int height, int *data,
				int data_size, const char *label,
				int current_value, const uint8_t *icon)
{
	/* Clear background */
	clear_area(x, y, width, height);

	/* Draw border */
	draw_rect(x, y, width, height, 0);

	/* Draw icon if provided */
	if (icon) {
		draw_icon(x + 2, y + 2, icon);
		/* Draw label next to icon */
		draw_string(x + 12, y + 2, label);
	} else {
		draw_string(x + 2, y + 2, label);
	}

	/* Draw current value */
	char value_str[16];
	snprintf(value_str, sizeof(value_str), "%d%%", current_value);
	draw_string(x + width - 25, y + 2, value_str);

	/* Graph area */
	int graph_y = y + 12;
	int graph_height = height - 15;
	int graph_x = x + 2;
	int graph_width = width - 4;

	/* Draw horizontal grid lines at 25%, 50%, 75% */
	for (int i = 1; i <= 3; i++) {
		int grid_y = graph_y + graph_height - (graph_height * i / 4);
		draw_horizontal_line(graph_x, grid_y, graph_width, 1);
	}

	/* Draw filled area graph */
	for (int i = 0; i < data_size && i < graph_width; i++) {
		int value = data[(history_index + i) % data_size];
		int bar_height = (value * graph_height) / 100;

		/* Draw vertical line for this data point */
		for (int j = 0; j < bar_height; j++) {
			int y_pos = graph_y + graph_height - j - 1;
			/* Use dithering for visual effect */
			if (j < bar_height / 4) {
				set_pixel(graph_x + i, y_pos, 1);
			} else if (j < bar_height / 2 && (i % 2 == 0)) {
				set_pixel(graph_x + i, y_pos, 1);
			} else if (j < 3 * bar_height / 4 &&
				   ((i + j) % 3 != 0)) {
				set_pixel(graph_x + i, y_pos, 1);
			} else {
				set_pixel(graph_x + i, y_pos, 1);
			}
		}
	}

	/* Draw alert indicator if value is high */
	if (current_value > 80) {
		/* Draw warning triangle */
		for (int i = 0; i < 5; i++) {
			set_pixel(x + width - 10 + i, y + 2 + (4 - i), 1);
			set_pixel(x + width - 10 + i, y + 2 + (4 + i), 1);
		}
		set_pixel(x + width - 8, y + 5, 1); /* Exclamation mark */
		set_pixel(x + width - 8, y + 7, 1);
	}
}

static void draw_enhanced_bar(int x, int y, int width, int height, int value,
			      const char *label)
{
	clear_area(x, y, width, height);
	draw_rect(x, y, width, height, 0);

	/* Draw label */
	draw_string(x + 2, y + 2, label);

	/* Draw percentage value */
	char value_str[16];
	snprintf(value_str, sizeof(value_str), "%d%%", value);
	draw_string(x + width - 25, y + 2, value_str);

	/* Draw progress bar */
	int bar_y = y + 12;
	int bar_height = 8;
	int bar_width = ((width - 4) * value) / 100;

	/* Use dithered fill based on value */
	int dither_level = (value < 25) ? 1 :
			   (value < 50) ? 2 :
			   (value < 75) ? 3 :
					  3;
	draw_dithered_rect(x + 2, bar_y, bar_width, bar_height, dither_level);

	/* Draw bar border */
	draw_rect(x + 2, bar_y, width - 4, bar_height, 0);
}

static void draw_header(void)
{
	/* Draw title bar */
	draw_rect(0, 0, vinfo.xres, 16, 1); /* Filled black bar */

	/* Draw inverted title text (white on black) */
	char title[] = "SYSTEM MONITOR";
	int title_x = (vinfo.xres - strlen(title) * 6) / 2;

	/* First draw the text area in black, then invert pixels for text */
	for (int i = 0; title[i]; i++) {
		for (int row = 0; row < 7; row++) {
			for (int col = 0; col < 6; col++) {
				/* Invert the pixel color for white text on black */
				set_pixel(title_x + i * 6 + col, 4 + row, 0);
			}
		}
	}

	/* Now draw the actual text in white */
	int pos = 0;
	for (int i = 0; title[i]; i++) {
		if (title[i] >= 'A' && title[i] <= 'Z') {
			/* Simple uppercase letter rendering */
			for (int px = 1; px < 5; px++) {
				for (int py = 1; py < 6; py++) {
					if ((title[i] == 'S' &&
					     ((px == 1 && py < 3) ||
					      (py == 1 || py == 3 || py == 5) ||
					      (px == 4 && py > 3))) ||
					    (title[i] == 'Y' &&
					     (((px == 1 || px == 4) && py < 3) ||
					      ((px == 2 || px == 3) &&
					       py >= 3))) ||
					    (title[i] == 'T' &&
					     (py == 1 || px == 2 || px == 3)) ||
					    (title[i] == 'E' &&
					     (px == 1 || py == 1 || py == 3 ||
					      py == 5)) ||
					    (title[i] == 'M' &&
					     (px == 1 || px == 4 ||
					      (px == 2 && py == 2) ||
					      (px == 3 && py == 2))) ||
					    (title[i] == 'O' &&
					     ((px == 1 || px == 4) ||
					      ((py == 1 || py == 5) && px > 1 &&
					       px < 4))) ||
					    (title[i] == 'N' &&
					     (px == 1 || px == 4 ||
					      (px == 2 && py == 2) ||
					      (px == 3 && py == 3))) ||
					    (title[i] == 'I' &&
					     ((px == 2 || px == 3) ||
					      ((py == 1 || py == 5) &&
					       px >= 1 && px <= 4))) ||
					    (title[i] == 'R' &&
					     (px == 1 || (py == 1 || py == 3) ||
					      (px == 4 && py != 3 &&
					       py != 4)))) {
						set_pixel(title_x + pos * 6 +
								  px,
							  4 + py, 0);
					}
				}
			}
		}
		pos++;
	}

	/* Draw timestamp */
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char time_str[9];
	strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
	draw_string(vinfo.xres - 50, 4, time_str);
}

static void update_display(void)
{
	/* Get current values */
	int cpu = get_cpu_usage();
	int mem = get_memory_usage();
	int disk = get_disk_usage();

	/* Update history */
	cpu_history[history_index] = cpu;
	mem_history[history_index] = mem;
	history_index = (history_index + 1) % HISTORY_SIZE;

	/* Clear display */
	memset(fb_mem, 0xFF, finfo.smem_len);

	/* Draw header */
	draw_header();

	/* Calculate layout - improved spacing */
	int graph_width = (vinfo.xres - 20) / 2; /* Two graphs side by side */
	int graph_height = 70;
	int bar_width = vinfo.xres - 16;
	int bar_height = 25;

	/* Ensure byte alignment */
	graph_width = (graph_width / 8) * 8;
	bar_width = (bar_width / 8) * 8;

	/* Draw CPU graph with icon */
	draw_enhanced_graph(8, 20, graph_width, graph_height, cpu_history,
			    HISTORY_SIZE, "CPU", cpu, icon_cpu);

	/* Draw Memory graph with icon */
	draw_enhanced_graph(8 + graph_width + 4, 20, graph_width, graph_height,
			    mem_history, HISTORY_SIZE, "MEM", mem, icon_mem);

	/* Draw current CPU bar */
	draw_enhanced_bar(8, 95, (bar_width - 4) / 2, bar_height, cpu,
			  "CPU NOW");

	/* Draw current Memory bar */
	draw_enhanced_bar(12 + (bar_width - 4) / 2, 95, (bar_width - 4) / 2,
			  bar_height, mem, "MEM NOW");

	/* Draw Disk usage bar with icon */
	draw_enhanced_bar(8, 125, bar_width, bar_height, disk, "DISK");
	draw_icon(14, 127, icon_disk);

	/* Draw system info at bottom */
	char info_str[32];
	snprintf(info_str, sizeof(info_str), "LOAD: %.2f",
		 (float)cpu_history[(history_index - 1 + HISTORY_SIZE) %
				    HISTORY_SIZE] /
			 100.0);
	draw_string(8, 155, info_str);

	/* Draw separator line */
	draw_horizontal_line(8, 165, vinfo.xres - 16, 0);

	/* Add update indicator */
	static int update_counter = 0;
	update_counter++;
	for (int i = 0; i < (update_counter % 4); i++) {
		set_pixel(vinfo.xres - 20 + i * 4, 170, 1);
	}

	/* Define partial update areas */
	struct epd_update_area areas[] = {
		{ .x = 8,
		  .y = 20,
		  .width = graph_width,
		  .height = graph_height },
		{ .x = 8 + graph_width + 4,
		  .y = 20,
		  .width = graph_width,
		  .height = graph_height },
		{ .x = 8,
		  .y = 95,
		  .width = (bar_width - 4) / 2,
		  .height = bar_height },
		{ .x = 12 + (bar_width - 4) / 2,
		  .y = 95,
		  .width = (bar_width - 4) / 2,
		  .height = bar_height },
		{ .x = 8, .y = 125, .width = bar_width, .height = bar_height },
		{ .x = 0,
		  .y = 0,
		  .width = vinfo.xres,
		  .height = 16 }, /* Header */
	};

	/* Apply byte alignment and set partial areas */
	for (int i = 0; i < 6; i++) {
		areas[i].x = (areas[i].x / 8) * 8;
		areas[i].width = (areas[i].width / 8) * 8;

		if (ioctl(fb_fd, EPD_IOC_SET_PARTIAL_AREA, &areas[i]) < 0) {
			perror("EPD_IOC_SET_PARTIAL_AREA");
		}
	}

	/* Trigger display update */
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

	/* Clear display and do initial full update */
	memset(fb_mem, 0xFF, finfo.smem_len);
	int mode = EPD_MODE_FULL;
	if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("EPD_IOC_SET_UPDATE_MODE");
	}
	if (ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
		perror("EPD_IOC_UPDATE_DISPLAY");
	}

	printf("Enhanced E-Ink System Monitor\n");
	printf("Display: %dx%d\n", vinfo.xres, vinfo.yres);
	printf("Press Ctrl+C to exit\n");

	/* Switch to partial mode for updates */
	mode = EPD_MODE_PARTIAL;
	if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
		perror("EPD_IOC_SET_UPDATE_MODE partial");
	}

	/* Main monitoring loop */
	while (keep_running) {
		update_display();
		sleep(2); /* Update every 2 seconds */
	}

	/* Clean up */
	printf("\nCleaning up...\n");

	/* Clear display on exit */
	printf("Clearing display...\n");
	if (ioctl(fb_fd, EPD_IOC_CLEAR_DISPLAY) < 0) {
		perror("EPD_IOC_CLEAR_DISPLAY");
	}

	close_framebuffer();

	return 0;
}