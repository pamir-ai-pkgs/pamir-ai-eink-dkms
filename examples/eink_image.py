#!/usr/bin/env python3
"""
eink_image.py - Display images on Pamir AI E-Ink display

Copyright (C) 2025 Pamir AI
License: GPL v2

Displays any image format (PNG, JPEG, BMP, TIFF, etc.) on the e-ink display.
Automatically converts to 1-bit monochrome with optional dithering.

Usage:
    eink_image.py <image_file> [options]

Options:
    --mode MODE       Scaling mode: fit, fill, stretch, center (default: fit)
    --dither          Enable dithering for better grayscale (default)
    --no-dither       Disable dithering
    --rotate ANGLE    Rotate image: 0, 90, 180, 270 degrees
    --invert          Invert black and white
    --update MODE     Update mode: full, partial (default: full)
    --threshold VAL   Threshold for B&W conversion without dither (0-255)
"""

import sys
import os
import argparse
from pathlib import Path
from PIL import Image, ImageOps

# Import common e-ink module
try:
    from eink_common import EInkDisplay, EPD_MODE_FULL, EPD_MODE_PARTIAL
except ImportError:
    # Try from parent directory if running from source
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from eink_common import EInkDisplay, EPD_MODE_FULL, EPD_MODE_PARTIAL


