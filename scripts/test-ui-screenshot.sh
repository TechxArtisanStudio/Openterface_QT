#!/bin/bash
#
# UI Screenshot Test Script
# Runs the Openterface application in a virtual framebuffer and captures a screenshot
# This is useful for CI/CD pipelines and headless testing environments
#

set -e  # Exit on error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp}"
SCREENSHOT_NAME="openterface_screenshot_$(date +%Y%m%d_%H%M%S).png"
SCREENSHOT_PATH="$OUTPUT_DIR/$SCREENSHOT_NAME"
LOG_FILE="$OUTPUT_DIR/openterface_test.log"
RUNTIME_SECONDS="${RUNTIME_SECONDS:-10}"
SCREEN_RESOLUTION="${SCREEN_RESOLUTION:-1920x1080x24}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Functions
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    print_info "Checking dependencies..."
    
    local missing_deps=()
    
    if ! command -v xvfb-run &> /dev/null; then
        missing_deps+=("xvfb")
    fi
    
    if ! command -v scrot &> /dev/null; then
        missing_deps+=("scrot")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Install with: sudo apt-get install -y ${missing_deps[*]}"
        exit 1
    fi
    
    print_info "All dependencies satisfied"
}

find_appimage() {
    print_info "Looking for AppImage in $BUILD_DIR..."
    
    local appimage=$(find "$BUILD_DIR" -maxdepth 1 -name "*.AppImage" -type f | head -n 1)
    
    if [ -z "$appimage" ]; then
        print_error "No AppImage found in $BUILD_DIR"
        print_info "Please build the project first"
        exit 1
    fi
    
    if [ ! -x "$appimage" ]; then
        print_warning "AppImage is not executable, fixing..."
        chmod +x "$appimage"
    fi
    
    print_info "Found AppImage: $(basename "$appimage")"
    echo "$appimage"
}

run_test() {
    local appimage="$1"
    
    print_info "Starting UI test..."
    print_info "Screen resolution: $SCREEN_RESOLUTION"
    print_info "Runtime: ${RUNTIME_SECONDS}s"
    print_info "Screenshot will be saved to: $SCREENSHOT_PATH"
    print_info "Log file: $LOG_FILE"
    
    # Run the application in Xvfb and capture screenshot
    xvfb-run -a -s "-screen 0 $SCREEN_RESOLUTION" bash -c "
        '$appimage' &> '$LOG_FILE' & 
        APP_PID=\$!
        
        # Wait for the specified runtime
        sleep $RUNTIME_SECONDS
        
        # Capture screenshot
        DISPLAY=:99 scrot '$SCREENSHOT_PATH' 2>/dev/null || true
        
        # Kill the application
        kill \$APP_PID 2>/dev/null || true
        wait \$APP_PID 2>/dev/null || true
    "
    
    print_info "Test completed"
}

verify_results() {
    print_info "Verifying results..."
    
    # Check if screenshot was created
    if [ ! -f "$SCREENSHOT_PATH" ]; then
        print_error "Screenshot was not created"
        print_info "Check log file for errors: $LOG_FILE"
        exit 1
    fi
    
    # Check screenshot size
    local size=$(stat -f%z "$SCREENSHOT_PATH" 2>/dev/null || stat -c%s "$SCREENSHOT_PATH" 2>/dev/null)
    if [ "$size" -lt 1000 ]; then
        print_warning "Screenshot file is very small ($size bytes), may be invalid"
    fi
    
    # Get image info
    local img_info=$(file "$SCREENSHOT_PATH")
    print_info "Screenshot details: $img_info"
    
    # Check log for errors
    if grep -qi "error\|crash\|segmentation fault" "$LOG_FILE" 2>/dev/null; then
        print_warning "Errors detected in log file"
    fi
    
    print_info "Screenshot saved: $SCREENSHOT_PATH"
    print_info "Size: $(du -h "$SCREENSHOT_PATH" | cut -f1)"
}

show_summary() {
    echo ""
    echo "======================================"
    echo "UI Screenshot Test Summary"
    echo "======================================"
    echo "Screenshot: $SCREENSHOT_PATH"
    echo "Log file:   $LOG_FILE"
    echo "Status:     ${GREEN}SUCCESS${NC}"
    echo "======================================"
    echo ""
    
    print_info "You can view the screenshot with:"
    echo "  xdg-open '$SCREENSHOT_PATH'"
    echo ""
    print_info "Or check the logs with:"
    echo "  cat '$LOG_FILE'"
}

# Main execution
main() {
    print_info "Openterface UI Screenshot Test"
    echo ""
    
    check_dependencies
    appimage=$(find_appimage)
    run_test "$appimage"
    verify_results
    show_summary
}

# Help message
if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
    cat << EOF
Openterface UI Screenshot Test Script

Usage: $0 [OPTIONS]

This script runs the Openterface application in a virtual framebuffer (Xvfb)
and captures a screenshot after a specified runtime period.

Environment Variables:
  OUTPUT_DIR          Directory for output files (default: /tmp)
  RUNTIME_SECONDS     How long to run before screenshot (default: 10)
  SCREEN_RESOLUTION   Virtual screen resolution (default: 1920x1080x24)

Examples:
  # Basic usage
  $0
  
  # Custom runtime and output directory
  RUNTIME_SECONDS=15 OUTPUT_DIR=./screenshots $0
  
  # Custom screen resolution
  SCREEN_RESOLUTION=1280x720x24 $0

Dependencies:
  - xvfb (X virtual framebuffer)
  - scrot (screenshot utility)

EOF
    exit 0
fi

main
