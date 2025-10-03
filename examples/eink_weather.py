#!/usr/bin/env python3
import os
import sys
import time
from datetime import datetime
from PIL import Image, ImageDraw
from eink_common import EInkDisplay, EPD_MODE_FULL, EPD_MODE_BASE_MAP

try:
    import requests
except ImportError:
    print("Please install requests: pip install requests")
    sys.exit(1)


class WeatherDashboard:
    def __init__(self, fb_device="/dev/fb0", api_key=None):
        self.display = EInkDisplay(fb_device)
        self.api_key = api_key or os.environ.get("OPENWEATHER_API_KEY", "demo")
        self.base_image = None
        print(
            f"Weather dashboard initialized: {self.display.width}x{self.display.height}"
        )

    def create_base_map(self):
        self.base_image = Image.new("1", (self.display.width, self.display.height), 1)
        draw = ImageDraw.Draw(self.base_image)

        # Calculate dynamic layout sections
        header_height = self.display.height // 6  # ~16%
        section_height = (
            self.display.height - header_height
        ) // 3  # Divide remaining into 3 sections

        # Header section
        draw.rectangle([(0, 0), (self.display.width - 1, header_height - 2)], fill=0)
        draw.text((5, 2), "Weather Dashboard", fill=1)

        # Section dividers
        y_current = header_height
        y_forecast = header_height + section_height
        y_details = header_height + 2 * section_height

        draw.line([(0, y_current), (self.display.width - 1, y_current)], fill=0)
        draw.line([(0, y_forecast), (self.display.width - 1, y_forecast)], fill=0)
        draw.line([(0, y_details), (self.display.width - 1, y_details)], fill=0)

        # Section labels
        draw.text((5, y_current + 2), "Current:", fill=0)
        draw.text((5, y_forecast + 2), "Forecast:", fill=0)
        draw.text((5, y_details + 2), "Details:", fill=0)

        # Store section positions for later use
        self.sections = {
            "header": header_height,
            "current": y_current,
            "forecast": y_forecast,
            "details": y_details,
            "section_height": section_height,
        }

        self.send_base_map()

    def send_base_map(self):
        if not self.base_image:
            return

        self.display.draw_image(self.base_image)
        self.display.set_base_map()

    def get_weather_data(self, city="San Francisco"):
        if self.api_key == "demo":
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
            url = f"http://api.openweathermap.org/data/2.5/weather?q={city}&appid={self.api_key}&units=metric"
            response = requests.get(url, timeout=5)
            data = response.json()

            weather = {
                "temp": round(data["main"]["temp"]),
                "description": data["weather"][0]["description"].title(),
                "humidity": data["main"]["humidity"],
                "wind_speed": round(data["wind"]["speed"] * 3.6),
                "forecast": [],
            }

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
            print(f"Weather API error: {e}")
            return None

    def update_weather_display(self, weather_data):
        if not weather_data:
            return

        overlay = Image.new("1", (self.display.width, self.display.height), 1)
        draw = ImageDraw.Draw(overlay)

        # Current weather section
        current_y = self.sections["current"] + 12
        temp_str = f"{weather_data['temp']}°C"
        draw.text((5, current_y), temp_str, fill=0)
        draw.text((5, current_y + 12), weather_data["description"], fill=0)

        # Forecast section
        forecast_y = self.sections["forecast"] + 12
        y_increment = min(
            12, (self.sections["section_height"] - 15) // 3
        )  # Dynamic spacing
        y_pos = forecast_y
        for i, forecast in enumerate(
            weather_data["forecast"][:3]
        ):  # Limit to 3 forecasts
            text = f"{forecast['day']}: {forecast['high']}/{forecast['low']}°"
            draw.text((5, y_pos), text, fill=0)
            y_pos += y_increment

        # Details section
        details_y = self.sections["details"] + 12
        draw.text((5, details_y), f"Humidity: {weather_data['humidity']}%", fill=0)

        # Add wind speed on same line if space permits, otherwise next line
        if self.display.width > 200:
            draw.text(
                (self.display.width // 2, details_y),
                f"Wind: {weather_data['wind_speed']} km/h",
                fill=0,
            )
        else:
            draw.text(
                (5, details_y + 12), f"Wind: {weather_data['wind_speed']} km/h", fill=0
            )

        # Add update time in top right corner if space permits
        now = datetime.now().strftime("%H:%M")
        if self.display.width > 150:
            draw.text((self.display.width - 40, 2), now, fill=1)  # In header bar

        combined = Image.new("1", (self.display.width, self.display.height), 1)
        combined.paste(self.base_image, (0, 0))

        for y in range(self.display.height):
            for x in range(self.display.width):
                if overlay.getpixel((x, y)) == 0:
                    combined.putpixel((x, y), 0)

        self.display.draw_image(combined)

    def run(self):
        print("Weather Dashboard Starting...")
        print(f"Display: {self.display.width}x{self.display.height}")
        print("Press Ctrl+C to exit")

        self.display.set_update_mode(EPD_MODE_FULL)
        self.create_base_map()
        self.display.update_display()
        self.display.set_update_mode(EPD_MODE_BASE_MAP)

        try:
            while True:
                weather = self.get_weather_data()

                if weather:
                    self.update_weather_display(weather)
                    self.display.update_display()
                    print(f"Updated: {weather['temp']}°C, {weather['description']}")

                time.sleep(600)

        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self.cleanup()

    def cleanup(self):
        self.display.set_update_mode(EPD_MODE_FULL)
        self.display.clear(255)
        self.display.update_display()
        self.display.close()


def main():
    api_key = None
    if len(sys.argv) > 1:
        api_key = sys.argv[1]

    dashboard = WeatherDashboard(api_key=api_key)
    dashboard.run()


if __name__ == "__main__":
    main()
