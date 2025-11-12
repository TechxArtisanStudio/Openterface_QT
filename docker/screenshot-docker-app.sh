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
}
trap cleanup EXIT

echo -e "${BLUE}📦 Using existing Docker image${NC}"

# Setup virtual display - Xvfb will run inside the container
echo -e "${BLUE}�️  Xvfb will be started inside the container${NC}"
export DISPLAY=:98
echo -e "${GREEN}✅ Display configured: $DISPLAY${NC}"

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
    -e LC_ALL=C.UTF-8 \
    -e LANG=C.UTF-8 \
    --network host \
    --privileged \
    --device /dev/fuse"

# Add volume mount if provided
if [ -n "$VOLUME_MOUNT" ]; then
    DOCKER_RUN_CMD="$DOCKER_RUN_CMD $VOLUME_MOUNT"
fi

# Add the image - let the entrypoint handle app startup
# The entrypoint will install and start the app automatically
DOCKER_RUN_CMD="$DOCKER_RUN_CMD $DOCKER_IMAGE:$DOCKER_TAG"

# Execute the docker run command
CONTAINER_ID=$(eval $DOCKER_RUN_CMD)

echo -e "${GREEN}✅ Container started${NC}"
echo -e "${BLUE}📦 Container ID: ${CONTAINER_ID:0:12}${NC}"

# Wait for app to be ready by monitoring container logs
# Look for the "ready for testing" message from entrypoint
echo -e "${BLUE}⏳ Waiting for app to start (monitoring logs, max 120 seconds)...${NC}"

WAIT_TIME=0
MAX_WAIT=120
APP_READY=false

while [ $WAIT_TIME -lt $MAX_WAIT ]; do
    # Check if container is still running
    if ! docker ps | grep -q $CONTAINER_ID; then
        echo -e "${YELLOW}⚠️  Container stopped after ${WAIT_TIME}s, checking logs...${NC}"
        docker logs $CONTAINER_ID 2>&1 | tail -20 | sed 's/^/   /'
        break
    fi
    
    # Check container logs for the ready message
    if docker logs $CONTAINER_ID 2>&1 | grep -q "ready for testing"; then
        echo -e "${GREEN}✅ App is ready! (detected after ${WAIT_TIME}s)${NC}"
        APP_READY=true
        break
    fi
    
    # Display progress
    printf "\r${BLUE}  ⏳ Progress: %d/%d seconds${NC}" $WAIT_TIME $MAX_WAIT
    sleep 1
    WAIT_TIME=$((WAIT_TIME + 1))
done

echo ""

if [ "$APP_READY" = false ]; then
    echo -e "${YELLOW}⚠️  'Ready' message not detected within ${MAX_WAIT}s timeout${NC}"
    echo -e "${YELLOW}   Proceeding with screenshot anyway...${NC}"
fi

# Give app final 3 seconds to fully render
echo -e "${BLUE}⏳ Giving app 3 seconds to render...${NC}"
sleep 3

# Take the main screenshot
echo -e "${BLUE}📸 Taking screenshot...${NC}"
timestamp=$(date +"%Y%m%d_%H%M%S")
screenshot_jpg="$SCREENSHOTS_DIR/openterface_app_${timestamp}.jpg"

# Take screenshot from inside the container where Xvfb is running
echo -e "${BLUE}📷 Taking screenshot from inside container...${NC}"
screenshot_success=false

# Debug: Check X11 display and window information before taking screenshot
echo -e "${BLUE}🔍 Pre-screenshot diagnostics:${NC}"
echo "   Display: $DISPLAY (inside container)"

# Check if X server is responding inside container
if docker exec $CONTAINER_NAME sh -c "DISPLAY=$DISPLAY xdpyinfo >/dev/null 2>&1"; then
    echo -e "${GREEN}   ✅ X server responding inside container${NC}"
else
    echo -e "${RED}   ❌ X server not responding inside container${NC}"
fi

# Check for active windows
window_count=$(docker exec $CONTAINER_NAME sh -c "DISPLAY=$DISPLAY xwininfo -tree -root 2>/dev/null | grep -c 'child'" || echo "0")
echo "   Active windows: $window_count"

# List all windows
if [ "$window_count" -gt 0 ]; then
    echo -e "${BLUE}   Window list:${NC}"
    docker exec $CONTAINER_NAME sh -c "DISPLAY=$DISPLAY xwininfo -tree -root 2>/dev/null | grep 'child' | head -5" | sed 's/^/     /'
fi

# Check if app window is visible
if docker exec $CONTAINER_NAME pgrep -x "openterfaceQT" >/dev/null 2>&1; then
    echo -e "${BLUE}   App process status: Running${NC}"
    
    # Check app logs for any GUI-related messages
    echo -e "${BLUE}   Recent app logs:${NC}"
    docker exec $CONTAINER_NAME tail -10 /tmp/openterfaceqt.log 2>&1 | sed 's/^/     /' || echo "     No logs available"
    
    # Check for Qt platform plugin messages
    echo -e "${BLUE}   Checking for Qt platform messages:${NC}"
    docker exec $CONTAINER_NAME grep -i "platform\|xcb\|plugin" /tmp/openterfaceqt.log 2>&1 | tail -3 | sed 's/^/     /' || echo "     No platform messages found"
else
    echo -e "${RED}   App process status: Not running${NC}"
fi

# Take screenshot inside container using ImageMagick
echo -e "${BLUE}   Taking screenshot with ImageMagick inside container...${NC}"
# Capture screenshot to temp location inside container
container_screenshot="/tmp/screenshot_${timestamp}.jpg"
if docker exec $CONTAINER_NAME sh -c "DISPLAY=$DISPLAY import -window root -quality 90 $container_screenshot 2>&1"; then
    # Copy screenshot from container to host
    if docker cp "$CONTAINER_NAME:$container_screenshot" "$screenshot_jpg" 2>/dev/null; then
        echo -e "${GREEN}✅ JPG screenshot saved: $screenshot_jpg${NC}"
        screenshot_success=true
        # Clean up inside container
        docker exec $CONTAINER_NAME rm -f "$container_screenshot" 2>/dev/null || true
    else
        echo -e "${RED}❌ Failed to copy screenshot from container${NC}"
    fi
else
    echo -e "${RED}❌ ImageMagick screenshot failed inside container${NC}"
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

# Show window information from inside container
echo -e "${BLUE}🪟 X11 window information (inside container):${NC}"
window_info=$(docker exec $CONTAINER_NAME sh -c "DISPLAY=$DISPLAY xwininfo -tree -root 2>/dev/null | head -10" || echo "Unable to get window information")
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
else
    echo -e "${RED}❌ No screenshot files generated${NC}"
fi

echo ""
echo -e "${GREEN}🎉 Test completed!${NC}"
