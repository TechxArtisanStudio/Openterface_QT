#!/bin/bash
#
# Simple fullsecho -e "${GREEN}[INFO]${NC} Fullscreen Test via Xvnc + xdotool"
echo -e "${GREEN}[INFO]${NC} Using Xvnc for complete X11 environment (better input handling than Xvfb)"

# Check if Xvnc is available, install if needed
if ! command -v Xvnc &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} Xvnc not found, installing tigervnc-standalone-server..."
    sudo apt-get update -qq
    sudo apt-get install -y tigervnc-standalone-server
    
    if ! command -v Xvnc &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to install Xvnc"
        exit 1
    fi
    echo -e "${GREEN}[INFO]${NC} Xvnc installed successfully"
fi

# Check if xdotool is available
if ! command -v xdotool &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} xdotool not found, installing..."
    sudo apt-get update -qq
    sudo apt-get install -y xdotool
    
    if ! command -v xdotool &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to install xdotool"
        exit 1
    fi
    echo -e "${GREEN}[INFO]${NC} xdotool installed successfully"
fi

# Clean up previous test
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"sing window manager hints
# This directly manipulates the X11 window state without keyboard events
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
TEST_DIR="/tmp/openterface_wm_fullscreen_test"

# Try to find AppImage first (has bundled libraries), fallback to binary
if ls "$BUILD_DIR"/*.AppImage 1> /dev/null 2>&1; then
    APP_PATH=$(ls "$BUILD_DIR"/*.AppImage | head -1)
    echo -e "${GREEN}[INFO]${NC} Using AppImage: $APP_PATH"
else
    APP_PATH="$BUILD_DIR/openterfaceQT"
    echo -e "${GREEN}[INFO]${NC} Using binary: $APP_PATH"
fi

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}[INFO]${NC} Fullscreen Test via Xvnc + xdotool"
echo -e "${GREEN}[INFO]${NC} Using Xvnc for complete X11 environment with better input handling"

# Check if Xvnc is available
if ! command -v Xvnc &> /dev/null; then
    echo -e "${YELLOW}[INFO]${NC} Xvnc not found, installing tigervnc-standalone-server..."
    sudo apt-get update -qq
    sudo apt-get install -y tigervnc-standalone-server
    
    if ! command -v Xvnc &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to install Xvnc"
        exit 1
    fi
    echo -e "${GREEN}[INFO]${NC} Xvnc installed successfully"
fi

# Clean up
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Check app exists
if [ ! -f "$APP_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} App not found: $APP_PATH"
    exit 1
fi

# Start Xvnc server
echo -e "${GREEN}[INFO]${NC} Starting Xvnc server..."
# Xvnc provides a complete VNC/X11 server with proper input handling
Xvnc :99 -geometry 1920x1080 -depth 24 -rfbport 5999 -SecurityTypes None > "$TEST_DIR/xvnc.log" 2>&1 &
XVNC_PID=$!
export DISPLAY=:99

echo -e "${GREEN}[INFO]${NC} Xvnc PID: $XVNC_PID, DISPLAY=:99"
sleep 3

# Start a window manager (important for proper event routing)
if command -v openbox &> /dev/null; then
    openbox &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started openbox window manager (PID: $WM_PID)"
    sleep 1
elif command -v fluxbox &> /dev/null; then
    fluxbox &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started fluxbox window manager (PID: $WM_PID)"
    sleep 1
elif command -v icewm &> /dev/null; then
    icewm &
    WM_PID=$!
    echo -e "${GREEN}[INFO]${NC} Started icewm window manager (PID: $WM_PID)"
    sleep 1
else
    echo -e "${YELLOW}[WARNING]${NC} No window manager available"
    WM_PID=""
fi

# Start application
echo -e "${GREEN}[INFO]${NC} Starting application..."
cd "$BUILD_DIR"
QT_LOGGING_RULES="opf.ui.*=true" \
    "$APP_PATH" > "$TEST_DIR/fullscreen_test.log" 2>&1 &
APP_PID=$!

echo -e "${GREEN}[INFO]${NC} Application PID: $APP_PID"
sleep 4

# Function to send Alt+F11 using xdotool
send_alt_f11() {
    local window_id="$1"
    
    echo -e "${GREEN}[INFO]${NC} Sending Alt+F11 via xdotool..."
    
    # Activate and focus the window (Xvnc handles this much better than Xvfb)
    echo -e "${GREEN}[INFO]${NC} Activating window $window_id..."
    xdotool windowactivate --sync "$window_id" 2>/dev/null || xdotool windowactivate "$window_id" 2>/dev/null || true
    sleep 0.3
    
    xdotool windowfocus --sync "$window_id" 2>/dev/null || xdotool windowfocus "$window_id" 2>/dev/null || true
    sleep 0.3
    
    # Send Alt+F11 key combination
    echo -e "${GREEN}[INFO]${NC} Sending Alt+F11 key event..."
    xdotool key --clearmodifiers --window "$window_id" Alt+F11 2>/dev/null || \
        xdotool key --window "$window_id" Alt+F11 2>/dev/null || true
    
    sleep 0.5
}

# Function to get window geometry using xdotool
get_window_geometry() {
    local window_id="$1"
    xdotool getwindowgeometry "$window_id" 2>/dev/null || echo "Geometry unavailable"
}

# Function to find the MAIN window ID (largest window) using xdotool
find_window_id() {
    # Find all Openterface windows and select the one with largest geometry
    local windows=$(xdotool search --name "Openterface" 2>/dev/null || echo "")
    
    if [ -z "$windows" ]; then
        windows=$(xdotool search --class "openterfaceQT" 2>/dev/null || echo "")
    fi
    
    if [ -z "$windows" ]; then
        echo ""
        return
    fi
    
    # Find the window with the largest area (width * height)
    local max_area=0
    local main_window=""
    
    for wid in $windows; do
        local geom=$(xdotool getwindowgeometry "$wid" 2>/dev/null | grep "Geometry:" | awk '{print $2}')
        if [ -n "$geom" ]; then
            local width=$(echo "$geom" | cut -d'x' -f1)
            local height=$(echo "$geom" | cut -d'x' -f2)
            local area=$((width * height))
            
            if [ $area -gt $max_area ]; then
                max_area=$area
                main_window=$wid
            fi
        fi
    done
    
    echo "$main_window"
}

# Find window - wait for it to be fully initialized
echo -e "${GREEN}[INFO]${NC} Looking for application window..."
sleep 3

for i in {1..10}; do
    WINDOW_ID=$(find_window_id)
    
    if [ -n "$WINDOW_ID" ]; then
        # Check if window has proper geometry (should be at least 800x600)
        GEOM=$(get_window_geometry "$WINDOW_ID" | grep "Geometry:" | awk '{print $2}' || echo "0x0")
        WIDTH=$(echo "$GEOM" | cut -d'x' -f1)
        HEIGHT=$(echo "$GEOM" | cut -d'x' -f2)
        
        if [ "$WIDTH" -gt 800 ] && [ "$HEIGHT" -gt 600 ]; then
            echo -e "${GREEN}[INFO]${NC} Window found and properly initialized: ${WIDTH}x${HEIGHT} (attempt $i)"
            break
        else
            echo -e "${YELLOW}[INFO]${NC} Window found but too small (${WIDTH}x${HEIGHT}), waiting... (attempt $i)"
            WINDOW_ID=""
            sleep 2
        fi
    else
        echo -e "${YELLOW}[INFO]${NC} Window not found yet, waiting... (attempt $i)"
        sleep 2
    fi
done

if [ -n "$WINDOW_ID" ]; then
    echo -e "${GREEN}[INFO]${NC} Found window ID: $WINDOW_ID"
else
    # Even if geometry check failed, try to find ANY window
    WINDOW_ID=$(find_window_id)
    
    if [ -n "$WINDOW_ID" ]; then
        echo -e "${YELLOW}[INFO]${NC} Found window ID (small geometry, testing anyway): $WINDOW_ID"
    fi
fi

if [ -n "$WINDOW_ID" ]; then
    # Take screenshots
    scrot "$TEST_DIR/01_initial.png" 2>/dev/null || echo "scrot not available"
    
    # Get initial geometry
    GEOM_INITIAL=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Initial geometry:\n$GEOM_INITIAL"
    
    # Test 1: Send Alt+F11 to toggle ON
    echo -e "\n${GREEN}[INFO]${NC} === Test 1: Send Alt+F11 (toggle fullscreen ON) ==="
    send_alt_f11 "$WINDOW_ID"
    sleep 2
    
    scrot "$TEST_DIR/02_after_alt_f11_on.png" 2>/dev/null || true
    GEOM_AFTER_ON=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Geometry after Alt+F11 ON:\n$GEOM_AFTER_ON"
    
    # Test 2: Send Alt+F11 again to toggle OFF
    echo -e "\n${GREEN}[INFO]${NC} === Test 2: Send Alt+F11 (toggle fullscreen OFF) ==="
    send_alt_f11 "$WINDOW_ID"
    sleep 2
    
    scrot "$TEST_DIR/03_after_alt_f11_off.png" 2>/dev/null || true
    GEOM_AFTER_OFF=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Geometry after Alt+F11 OFF:\n$GEOM_AFTER_OFF"
    
    # Test 3: Send Alt+F11 one more time
    echo -e "\n${GREEN}[INFO]${NC} === Test 3: Send Alt+F11 (toggle ON again) ==="
    send_alt_f11 "$WINDOW_ID"
    sleep 2
    
    scrot "$TEST_DIR/04_after_alt_f11_final.png" 2>/dev/null || true
    GEOM_FINAL=$(get_window_geometry "$WINDOW_ID")
    echo -e "${GREEN}[INFO]${NC} Final geometry:\n$GEOM_FINAL"
    
else
    echo -e "${RED}[ERROR]${NC} Could not find application window"
fi

# Wait for events to be logged
sleep 2

# Cleanup
echo -e "\n${GREEN}[INFO]${NC} Cleaning up..."
kill $APP_PID 2>/dev/null || true
[ -n "$WM_PID" ] && kill $WM_PID 2>/dev/null || true
kill $XVNC_PID 2>/dev/null || true
sleep 1
# Force kill if still running
kill -9 $XVNC_PID 2>/dev/null || true

# Results
echo -e "\n${GREEN}[INFO]${NC} ==================== RESULTS ===================="
echo -e "${GREEN}[INFO]${NC} Log: $TEST_DIR/fullscreen_test.log"
echo -e "${GREEN}[INFO]${NC} Screenshots: $TEST_DIR/*.png"

# Check for shortcut activation
echo -e "\n${YELLOW}[CHECK]${NC} Looking for Alt+F11 activation..."
if grep -q "Alt+F11 SHORTCUT ACTIVATED" "$TEST_DIR/fullscreen_test.log"; then
    echo -e "${GREEN}[SUCCESS]${NC} ✓ Shortcut was activated!"
else
    echo -e "${YELLOW}[WARNING]${NC} ✗ Shortcut not activated"
fi

# Check for fullscreen events
echo -e "\n${YELLOW}[CHECK]${NC} Looking for fullscreen events..."
grep -i "fullscreen\|windowstate\|window.*state" "$TEST_DIR/fullscreen_test.log" | tail -20 || echo "No fullscreen events found"

echo -e "\n${GREEN}[INFO]${NC} Test complete!"
echo -e "${GREEN}[INFO]${NC} To review logs:"
echo -e "  cat $TEST_DIR/fullscreen_test.log | grep -i fullscreen"
