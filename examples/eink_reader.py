#!/usr/bin/env python3
import os
import sys
import textwrap
from PIL import Image, ImageDraw
from eink_common import (
    EInkDisplay,
    EPD_MODE_FULL,
    EPD_MODE_PARTIAL,
)


class EInkReader:
    def __init__(self, fb_device="/dev/fb0"):
        self.display = EInkDisplay(fb_device)

        self.font_size = 12
        self.line_height = 14
        self.margin = 5
        self.text_width = self.display.width - (2 * self.margin)
        self.text_height = self.display.height - (2 * self.margin) - 20
        self.lines_per_page = self.text_height // self.line_height

        self.book_text = ""
        self.pages = []
        self.current_page = 0

        print(f"E-book reader initialized: {self.display.width}x{self.display.height}")

    def load_text(self, text_file):
        if text_file and os.path.exists(text_file):
            with open(text_file, "r", encoding="utf-8") as f:
                self.book_text = f.read()
        else:
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

        self.paginate_text()

    def paginate_text(self):
        wrapper = textwrap.TextWrapper(width=self.text_width // 6)
        lines = []

        for paragraph in self.book_text.split("\n"):
            if paragraph.strip():
                wrapped = wrapper.wrap(paragraph)
                lines.extend(wrapped)
            else:
                lines.append("")

        self.pages = []
        for i in range(0, len(lines), self.lines_per_page):
            page_lines = lines[i : i + self.lines_per_page]
            self.pages.append(page_lines)

        print(f"Loaded {len(self.pages)} pages")

    def render_page(self, page_num):
        if page_num < 0 or page_num >= len(self.pages):
            return

        img = Image.new("1", (self.display.width, self.display.height), 1)
        draw = ImageDraw.Draw(img)

        y_pos = self.margin
        for line in self.pages[page_num]:
            draw.text((self.margin, y_pos), line, fill=0)
            y_pos += self.line_height

        status_y = self.display.height - 18
        draw.line([(0, status_y - 2), (self.display.width - 1, status_y - 2)], fill=0)

        page_info = f"Page {page_num + 1}/{len(self.pages)}"
        draw.text((self.margin, status_y), page_info, fill=0)

        progress = (page_num + 1) / len(self.pages)
        bar_width = self.display.width - 2 * self.margin
        bar_x = self.margin
        bar_y = status_y + 10
        draw.rectangle([(bar_x, bar_y), (bar_x + bar_width, bar_y + 3)], outline=0)
        fill_width = int(bar_width * progress)
        if fill_width > 0:
            draw.rectangle([(bar_x, bar_y), (bar_x + fill_width, bar_y + 3)], fill=0)

        self.display.draw_image(img)

    def next_page(self):
        if self.current_page < len(self.pages) - 1:
            self.current_page += 1
            self.render_page(self.current_page)
            self.display.set_partial_area(
                0, 0, self.display.width, self.display.height - 20
            )
            self.display.update_display()
            return True
        return False

    def prev_page(self):
        if self.current_page > 0:
            self.current_page -= 1
            self.render_page(self.current_page)
            self.display.set_partial_area(
                0, 0, self.display.width, self.display.height - 20
            )
            self.display.update_display()
            return True
        return False

    def run_interactive(self):
        print("E-Ink Reader")
        print("Commands:")
        print("  n/ENTER - Next page")
        print("  p       - Previous page")
        print("  g <num> - Go to page")
        print("  q       - Quit")
        print("")

        self.display.set_update_mode(EPD_MODE_FULL)
        self.render_page(0)
        self.display.update_display()

        self.display.set_update_mode(EPD_MODE_PARTIAL)

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
                            self.display.update_display()
                        else:
                            print(f"Invalid page (1-{len(self.pages)})")
                    except ValueError:
                        print("Invalid page number")

        except KeyboardInterrupt:
            print("\nExiting...")
        finally:
            self.cleanup()

    def cleanup(self):
        self.display.set_update_mode(EPD_MODE_FULL)
        self.display.clear(255)
        self.display.update_display()
        self.display.close()


def main():
    text_file = None
    if len(sys.argv) > 1:
        text_file = sys.argv[1]

    reader = EInkReader()
    reader.load_text(text_file)
    reader.run_interactive()


if __name__ == "__main__":
    main()
