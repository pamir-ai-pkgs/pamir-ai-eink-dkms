#!/usr/bin/env python3
"""
eink_reader.py - E-Book Reader for Pamir AI E-Ink Driver

This example demonstrates text rendering and page navigation
optimized for e-ink displays using partial updates
"""

import os
import sys
import fcntl
import mmap
import struct
import textwrap
from PIL import Image, ImageDraw

# IOCTL definitions
EPD_IOC_SET_UPDATE_MODE = 0x40044501  # _IOW('E', 1, int)
EPD_IOC_SET_PARTIAL_AREA = 0x40084503  # _IOW('E', 3, struct epd_update_area)

EPD_MODE_FULL = 0
EPD_MODE_PARTIAL = 1


class EInkReader:
    def __init__(self, fb_device="/dev/fb0"):
        self.fb_device = fb_device
        self.width = 128
        self.height = 250
        self.fb_fd = None
        self.fb_mem = None

        # Reader settings
        self.font_size = 12
        self.line_height = 14
        self.margin = 5
        self.text_width = self.width - (2 * self.margin)
        self.text_height = (
            self.height - (2 * self.margin) - 20
        )  # Reserve space for status bar
        self.lines_per_page = self.text_height // self.line_height

        # Book state
        self.book_text = ""
        self.pages = []
        self.current_page = 0

        self.setup_framebuffer()

    def setup_framebuffer(self):
        """Open and map the framebuffer device"""
        try:
            self.fb_fd = os.open(self.fb_device, os.O_RDWR)
            fb_size = (self.width * self.height) // 8
            self.fb_mem = mmap.mmap(
                self.fb_fd, fb_size, mmap.MAP_SHARED, mmap.PROT_WRITE | mmap.PROT_READ
            )
            print(f"Framebuffer opened: {self.width}x{self.height}")
        except Exception as e:
            print(f"Failed to open framebuffer: {e}")
            sys.exit(1)

    def set_update_mode(self, mode):
        """Set the display update mode"""
        try:
            fcntl.ioctl(self.fb_fd, EPD_IOC_SET_UPDATE_MODE, struct.pack("I", mode))
        except Exception as e:
            print(f"Warning: Failed to set update mode: {e}")

    def set_partial_area(self, x, y, width, height):
        """Set partial update area"""
        # Ensure byte alignment
        x = (x // 8) * 8
        width = (width // 8) * 8

        area = struct.pack("IIII", x, y, width, height)
        try:
            fcntl.ioctl(self.fb_fd, EPD_IOC_SET_PARTIAL_AREA, area)
        except Exception as e:
            print(f"Warning: Failed to set partial area: {e}")

    def load_text(self, text_file):
        """Load text from file or use sample text"""
        if text_file and os.path.exists(text_file):
            with open(text_file, "r", encoding="utf-8") as f:
                self.book_text = f.read()
        else:
            # Sample text if no file provided
            self.book_text = """
E-Ink Display Technology

Electronic ink, or e-ink, is a type of electronic paper display technology 
that mimics the appearance of ordinary ink on paper. Unlike conventional 
backlit displays, e-ink displays reflect ambient light like paper, making 
them more comfortable to read and visible in direct sunlight.

The technology works using millions of tiny microcapsules, each containing 
positively charged white particles and negatively charged black particles 
suspended in a clear fluid. When an electric field is applied, the particles 
move to the top or bottom of the microcapsule, creating the appearance of 
white or black on the surface.

Key advantages of e-ink displays include:

1. Ultra-low power consumption - E-ink displays only consume power when 
   changing the image. Once an image is displayed, it remains visible 
   without any power consumption.

2. Paper-like readability - The reflective nature of e-ink provides a 
   reading experience similar to printed paper, reducing eye strain during 
   extended reading sessions.

3. Wide viewing angles - E-ink displays can be read from almost any angle 
   without loss of contrast or color shifting.

4. Sunlight visibility - Unlike LCD or OLED displays, e-ink becomes more 
   visible in bright sunlight, just like regular paper.

Applications of e-ink technology extend beyond e-readers to include:
- Digital signage and price tags
- Smartwatches and wearables
- Electronic shelf labels in retail
- Public transportation displays
- Architectural and design elements

The Pamir AI E-Ink driver showcases these capabilities by providing a 
comprehensive interface for controlling e-ink displays in Linux systems, 
enabling developers to create innovative applications that leverage the 
unique properties of electronic paper technology.
"""

        # Paginate the text
        self.paginate_text()

    def paginate_text(self):
        """Split text into pages"""
        # Wrap text to fit display width
        wrapper = textwrap.TextWrapper(
            width=self.text_width // 6
        )  # Approximate chars per line
        lines = []

        for paragraph in self.book_text.split("\n"):
            if paragraph.strip():
                wrapped = wrapper.wrap(paragraph)
                lines.extend(wrapped)
            else:
                lines.append("")  # Empty line for paragraph break

        # Split into pages
        self.pages = []
        for i in range(0, len(lines), self.lines_per_page):
            page_lines = lines[i : i + self.lines_per_page]
            self.pages.append(page_lines)

        print(f"Loaded {len(self.pages)} pages")

    def render_page(self, page_num):
        """Render a page of text"""
        if page_num < 0 or page_num >= len(self.pages):
            return

        # Create image for page
        img = Image.new("1", (self.width, self.height), 1)  # White background
        draw = ImageDraw.Draw(img)

        # Draw page content
        y_pos = self.margin
        for line in self.pages[page_num]:
            draw.text((self.margin, y_pos), line, fill=0)
            y_pos += self.line_height

        # Draw status bar
        status_y = self.height - 18
        draw.line([(0, status_y - 2), (self.width - 1, status_y - 2)], fill=0)

        # Page indicator
        page_info = f"Page {page_num + 1}/{len(self.pages)}"
        draw.text((self.margin, status_y), page_info, fill=0)

        # Progress bar
        progress = (page_num + 1) / len(self.pages)
        bar_width = self.width - 2 * self.margin
        bar_x = self.margin
        bar_y = status_y + 10
        draw.rectangle([(bar_x, bar_y), (bar_x + bar_width, bar_y + 3)], outline=0)
        fill_width = int(bar_width * progress)
        if fill_width > 0:
            draw.rectangle([(bar_x, bar_y), (bar_x + fill_width, bar_y + 3)], fill=0)

        # Send to framebuffer
        pixels = img.tobytes()
        self.fb_mem.seek(0)
        self.fb_mem.write(pixels)

    def next_page(self):
        """Go to next page"""
        if self.current_page < len(self.pages) - 1:
            self.current_page += 1
            self.render_page(self.current_page)
            # Use partial update for page turns
            self.set_partial_area(0, 0, self.width, self.height - 20)
            return True
        return False

    def prev_page(self):
        """Go to previous page"""
        if self.current_page > 0:
            self.current_page -= 1
            self.render_page(self.current_page)
            # Use partial update for page turns
            self.set_partial_area(0, 0, self.width, self.height - 20)
            return True
        return False

    def run_interactive(self):
        """Run interactive reader mode"""
        print("E-Ink Reader")
        print("Commands:")
        print("  n/ENTER - Next page")
        print("  p       - Previous page")
        print("  g <num> - Go to page")
        print("  q       - Quit")
        print("")

        # Initial full refresh
        self.set_update_mode(EPD_MODE_FULL)
        self.render_page(0)

        # Switch to partial mode for page turns
        self.set_update_mode(EPD_MODE_PARTIAL)

        try:
            while True:
                cmd = (
                    input(f"[Page {self.current_page + 1}/{len(self.pages)}] > ")
                    .strip()
                    .lower()
                )

                if cmd == "q":
                    break
                elif cmd == "n" or cmd == "":
                    if not self.next_page():
                        print("Last page")
                elif cmd == "p":
                    if not self.prev_page():
                        print("First page")
                elif cmd.startswith("g "):
                    try:
                        page = int(cmd[2:]) - 1
                        if 0 <= page < len(self.pages):
                            self.current_page = page
                            self.render_page(self.current_page)
                        else:
                            print(f"Invalid page (1-{len(self.pages)})")
                    except ValueError:
                        print("Invalid page number")

        except KeyboardInterrupt:
            print("\nExiting...")
        finally:
            self.cleanup()

    def cleanup(self):
        """Clean up resources"""
        # Final full refresh to clear
        self.set_update_mode(EPD_MODE_FULL)

        # Clear display
        if self.fb_mem:
            self.fb_mem.seek(0)
            self.fb_mem.write(b"\xff" * len(self.fb_mem))
            self.fb_mem.close()

        if self.fb_fd:
            os.close(self.fb_fd)

        print("Cleanup complete")


def main():
    # Get text file from command line
    text_file = None
    if len(sys.argv) > 1:
        text_file = sys.argv[1]

    # Create and run reader
    reader = EInkReader()
    reader.load_text(text_file)
    reader.run_interactive()


if __name__ == "__main__":
    main()
