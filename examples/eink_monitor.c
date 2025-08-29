/*
 * eink_monitor.c - System Resource Monitor for Pamir AI E-Ink Driver
 *
 * This example demonstrates multiple partial update regions by displaying
 * system statistics like CPU usage, memory, and disk space with graphs
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
#include <errno.h>
#include "pamir-ai-eink.h"

static volatile int keep_running = 1;
static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;

/* History buffers for graphs */
#define HISTORY_SIZE 50
static int cpu_history[HISTORY_SIZE] = {0};
static int mem_history[HISTORY_SIZE] = {0};
static int history_index = 0;

static void signal_handler(int sig) {
    keep_running = 0;
}

static int open_framebuffer(const char *device) {
    fb_fd = open(device, O_RDWR);
    if (fb_fd < 0) {
        perror("Failed to open framebuffer device");
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("Failed to get variable screen info");
        close(fb_fd);
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("Failed to get fixed screen info");
        close(fb_fd);
        return -1;
    }
    
    size_t fb_size = finfo.smem_len;
    fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("Failed to map framebuffer memory");
        close(fb_fd);
        return -1;
    }
    
    printf("Framebuffer: %dx%d, %d bpp\n",
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    
    return 0;
}

static void close_framebuffer(void) {
    if (fb_mem && fb_mem != MAP_FAILED) {
        munmap(fb_mem, finfo.smem_len);
    }
    if (fb_fd >= 0) {
        close(fb_fd);
    }
}

static void set_pixel(int x, int y, int value) {
    if (x < 0 || x >= vinfo.xres || y < 0 || y >= vinfo.yres) return;
    
    int byte_offset = (y * vinfo.xres + x) / 8;
    int bit_offset = 7 - ((y * vinfo.xres + x) % 8);
    
    if (value) {
        fb_mem[byte_offset] &= ~(1 << bit_offset);  /* Black */
    } else {
        fb_mem[byte_offset] |= (1 << bit_offset);   /* White */
    }
}

static void draw_rect(int x, int y, int width, int height, int filled) {
    if (filled) {
        for (int row = y; row < y + height; row++) {
            for (int col = x; col < x + width; col++) {
                set_pixel(col, row, 1);
            }
        }
    } else {
        /* Top and bottom */
        for (int col = x; col < x + width; col++) {
            set_pixel(col, y, 1);
            set_pixel(col, y + height - 1, 1);
        }
        /* Left and right */
        for (int row = y; row < y + height; row++) {
            set_pixel(x, row, 1);
            set_pixel(x + width - 1, row, 1);
        }
    }
}

static void clear_area(int x, int y, int width, int height) {
    for (int row = y; row < y + height && row < vinfo.yres; row++) {
        for (int col = x; col < x + width && col < vinfo.xres; col++) {
            set_pixel(col, row, 0);  /* White */
        }
    }
}

static void draw_text(int x, int y, const char *text) {
    /* Simple 5x7 font for basic text */
    static const uint8_t font_5x7[][7] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* Space */
        /* Add more characters as needed - simplified for demo */
    };
    
    /* For simplicity, just draw placeholder rectangles for text */
    int len = strlen(text);
    draw_rect(x, y, len * 6, 8, 0);
}

static int get_cpu_usage(void) {
    static long prev_idle = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;
    
    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);
    
    long user, nice, system, idle, iowait, irq, softirq;
    sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    
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

static int get_memory_usage(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;
    
    char buffer[256];
    long total = 0, available = 0;
    
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (sscanf(buffer, "MemTotal: %ld", &total) == 1) continue;
        if (sscanf(buffer, "MemAvailable: %ld", &available) == 1) break;
    }
    fclose(fp);
    
    if (total > 0) {
        return 100 * (total - available) / total;
    }
    return 0;
}

static int get_disk_usage(void) {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) return 0;
    
    unsigned long total = stat.f_blocks * stat.f_frsize;
    unsigned long available = stat.f_bavail * stat.f_frsize;
    
    if (total > 0) {
        return 100 * (total - available) / total;
    }
    return 0;
}

static void draw_graph(int x, int y, int width, int height, 
                      int *data, int data_size, const char *label) {
    /* Clear graph area */
    clear_area(x, y, width, height);
    
    /* Draw border */
    draw_rect(x, y, width, height, 0);
    
    /* Draw label */
    draw_text(x + 2, y + 2, label);
    
    /* Draw graph data */
    int graph_y = y + 15;
    int graph_height = height - 20;
    
    for (int i = 0; i < data_size && i < width - 4; i++) {
        int value = data[i];
        int bar_height = (value * graph_height) / 100;
        
        /* Draw vertical line for this data point */
        for (int j = 0; j < bar_height; j++) {
            set_pixel(x + 2 + i, graph_y + graph_height - j - 1, 1);
        }
    }
}

