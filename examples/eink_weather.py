#!/usr/bin/env python3
"""
eink_weather.py - Weather Dashboard for Pamir AI E-Ink Driver

This example demonstrates base map mode for complex dashboard layouts
with weather information from OpenWeatherMap API
"""

import os
import sys
import time
import fcntl
import mmap
import struct
from datetime import datetime
from PIL import Image, ImageDraw

try:
    import requests
except ImportError:
    print("Please install requests: pip install requests")
    sys.exit(1)

# IOCTL definitions from pamir-ai-eink.h
EPD_IOC_SET_UPDATE_MODE = 0x40044501  # _IOW('E', 1, int)
EPD_IOC_SET_BASE_MAP = 0x40084506  # _IOW('E', 6, void *)

EPD_MODE_FULL = 0
EPD_MODE_PARTIAL = 1
EPD_MODE_BASE_MAP = 2


class WeatherDashboard:
    def __init__(self, fb_device="/dev/fb0", api_key=None):
        self.fb_device = fb_device
        self.api_key = api_key or os.environ.get("OPENWEATHER_API_KEY", "demo")
        self.width = 128
        self.height = 250
        self.fb_fd = None
        self.fb_mem = None
        self.base_image = None
        self.setup_framebuffer()

    def setup_framebuffer(self):
        """Open and map the framebuffer device"""
        try:
            self.fb_fd = os.open(self.fb_device, os.O_RDWR)

            # Get framebuffer info (simplified - assumes known dimensions)
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
            print(f"Failed to set update mode: {e}")

    def create_base_map(self):
        """Create the static base map with dashboard layout"""
        self.base_image = Image.new(
            "1", (self.width, self.height), 1
        )  # White background
        draw = ImageDraw.Draw(self.base_image)

        # Draw dashboard layout
        # Title bar
        draw.rectangle([(0, 0), (self.width - 1, 25)], fill=0)
        draw.text((5, 5), "Weather Dashboard", fill=1)

        # Divider lines
        draw.line([(0, 30), (self.width - 1, 30)], fill=0)
        draw.line([(0, 100), (self.width - 1, 100)], fill=0)
        draw.line([(0, 170), (self.width - 1, 170)], fill=0)

        # Section labels
        draw.text((5, 35), "Current:", fill=0)
        draw.text((5, 105), "Forecast:", fill=0)
        draw.text((5, 175), "Details:", fill=0)

        # Send base map to display
        self.send_base_map()

    def send_base_map(self):
        """Send the base map image to the display driver"""
        if not self.base_image:
            return

        # Convert PIL image to bytes
        pixels = self.base_image.tobytes()

        # Send to framebuffer
        self.fb_mem.seek(0)
        self.fb_mem.write(pixels)

        # Set as base map
        try:
            fcntl.ioctl(self.fb_fd, EINK_SET_BASE_MAP, 0)
        except Exception as e:
            print(f"Failed to set base map: {e}")

    def get_weather_data(self, city="San Francisco"):
        """Fetch weather data from OpenWeatherMap API"""
        if self.api_key == "demo":
            # Return demo data if no API key
            return {
                "temp": 22,
                "description": "Partly Cloudy",
                "humidity": 65,
                "wind_speed": 12,
                "forecast": [
                    {"day": "Mon", "high": 24, "low": 18},
                    {"day": "Tue", "high": 25, "low": 19},
                    {"day": "Wed", "high": 23, "low": 17},
                ],
            }

        try:
            # Current weather
            url = f"http://api.openweathermap.org/data/2.5/weather?q={city}&appid={self.api_key}&units=metric"
            response = requests.get(url, timeout=5)
            data = response.json()

            weather = {
                "temp": round(data["main"]["temp"]),
                "description": data["weather"][0]["description"].title(),
                "humidity": data["main"]["humidity"],
                "wind_speed": round(data["wind"]["speed"] * 3.6),  # Convert to km/h
                "forecast": [],
            }

            # Get forecast
            url = f"http://api.openweathermap.org/data/2.5/forecast?q={city}&appid={self.api_key}&units=metric&cnt=3"
            response = requests.get(url, timeout=5)
            data = response.json()

            for item in data["list"][:3]:
                dt = datetime.fromtimestamp(item["dt"])
                weather["forecast"].append(
                    {
                        "day": dt.strftime("%a"),
                        "high": round(item["main"]["temp_max"]),
                        "low": round(item["main"]["temp_min"]),
                    }
                )

            return weather

        except Exception as e:
            print(f"Failed to get weather data: {e}")
            return None

    def update_weather_display(self, weather_data):
        """Update the dynamic weather information"""
        if not weather_data:
            return

        # Create overlay image for dynamic content
        overlay = Image.new("1", (self.width, self.height), 1)
        draw = ImageDraw.Draw(overlay)

        # Current temperature
        temp_str = f"{weather_data['temp']}°C"
        draw.text((5, 50), temp_str, fill=0)

        # Description
        draw.text((5, 70), weather_data["description"], fill=0)

        # Forecast
        y_pos = 125
        for forecast in weather_data["forecast"]:
            text = f"{forecast['day']}: {forecast['high']}/{forecast['low']}°"
            draw.text((5, y_pos), text, fill=0)
            y_pos += 15

        # Details
        draw.text((5, 195), f"Humidity: {weather_data['humidity']}%", fill=0)
        draw.text((5, 210), f"Wind: {weather_data['wind_speed']} km/h", fill=0)

        # Update time
        now = datetime.now().strftime("%H:%M")
        draw.text((5, 230), f"Updated: {now}", fill=0)

        # Combine base map with overlay and send to display
        combined = Image.new("1", (self.width, self.height), 1)
        combined.paste(self.base_image, (0, 0))

        # Overlay dynamic content (XOR to preserve base map)
        for y in range(self.height):
            for x in range(self.width):
                if overlay.getpixel((x, y)) == 0:  # Black pixel in overlay
                    combined.putpixel((x, y), 0)

        # Send to framebuffer
        pixels = combined.tobytes()
        self.fb_mem.seek(0)
        self.fb_mem.write(pixels)

    def run(self):
        """Main dashboard loop"""
        print("Weather Dashboard Starting...")
        print(f"Display: {self.width}x{self.height}")
        print("Press Ctrl+C to exit")

        # Initial full refresh
        self.set_update_mode(EPD_MODE_FULL)

        # Create and set base map
        self.create_base_map()

        # Switch to base map mode
        self.set_update_mode(UPDATE_MODE_BASE_MAP)

        try:
            while True:
                # Get weather data
                weather = self.get_weather_data()

                if weather:
                    # Update display
                    self.update_weather_display(weather)
                    print(f"Updated: {weather['temp']}°C, {weather['description']}")

                # Wait 10 minutes before next update
                time.sleep(600)

        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self.cleanup()

    def cleanup(self):
        """Clean up resources"""
        # Clear base map
        try:
            fcntl.ioctl(self.fb_fd, EINK_CLEAR_BASE_MAP, 0)
        except:
            pass

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
    # Check for API key
    api_key = None
    if len(sys.argv) > 1:
        api_key = sys.argv[1]

    # Create and run dashboard
    dashboard = WeatherDashboard(api_key=api_key)
    dashboard.run()


if __name__ == "__main__":
    main()
