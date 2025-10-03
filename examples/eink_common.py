#!/usr/bin/env python3
"""
eink_common.py - Common module for Pamir AI E-Ink display driver Python examples

Copyright (C) 2025 Pamir AI
License: GPL v2

This module contains:
1. All IOCTL definitions extracted from pamir-ai-eink.h
2. Common framebuffer helper functions
3. Display dimensions constants
4. EInkDisplay class with all common functionality
"""

import mmap
import fcntl
import struct
from PIL import Image

# Display dimensions constants
EINK_DEFAULT_WIDTH = 128
EINK_DEFAULT_HEIGHT = 250

# IOCTL constants from pamir-ai-eink.h
EPD_IOC_MAGIC = ord("E")

# Framebuffer IOCTL constants for getting screen info
FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602


# IOCTL command construction helpers
def _IOW(type, nr, size):
    """Construct IOW ioctl command number."""
    return (1 << 30) | (type << 8) | nr | (size << 16)


def _IOR(type, nr, size):
    """Construct IOR ioctl command number."""
    return (2 << 30) | (type << 8) | nr | (size << 16)


def _IO(type, nr):
    """Construct IO ioctl command number."""
    return (0 << 30) | (type << 8) | nr


# IOCTL commands from pamir-ai-eink.h
EPD_IOC_SET_UPDATE_MODE = _IOW(EPD_IOC_MAGIC, 1, 4)  # _IOW('E', 1, int)
EPD_IOC_GET_UPDATE_MODE = _IOR(EPD_IOC_MAGIC, 2, 4)  # _IOR('E', 2, int)
EPD_IOC_SET_PARTIAL_AREA = _IOW(
    EPD_IOC_MAGIC, 3, 8
)  # _IOW('E', 3, struct epd_update_area)
EPD_IOC_UPDATE_DISPLAY = _IO(EPD_IOC_MAGIC, 4)  # _IO('E', 4)
EPD_IOC_DEEP_SLEEP = _IO(EPD_IOC_MAGIC, 5)  # _IO('E', 5)
EPD_IOC_SET_BASE_MAP = _IOW(EPD_IOC_MAGIC, 6, 8)  # _IOW('E', 6, void *)
EPD_IOC_RESET = _IO(EPD_IOC_MAGIC, 7)  # _IO('E', 7)
EPD_IOC_CLEAR_DISPLAY = _IO(EPD_IOC_MAGIC, 8)  # _IO('E', 8)

# Display update modes from pamir-ai-eink.h
EPD_MODE_FULL = 0  # Full refresh, 2-4 seconds
EPD_MODE_PARTIAL = 1  # Fast partial, ~500ms
EPD_MODE_BASE_MAP = 2  # Dual-buffer mode