static void draw_bar(int x, int y, int width, int height, 
                    int value, const char *label) {
    /* Clear bar area */
    clear_area(x, y, width, height);
    
    /* Draw border */
    draw_rect(x, y, width, height, 0);
    
    /* Draw label */
    draw_text(x + 2, y + 2, label);
    
    /* Draw percentage bar */
    int bar_width = ((width - 4) * value) / 100;
    draw_rect(x + 2, y + 12, bar_width, 8, 1);
}

static void update_display(void) {
    /* Get current system stats */
    int cpu = get_cpu_usage();
    int mem = get_memory_usage();
    int disk = get_disk_usage();
    
    /* Update history */
    cpu_history[history_index] = cpu;
    mem_history[history_index] = mem;
    history_index = (history_index + 1) % HISTORY_SIZE;
    
    /* Layout calculation */
    int graph_width = (vinfo.xres - 16) / 2;
    int graph_height = 60;
    int bar_height = 25;
    
    /* Ensure byte-aligned regions */
    graph_width = (graph_width / 8) * 8;
    
    /* Draw CPU graph - top left */
    draw_graph(8, 8, graph_width, graph_height, 
              cpu_history, HISTORY_SIZE, "CPU");
    
    /* Draw Memory graph - top right */
    draw_graph(8 + graph_width + 8, 8, graph_width, graph_height, 
              mem_history, HISTORY_SIZE, "Memory");
    
    /* Draw current values as bars */
    draw_bar(8, 80, graph_width, bar_height, cpu, "CPU Now");
    draw_bar(8 + graph_width + 8, 80, graph_width, bar_height, mem, "Mem Now");
    draw_bar(8, 110, vinfo.xres - 16, bar_height, disk, "Disk Usage");
    
    /* Update using partial refresh for each region */
    struct epd_update_area areas[] = {
        { .x = 8, .y = 8, .width = graph_width, .height = graph_height },
        { .x = 8 + graph_width + 8, .y = 8, .width = graph_width, .height = graph_height },
        { .x = 8, .y = 80, .width = graph_width, .height = bar_height },
        { .x = 8 + graph_width + 8, .y = 80, .width = graph_width, .height = bar_height },
        { .x = 8, .y = 110, .width = (vinfo.xres - 16) / 8 * 8, .height = bar_height }
    };
    
    for (int i = 0; i < 5; i++) {
        /* Ensure byte alignment */
        areas[i].x = (areas[i].x / 8) * 8;
        areas[i].width = (areas[i].width / 8) * 8;
        
        if (ioctl(fb_fd, EPD_IOC_SET_PARTIAL_AREA, &areas[i]) < 0) {
            fprintf(stderr, "Failed to set partial area %d: %s\n", 
                   i, strerror(errno));
        }
    }
    
    /* Trigger display update */
    if (ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
        perror("Failed to update display");
    }
}

int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    
    if (argc > 1) {
        fb_device = argv[1];
    }
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Open framebuffer */
    if (open_framebuffer(fb_device) < 0) {
        return 1;
    }
    
    /* Initial full refresh to clear display */
    memset(fb_mem, 0xFF, finfo.smem_len);
    int mode = EPD_MODE_FULL;
    if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
        perror("Failed to set update mode");
    }
    /* Trigger initial display update */
    if (ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY) < 0) {
        perror("Failed to update display");
    }
    
    printf("E-Ink System Monitor\n");
    printf("Display: %dx%d\n", vinfo.xres, vinfo.yres);
    printf("Press Ctrl+C to exit\n");
    
    /* Switch to partial update mode */
    mode = EPD_MODE_PARTIAL;
    if (ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
        perror("Failed to set partial update mode");
    }
    
    /* Main loop - update every 2 seconds */
    while (keep_running) {
        update_display();
        sleep(2);
    }
    
    /* Final cleanup */
    printf("\nCleaning up...\n");
    mode = EPD_MODE_FULL;
    ioctl(fb_fd, EPD_IOC_SET_UPDATE_MODE, &mode);
    memset(fb_mem, 0xFF, finfo.smem_len);
    /* Trigger final display update */
    ioctl(fb_fd, EPD_IOC_UPDATE_DISPLAY);
    
    close_framebuffer();
    
    return 0;
}