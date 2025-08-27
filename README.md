# Pamir AI E-Ink Display Driver

A Linux kernel driver for the Pamir AI E-Ink display, providing high-performance monochrome display support with advanced update modes and comprehensive userspace interfaces.

## Overview

The `pamir-ai-eink` driver is a fully-featured framebuffer driver for e-ink displays based on the SSD1681 controller. It provides seamless integration with the Linux framebuffer subsystem while offering specialized e-ink functionality including partial updates, deep sleep mode, and multiple refresh strategies.

### Key Features

- **Full Linux framebuffer integration** - Works with standard framebuffer applications
- **Multiple update modes** - Full refresh, partial updates, and base map mode
- **Hardware acceleration** - Direct SPI communication with optimized data transfer
- **Power management** - Deep sleep mode for ultra-low power consumption
- **Flexible interfaces** - IOCTL, sysfs, and direct framebuffer access
- **Modular architecture** - Clean separation of hardware, display, and interface layers
- **Production ready** - Comprehensive error handling and kernel best practices

## Hardware Specifications

### Display Controller
- **Controller**: SSD1681 or compatible
- **Interface**: SPI (up to 40MHz)
- **Resolution**: Configurable via device tree (tested up to 250x122)
- **Color Depth**: Monochrome (1-bit per pixel)
- **Refresh Time**: ~2-4 seconds (full), ~500ms (partial)

### Hardware Requirements
- **SPI Bus**: Standard 4-wire SPI interface
- **GPIO Pins**:
  - Reset (optional but recommended)
  - Data/Command (required)
  - Busy (optional but recommended)
- **Power**: 3.3V logic level
- **Temperature Range**: -20°C to 70°C operating

## Installation

### Prerequisites

```bash
# Install kernel headers and build tools
sudo apt-get update
sudo apt-get install linux-headers-$(uname -r) build-essential

# For device tree overlay compilation (optional)
sudo apt-get install device-tree-compiler
```

### Building the Driver

```bash
# Clone the repository
git clone https://github.com/pamir-ai/pamir-ai-eink-dkms
cd pamir-ai-eink-dkms

# Build the kernel module
make

# Install the module (optional)
sudo make install
sudo modprobe pamir-ai-eink
```

### DKMS Installation (Recommended)

```bash
# Install DKMS
sudo apt-get install dkms

# Add to DKMS
sudo dkms add .
sudo dkms build pamir-ai-eink/1.0
sudo dkms install pamir-ai-eink/1.0

# Load the module
sudo modprobe pamir-ai-eink
```

## Device Tree Configuration

### Basic Configuration

```dts
&spi0 {
    status = "okay";

    eink@0 {
        compatible = "pamir-ai,eink-display";
        reg = <0>;
        spi-max-frequency = <40000000>;

        /* Display dimensions in pixels */
        width = <250>;
        height = <122>;

        /* GPIO pins */
        reset-gpios = <&gpio0 23 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpio0 24 GPIO_ACTIVE_LOW>;
        busy-gpios = <&gpio0 25 GPIO_ACTIVE_LOW>;
    };
};
```

### Device Tree Overlay Example

Create a file `pamir-ai-eink-overlay.dts`:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2835";

    fragment@0 {
        target = <&spi0>;
        __overlay__ {
            status = "okay";

            eink@0 {
                compatible = "pamir-ai,eink-display";
                reg = <0>;
                spi-max-frequency = <20000000>;

                width = <250>;
                height = <122>;

                reset-gpios = <&gpio 23 0>;
                dc-gpios = <&gpio 24 0>;
                busy-gpios = <&gpio 25 0>;
            };
        };
    };
};
```

Compile and load:

```bash
# Compile the overlay
dtc -@ -I dts -O dtb -o pamir-ai-eink.dtbo pamir-ai-eink-overlay.dts

# Copy to overlays directory (Raspberry Pi example)
sudo cp pamir-ai-eink.dtbo /boot/overlays/

# Enable in config.txt
echo "dtoverlay=pamir-ai-eink" | sudo tee -a /boot/config.txt

# Reboot to apply
sudo reboot
```

## Userspace Usage Examples

### Basic Framebuffer Operations (C)

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include "pamir-ai-eink.h"

int main() {
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Error opening framebuffer");
        return 1;
    }

    /* Get framebuffer info */
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);

    size_t screensize = vinfo.yres_virtual * finfo.line_length;

    /* Memory map the framebuffer */
    char *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fbfd, 0);

    /* Clear screen (white) */
    memset(fbp, 0xFF, screensize);

    /* Draw a black rectangle (example) */
    for (int y = 10; y < 50; y++) {
        for (int x = 10; x < 90; x++) {
            int byte_offset = y * finfo.line_length + (x / 8);
            int bit_offset = 7 - (x % 8);
            fbp[byte_offset] &= ~(1 << bit_offset);
        }
    }

    /* Trigger display update (automatic on write) */

    munmap(fbp, screensize);
    close(fbfd);
    return 0;
}
```

