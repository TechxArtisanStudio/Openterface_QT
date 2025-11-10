#!/bin/bash

# Simple Docker App Screenshot Script
# Starts Docker container, waits 10 seconds, takes screenshot

set -e

# Configuration - can be overridden by environment variables
DOCKER_IMAGE="${DOCKER_IMAGE:-openterface-test-shared}"
DOCKER_TAG="${DOCKER_TAG:-screenshot-test}"
DOCKERFILE_PATH="docker/testos/Dockerfile.ubuntu-test-shared"
SCREENSHOTS_DIR="${SCREENSHOTS_DIR:-app-screenshots}"
CONTAINER_NAME="openterface-screenshot-test"
GITHUB_TOKEN="${GITHUB_TOKEN:-}"
VOLUME_MOUNT="${VOLUME_MOUNT:-}"
INSTALL_TYPE="${INSTALL_TYPE:-deb}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}🚀 Starting Openterface Docker App Screenshot Test${NC}"
echo "================================================"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}🧹 Cleaning up resources...${NC}"
    docker stop $CONTAINER_NAME 2>/dev/null || true
    docker rm $CONTAINER_NAME 2>/dev/null || true
    if [ ! -z "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Check if Docker image exists, if not build it
if ! docker images | grep -q "$DOCKER_IMAGE.*$DOCKER_TAG"; then
    echo -e "${BLUE}🔨 Building Docker image...${NC}"
    if [ ! -f "$DOCKERFILE_PATH" ]; then
        echo -e "${RED}❌ Dockerfile does not exist: $DOCKERFILE_PATH${NC}"
        exit 1
    fi
    
    # Force rebuild to include latest fixes
    echo -e "${BLUE}💡 Rebuilding image to include latest fixes...${NC}"
    docker build --no-cache \
        --build-arg INSTALL_TYPE=$INSTALL_TYPE \
        --build-arg GITHUB_TOKEN=$GITHUB_TOKEN \
        -f "$DOCKERFILE_PATH" -t "$DOCKER_IMAGE:$DOCKER_TAG" docker/
    echo -e "${GREEN}✅ Image build completed${NC}"
else
    echo -e "${BLUE}📦 Using existing Docker image${NC}"
    # Check if we should rebuild (optional)
    echo -e "${YELLOW}💡 Tip: If you encounter issues, you can delete the image and rebuild: docker rmi $DOCKER_IMAGE:$DOCKER_TAG${NC}"
fi

# Install virtual display and ImageMagick dependencies if needed
if ! command -v Xvfb >/dev/null 2>&1 || ! command -v import >/dev/null 2>&1; then
    echo -e "${BLUE}📦 Installing virtual display and image processing dependencies...${NC}"
    sudo apt-get update -y >/dev/null
    sudo apt-get install -y xvfb imagemagick x11-utils xdotool wmctrl >/dev/null
    echo -e "${GREEN}✅ Dependencies installation completed${NC}"
fi

# Setup virtual display
echo -e "${BLUE}🖥️  Setting up virtual display...${NC}"
export DISPLAY=:98
Xvfb :98 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/dev/null 2>&1 &
XVFB_PID=$!
sleep 3

# Verify X server
if ! DISPLAY=:98 xdpyinfo >/dev/null 2>&1; then
    echo -e "${RED}❌ Virtual display startup failed${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Virtual display started successfully ($DISPLAY)${NC}"

# Create screenshots directory
mkdir -p $SCREENSHOTS_DIR

# Start Docker container with the app
echo -e "${BLUE}🐳 Starting Docker container and app...${NC}"

# Build the docker run command with optional volume mount and environment variables
DOCKER_RUN_CMD="docker run -d \
    --name $CONTAINER_NAME \
    -e DISPLAY=$DISPLAY \
    -e GITHUB_TOKEN=$GITHUB_TOKEN \
    -e INSTALL_TYPE=$INSTALL_TYPE \
    -e QT_X11_NO_MITSHM=1 \
    -e QT_QPA_PLATFORM=xcb \
    -e QT_PLUGIN_PATH=/usr/lib/qt6/plugins \
    -e QML2_IMPORT_PATH=/usr/lib/qt6/qml \
    -e LC_ALL=C.UTF-8 \
    -e LANG=C.UTF-8 \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    --network host \
    --privileged"

# Add volume mount if provided
if [ -n "$VOLUME_MOUNT" ]; then
    DOCKER_RUN_CMD="$DOCKER_RUN_CMD $VOLUME_MOUNT"
fi

# Add the image and command to launch the app
# Keep container running in foreground by tailing logs
DOCKER_RUN_CMD="$DOCKER_RUN_CMD $DOCKER_IMAGE:$DOCKER_TAG \
    bash -c 'export DISPLAY=$DISPLAY QT_X11_NO_MITSHM=1 QT_QPA_PLATFORM=xcb && \
    set -x && \
    echo \"\" && \
    echo \"========================================\" && \
    echo \"Qt Dependencies Check\" && \
    echo \"========================================\" && \
    /usr/local/bin/check-qt-deps.sh && \
    echo \"\" && \
    echo \"========================================\" && \
    echo \"Starting openterfaceQT application\" && \
    echo \"========================================\" && \
    /usr/local/bin/openterfaceQT 2>&1'"

# Execute the docker run command
CONTAINER_ID=$(eval $DOCKER_RUN_CMD)

echo -e "${GREEN}✅ Container started${NC}"
echo -e "${BLUE}📱 App is initializing...${NC}"

# Wait for app to start and stabilize
echo -e "${YELLOW}⏱️  Waiting for app to launch and UI to render:${NC}"

# Check application startup with window detection
echo -e "${BLUE}🔍 Detecting application window...${NC}"
start_time=$(date +%s)
timeout=120  # Increased to 120 seconds to allow for installation and startup
window_detected=false
window_id=""

while [ $(($(date +%s) - start_time)) -lt $timeout ]; do
    # Check for running app process
    if docker exec $CONTAINER_NAME ps aux 2>/dev/null | grep -E "[o]penterfaceQT" | grep -v grep > /dev/null 2>&1; then
        echo -e "${GREEN}✅ App process is running${NC}"
        
        # Try to detect window from host X11
        if command -v xdotool >/dev/null 2>&1 || command -v wmctrl >/dev/null 2>&1; then
            DISPLAY=:98 xdotool search --class "openterface\|QT\|qt-" 2>/dev/null | head -1 > /tmp/window_id.txt && \
            window_id=$(cat /tmp/window_id.txt 2>/dev/null)
            
            if [ -n "$window_id" ] && [ "$window_id" != "0" ]; then
                echo -e "${GREEN}✅ Application window detected (ID: $window_id)${NC}"
                window_detected=true
                break
            fi
        fi
        
        # Alternative: check X11 window tree
        if ! [ "$window_detected" = true ]; then
            window_count=$(DISPLAY=:98 xwininfo -tree -root 2>/dev/null | grep -iE "openterface|openterface.*\()" | wc -l)
            if [ "$window_count" -gt 0 ]; then
                echo -e "${GREEN}✅ Application window detected in X11 tree${NC}"
                window_detected=true
                break
            fi
        fi
    fi
    
    elapsed=$(($(date +%s) - start_time))
    printf "\r${YELLOW}⏳ Waiting for app window... (%d/%d seconds)${NC}" $elapsed $timeout
    sleep 1
done
echo ""

# Determine wait time based on detection
if [ "$window_detected" = true ]; then
    echo -e "${GREEN}✅ Window detected, waiting 10 seconds for full UI render and startup${NC}"
    for i in {10..1}; do
        printf "\r${YELLOW}⏳ Rendering: %d seconds${NC}" $i
        sleep 1
    done
    echo ""
else
    echo -e "${YELLOW}⚠️  Window not detected, waiting 30 seconds for UI initialization and dependencies${NC}"
    for i in {30..1}; do
        printf "\r${YELLOW}⏳ Initializing: %d seconds${NC}" $i
        sleep 1
    done
    echo ""
fi

# Additional diagnostic before screenshot
echo -e "${BLUE}🔍 Pre-screenshot diagnostics:${NC}"
echo -e "${BLUE}  📋 Container logs (last 20 lines):${NC}"
docker logs --tail 20 $CONTAINER_NAME 2>&1 | sed 's/^/    /'

echo ""
echo -e "${BLUE}  🔍 Running processes in container:${NC}"
docker exec $CONTAINER_NAME ps aux 2>/dev/null | grep -E "openterface|qt|Qt" | sed 's/^/    /'

echo ""
echo -e "${BLUE}  🪟 X11 active windows:${NC}"
DISPLAY=:98 xwininfo -tree -root 2>/dev/null | head -20 | sed 's/^/    /'

# Take the main screenshot
echo -e "${BLUE}📸 Taking screenshot...${NC}"
timestamp=$(date +"%Y%m%d_%H%M%S")
screenshot_jpg="$SCREENSHOTS_DIR/openterface_app_${timestamp}.jpg"

# Use ImageMagick import for reliable JPG screenshot capture
echo -e "${BLUE}📷 Taking screenshot with ImageMagick (PNG/JPG)...${NC}"
screenshot_success=false

# ImageMagick import is the most reliable method for this virtual display setup
if command -v import >/dev/null 2>&1; then
    if DISPLAY=:98 import -window root -quality 90 "$screenshot_jpg" 2>/dev/null; then
        echo -e "${GREEN}✅ JPG screenshot saved: $screenshot_jpg${NC}"
        screenshot_success=true
    else
        echo -e "${RED}❌ ImageMagick screenshot failed${NC}"
    fi
else
    echo -e "${RED}❌ ImageMagick import command not available${NC}"
    echo -e "${YELLOW}💡 Please install ImageMagick: sudo apt-get install imagemagick${NC}"
fi

if [ "$screenshot_success" = true ]; then
    echo -e "${GREEN}✅ Screenshot generated successfully${NC}"
    
    # Analyze screenshot
    if [ -f "$screenshot_jpg" ] && command -v identify >/dev/null 2>&1; then
        filesize=$(ls -lh "$screenshot_jpg" | awk '{print $5}')
        dimensions=$(identify "$screenshot_jpg" | awk '{print $3}')
        # Calculate mean color using convert and statistics
        mean_value=$(convert "$screenshot_jpg" -format "%[fx:mean*65535]" info: 2>/dev/null | awk '{print int($1)}' || echo "0")
        
        echo -e "${BLUE}📊 JPG screenshot analysis:${NC}"
        echo "   File size: $filesize"
        echo "   Image dimensions: $dimensions"
        echo "   Average color value: $mean_value"
        
        if [ "$mean_value" -gt 1000 ]; then
            echo -e "${GREEN}   Status: ✅ Rich app content detected${NC}"
        elif [ "$mean_value" -gt 100 ]; then
            echo -e "${YELLOW}   Status: ⚠️  Basic content detected${NC}"
        else
            echo -e "${RED}   Status: ❌ Screenshot may be blank${NC}"
        fi
    fi
else
    echo -e "${RED}❌ All screenshot methods failed${NC}"
fi

# Analyze all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR/*.jpg 2>/dev/null)" ]; then
    echo -e "${BLUE}📊 All screenshots analysis:${NC}"
    for img in $SCREENSHOTS_DIR/*.jpg; do
        if [ -f "$img" ]; then
            filename=$(basename "$img")
            if command -v identify >/dev/null 2>&1; then
                filesize=$(ls -lh "$img" | awk '{print $5}')
                dimensions=$(identify "$img" | awk '{print $3}' | head -1)
                # Calculate mean color using convert and statistics
                mean_value=$(convert "$img" -format "%[fx:mean*65535]" info: 2>/dev/null | awk '{print int($1)}' || echo "0")
                
                echo -e "${BLUE}   📸 $filename:${NC}"
                echo "      Size: $filesize | Dimensions: $dimensions | Average: $mean_value"
                
                if [ "$mean_value" -gt 1000 ]; then
                    echo -e "${GREEN}      Status: ✅ Rich content detected${NC}"
                elif [ "$mean_value" -gt 100 ]; then
                    echo -e "${YELLOW}      Status: ⚠️  Basic content detected${NC}"
                else
                    echo -e "${RED}      Status: ❌ Screenshot may be blank${NC}"
                fi
            fi
        fi
    done
fi

# Show container status and logs
echo -e "${BLUE}🔍 Container status check:${NC}"
if docker ps | grep -q $CONTAINER_ID; then
    echo -e "${GREEN}✅ Container is running${NC}"
    
    # Show recent logs
    echo -e "${BLUE}📋 Container logs (last 10 lines):${NC}"
    docker logs --tail 10 $CONTAINER_ID 2>&1 | sed 's/^/   /'
    
    # Check processes
    echo -e "${BLUE}🔍 App process check:${NC}"
    process_count=$(docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | wc -l)
    if [ $process_count -gt 0 ]; then
        echo -e "${GREEN}   ✅ Found $process_count related processes${NC}"
        docker exec $CONTAINER_NAME ps aux | grep -E "openterface|Qt|qt" | grep -v grep | sed 's/^/   /'
    else
        echo -e "${YELLOW}   ⚠️  No obvious app processes found${NC}"
    fi
else
    echo -e "${RED}❌ Container has exited${NC}"
    echo -e "${BLUE}📋 Container exit logs:${NC}"
    docker logs $CONTAINER_ID 2>&1 | sed 's/^/   /'
fi

# Show window information
echo -e "${BLUE}🪟 X11 window information:${NC}"
window_info=$(DISPLAY=:98 xwininfo -tree -root 2>/dev/null | head -10 || echo "Unable to get window information")
if echo "$window_info" | grep -q "child"; then
    echo -e "${GREEN}   ✅ Active windows detected${NC}"
    echo "$window_info" | grep "child" | head -3 | sed 's/^/   /'
else
    echo -e "${YELLOW}   ⚠️  No active windows detected${NC}"
fi

# Summary
echo ""
echo -e "${BLUE}📋 Test Summary:${NC}"
echo "================================================"
echo "Container Image: $DOCKER_IMAGE:$DOCKER_TAG"
echo "Container Name: $CONTAINER_NAME"
echo "Display Environment: $DISPLAY"
echo "Screenshot Directory: $SCREENSHOTS_DIR/"
echo ""

# List all screenshots
if [ -d "$SCREENSHOTS_DIR" ] && [ "$(ls -A $SCREENSHOTS_DIR 2>/dev/null)" ]; then
    echo -e "${GREEN}📸 Generated screenshot files:${NC}"
    ls -lh $SCREENSHOTS_DIR/ | grep -v "^total" | sed 's/^/   /'
    echo ""
    
    # Show specific viewing commands for main screenshots
    main_jpg=$(ls $SCREENSHOTS_DIR/openterface_app_*[0-9].jpg 2>/dev/null | head -1)
    
    if [ -n "$main_jpg" ]; then
        echo -e "${BLUE}💡 View screenshot commands:${NC}"
        echo "   display $main_jpg"
        echo "   eog $main_jpg"
        echo "   firefox $main_jpg"
    fi
    
    # Count JPG files
    jpg_count=$(ls $SCREENSHOTS_DIR/*.jpg 2>/dev/null | wc -l)
    
    echo ""
    echo -e "${GREEN}📈 Screenshot Statistics:${NC}"
    echo "   JPG files: $jpg_count files"
    
    # Validate screenshot quality
    echo ""
    echo -e "${BLUE}📊 Screenshot Content Validation:${NC}"
    
    valid_screenshot_count=0
    has_blank_screenshot=false
    
    for img in $SCREENSHOTS_DIR/*.jpg; do
        if [ -f "$img" ]; then
            filesize=$(stat -f%z "$img" 2>/dev/null || stat -c%s "$img" 2>/dev/null || echo "0")
            filename=$(basename "$img")
            
            # Check file size - should be > 50KB for meaningful content
            if [ "$filesize" -gt 51200 ]; then
                echo -e "${GREEN}   ✅ $filename: $filesize bytes (VALID)${NC}"
                valid_screenshot_count=$((valid_screenshot_count + 1))
            else
                echo -e "${RED}   ❌ $filename: $filesize bytes (TOO SMALL - Likely blank/black)${NC}"
                has_blank_screenshot=true
            fi
        fi
    done
    
    echo ""
    if [ "$valid_screenshot_count" -gt 0 ] && [ "$has_blank_screenshot" = false ]; then
        echo -e "${GREEN}📈 Test Result: PASSED ✅${NC}"
        echo "   All $jpg_count screenshots contain valid content (>50KB)"
    else
        if [ "$valid_screenshot_count" -eq 0 ]; then
            echo -e "${RED}📈 Test Result: FAILED ❌${NC}"
            echo "   No screenshots with valid content found!"
            echo "   Expected: Screenshots >50KB with rendered UI"
            echo "   Got: $jpg_count screenshot(s) with $valid_screenshot_count valid"
            exit 1
        elif [ "$has_blank_screenshot" = true ]; then
            echo -e "${RED}📈 Test Result: FAILED ❌${NC}"
            echo "   Some screenshots are too small (likely blank/black)"
            echo "   Application UI may not be rendering properly"
            exit 1
        fi
    fi
else
    echo -e "${RED}❌ No screenshot files generated${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}🎉 Test completed!${NC}"