class EInkDisplay:
    """Python interface to Pamir AI E-Ink display.

    This class provides a high-level interface to the e-ink display driver,
    handling framebuffer operations, IOCTL commands, and display updates.
    """

    def __init__(self, fb_device="/dev/fb0"):
        """Initialize the e-ink display interface.

        Args:
            fb_device: Path to framebuffer device (default: /dev/fb0)
        """
        self.fb_device = fb_device
        self.fb_file = None
        self.fb_mmap = None
        self.width = EINK_DEFAULT_WIDTH
        self.height = EINK_DEFAULT_HEIGHT
        self.bytes_per_line = 0

        self._open_framebuffer()

    def _open_framebuffer(self):
        """Open and memory-map the framebuffer device."""
        # Open framebuffer
        self.fb_file = open(self.fb_device, "r+b")

        # Get framebuffer variable screen info to read dimensions
        try:
            vinfo = fcntl.ioctl(self.fb_file, FBIOGET_VSCREENINFO, b"\x00" * 160)
            # Parse xres and yres from the structure (first two 32-bit values)
            self.width, self.height = struct.unpack("II", vinfo[:8])
        except (OSError, struct.error):
            pass  # Use default dimensions

        # Fallback to defaults if we get invalid values
        if self.width == 0 or self.height == 0:
            print("Warning: Could not read display dimensions, using defaults")
            self.width = EINK_DEFAULT_WIDTH
            self.height = EINK_DEFAULT_HEIGHT

        print(f"Display dimensions: {self.width}x{self.height}")
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

    def get_update_mode(self):
        """Get the current display update mode.

        Returns:
            int: Current update mode (EPD_MODE_FULL, EPD_MODE_PARTIAL, or EPD_MODE_BASE_MAP)
        """
        result = fcntl.ioctl(self.fb_file, EPD_IOC_GET_UPDATE_MODE, struct.pack("i", 0))
        return struct.unpack("i", result)[0]

    def set_partial_area(self, x, y, width, height):
        """Set the partial update area.

        Args:
            x: X coordinate (must be multiple of 8)
            y: Y coordinate
            width: Width (must be multiple of 8)
            height: Height

        Raises:
            ValueError: If x or width are not multiples of 8
        """
        if x % 8 != 0 or width % 8 != 0:
            raise ValueError("X and width must be multiples of 8")

        # Ensure coordinates are within bounds
        x = max(0, min(x, self.width - 8))
        y = max(0, min(y, self.height - 1))
        width = max(8, min(width, self.width - x))
        height = max(1, min(height, self.height - y))

        area_bytes = struct.pack("HHHH", x, y, width, height)
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_PARTIAL_AREA, area_bytes)

    def update_display(self):
        """Trigger a display update."""
        fcntl.ioctl(self.fb_file, EPD_IOC_UPDATE_DISPLAY)

    def deep_sleep(self):
        """Enter deep sleep mode."""
        fcntl.ioctl(self.fb_file, EPD_IOC_DEEP_SLEEP)

    def set_base_map(self):
        """Set the current framebuffer content as the base map for base map mode."""
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_BASE_MAP, 0)

    def clear_display(self):
        """Clear both RAM buffers in the display.

        This ensures no ghost images remain and is useful
        when switching between different display modes or examples.
        """
        try:
            fcntl.ioctl(self.fb_file, EPD_IOC_CLEAR_DISPLAY)
        except OSError as e:
            print(f"Failed to clear display: {e}")

    def reset_display(self):
        """Reset the display hardware.

        Use this to recover from stuck display states.
        """
        try:
            fcntl.ioctl(self.fb_file, EPD_IOC_RESET)
            print("Display reset successful")
            # After reset, default to full update mode
            self.set_update_mode(EPD_MODE_FULL)
        except IOError as e:
            print(f"Display reset failed: {e}")

    def draw_image(self, image, x=0, y=0):
        """Draw a PIL Image to the display.

        Args:
            image: PIL Image object (will be converted to 1-bit)
            x: X position on display
            y: Y position on display
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

    def draw_pixel(self, x, y, color=0):
        """Draw a single pixel.

        Args:
            x: X coordinate
            y: Y coordinate
            color: 0 for black, 1 for white
        """
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            return

        byte_idx = y * self.bytes_per_line + (x // 8)
        bit_idx = 7 - (x % 8)

        # Read current byte
        self.fb_mmap.seek(byte_idx)
        current = ord(self.fb_mmap.read(1))

        # Update bit
        if color == 0:  # Black pixel
            current &= ~(1 << bit_idx)
        else:  # White pixel
            current |= 1 << bit_idx

        # Write back
        self.fb_mmap.seek(byte_idx)
        self.fb_mmap.write(bytes([current]))

    def get_pixel(self, x, y):
        """Get the color of a pixel.

        Args:
            x: X coordinate
            y: Y coordinate

        Returns:
            int: 0 for black, 1 for white, or None if coordinates are invalid
        """
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            return None

        byte_idx = y * self.bytes_per_line + (x // 8)
        bit_idx = 7 - (x % 8)

        # Read byte
        self.fb_mmap.seek(byte_idx)
        current = ord(self.fb_mmap.read(1))

        # Extract bit
        return 1 if (current & (1 << bit_idx)) else 0

    def draw_rectangle(self, x, y, width, height, fill=None, outline=None):
        """Draw a rectangle.

        Args:
            x: X coordinate of top-left corner
            y: Y coordinate of top-left corner
            width: Rectangle width
            height: Rectangle height
            fill: Fill color (0 for black, 1 for white)
            outline: Outline color (0 for black, 1 for white)
        """
        # Create a temporary image for the rectangle
        img = Image.new("1", (width, height), 1)
        from PIL import ImageDraw

        draw = ImageDraw.Draw(img)

        if fill is not None:
            draw.rectangle([(0, 0), (width - 1, height - 1)], fill=fill)
        if outline is not None:
            draw.rectangle([(0, 0), (width - 1, height - 1)], outline=outline)

        self.draw_image(img, x, y)

    def draw_text(self, x, y, text, color=0):
        """Draw text at the specified position.

        Args:
            x: X coordinate
            y: Y coordinate
            text: Text to draw
            color: Text color (0 for black, 1 for white)

        Note: This is a basic implementation. For better text rendering,
        create a PIL Image with text and use draw_image().
        """
        # Create a temporary image for the text
        from PIL import ImageDraw

        # Estimate text size (this is approximate)
        text_width = len(text) * 6  # Approximate character width
        text_height = 12  # Approximate character height

        img = Image.new("1", (text_width, text_height), 1)
        draw = ImageDraw.Draw(img)
        draw.text((0, 0), text, fill=color)

        self.draw_image(img, x, y)

    def close(self, clear_on_exit=True):
        """Close the framebuffer device and clean up resources.

        Args:
            clear_on_exit: If True, clear display before closing (default: True)
        """
        if clear_on_exit and self.fb_file:
            try:
                self.clear_display()
            except Exception:
                pass  # Ignore errors on cleanup

        if self.fb_mmap:
            self.fb_mmap.close()
            self.fb_mmap = None
        if self.fb_file:
            self.fb_file.close()
            self.fb_file = None

    def __enter__(self):
        """Context manager entry."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()


