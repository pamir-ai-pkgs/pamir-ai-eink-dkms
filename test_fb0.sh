#!/bin/bash
#
# test_fb0.sh - Comprehensive test script for /dev/fb0 framebuffer (e-ink display)
# Tests the 128x250 1bpp monochrome display
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
FB_DEVICE="/dev/fb0"
WIDTH=128
HEIGHT=250
BPP=1
BYTES_PER_LINE=$((WIDTH / 8))  # 16 bytes per line for 128 pixels
TOTAL_BYTES=$((BYTES_PER_LINE * HEIGHT))  # 4000 bytes total

# Test mode
VERBOSE=0
TEST_PATTERN="all"
DELAY=2

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -p|--pattern)
            TEST_PATTERN="$2"
            shift 2
            ;;
        -d|--delay)
            DELAY="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -v, --verbose     Enable verbose output"
            echo "  -p, --pattern     Test pattern: all|checker|lines|solid|random|text"
            echo "  -d, --delay       Delay between tests in seconds (default: 2)"
            echo "  -h, --help        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

verbose_log() {
    if [ $VERBOSE -eq 1 ]; then
        echo -e "${BLUE}[DEBUG]${NC} $1"
    fi
}

# Check if running as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_warning "Not running as root. Some tests may require sudo."
        echo -n "Continue anyway? (y/n): "
        read -r response
        if [ "$response" != "y" ]; then
            exit 1
        fi
    fi
}

# Check if framebuffer device exists
check_fb_device() {
    log_info "Checking framebuffer device..."
    
    if [ ! -c "$FB_DEVICE" ]; then
        log_error "Framebuffer device $FB_DEVICE does not exist!"
        log_info "Available framebuffer devices:"
        ls -la /dev/fb* 2>/dev/null || echo "None found"
        exit 1
    fi
    
    log_success "Framebuffer device $FB_DEVICE exists"
    
    # Check permissions
    if [ -w "$FB_DEVICE" ]; then
        log_success "Write permission OK"
    else
        log_warning "No write permission. Tests will require sudo."
    fi
}

# Get framebuffer information
get_fb_info() {
    log_info "Getting framebuffer information..."
    
    # Try using fbset if available
    if command -v fbset &> /dev/null; then
        verbose_log "Using fbset to query framebuffer"
        fbset -fb $FB_DEVICE -i || true
    fi
    
    # Get info from sysfs
    if [ -d "/sys/class/graphics/fb0" ]; then
        verbose_log "Reading sysfs attributes"
        echo "  Resolution: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo 'unknown')"
        echo "  Bits per pixel: $(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null || echo 'unknown')"
        echo "  Name: $(cat /sys/class/graphics/fb0/name 2>/dev/null || echo 'unknown')"
    fi
    
    # Calculate expected size
    log_info "Expected framebuffer size: $TOTAL_BYTES bytes ($WIDTH x $HEIGHT @ $BPP bpp)"
}

# Create test pattern: all black
create_black_pattern() {
    dd if=/dev/zero of=/tmp/fb_black.bin bs=$TOTAL_BYTES count=1 2>/dev/null
    echo "/tmp/fb_black.bin"
}

# Create test pattern: all white
create_white_pattern() {
    perl -e "print \"\xFF\" x $TOTAL_BYTES" > /tmp/fb_white.bin
    echo "/tmp/fb_white.bin"
}

# Create test pattern: checkerboard
create_checkerboard_pattern() {
    local file="/tmp/fb_checker.bin"
    > "$file"
    
    for ((y=0; y<HEIGHT; y++)); do
        for ((x=0; x<BYTES_PER_LINE; x++)); do
            if [ $(((y / 8 + x) % 2)) -eq 0 ]; then
                printf '\xAA' >> "$file"  # 10101010
            else
                printf '\x55' >> "$file"  # 01010101
            fi
        done
    done
    echo "$file"
}

# Create test pattern: horizontal lines
create_lines_pattern() {
    local file="/tmp/fb_lines.bin"
    > "$file"
    
    for ((y=0; y<HEIGHT; y++)); do
        if [ $((y % 4)) -lt 2 ]; then
            # Black lines
            dd if=/dev/zero bs=$BYTES_PER_LINE count=1 2>/dev/null >> "$file"
        else
            # White lines
            perl -e "print \"\xFF\" x $BYTES_PER_LINE" >> "$file"
        fi
    done
    echo "$file"
}

# Create test pattern: random
create_random_pattern() {
    dd if=/dev/urandom of=/tmp/fb_random.bin bs=$TOTAL_BYTES count=1 2>/dev/null
    echo "/tmp/fb_random.bin"
}