### Using Update Modes (C)

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "pamir-ai-eink.h"

int main() {
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Error opening framebuffer");
        return 1;
    }

    /* Set full update mode for best quality */
    int mode = EPD_MODE_FULL;
    ioctl(fbfd, EPD_IOC_SET_UPDATE_MODE, &mode);

    /* ... draw something ... */

    /* Switch to partial mode for faster updates */
    mode = EPD_MODE_PARTIAL;
    ioctl(fbfd, EPD_IOC_SET_UPDATE_MODE, &mode);

    /* Define partial update area */
    struct epd_update_area area = {
        .x = 0,       /* Must be multiple of 8 */
        .y = 0,
        .width = 128, /* Must be multiple of 8 */
        .height = 64
    };
    ioctl(fbfd, EPD_IOC_SET_PARTIAL_AREA, &area);

    /* ... update partial area ... */

    /* Manual update trigger */
    ioctl(fbfd, EPD_IOC_UPDATE_DISPLAY);

    /* Enter deep sleep to save power */
    ioctl(fbfd, EPD_IOC_DEEP_SLEEP);

    close(fbfd);
    return 0;
}
```

### Python Example using Pillow

```python
#!/usr/bin/env python3
from PIL import Image, ImageDraw, ImageFont
import mmap
import fcntl
import os
import struct

# IOCTL constants
EPD_IOC_MAGIC = ord('E')
EPD_IOC_SET_UPDATE_MODE = (1 << 30) | (EPD_IOC_MAGIC << 8) | 1 | (4 << 16)
EPD_IOC_UPDATE_DISPLAY = (0 << 30) | (EPD_IOC_MAGIC << 8) | 4

# Update modes
EPD_MODE_FULL = 0
EPD_MODE_PARTIAL = 1

