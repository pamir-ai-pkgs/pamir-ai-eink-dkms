#!/usr/bin/env python3
"""
gif.py - GIF animation display example for Pamir AI E-Ink display

Copyright (C) 2025 Pamir AI
License: GPL v2

This example demonstrates how to display animated GIF files on the e-ink display.
It includes frame extraction, monochrome conversion, and animation playback.

Requirements:
  pip install pillow imageio[ffmpeg]
"""

import mmap
import fcntl
import struct
import time
import os
import argparse
from PIL import Image, ImageDraw
import imageio.v3 as iio

# IOCTL constants
EPD_IOC_MAGIC = ord("E")

# Framebuffer IOCTL constants for getting screen info
FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602


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
EPD_IOC_RESET = _IO(EPD_IOC_MAGIC, 7)  # Reset display

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
        self.width = 128  # Default, will be updated
        self.height = 250  # Default, will be updated
        self.bytes_per_line = 0

        self._open_framebuffer()

    def _open_framebuffer(self):
        """Open and memory-map the framebuffer device."""
        # Open framebuffer
        self.fb_file = open(self.fb_device, "r+b")

        # Get framebuffer variable screen info to read dimensions
        vinfo = fcntl.ioctl(self.fb_file, FBIOGET_VSCREENINFO, b"\x00" * 160)

        # Parse xres and yres from the structure (first two 32-bit values)
        self.width, self.height = struct.unpack("II", vinfo[:8])

        # Fallback to defaults if we get invalid values
        if self.width == 0 or self.height == 0:
            print("Warning: Could not read display dimensions, using defaults")
            self.width = 128
            self.height = 250

        print(f"Display dimensions: {self.width}x{self.height}")
        self.bytes_per_line = (self.width + 7) // 8

        # Calculate framebuffer size
        fb_size = self.bytes_per_line * self.height

        # Memory map the framebuffer
        self.fb_mmap = mmap.mmap(
            self.fb_file.fileno(), fb_size, mmap.MAP_SHARED, mmap.PROT_WRITE
        )

    def set_update_mode(self, mode):
        """Set the display update mode."""
        mode_bytes = struct.pack("i", mode)
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_UPDATE_MODE, mode_bytes)

    def set_partial_area(self, x, y, width, height):
        """Set the partial update area."""
        # Ensure byte alignment
        x = (x // 8) * 8
        width = ((width + 7) // 8) * 8

        # Clip to display bounds
        if x + width > self.width:
            width = self.width - x
        if y + height > self.height:
            height = self.height - y

        area_bytes = struct.pack("HHHH", x, y, width, height)
        fcntl.ioctl(self.fb_file, EPD_IOC_SET_PARTIAL_AREA, area_bytes)

    def update_display(self):
        """Trigger a display update."""
        fcntl.ioctl(self.fb_file, EPD_IOC_UPDATE_DISPLAY)

    def deep_sleep(self):
        """Enter deep sleep mode."""
        fcntl.ioctl(self.fb_file, EPD_IOC_DEEP_SLEEP)

    def reset_display(self):
        """Reset the display hardware."""
        try:
            fcntl.ioctl(self.fb_file, EPD_IOC_RESET)
            print("Display reset successful")
            self.set_update_mode(EPD_MODE_FULL)
        except IOError as e:
            print(f"Display reset failed: {e}")

    def draw_image(self, image, x=0, y=0):
        """Draw a PIL Image to the display."""
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
        """Clear the display."""
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


def create_simple_gif_frames():
    """Create a simple animated GIF in memory for demonstration.

    Returns:
        list: List of PIL Image frames
    """
    frames = []
    size = (64, 64)  # Small size for e-ink display

    # Create a simple spinning animation
    for angle in range(0, 360, 45):
        img = Image.new("1", size, 1)  # White background
        draw = ImageDraw.Draw(img)

        # Draw a rotating line
        import math

        center_x, center_y = size[0] // 2, size[1] // 2
        radius = min(size) // 3

        # Calculate line endpoints
        x1 = center_x + radius * math.cos(math.radians(angle))
        y1 = center_y + radius * math.sin(math.radians(angle))
        x2 = center_x - radius * math.cos(math.radians(angle))
        y2 = center_y - radius * math.sin(math.radians(angle))

        # Draw the line
        draw.line([(x1, y1), (x2, y2)], fill=0, width=3)

        # Draw a circle outline
        draw.ellipse(
            (
                center_x - radius,
                center_y - radius,
                center_x + radius,
                center_y + radius,
            ),
            outline=0,
        )

        # Add frame number
        draw.text((5, 5), str(angle // 45 + 1), fill=0)

        frames.append(img)

    return frames


def load_gif_frames(gif_path, max_width=128, max_height=250):
    """Load frames from a GIF file and resize if needed.

    Args:
        gif_path: Path to the GIF file
        max_width: Maximum width for frames
        max_height: Maximum height for frames

    Returns:
        tuple: (frames list, durations list in milliseconds)
    """
    frames = []
    durations = []

    try:
        # Read GIF using imageio
        gif_data = iio.imread(gif_path, plugin="pillow")

        # Check if it's animated (has multiple frames)
        if hasattr(gif_data, "__len__") and len(gif_data.shape) > 2:
            # Animated GIF
            for frame_data in gif_data:
                # Convert numpy array to PIL Image
                frame = Image.fromarray(frame_data)
                frames.append(frame)
                durations.append(100)  # Default 100ms per frame
        else:
            # Static GIF
            frame = Image.fromarray(gif_data)
            frames.append(frame)
            durations.append(1000)  # 1 second for static image

        # Try to get actual frame durations if available
        try:
            with Image.open(gif_path) as gif:
                for i in range(len(frames)):
                    gif.seek(i)
                    duration = gif.info.get("duration", 100)
                    if i < len(durations):
                        durations[i] = duration
        except:
            pass  # Use default durations if we can't get them

        # Resize frames if they're too large
        if frames and (frames[0].size[0] > max_width or frames[0].size[1] > max_height):
            print(f"Original size: {frames[0].size[0]}x{frames[0].size[1]}")

            # Calculate scaling to fit display
            scale_x = max_width / frames[0].size[0]
            scale_y = max_height / frames[0].size[1]
            scale = min(scale_x, scale_y)

            new_width = int(frames[0].size[0] * scale)
            new_height = int(frames[0].size[1] * scale)

            # Ensure width is multiple of 8 for byte alignment
            new_width = (new_width // 8) * 8
            if new_width == 0:
                new_width = 8

            print(f"Resizing to: {new_width}x{new_height}")

            resized_frames = []
            for frame in frames:
                resized = frame.resize(
                    (new_width, new_height), Image.Resampling.LANCZOS
                )
                resized_frames.append(resized)
            frames = resized_frames

    except Exception as e:
        print(f"Error loading GIF: {e}")
        print("Creating demo animation instead...")
        frames = create_simple_gif_frames()
        durations = [200] * len(frames)  # 200ms per frame

    return frames, durations


def display_gif(display, gif_path=None, loops=3, use_partial=True):
    """Display a GIF animation on the e-ink display.

    Args:
        display: EInkDisplay instance
        gif_path: Path to GIF file (None for demo animation)
        loops: Number of times to loop the animation (0 for infinite)
        use_partial: Use partial updates for animation (faster but may ghost)
    """
    # Load or create frames (pass display dimensions for proper resizing)
    if gif_path and os.path.exists(gif_path):
        print(f"Loading GIF from: {gif_path}")
        frames, durations = load_gif_frames(gif_path, display.width, display.height)
    else:
        print("Creating demo animation...")
        frames = create_simple_gif_frames()
        durations = [200] * len(frames)

    print(f"Loaded {len(frames)} frames")

    # First, do a full clear
    print("Clearing display...")
    display.set_update_mode(EPD_MODE_FULL)
    display.clear(255)
    display.update_display()
    time.sleep(1)

    # Calculate display position (center the animation)
    if frames:
        frame_width, frame_height = frames[0].size
        x_pos = (display.width - frame_width) // 2
        y_pos = (display.height - frame_height) // 2

        # Ensure x position is byte-aligned
        x_pos = (x_pos // 8) * 8

        # Make sure coordinates are non-negative
        x_pos = max(0, x_pos)
        y_pos = max(0, y_pos)

        # Ensure frame fits within display bounds
        frame_width = min(frame_width, display.width - x_pos)
        frame_height = min(frame_height, display.height - y_pos)

        print(f"Animation size: {frame_width}x{frame_height}")
        print(f"Display position: ({x_pos}, {y_pos})")

        # Set update mode
        if use_partial and len(frames) > 1:
            print("Using partial update mode for animation")
            display.set_update_mode(EPD_MODE_PARTIAL)
            # Set partial area for the animation region
            display.set_partial_area(x_pos, y_pos, frame_width, frame_height)
        else:
            print("Using full update mode")
            display.set_update_mode(EPD_MODE_FULL)

        # Animation loop
        loop_count = 0
        try:
            while loops == 0 or loop_count < loops:
                for i, (frame, duration) in enumerate(zip(frames, durations)):
                    # Convert frame to monochrome
                    mono_frame = frame.convert("1")

                    # Note: Frames should already be resized during loading,
                    # but keep this check as a safety measure
                    if (
                        mono_frame.size[0] > display.width
                        or mono_frame.size[1] > display.height
                    ):
                        print(f"Warning: Frame {i + 1} needs additional resizing")
                        continue

                    # Clear the animation area (for partial update)
                    if use_partial and len(frames) > 1:
                        # Create a white rectangle to clear the area
                        clear_img = Image.new("1", (frame_width, frame_height), 1)
                        display.draw_image(clear_img, x_pos, y_pos)

                    # Draw frame
                    display.draw_image(mono_frame, x_pos, y_pos)
                    display.update_display()

                    # Show progress
                    print(
                        f"Frame {i + 1}/{len(frames)} (loop {loop_count + 1}/{loops if loops > 0 else '∞'})",
                        end="\r",
                    )

                    # Wait for frame duration (convert ms to seconds)
                    time.sleep(duration / 1000.0)

                loop_count += 1

        except KeyboardInterrupt:
            print("\nAnimation interrupted")

    print("\nAnimation complete")

    # Always do a final full refresh to clean up any ghosting and reset state
    if use_partial and len(frames) > 1:
        print("Performing final full refresh and clearing partial area...")
        # First switch to full mode (this will clear partial_area_set with the driver fix)
        display.set_update_mode(EPD_MODE_FULL)
        # Clear the display to ensure clean state
        display.clear(255)
        # Perform the full update
        display.update_display()
    else:
        # Even for full update mode, ensure we're in a clean state
        display.set_update_mode(EPD_MODE_FULL)
        display.update_display()


def main():
    """Main function for GIF display example."""
    parser = argparse.ArgumentParser(
        description="Display GIF animations on Pamir AI E-Ink display"
    )
    parser.add_argument(
        "gif_file",
        nargs="?",
        help="Path to GIF file (optional, uses demo if not provided)",
    )
    parser.add_argument(
        "--loops", type=int, default=3, help="Number of loops (0 for infinite)"
    )
    parser.add_argument(
        "--full-update",
        action="store_true",
        help="Use full update mode (slower but cleaner)",
    )
    parser.add_argument(
        "--reset", action="store_true", help="Reset display before starting"
    )
    parser.add_argument("--device", default="/dev/fb0", help="Framebuffer device path")

    args = parser.parse_args()

    print("Pamir AI E-Ink GIF Display Example")
    print("===================================\n")

    try:
        with EInkDisplay(args.device) as display:
            if args.reset:
                print("Resetting display...")
                display.reset_display()
                time.sleep(1)

            # Display the GIF
            display_gif(
                display,
                gif_path=args.gif_file,
                loops=args.loops,
                use_partial=not args.full_update,
            )

            # Enter deep sleep
            print("Entering deep sleep...")
            display.deep_sleep()

    except FileNotFoundError:
        print("Error: Framebuffer device not found.")
        print("Make sure the driver is loaded: sudo modprobe pamir-ai-eink")
    except PermissionError:
        print("Error: Permission denied.")
        print("Try running with sudo or add user to video group:")
        print("  sudo usermod -a -G video $USER")
    except Exception as e:
        print(f"Error: {e}")
        print("\nIf the display is stuck, try running with --reset option.")


if __name__ == "__main__":
    main()
