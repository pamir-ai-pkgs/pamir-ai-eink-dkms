#!/usr/bin/env python3
import time
from PIL import Image, ImageDraw
from eink_common import (
    EInkDisplay,
    EPD_MODE_FULL,
    EPD_MODE_PARTIAL,
    create_pattern_image,
)


def demo_full_update():
    print("=== Full Update Demo ===")

    with EInkDisplay() as display:
        display.set_update_mode(EPD_MODE_FULL)
        display.clear(255)

        img = Image.new("1", (display.width, display.height), 1)
        draw = ImageDraw.Draw(img)

        draw.rectangle((0, 0, display.width - 1, display.height - 1), outline=0)
        draw.text((10, 10), "Pamir AI E-Ink", fill=0)
        draw.text((10, 30), "Full Update Mode", fill=0)
        draw.rectangle((10, 50, 60, 80), fill=0)
        draw.ellipse((70, 50, 120, 80), outline=0)

        display.draw_image(img)
        display.update_display()

        print("Full update complete")
        time.sleep(3)


def demo_partial_update():
    print("\n=== Partial Update Demo ===")

    with EInkDisplay() as display:
        display.set_update_mode(EPD_MODE_FULL)
        display.clear(255)

        img = Image.new("1", (display.width, display.height), 1)
        draw = ImageDraw.Draw(img)
        draw.text((10, 10), "Partial Update Demo", fill=0)
        draw.rectangle((0, 0, display.width - 1, display.height - 1), outline=0)

        display.draw_image(img)
        display.update_display()
        time.sleep(2)

        display.set_update_mode(EPD_MODE_PARTIAL)

        partial_width = 64
        partial_height = 32

        # Center the partial update area dynamically
        partial_x = (display.width - partial_width) // 2
        partial_y = (display.height - partial_height) // 2

        # Ensure byte alignment for x coordinate (must be multiple of 8)
        partial_x = (partial_x // 8) * 8
        partial_width = min(partial_width, display.width - partial_x)
        partial_height = min(partial_height, display.height - partial_y)

        for i in range(5):
            display.set_partial_area(
                partial_x, partial_y, partial_width, partial_height
            )

            counter_img = Image.new("1", (partial_width, partial_height), 1)
            counter_draw = ImageDraw.Draw(counter_img)
            counter_draw.rectangle(
                (0, 0, partial_width - 1, partial_height - 1), outline=0
            )
            counter_draw.text((10, 8), f"Count: {i}", fill=0)

            display.draw_image(counter_img, partial_x, partial_y)
            display.update_display()

            print(f"Partial update {i}")
            time.sleep(1)

        display.set_update_mode(EPD_MODE_FULL)


def demo_patterns():
    print("\n=== Pattern Demo ===")

    with EInkDisplay() as display:
        display.set_update_mode(EPD_MODE_FULL)

        img = create_pattern_image(display.width, display.height, "checkerboard", 10)
        display.draw_image(img)
        display.update_display()

        print("Pattern display complete")
        time.sleep(3)

        print("Entering deep sleep...")
        display.deep_sleep()


def main():
    print("Pamir AI E-Ink Display Python Demo")
    print("===================================\n")

    try:
        with EInkDisplay() as display:
            print(f"Display dimensions: {display.width}x{display.height}")

            response = input("Reset display before starting? (y/N): ").lower()
            if response == "y":
                display.reset_display()
                time.sleep(1)

        demo_full_update()
        demo_partial_update()
        demo_patterns()

        print("\nDemo complete!")

    except FileNotFoundError:
        print("Framebuffer device not found. Load driver: sudo modprobe pamir-ai-eink")
    except PermissionError:
        print("Permission denied. Run with sudo or add user to video group.")
    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    main()
