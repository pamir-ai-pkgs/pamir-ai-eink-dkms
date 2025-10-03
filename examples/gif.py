#!/usr/bin/env python3
import time
import os
import argparse
from PIL import Image, ImageDraw
from eink_common import EInkDisplay, EPD_MODE_FULL, EPD_MODE_PARTIAL

try:
    import imageio.v3 as iio
except ImportError:
    print("Please install imageio: pip install imageio[ffmpeg]")
    import sys

    sys.exit(1)


def create_simple_gif_frames():
    import math

    frames = []
    size = (64, 64)

    for angle in range(0, 360, 45):
        img = Image.new("1", size, 1)
        draw = ImageDraw.Draw(img)

        center_x, center_y = size[0] // 2, size[1] // 2
        radius = min(size) // 3

        x1 = center_x + radius * math.cos(math.radians(angle))
        y1 = center_y + radius * math.sin(math.radians(angle))
        x2 = center_x - radius * math.cos(math.radians(angle))
        y2 = center_y - radius * math.sin(math.radians(angle))

        draw.line([(x1, y1), (x2, y2)], fill=0, width=3)
        draw.ellipse(
            (
                center_x - radius,
                center_y - radius,
                center_x + radius,
                center_y + radius,
            ),
            outline=0,
        )
        draw.text((5, 5), str(angle // 45 + 1), fill=0)

        frames.append(img)

    return frames


def load_gif_frames(gif_path, max_width=128, max_height=250):
    frames = []
    durations = []

    try:
        gif_data = iio.imread(gif_path, plugin="pillow")

        if hasattr(gif_data, "__len__") and len(gif_data.shape) > 2:
            for frame_data in gif_data:
                frame = Image.fromarray(frame_data)
                frames.append(frame)
                durations.append(100)
        else:
            frame = Image.fromarray(gif_data)
            frames.append(frame)
            durations.append(1000)

        try:
            with Image.open(gif_path) as gif:
                for i in range(len(frames)):
                    gif.seek(i)
                    duration = gif.info.get("duration", 100)
                    if i < len(durations):
                        durations[i] = duration
        except:
            pass

        if frames and (frames[0].size[0] > max_width or frames[0].size[1] > max_height):
            print(f"Original size: {frames[0].size[0]}x{frames[0].size[1]}")

            scale_x = max_width / frames[0].size[0]
            scale_y = max_height / frames[0].size[1]
            scale = min(scale_x, scale_y)

            new_width = int(frames[0].size[0] * scale)
            new_height = int(frames[0].size[1] * scale)

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
        durations = [200] * len(frames)

    return frames, durations


def display_gif(display, gif_path=None, loops=3, use_partial=True):
    if gif_path and os.path.exists(gif_path):
        print(f"Loading GIF from: {gif_path}")
        frames, durations = load_gif_frames(gif_path, display.width, display.height)
    else:
        print("Creating demo animation...")
        frames = create_simple_gif_frames()
        durations = [200] * len(frames)

    print(f"Loaded {len(frames)} frames")

    print("Clearing display...")
    display.set_update_mode(EPD_MODE_FULL)
    display.clear(255)
    display.update_display()
    time.sleep(1)

    if frames:
        frame_width, frame_height = frames[0].size
        x_pos = (display.width - frame_width) // 2
        y_pos = (display.height - frame_height) // 2

        x_pos = (x_pos // 8) * 8
        x_pos = max(0, x_pos)
        y_pos = max(0, y_pos)

        frame_width = min(frame_width, display.width - x_pos)
        frame_height = min(frame_height, display.height - y_pos)

        print(f"Animation size: {frame_width}x{frame_height}")
        print(f"Display position: ({x_pos}, {y_pos})")

        if use_partial and len(frames) > 1:
            print("Using partial update mode for animation")
            display.set_update_mode(EPD_MODE_PARTIAL)
            display.set_partial_area(x_pos, y_pos, frame_width, frame_height)
        else:
            print("Using full update mode")
            display.set_update_mode(EPD_MODE_FULL)

        loop_count = 0
        try:
            while loops == 0 or loop_count < loops:
                for i, (frame, duration) in enumerate(zip(frames, durations)):
                    mono_frame = frame.convert("1")

                    if (
                        mono_frame.size[0] > display.width
                        or mono_frame.size[1] > display.height
                    ):
                        print(f"Warning: Frame {i + 1} needs additional resizing")
                        continue

                    if use_partial and len(frames) > 1:
                        clear_img = Image.new("1", (frame_width, frame_height), 1)
                        display.draw_image(clear_img, x_pos, y_pos)

                    display.draw_image(mono_frame, x_pos, y_pos)
                    display.update_display()

                    print(
                        f"Frame {i + 1}/{len(frames)} (loop {loop_count + 1}/{loops if loops > 0 else '∞'})",
                        end="\r",
                    )
                    time.sleep(duration / 1000.0)

                loop_count += 1

        except KeyboardInterrupt:
            print("\nAnimation interrupted")

    print("\nAnimation complete")

    if use_partial and len(frames) > 1:
        print("Performing final full refresh...")
        display.set_update_mode(EPD_MODE_FULL)
        display.clear(255)
        display.update_display()
    else:
        display.set_update_mode(EPD_MODE_FULL)
        display.update_display()


def main():
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

            display_gif(
                display,
                gif_path=args.gif_file,
                loops=args.loops,
                use_partial=not args.full_update,
            )

            print("Entering deep sleep...")
            display.deep_sleep()

    except FileNotFoundError:
        print("Framebuffer device not found. Load driver: sudo modprobe pamir-ai-eink")
    except PermissionError:
        print("Permission denied. Run with sudo or add user to video group.")
    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    main()