# Common helper functions


def validate_byte_alignment(x, width):
    """Validate that coordinates are byte-aligned for partial updates.

    Args:
        x: X coordinate
        width: Width

    Returns:
        tuple: (aligned_x, aligned_width)

    Raises:
        ValueError: If alignment cannot be achieved
    """
    if x % 8 != 0:
        aligned_x = (x // 8) * 8
        print(f"Warning: X coordinate {x} aligned to {aligned_x}")
        x = aligned_x

    if width % 8 != 0:
        aligned_width = ((width + 7) // 8) * 8
        print(f"Warning: Width {width} aligned to {aligned_width}")
        width = aligned_width

    return x, width


def create_text_image(text, font_size=12, width=None, height=None):
    """Create a PIL Image with text.

    Args:
        text: Text to render
        font_size: Font size (approximate, depends on system fonts)
        width: Image width (auto if None)
        height: Image height (auto if None)

    Returns:
        PIL.Image: Image containing the text
    """
    from PIL import ImageDraw

    # Estimate size if not provided
    if width is None:
        width = len(text) * (font_size // 2) + 10
    if height is None:
        height = font_size + 10

    img = Image.new("1", (width, height), 1)  # White background
    draw = ImageDraw.Draw(img)

    try:
        # Try to use a system font
        from PIL import ImageFont

        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size
        )
        draw.text((5, 5), text, fill=0, font=font)
    except (ImportError, OSError):
        # Fallback to default font
        draw.text((5, 5), text, fill=0)

    return img


def get_display_info(fb_device="/dev/fb0"):
    """Get display information without creating a full EInkDisplay instance.

    Args:
        fb_device: Framebuffer device path

    Returns:
        dict: Display information (width, height, bytes_per_line)
    """
    try:
        with open(fb_device, "rb") as fb_file:
            vinfo = fcntl.ioctl(fb_file, FBIOGET_VSCREENINFO, b"\x00" * 160)
            width, height = struct.unpack("II", vinfo[:8])

            if width == 0 or height == 0:
                width, height = EINK_DEFAULT_WIDTH, EINK_DEFAULT_HEIGHT

            return {
                "width": width,
                "height": height,
                "bytes_per_line": (width + 7) // 8,
                "total_bytes": ((width + 7) // 8) * height,
            }
    except Exception:
        return {
            "width": EINK_DEFAULT_WIDTH,
            "height": EINK_DEFAULT_HEIGHT,
            "bytes_per_line": (EINK_DEFAULT_WIDTH + 7) // 8,
            "total_bytes": ((EINK_DEFAULT_WIDTH + 7) // 8) * EINK_DEFAULT_HEIGHT,
        }


def create_pattern_image(width, height, pattern="checkerboard", square_size=10):
    """Create an image with a test pattern.

    Args:
        width: Image width
        height: Image height
        pattern: Pattern type ("checkerboard", "stripes", "grid")
        square_size: Size of pattern elements

    Returns:
        PIL.Image: Image with the pattern
    """
    from PIL import ImageDraw

    img = Image.new("1", (width, height), 1)  # White background
    draw = ImageDraw.Draw(img)

    if pattern == "checkerboard":
        for y in range(0, height, square_size):
            for x in range(0, width, square_size):
                if (x // square_size + y // square_size) % 2 == 0:
                    draw.rectangle(
                        (x, y, x + square_size - 1, y + square_size - 1), fill=0
                    )
    elif pattern == "stripes":
        for x in range(0, width, square_size * 2):
            draw.rectangle((x, 0, x + square_size - 1, height - 1), fill=0)
    elif pattern == "grid":
        # Vertical lines
        for x in range(0, width, square_size):
            draw.line([(x, 0), (x, height - 1)], fill=0)
        # Horizontal lines
        for y in range(0, height, square_size):
            draw.line([(0, y), (width - 1, y)], fill=0)

    return img


# Exception classes for better error handling


class EInkError(Exception):
    """Base exception for e-ink display errors."""

    pass


class EInkIOError(EInkError):
    """Exception for I/O errors with the display."""

    pass


class EInkAlignmentError(EInkError):
    """Exception for byte alignment errors."""

    pass
