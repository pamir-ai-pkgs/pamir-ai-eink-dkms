#!/usr/bin/env python3
"""
eink_demo.py - Python demonstration for Pamir AI E-Ink display driver

Copyright (C) 2025 Pamir AI
License: GPL v2

Requirements:
  pip install pillow
"""

import mmap
import fcntl
import struct
import time
from PIL import Image, ImageDraw

# IOCTL constants
EPD_IOC_MAGIC = ord("E")


# IOCTL command construction
def _IOW(type, nr, size):
    return (1 << 30) | (type << 8) | nr | (size << 16)


def _IOR(type, nr, size):
    return (2 << 30) | (type << 8) | nr | (size << 16)


def _IO(type, nr):
    return (0 << 30) | (type << 8) | nr


# IOCTL commands
EPD_IOC_SET_UPDATE_MODE = _IOW(EPD_IOC_MAGIC, 1, 4)  # int size = 4
EPD_IOC_GET_UPDATE_MODE = _IOR(EPD_IOC_MAGIC, 2, 4)
EPD_IOC_SET_PARTIAL_AREA = _IOW(EPD_IOC_MAGIC, 3, 8)  # 4 * u16 = 8
EPD_IOC_UPDATE_DISPLAY = _IO(EPD_IOC_MAGIC, 4)
EPD_IOC_DEEP_SLEEP = _IO(EPD_IOC_MAGIC, 5)
EPD_IOC_SET_BASE_MAP = _IOW(EPD_IOC_MAGIC, 6, 8)  # pointer = 8

# Update modes
EPD_MODE_FULL = 0
EPD_MODE_PARTIAL = 1
EPD_MODE_BASE_MAP = 2


