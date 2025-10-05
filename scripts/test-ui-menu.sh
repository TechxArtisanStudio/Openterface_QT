#!/bin/bash
#
# Simple Interactive UI Test Script
# Runs the Openterface application and simulates clicking File->Preferences
# Uses mouse coordinates for more reliable interaction
#

set -e  # Exit on error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/openterface_ui_test}"
LOG_FILE="$OUTPUT_DIR/openterface_test.log"
SCREEN_RESOLUTION="${SCREEN_RESOLUTION:-1920x1080x24}"
DISPLAY_NUM="${DISPLAY_NUM:-:99}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_step() { echo -e "${BLUE}[STEP]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_dependencies() {
    print_info "Checking dependencies..."
    local missing_deps=()
    for dep in Xvfb scrot xdotool; do
        if ! command -v $dep &> /dev/null; then
            missing_deps+=("$dep")
        fi
    done
    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Install with: sudo apt-get install -y xvfb scrot xdotool"
        exit 1
    fi
    print_info "All dependencies satisfied"
}

find_appimage() {
    local appimage=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -n 1)
    if [ -z "$appimage" ]; then
        print_error "No AppImage found in $BUILD_DIR"
        exit 1
    fi
    chmod +x "$appimage" 2>/dev/null || true
    echo "$appimage"
}

capture_screenshot() {
    local name="$1"
    local filepath="$OUTPUT_DIR/${name}.png"
    sleep 0.5
    DISPLAY=$DISPLAY_NUM scrot "$filepath" 2>/dev/null || true
    if [ -f "$filepath" ]; then
        local size=$(du -h "$filepath" | cut -f1)
        print_info "📸 Screenshot: $name ($size)"
    fi
}

click_at() {
    local x=$1
    local y=$2
    local desc=$3
    print_step "$desc at ($x, $y)"
    DISPLAY=$DISPLAY_NUM xdotool mousemove --sync $x $y
    sleep 0.3
    DISPLAY=$DISPLAY_NUM xdotool click 1
    sleep 0.5
}

send_key() {
    local key=$1
    local desc=$2
    print_step "$desc"
    DISPLAY=$DISPLAY_NUM xdotool key --delay 100 $key
    sleep 0.5
}

main() {
    echo ""
    print_info "=== Openterface UI Interactive Test ==="
    print_info "Testing: File -> Preferences menu"
    echo ""
    
    check_dependencies
    mkdir -p "$OUTPUT_DIR"
    local appimage=$(find_appimage)
    
    print_info "Output: $OUTPUT_DIR"
    print_info "Display: $DISPLAY_NUM"
    
    # Start Xvfb
    print_step "Starting virtual display..."
    Xvfb $DISPLAY_NUM -screen 0 $SCREEN_RESOLUTION &
    local xvfb_pid=$!
    sleep 2
    export DISPLAY=$DISPLAY_NUM
    
    # Start application
    print_step "Launching application..."
    "$appimage" &> "$LOG_FILE" &
    local app_pid=$!
    sleep 4  # Wait for app to fully initialize
    
    # Test sequence with keyboard shortcuts (more reliable)
    print_step "1️⃣  Capturing initial state..."
    capture_screenshot "01_initial_window"
    
    print_step "2️⃣  Opening File menu (Alt+F)..."
    send_key "alt+f" "Pressing Alt+F"
    capture_screenshot "02_file_menu_open"
    
    print_step "3️⃣  Navigating to Preferences..."
    # In most Qt apps, you can navigate menus with arrow keys or press the first letter
    send_key "p" "Pressing 'P' for Preferences"
    sleep 1.5  # Give dialog time to open
    capture_screenshot "03_preferences_dialog"
    
    print_step "4️⃣  Closing dialog..."
    send_key "Escape" "Pressing Escape"
    sleep 1
    capture_screenshot "04_after_close"
    
    # Try clicking menu with mouse (if keyboard didn't work)
    print_step "5️⃣  Testing mouse click on File menu..."
    # Approximate position of File menu (adjust if needed)
    click_at 50 30 "Clicking File menu"
    capture_screenshot "05_file_menu_mouse"
    
    print_step "6️⃣  Closing menu..."
    send_key "Escape" "Pressing Escape"
    capture_screenshot "06_final_state"
    
    # Cleanup
    print_step "Cleaning up..."
    kill $app_pid 2>/dev/null || true
    wait $app_pid 2>/dev/null || true
    kill $xvfb_pid 2>/dev/null || true
    
    # Results
    echo ""
    print_info "=== Test Complete ==="
    local count=$(find "$OUTPUT_DIR" -name "*.png" -type f | wc -l)
    print_info "Captured $count screenshots in: $OUTPUT_DIR"
    echo ""
    find "$OUTPUT_DIR" -name "*.png" -type f -printf "  %f (%s bytes)\n" | sort
    echo ""
    print_info "View screenshots: xdg-open '$OUTPUT_DIR'"
    print_info "View log: cat '$LOG_FILE'"
}

if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
    cat << EOF
Openterface Interactive UI Test Script

Usage: $0

This script simulates clicking File -> Preferences menu and captures screenshots.

Environment Variables:
  OUTPUT_DIR    Output directory (default: /tmp/openterface_ui_test)

Test Sequence:
  1. Launch application
  2. Open File menu (Alt+F)
  3. Select Preferences (P key)
  4. Close dialog (Escape)
  5. Try mouse click on File menu
  6. Capture final state

Dependencies: xvfb, scrot, xdotool
EOF
    exit 0
fi

main