def draw_to_eink():
    # Open framebuffer
    with open('/dev/fb0', 'r+b') as fb:
        # Get framebuffer info (simplified, assumes 250x122)
        width, height = 250, 122

        # Create image
        img = Image.new('1', (width, height), 1)  # 1-bit, white background
        draw = ImageDraw.Draw(img)

        # Draw something
        draw.rectangle((10, 10, 100, 50), fill=0)  # Black rectangle
        draw.text((10, 60), "Hello E-Ink!", fill=0)

        # Convert to framebuffer format
        pixels = img.load()
        fb_size = (width // 8) * height
        fb_data = bytearray(fb_size)

        for y in range(height):
            for x in range(width):
                byte_idx = y * (width // 8) + (x // 8)
                bit_idx = 7 - (x % 8)
                if pixels[x, y] == 0:  # Black pixel
                    fb_data[byte_idx] &= ~(1 << bit_idx)
                else:  # White pixel
                    fb_data[byte_idx] |= (1 << bit_idx)

        # Write to framebuffer
        fb.write(fb_data)

        # Trigger update
        fcntl.ioctl(fb, EPD_IOC_UPDATE_DISPLAY)

if __name__ == "__main__":
    draw_to_eink()
```

## Update Modes Explained

### Full Update (EPD_MODE_FULL)
- **Purpose**: Complete screen refresh with best image quality
- **Characteristics**:
  - Clears all ghosting and artifacts
  - Takes 2-4 seconds
  - Visible flashing during update
  - Best contrast and clarity
- **Use Cases**: Initial display, periodic refresh, after many partial updates

### Partial Update (EPD_MODE_PARTIAL)
- **Purpose**: Fast refresh of specific screen regions
- **Characteristics**:
  - Update time ~500ms
  - May cause slight ghosting
  - No full-screen flash
  - Region must be byte-aligned (x and width multiple of 8)
- **Use Cases**: Interactive UI, status updates, animations

### Base Map Mode (EPD_MODE_BASE_MAP)
- **Purpose**: Dual-buffer technique for optimized partial updates
- **Characteristics**:
  - Sets both internal RAM buffers
  - Provides reference image for differential updates
  - Reduces ghosting in subsequent partial updates
- **Use Cases**: Setting background image, template layouts

## Sysfs Interface Documentation

The driver exposes several sysfs attributes for runtime configuration:

### `/sys/bus/spi/devices/spiX.Y/update_mode`
- **Read**: Get current update mode (`full`, `partial`, `base_map`)
- **Write**: Set update mode
```bash
# Set to partial update mode
echo "partial" > /sys/bus/spi/devices/spi0.0/update_mode

# Read current mode
cat /sys/bus/spi/devices/spi0.0/update_mode
```

### `/sys/bus/spi/devices/spiX.Y/partial_area`
- **Read**: Get current partial update area or "not set"
- **Write**: Set partial area as "x,y,width,height"
```bash
# Set partial area (x=0, y=0, width=128, height=64)
echo "0,0,128,64" > /sys/bus/spi/devices/spi0.0/partial_area

# Read current area
cat /sys/bus/spi/devices/spi0.0/partial_area
```

### `/sys/bus/spi/devices/spiX.Y/trigger_update`
- **Write only**: Trigger display update
```bash
# Trigger update
echo "1" > /sys/bus/spi/devices/spi0.0/trigger_update
```

### `/sys/bus/spi/devices/spiX.Y/deep_sleep`
- **Write only**: Enter deep sleep mode
```bash
# Enter deep sleep
echo "1" > /sys/bus/spi/devices/spi0.0/deep_sleep
```

## IOCTL Interface Documentation

### Update Mode Control
```c
/* Set update mode */
int mode = EPD_MODE_PARTIAL;
ioctl(fd, EPD_IOC_SET_UPDATE_MODE, &mode);

/* Get current update mode */
int current_mode;
ioctl(fd, EPD_IOC_GET_UPDATE_MODE, &current_mode);
```

### Partial Update Area
```c
/* Set partial update area */
struct epd_update_area area = {
    .x = 0,      /* Must be multiple of 8 */
    .y = 0,
    .width = 128, /* Must be multiple of 8 */
    .height = 64
};
ioctl(fd, EPD_IOC_SET_PARTIAL_AREA, &area);
```

### Display Control
```c
/* Trigger display update */
ioctl(fd, EPD_IOC_UPDATE_DISPLAY);

/* Enter deep sleep mode */
ioctl(fd, EPD_IOC_DEEP_SLEEP);

/* Set base map (dual-buffer mode) */
ioctl(fd, EPD_IOC_SET_BASE_MAP, NULL);
```

## Performance Considerations

### Update Speed Optimization
1. **Use partial updates** for interactive elements
2. **Batch updates** when possible to reduce overhead
3. **Align updates** to byte boundaries (x and width as multiples of 8)
4. **Minimize update area** to only changed regions

### Memory Usage
- Framebuffer size: `(width / 8) * height` bytes
- Example: 250x122 display uses ~3.8KB
- Double buffering in base map mode doubles RAM usage

### Power Management
1. **Use deep sleep** when display is idle for extended periods
2. **Reduce update frequency** to save power
3. **Consider ambient temperature** - updates are slower in cold conditions

### SPI Performance
- Maximum SPI clock: 20MHz (controller limitation)
- Actual throughput: ~2.5MB/s theoretical maximum
- Full screen update data transfer: ~50ms for 250x122 display

## Troubleshooting

### Display Not Detected
```bash
# Check if module is loaded
lsmod | grep pamir_ai_eink

# Check kernel messages
dmesg | grep -i eink

# Verify SPI device
ls -la /dev/spidev*

# Check device tree
dtc -I fs -O dts /proc/device-tree | grep -A5 eink
```

### Display Not Updating
1. **Check busy signal**: Ensure busy GPIO is properly connected
2. **Verify power**: Confirm 3.3V supply is stable
3. **Check SPI connection**: Use oscilloscope to verify SPI signals
4. **Test with full update**: Partial updates may fail if alignment is wrong

### Ghosting Issues
1. **Perform full refresh** periodically (every 10-20 partial updates)
2. **Use base map mode** for better partial update quality
3. **Adjust refresh rate** - slower updates reduce ghosting
4. **Check temperature** - ghosting is worse at temperature extremes

### Common Error Messages

```bash
# "Failed to send command": SPI communication error
# Solution: Check SPI wiring and chip select

# "Busy timeout": Display controller not responding
# Solution: Check busy GPIO and power supply

# "X coordinates must be byte-aligned": Invalid partial area
# Solution: Ensure x and width are multiples of 8

# "Update area exceeds display bounds": Invalid dimensions
# Solution: Check area coordinates against display size
```

### Debug Mode
Enable debug messages:
```bash
# Enable dynamic debug
echo 'module pamir_ai_eink +p' > /sys/kernel/debug/dynamic_debug/control

# View debug messages
dmesg -w | grep pamir
```

## Contributing

We welcome contributions! Please ensure:
1. Follow Linux kernel coding style
2. Test on actual hardware
3. Document any new features
4. Include relevant device tree examples

## License

This driver is licensed under GPL v2. See the COPYING file for details.

## Support

For issues and questions:
- GitHub Issues: https://github.com/pamir-ai/pamir-ai-eink-dkms/issues
- Email: support@pamir.ai

## Authors

Pamir AI <support@pamir.ai>

## Acknowledgments

- Linux kernel framebuffer subsystem maintainers
- SSD1681 controller documentation contributors
- E-ink display technology pioneers