class EInkDisplay:
    """Python interface to Pamir AI E-Ink display."""

    def __init__(self, fb_device="/dev/fb0"):
        """Initialize the e-ink display interface.

        Args:
            fb_device: Path to framebuffer device
        """
        self.fb_device = fb_device
        self.fb_file = None
        self.fb_mmap = None
        self.width = 250  # Default, will be updated
        self.height = 122  # Default, will be updated
        self.bytes_per_line = 0

        self._open_framebuffer()

    def _open_framebuffer(self):
        """Open and memory-map the framebuffer device."""
        # Open framebuffer
        self.fb_file = open(self.fb_device, "r+b")

        # Get framebuffer info (simplified - assumes known dimensions)
        # In a real implementation, you would use FBIOGET_VSCREENINFO
        # and FBIOGET_FSCREENINFO ioctls to get actual dimensions
        self.width = 250
        self.height = 122
        self.bytes_per_line = (self.width + 7) // 8

        # Calculate framebuffer size
        fb_size = self.bytes_per_line * self.height

        # Memory map the framebuffer
        self.fb_mmap = mmap.mmap(
            self.fb_file.fileno(), fb_size, mmap.MAP_SHARED, mmap.PROT_WRITE
        )

    def set_update_mode(self, mode):
        """Set the display update mode.

        Args:
            mode: EPD_MODE_FULL, EPD_MODE_PARTIAL, or EPD_MODE_BASE_MAP
        """
        mode_bytes = struct.pack("i", mode)
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_UPDATE_MODE, mode_bytes)

    def set_partial_area(self, x, y, width, height):
        """Set the partial update area.

        Args:
            x: X coordinate (must be multiple of 8)
            y: Y coordinate
            width: Width (must be multiple of 8)
            height: Height
        """
        if x % 8 != 0 or width % 8 != 0:
            raise ValueError("X and width must be multiples of 8")

        area_bytes = struct.pack("HHHH", x, y, width, height)
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_PARTIAL_AREA, area_bytes)

    def update_display(self):
        """Trigger a display update."""
        fcntl.ioctl(self.fb_file, EPD_IOC_UPDATE_DISPLAY)

    def deep_sleep(self):
        """Enter deep sleep mode."""
        fcntl.ioctl(self.fb_file, EPD_IOC_DEEP_SLEEP)

    def draw_image(self, image, x=0, y=0):
        """Draw a PIL Image to the display.

        Args:
            image: PIL Image object (will be converted to 1-bit)
            x: X position
            y: Y position
        """
        # Convert image to 1-bit
        if image.mode != "1":
            image = image.convert("1")

        # Get pixels
        pixels = image.load()
        width, height = image.size

        # Draw to framebuffer
        for py in range(min(height, self.height - y)):
            for px in range(min(width, self.width - x)):
                # Calculate position in framebuffer
                fb_x = x + px
                fb_y = y + py
                byte_idx = fb_y * self.bytes_per_line + (fb_x // 8)
                bit_idx = 7 - (fb_x % 8)

                # Read current byte
                self.fb_mmap.seek(byte_idx)
                current = ord(self.fb_mmap.read(1))

                # Update bit
                if pixels[px, py] == 0:  # Black pixel
                    current &= ~(1 << bit_idx)
                else:  # White pixel
                    current |= 1 << bit_idx

                # Write back
                self.fb_mmap.seek(byte_idx)
                self.fb_mmap.write(bytes([current]))

    def clear(self, color=255):
        """Clear the display.

        Args:
            color: 0 for black, 255 for white
        """
        fill_byte = 0xFF if color else 0x00
        self.fb_mmap.seek(0)
        self.fb_mmap.write(bytes([fill_byte]) * len(self.fb_mmap))

    def close(self):
        """Close the framebuffer device."""
        if self.fb_mmap:
            self.fb_mmap.close()
        if self.fb_file:
            self.fb_file.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


def demo_full_update():
    """Demonstrate full update mode."""
    print("=== Full Update Demo ===")

    with EInkDisplay() as display:
        # Set full update mode
        display.set_update_mode(EPD_MODE_FULL)

        # Clear display
        display.clear(255)  # White

        # Create an image
        img = Image.new("1", (display.width, display.height), 1)
        draw = ImageDraw.Draw(img)

        # Draw border
        draw.rectangle((0, 0, display.width - 1, display.height - 1), outline=0)

        # Draw text
        draw.text((10, 10), "Pamir AI E-Ink", fill=0)
        draw.text((10, 30), "Full Update Mode", fill=0)

        # Draw shapes
        draw.rectangle((10, 50, 60, 80), fill=0)
        draw.ellipse((70, 50, 120, 80), outline=0)

        # Send to display
        display.draw_image(img)
        display.update_display()

        print("Full update complete")
        time.sleep(3)


def demo_partial_update():
    """Demonstrate partial update mode."""
    print("\n=== Partial Update Demo ===")

    with EInkDisplay() as display:
        # First do a full update to set the base
        display.set_update_mode(EPD_MODE_FULL)
        display.clear(255)

        # Create base image
        img = Image.new("1", (display.width, display.height), 1)
        draw = ImageDraw.Draw(img)
        draw.text((10, 10), "Partial Update Demo", fill=0)
        draw.rectangle((0, 0, display.width - 1, display.height - 1), outline=0)

        display.draw_image(img)
        display.update_display()
        time.sleep(2)

        # Switch to partial mode
        display.set_update_mode(EPD_MODE_PARTIAL)

        # Update counter in a small area
        for i in range(5):
            # Set partial area (must be byte-aligned)
            display.set_partial_area(80, 50, 80, 32)

            # Create small image for counter
            counter_img = Image.new("1", (80, 32), 1)
            counter_draw = ImageDraw.Draw(counter_img)
            counter_draw.rectangle((0, 0, 79, 31), outline=0)
            counter_draw.text((10, 8), f"Count: {i}", fill=0)

            # Draw to display
            display.draw_image(counter_img, 80, 50)
            display.update_display()

            print(f"Partial update {i}")
            time.sleep(1)


def demo_patterns():
    """Demonstrate various patterns."""
    print("\n=== Pattern Demo ===")

    with EInkDisplay() as display:
        display.set_update_mode(EPD_MODE_FULL)

        # Create pattern image
        img = Image.new("1", (display.width, display.height), 1)
        draw = ImageDraw.Draw(img)

        # Checkerboard pattern
        square_size = 10
        for y in range(0, display.height, square_size):
            for x in range(0, display.width, square_size):
                if (x // square_size + y // square_size) % 2 == 0:
                    draw.rectangle(
                        (x, y, x + square_size - 1, y + square_size - 1), fill=0
                    )

        display.draw_image(img)
        display.update_display()

        print("Pattern display complete")
        time.sleep(3)

        # Enter deep sleep
        print("Entering deep sleep...")
        display.deep_sleep()


def main():
    """Main demo function."""
    print("Pamir AI E-Ink Display Python Demo")
    print("===================================\n")

    try:
        demo_full_update()
        demo_partial_update()
        demo_patterns()

        print("\nDemo complete!")

    except FileNotFoundError:
        print("Error: Framebuffer device not found.")
        print("Make sure the driver is loaded: sudo modprobe pamir-ai-eink")
    except PermissionError:
        print("Error: Permission denied.")
        print("Try running with sudo or add user to video group:")
        print("  sudo usermod -a -G video $USER")
    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    main()