def load_and_convert_image(
    file_path,
    width,
    height,
    mode="fit",
    dither=True,
    rotate=0,
    invert=False,
    threshold=128,
):
    """Load an image and convert it for e-ink display.

    Args:
        file_path: Path to the image file
        width: Display width
        height: Display height
        mode: Scaling mode (fit, fill, stretch, center)
        dither: Enable dithering for better grayscale representation
        rotate: Rotation angle in degrees (0, 90, 180, 270)
        invert: Invert black and white
        threshold: Threshold for B&W conversion without dither

    Returns:
        PIL Image in mode '1' (1-bit black and white)
    """
    # Load the image
    try:
        img = Image.open(file_path)
    except IOError as e:
        raise IOError(f"Cannot open image file: {e}")

    # Convert to RGB first if needed (handles transparency)
    if img.mode in ("RGBA", "LA", "P"):
        # Create white background for transparent images
        background = Image.new("RGB", img.size, (255, 255, 255))
        if img.mode == "P":
            img = img.convert("RGBA")
        background.paste(img, mask=img.split()[-1] if "A" in img.mode else None)
        img = background
    elif img.mode != "RGB":
        img = img.convert("RGB")

    # Apply rotation if specified
    if rotate != 0:
        img = img.rotate(-rotate, expand=True)

    # Handle different scaling modes
    if mode == "fit":
        # Scale to fit within display, maintaining aspect ratio
        img.thumbnail((width, height), Image.Resampling.LANCZOS)
        # Center on display
        new_img = Image.new("RGB", (width, height), (255, 255, 255))
        x = (width - img.width) // 2
        y = (height - img.height) // 2
        new_img.paste(img, (x, y))
        img = new_img

    elif mode == "fill":
        # Scale to fill display, cropping if necessary
        img_ratio = img.width / img.height
        display_ratio = width / height

        if img_ratio > display_ratio:
            # Image is wider - scale by height
            new_height = height
            new_width = int(height * img_ratio)
        else:
            # Image is taller - scale by width
            new_width = width
            new_height = int(width / img_ratio)

        img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
        # Crop to center
        left = (img.width - width) // 2
        top = (img.height - height) // 2
        img = img.crop((left, top, left + width, top + height))

    elif mode == "stretch":
        # Stretch to fill display, ignoring aspect ratio
        img = img.resize((width, height), Image.Resampling.LANCZOS)

    elif mode == "center":
        # Center image without scaling
        new_img = Image.new("RGB", (width, height), (255, 255, 255))
        x = (width - min(img.width, width)) // 2
        y = (height - min(img.height, height)) // 2
        # Crop if image is larger than display
        if img.width > width or img.height > height:
            left = max(0, (img.width - width) // 2)
            top = max(0, (img.height - height) // 2)
            img = img.crop((left, top, left + width, top + height))
        new_img.paste(img, (x, y))
        img = new_img

    # Convert to grayscale
    img = img.convert("L")

    # Invert if requested (before conversion to 1-bit)
    if invert:
        img = ImageOps.invert(img)

    # Convert to 1-bit black and white
    if dither:
        # Use Floyd-Steinberg dithering for better quality
        img = img.convert("1", dither=Image.Dither.FLOYDSTEINBERG)
    else:
        # Simple threshold conversion
        img = img.point(lambda x: 255 if x > threshold else 0, mode="1")

    return img


def main():
    parser = argparse.ArgumentParser(
        description="Display images on Pamir AI E-Ink display",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    eink_image.py photo.jpg                    # Display with default settings
    eink_image.py logo.png --mode center       # Center without scaling
    eink_image.py art.bmp --no-dither          # Disable dithering
    eink_image.py diagram.tiff --rotate 90     # Rotate 90 degrees
    eink_image.py text.png --invert            # Invert colors
    eink_image.py icon.gif --update partial    # Use partial update

Supported formats: PNG, JPEG, BMP, TIFF, GIF, WEBP, PPM, and more via PIL/Pillow
        """,
    )

    parser.add_argument("image", help="Path to image file")
    parser.add_argument(
        "--mode",
        choices=["fit", "fill", "stretch", "center"],
        default="fit",
        help="Image scaling mode (default: fit)",
    )
    parser.add_argument(
        "--dither", action="store_true", default=True, help="Enable dithering (default)"
    )
    parser.add_argument(
        "--no-dither", dest="dither", action="store_false", help="Disable dithering"
    )
    parser.add_argument(
        "--rotate",
        type=int,
        choices=[0, 90, 180, 270],
        default=0,
        help="Rotation angle in degrees",
    )
    parser.add_argument("--invert", action="store_true", help="Invert black and white")
    parser.add_argument(
        "--update",
        choices=["full", "partial"],
        default="full",
        help="Display update mode (default: full)",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=128,
        help="B&W threshold without dither (0-255, default: 128)",
    )

    args = parser.parse_args()

    # Validate image file exists
    image_path = Path(args.image)
    if not image_path.exists():
        print(f"Error: Image file not found: {args.image}")
        sys.exit(1)

    if not image_path.is_file():
        print(f"Error: Not a file: {args.image}")
        sys.exit(1)

    try:
        # Initialize display
        print("Opening e-ink display...")
        display = EInkDisplay()

        # Set update mode
        if args.update == "partial":
            display.set_update_mode(EPD_MODE_PARTIAL)
            print("Using partial update mode")
        else:
            display.set_update_mode(EPD_MODE_FULL)
            print("Using full update mode")

        # Load and convert image
        print(f"Loading image: {args.image}")
        print(f"Mode: {args.mode}, Dither: {args.dither}, Rotate: {args.rotate}°")

        img = load_and_convert_image(
            image_path,
            display.width,
            display.height,
            mode=args.mode,
            dither=args.dither,
            rotate=args.rotate,
            invert=args.invert,
            threshold=args.threshold,
        )

        # Clear display first
        print("Clearing display...")
        display.clear(255)  # White

        # Draw the image
        print(f"Drawing image ({img.width}x{img.height})...")
        display.draw_image(img, 0, 0)

        # Update display
        print("Updating display...")
        display.update_display()

        print(f"Successfully displayed: {image_path.name}")

        # Show image info
        file_size = image_path.stat().st_size
        print(f"Image info: {img.width}x{img.height} pixels, {file_size:,} bytes")

    except IOError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"Unexpected error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)
    finally:
        # Clean up
        if "display" in locals():
            display.close()


if __name__ == "__main__":
    main()
