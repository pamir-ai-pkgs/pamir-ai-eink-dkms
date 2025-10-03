#!/usr/bin/env python3
import sys
import time
from eink_common import (
    EInkDisplay,
    EPD_MODE_FULL,
    EPD_MODE_BASE_MAP,
)


def reset_display(fb_device="/dev/fb0"):
    """
    Perform display recovery.
    """
    try:
        print(f"Opening display device: {fb_device}")
        with EInkDisplay(fb_device) as display:
            print("\n=== Starting display recovery ===")

            total_steps = 9

            step = 0

            # Step 1: Hardware reset using kernel IOCTL
            step += 1
            print(f"[{step}/{total_steps}] Performing hardware reset...")
            try:
                display.reset_display()
                time.sleep(1)
            except IOError as e:
                print(f"  Regular reset failed ({e}), trying force reset via sysfs...")
                # Try force reset through sysfs if regular reset fails
                try:
                    with open("/sys/bus/spi/devices/spi0.0/force_reset", "w") as f:
                        f.write("1")
                    print("  Force reset successful!")
                    time.sleep(1)
                except Exception as sysfs_err:
                    print(f"  Force reset also failed: {sysfs_err}")
                    raise e

            # Step 2: Set full update mode for thorough refresh
            step += 1
            print(f"[{step}/{total_steps}] Setting full update mode...")
            display.set_update_mode(EPD_MODE_FULL)

            # Step 3: Clear to black first (inverts all pixels)
            step += 1
            print(f"[{step}/{total_steps}] Clearing display to black...")
            display.clear(0)  # Black
            display.update_display()
            time.sleep(2)

            # Step 4: Set black as base map to clear RED RAM
            step += 1
            print(f"[{step}/{total_steps}] Setting black screen as base map...")
            display.set_update_mode(EPD_MODE_BASE_MAP)
            display.update_display()
            time.sleep(2)

            # Step 5: Clear to white (inverts all pixels again)
            step += 1
            print(f"[{step}/{total_steps}] Clearing display to white...")
            display.set_update_mode(EPD_MODE_FULL)
            display.clear(255)  # White
            display.update_display()
            time.sleep(2)

            # Step 6: Set white as base map
            step += 1
            print(f"[{step}/{total_steps}] Setting white screen as base map...")
            display.set_update_mode(EPD_MODE_BASE_MAP)
            display.update_display()
            time.sleep(2)

            # Step N-3: Final white clear
            step += 1
            print(f"[{step}/{total_steps}] Final clear to white...")
            display.set_update_mode(EPD_MODE_FULL)
            display.clear(255)
            display.update_display()
            time.sleep(2)

            # Step N-2: Set clean white as base map
            step += 1
            print(f"[{step}/{total_steps}] Setting clean base map...")
            display.set_update_mode(EPD_MODE_BASE_MAP)
            display.update_display()
            time.sleep(1)

            # Step N-1: Reset to full mode for normal operation
            step += 1
            print(f"[{step}/{total_steps}] Resetting to full update mode...")
            display.set_update_mode(EPD_MODE_FULL)

            print("\n✓ All RAM buffers cleared successfully!")
            print("✓ Display recovery complete!")

            response = input("\nPut display to deep sleep? (y/N): ").lower()
            if response == "y":
                print("Entering deep sleep mode...")
                display.deep_sleep()

            return True

    except FileNotFoundError:
        print(f"Framebuffer device {fb_device} not found.")
        print("Load driver: sudo modprobe pamir-ai-eink")
        return False

    except PermissionError:
        print("Permission denied. Try with sudo.")
        return False

    except IOError as e:
        print(f"Reset failed: {e}")
        return False

    except Exception as e:
        print(f"Error: {e}")
        return False


def main():
    print("Pamir AI E-Ink Display Recovery Tool")
    print("=====================================\n")

    fb_device = "/dev/fb0"
    mode = "standard"

    # Parse arguments
    args = sys.argv[1:]
    for arg in args:
        if arg in ["-h", "--help"]:
            print("Usage: eink_recovery.py [OPTIONS] [DEVICE]")
            print("\nOptions:")
            print("  -h, --help      Show this help message")
            print("\nDevice:")
            print("  DEVICE          Framebuffer device (default: /dev/fb0)")
            print("\nExamples:")
            print("  eink_recovery.py                    # Standard recovery")
            sys.exit(0)
        elif not arg.startswith("--"):
            fb_device = arg

    print("This tool will reset your e-ink display to recover from stuck states.")
    print(f"Using framebuffer device: {fb_device}")
    print(f"Recovery mode: {mode.upper()}")

    response = input("Proceed with display reset? (y/N): ").lower()
    if response != "y":
        print("Reset cancelled.")
        return

    print()
    if not reset_display(fb_device):
        sys.exit(1)


if __name__ == "__main__":
    main()
