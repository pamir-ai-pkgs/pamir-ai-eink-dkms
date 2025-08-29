#!/usr/bin/env python3
"""
eink_recovery.py - Recovery tool for stuck Pamir AI E-Ink display

Copyright (C) 2025 Pamir AI
License: GPL v2

Use this tool to reset and recover a stuck e-ink display.
"""

import fcntl
import struct
import sys
import time

# IOCTL constants
EPD_IOC_MAGIC = ord("E")


def _IOW(type, nr, size):
    return (1 << 30) | (type << 8) | nr | (size << 16)


def _IO(type, nr):
    return (0 << 30) | (type << 8) | nr


# IOCTL commands
EPD_IOC_SET_UPDATE_MODE = _IOW(EPD_IOC_MAGIC, 1, 4)
EPD_IOC_UPDATE_DISPLAY = _IO(EPD_IOC_MAGIC, 4)
EPD_IOC_DEEP_SLEEP = _IO(EPD_IOC_MAGIC, 5)
EPD_IOC_RESET = _IO(EPD_IOC_MAGIC, 7)

# Update modes
EPD_MODE_FULL = 0


def reset_display(fb_device="/dev/fb0"):
    """Reset the e-ink display and clear it."""
    try:
        print(f"Opening framebuffer device: {fb_device}")
        with open(fb_device, "r+b") as fb_file:
            # Step 1: Reset the display hardware
            print("Resetting display hardware...")
            fcntl.ioctl(fb_file, EPD_IOC_RESET)
            time.sleep(1)

            # Step 2: Set to full update mode
            print("Setting full update mode...")
            mode_bytes = struct.pack("i", EPD_MODE_FULL)
            fcntl.ioctl(fb_file, EPD_IOC_SET_UPDATE_MODE, mode_bytes)

            # Step 3: Clear the display (write all white)
            print("Clearing display...")
            # Get display size (assuming 128x250 or reading from device)
            width = 128
            height = 250
            bytes_per_line = (width + 7) // 8
            fb_size = bytes_per_line * height

            # Write all white (0xFF)
            fb_file.seek(0)
            fb_file.write(b"\xff" * fb_size)
            fb_file.flush()

            # Step 4: Trigger display update
            print("Triggering display update...")
            fcntl.ioctl(fb_file, EPD_IOC_UPDATE_DISPLAY)
            time.sleep(2)

            print("Display recovery complete!")
            print("The display has been reset and cleared.")

            # Optional: Ask if user wants to put display to sleep
            response = input("\nPut display to deep sleep? (y/N): ").lower()
            if response == "y":
                print("Entering deep sleep mode...")
                fcntl.ioctl(fb_file, EPD_IOC_DEEP_SLEEP)
                print("Display is now in deep sleep mode.")

            return True

    except FileNotFoundError:
        print(f"Error: Framebuffer device {fb_device} not found.")
        print("Make sure the driver is loaded: sudo modprobe pamir-ai-eink")
        return False

    except PermissionError:
        print("Error: Permission denied.")
        print("Try running with sudo:")
        print(f"  sudo {sys.argv[0]}")
        return False

    except IOError as e:
        print(f"Error during reset: {e}")
        print("\nPossible causes:")
        print("1. Driver not loaded (check with 'lsmod | grep pamir_ai_eink')")
        print("2. Display hardware not connected properly")
        print("3. Old driver version without reset support")
        return False

    except Exception as e:
        print(f"Unexpected error: {e}")
        return False


def main():
    """Main function."""
    print("Pamir AI E-Ink Display Recovery Tool")
    print("=====================================\n")

    # Check command line arguments
    fb_device = "/dev/fb0"
    if len(sys.argv) > 1:
        fb_device = sys.argv[1]

    print("This tool will reset your e-ink display to recover from stuck states.")
    print(f"Using framebuffer device: {fb_device}\n")

    response = input("Proceed with display reset? (y/N): ").lower()
    if response != "y":
        print("Reset cancelled.")
        return

    print()
    if reset_display(fb_device):
        print("\nIf the display is still stuck after reset:")
        print("1. Check hardware connections (SPI, GPIO pins)")
        print("2. Power cycle the display")
        print(
            "3. Reload the driver: sudo modprobe -r pamir-ai-eink && sudo modprobe pamir-ai-eink"
        )
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
