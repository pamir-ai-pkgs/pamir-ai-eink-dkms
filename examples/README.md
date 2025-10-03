# Pamir AI E-Ink Display Examples

This directory contains comprehensive examples demonstrating all features and update modes of the Pamir AI E-Ink display driver.

## Overview

Each example showcases different aspects of the e-ink display technology and demonstrates best practices for:
- **Update Mode Selection**: When to use full, partial, or base map modes
- **Byte Alignment**: Proper handling of x-coordinate and width requirements (multiples of 8)
- **Ghosting Prevention**: Strategic full refreshes between partial updates

## Examples

### 1. Real-Time Clock (`eink_clock.c`)

A digital and analog clock display that updates every minute.

**Features:**
- Large 7-segment style digits for time display
- Analog clock face with hour and minute hands
- Date and day of week display
- Automatic full refresh every hour to prevent ghosting
- Partial updates for minute changes (minimal flashing)

**Update Modes Used:**
- **Full Update**: Every hour and on startup
- **Partial Update**: Every minute for time changes

**Compile & Run:**
```bash
make eink_clock
sudo ./eink_clock
```

### 2. Weather Dashboard (`eink_weather.py`)

Real-time weather information display with forecast.

**Features:**
- Current weather conditions with temperature
- Weather icons (ASCII art optimized for e-ink)
- Forecast information
- Multiple independent update regions
- OpenWeatherMap API integration (optional)

**Update Modes Used:**
- **Base Map Mode**: Background template and layout
- **Partial Update**: Weather data updates
- **Full Update**: Hourly refresh to prevent ghosting

**Setup & Run:**
```bash
pip install -r requirements.txt

# With API key (get free key from openweathermap.org):
export OPENWEATHER_API_KEY=your_key_here
export WEATHER_CITY=London
python3 eink_weather.py

# Without API key (demo mode):
python3 eink_weather.py
```

### 3. System Monitor (`eink_monitor.c`)

Real-time system resource monitoring with scrolling graphs.

**Features:**
- CPU usage graph with history
- Memory utilization tracking
- Disk usage monitoring
- Network traffic visualization
- Uptime and system information
- Circular buffer implementation for efficient graph scrolling

**Update Modes Used:**
- **Partial Update**: Individual metric regions
- **Full Update**: Every 60 updates to prevent ghosting

**Compile & Run:**
```bash
make eink_monitor
sudo ./eink_monitor
```

### 4. E-Book Reader (`eink_reader.py`)

Text reader with pagination and navigation.

**Features:**
- Automatic text wrapping and pagination
- Header with book title
- Footer with page numbers
- Interactive navigation (next/prev/goto)
- Support for loading text files
- Optimized text rendering for e-ink

**Update Modes Used:**
- **Full Update**: Page turns for best text quality
- **Partial Update**: Menu and navigation elements

**Run:**
```bash
# With a text file:
python3 eink_reader.py /path/to/book.txt

# With sample text:
python3 eink_reader.py
```

### 5. Basic Demo (`eink_demo.c`)

Original demonstration of basic e-ink operations.

**Features:**
- Simple shapes and text rendering
- All three update modes demonstration
- Basic framebuffer operations

**Compile & Run:**
```bash
make eink_demo
sudo ./eink_demo
```

### 6. Python Demo (`eink_demo.py`)

Python implementation showing PIL integration.

**Features:**
- Image drawing with Pillow library
- Pattern generation
- Counter with partial updates
- Dynamic dimension detection from framebuffer
- Automatic display reset on errors

**Run:**
```bash
pip install -r requirements.txt
python3 eink_demo.py
```

### 7. Display Recovery Tool (`eink_recovery.py`)

Standalone tool to reset and recover a stuck e-ink display.

**Features:**
- Hardware reset functionality
- Display clearing (all white)
- Recovery from stuck states

**Use Cases:**
- Display stuck after failed partial update
- Display showing ghosting or artifacts
- Display not responding to commands
- Emergency display reset

**Run:**
```bash
# Basic recovery
sudo python3 eink_recovery.py

# Specify different framebuffer device
sudo python3 eink_recovery.py /dev/fb0
```

## Building C Examples

Build all C examples at once:
```bash
make all
```

Clean build artifacts:
```bash
make clean
```

Install examples system-wide:
```bash
sudo make install
```

## Python Dependencies

Install all Python dependencies:
```bash
pip install -r requirements.txt
```

Required packages:
- `Pillow`: Image processing and drawing
- `requests`: HTTP requests for weather data

## Update Mode Guidelines

### Full Update Mode
- **When to use**: Initial display, page turns, hourly refreshes
- **Characteristics**: Best image quality, 2-4 seconds, visible flashing
- **Example**: E-book page turns, clock hourly refresh

### Partial Update Mode  
- **When to use**: Small region updates, real-time data
- **Characteristics**: Fast (~500ms), may cause ghosting over time
- **Requirements**: X and width must be multiples of 8
- **Example**: Clock minutes, system monitor graphs

### Base Map Mode
- **When to use**: Setting background templates
- **Characteristics**: Sets both RAM buffers for differential updates
- **Example**: Weather dashboard template

## Best Practices

### 1. Byte Alignment
Always ensure x-coordinates and widths are multiples of 8:
```c
// Correct
area.x = 8;      // Multiple of 8
area.width = 80; // Multiple of 8

// Incorrect
area.x = 10;     // Not aligned!
area.width = 75; // Not aligned!
```

### 2. Ghosting Prevention
Perform full refresh periodically:
```c
if (update_counter % 60 == 0) {
    // Full refresh every 60 partial updates
    perform_full_refresh();
}
```

### 3. Error Handling
Always check IOCTL return values:
```c
if (ioctl(fbfd, EPD_IOC_SET_UPDATE_MODE, &mode) < 0) {
    perror("Error setting update mode");
    // Handle error appropriately
}
```

## Troubleshooting

### Permission Denied
Add your user to the video group:
```bash
sudo usermod -a -G video $USER
# Log out and back in for changes to take effect
```

Or run with sudo:
```bash
sudo ./eink_clock
```

### Driver Not Loaded
Ensure the driver module is loaded:
```bash
sudo modprobe pamir-ai-eink
lsmod | grep pamir_ai_eink
```

### Display Not Updating or Stuck
If the display is stuck or not responding:

1. **Use the recovery tool**:
```bash
sudo python3 eink_recovery.py
```

2. **Check kernel messages for errors**:
```bash
dmesg | tail -20
```

3. **Power cycle and reload driver if needed**:
```bash
sudo modprobe -r pamir-ai-eink
sudo modprobe pamir-ai-eink
```

### Alignment Errors
If you see "X coordinates must be byte-aligned" errors, ensure all x-coordinates and widths are multiples of 8.

## Performance Tips

1. **Batch Updates**: Group multiple small updates into one larger update
2. **Minimize Update Area**: Only update regions that changed
3. **Use Appropriate Mode**: Don't use full refresh for small changes
4. **Cache Static Content**: Use base map mode for unchanging backgrounds
5. **Optimize Refresh Rate**: Balance update frequency with power consumption

## Contributing

When adding new examples:
1. Demonstrate unique use cases or update mode combinations
2. Include clear comments explaining update mode choices
3. Follow byte alignment requirements
4. Add error handling for all operations
5. Update this README with example description

## License

All examples are licensed under GPL v2. See the main LICENSE file for details.

## Support

For issues with examples:
- GitHub Issues: https://github.com/pamir-ai-pkgs/pamir-ai-eink-dkms/issues
- Email: support@pamir.ai