# Create test pattern: simple text/box
create_text_pattern() {
    local file="/tmp/fb_text.bin"
    > "$file"
    
    # Create a simple pattern with a border and cross
    for ((y=0; y<HEIGHT; y++)); do
        for ((x=0; x<BYTES_PER_LINE; x++)); do
            local byte=0x00
            
            # Top and bottom border
            if [ $y -eq 0 ] || [ $y -eq $((HEIGHT-1)) ]; then
                byte=0xFF
            # Left and right border (first and last bits of first/last bytes)
            elif [ $x -eq 0 ]; then
                byte=0x80  # Leftmost pixel
            elif [ $x -eq $((BYTES_PER_LINE-1)) ]; then
                byte=0x01  # Rightmost pixel
            # Center cross
            elif [ $y -eq $((HEIGHT/2)) ] || [ $x -eq $((BYTES_PER_LINE/2)) ]; then
                byte=0xFF
            fi
            
            printf "\\x$(printf %02x $byte)" >> "$file"
        done
    done
    echo "$file"
}

# Write pattern to framebuffer
write_to_fb() {
    local pattern_file="$1"
    local pattern_name="$2"
    
    log_info "Writing $pattern_name pattern to framebuffer..."
    
    # Check file size
    local file_size=$(stat -c%s "$pattern_file")
    if [ $file_size -ne $TOTAL_BYTES ]; then
        log_warning "Pattern file size ($file_size) doesn't match expected size ($TOTAL_BYTES)"
    fi
    
    # Write to framebuffer
    if [ -w "$FB_DEVICE" ]; then
        cat "$pattern_file" > "$FB_DEVICE"
    else
        sudo bash -c "cat '$pattern_file' > '$FB_DEVICE'"
    fi
    
    if [ $? -eq 0 ]; then
        log_success "Successfully wrote $pattern_name pattern"
    else
        log_error "Failed to write $pattern_name pattern"
        return 1
    fi
}

# Test a specific pattern
test_pattern() {
    local pattern_type="$1"
    
    case $pattern_type in
        black)
            local file=$(create_black_pattern)
            write_to_fb "$file" "black"
            ;;
        white)
            local file=$(create_white_pattern)
            write_to_fb "$file" "white"
            ;;
        checker)
            local file=$(create_checkerboard_pattern)
            write_to_fb "$file" "checkerboard"
            ;;
        lines)
            local file=$(create_lines_pattern)
            write_to_fb "$file" "horizontal lines"
            ;;
        random)
            local file=$(create_random_pattern)
            write_to_fb "$file" "random"
            ;;
        text)
            local file=$(create_text_pattern)
            write_to_fb "$file" "border and cross"
            ;;
    esac
    
    sleep $DELAY
}

# Performance test
test_performance() {
    log_info "Testing write performance..."
    
    local pattern_file=$(create_random_pattern)
    local start_time=$(date +%s%N)
    
    for i in {1..10}; do
        if [ -w "$FB_DEVICE" ]; then
            cat "$pattern_file" > "$FB_DEVICE"
        else
            sudo bash -c "cat '$pattern_file' > '$FB_DEVICE'"
        fi
    done
    
    local end_time=$(date +%s%N)
    local elapsed=$((($end_time - $start_time) / 1000000))  # Convert to milliseconds
    local avg_time=$(($elapsed / 10))
    
    log_success "Average write time: ${avg_time}ms per frame"
    log_info "Theoretical FPS: $((1000 / $avg_time))"
}

# Clean up temporary files
cleanup() {
    verbose_log "Cleaning up temporary files..."
    rm -f /tmp/fb_*.bin
}

# Main test sequence
main() {
    echo "======================================"
    echo "   E-ink Framebuffer Test Script"
    echo "   Device: $FB_DEVICE"
    echo "   Resolution: ${WIDTH}x${HEIGHT}"
    echo "   Bits per pixel: $BPP"
    echo "======================================"
    echo
    
    # Check prerequisites
    check_root
    check_fb_device
    get_fb_info
    
    echo
    log_info "Starting display tests..."
    echo
    
    # Run tests based on pattern selection
    if [ "$TEST_PATTERN" == "all" ]; then
        log_info "Running all test patterns..."
        test_pattern black
        test_pattern white
        test_pattern checker
        test_pattern lines
        test_pattern text
        test_pattern random
        test_performance
    else
        test_pattern "$TEST_PATTERN"
    fi
    
    # Cleanup
    cleanup
    
    echo
    log_success "All tests completed!"
    echo
    echo "Please check your e-ink display to verify the patterns were displayed correctly."
    echo "If the display didn't update, check:"
    echo "  1. Driver is loaded: lsmod | grep pamir_ai_eink"
    echo "  2. Device tree overlay is applied"
    echo "  3. Hardware connections (SPI, GPIO pins)"
    echo "  4. dmesg for any error messages"
}

# Set up trap to cleanup on exit
trap cleanup EXIT

# Run main function
main