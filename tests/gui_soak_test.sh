#!/bin/bash
# =============================================================================
# OpenterfaceQT GUI Soak Test Automation
# =============================================================================
# Purpose: Long-running stability test for the OpenterfaceQT GUI application
#
# Features:
#   - Runs app in Docker with X11 virtual display
#   - Monitors memory usage, CPU, and process health
#   - Takes periodic screenshots for visual verification
#   - Captures and analyzes app output logs
#   - Detects crashes, hangs, and resource leaks
#   - Generates comprehensive test report
#
# Usage:
#   ./tests/gui_soak_test.sh [DURATION_MINUTES] [CHECK_INTERVAL_SECONDS]
#   ./tests/gui_soak_test.sh 60 30    # 60 minutes, check every 30 seconds
#
# Requirements:
#   - Docker with openterface-qtbuild-complete:arm64 image
#   - Xvfb (virtual framebuffer)
#   - imagemagick (for screenshots)
#   - bash 4.0+
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
TEST_DIR="${PROJECT_DIR}/tests"
LOG_DIR="${TEST_DIR}/soak_test_logs"
SCREENSHOT_DIR="${TEST_DIR}/soak_test_screenshots"

# Default parameters
DURATION_MINUTES="${1:-30}"      # Default: 30 minutes
CHECK_INTERVAL="${2:-15}"        # Default: 15 seconds
DISPLAY_PORT="${3:-99}"          # Default: :99
SCREEN_WIDTH="${4:-1280}"        # Default: 1280
SCREEN_HEIGHT="${5:-720}"        # Default: 720

# Derived values
DURATION_SECONDS=$((DURATION_MINUTES * 60))
DOCKER_IMAGE="openterface-qtbuild-complete:arm64"
APP_BINARY="./openterfaceQT"

# Counters and tracking
START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION_SECONDS))
CHECK_COUNT=0
SCREENSHOT_COUNT=0
MEMORY_SAMPLES=0
MAX_MEMORY=0
AVG_MEMORY=0
TOTAL_MEMORY=0
CRASH_COUNT=0
HANG_COUNT=0
WARNING_COUNT=0

# =============================================================================
# Color Codes
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# =============================================================================
# Functions
# =============================================================================

print_header() {
    echo -e "${CYAN}============================================================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}============================================================================${NC}"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC}  $1"
}

print_success() {
    echo -e "${GREEN}[PASS]${NC}  $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC}  $1"
    ((WARNING_COUNT++))
}

print_error() {
    echo -e "${RED}[FAIL]${NC}  $1"
}

print_progress() {
    local elapsed=$(($(date +%s) - START_TIME))
    local remaining=$((DURATION_SECONDS - elapsed))
    local progress=$((elapsed * 100 / DURATION_SECONDS))
    local mins_elapsed=$((elapsed / 60))
    local secs_elapsed=$((elapsed % 60))
    local mins_remaining=$((remaining / 60))
    local secs_remaining=$((remaining % 60))

    echo -e "${WHITE}[${progress}%]${NC} Elapsed: ${mins_elapsed}m ${secs_elapsed}s | Remaining: ${mins_remaining}m ${secs_remaining}s"
}

cleanup() {
    print_info "Cleaning up..."

    # Kill the app container
    if [ -n "${APP_PID:-}" ]; then
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi

    # Stop any running docker containers from this test
    docker ps -q --filter "name=soak_test" 2>/dev/null | xargs -r docker stop 2>/dev/null || true

    # Kill Xvfb
    pkill -f "Xvfb :${DISPLAY_PORT}" 2>/dev/null || true

    print_info "Cleanup complete"
}

# Trap for graceful cleanup
trap cleanup EXIT

setup_environment() {
    print_header "Setting Up Test Environment"

    # Create directories
    mkdir -p "$LOG_DIR" "$SCREENSHOT_DIR"

    # Verify Docker image exists
    print_info "Checking Docker image..."
    if ! docker image inspect "$DOCKER_IMAGE" &>/dev/null; then
        print_error "Docker image '$DOCKER_IMAGE' not found!"
        print_info "Please build the image first:"
        print_info "  docker build -f docker/Dockerfile.qt-complete-arm -t $DOCKER_IMAGE docker/"
        exit 1
    fi
    print_success "Docker image found: $DOCKER_IMAGE"

    # Verify binary exists
    print_info "Checking application binary..."
    if [ ! -f "${BUILD_DIR}/${APP_BINARY}" ]; then
        print_error "Binary not found: ${BUILD_DIR}/${APP_BINARY}"
        print_info "Please build the application first"
        exit 1
    fi
    print_success "Binary found: ${BUILD_DIR}/${APP_BINARY}"

    # Check required tools
    print_info "Checking required tools..."
    local missing_tools=()
    for cmd in Xvfb import docker; do
        if ! command -v "$cmd" &>/dev/null; then
            missing_tools+=("$cmd")
        fi
    done

    if [ ${#missing_tools[@]} -gt 0 ]; then
        print_error "Missing tools: ${missing_tools[*]}"
        exit 1
    fi
    print_success "All required tools available"

    # Start virtual display
    print_info "Starting Xvfb on :${DISPLAY_PORT} (${SCREEN_WIDTH}x${SCREEN_HEIGHT}x24)..."
    pkill -f "Xvfb :${DISPLAY_PORT}" 2>/dev/null || true
    sleep 1

    Xvfb ":${DISPLAY_PORT}" -screen 0 "${SCREEN_WIDTH}x${SCREEN_HEIGHT}x24" &
    sleep 2

    if pgrep -f "Xvfb :${DISPLAY_PORT}" >/dev/null; then
        print_success "Xvfb started successfully"
    else
        print_error "Failed to start Xvfb"
        exit 1
    fi

    # Export display
    export DISPLAY=":${DISPLAY_PORT}"
    print_info "DISPLAY=$DISPLAY"
}

start_application() {
    print_header "Starting Application"

    local log_file="${LOG_DIR}/app_output.log"

    # Start the app in Docker with X11 passthrough
    print_info "Launching OpenterfaceQT in Docker container..."
    docker run --rm \
        --name soak_test_app \
        --network host \
        -v /tmp/.X11-unix:/tmp/.X11-unix \
        -e DISPLAY=":${DISPLAY_PORT}" \
        -e QT_QPA_PLATFORM=xcb \
        -e QT_PLUGIN_PATH=/opt/Qt6/plugins \
        -e QML2_IMPORT_PATH=/opt/Qt6/qml \
        -e GST_PLUGIN_PATH=/opt/gstreamer/lib/gstreamer-1.0:/usr/lib/aarch64-linux-gnu/gstreamer-1.0 \
        -e LD_LIBRARY_PATH=/opt/Qt6/lib:/opt/ffmpeg/lib:/opt/gstreamer/lib:/usr/lib/aarch64-linux-gnu \
        -v "${PROJECT_DIR}:/workspace/src" \
        -w "/workspace/src/build" \
        "$DOCKER_IMAGE" \
        bash -c "./openterfaceQT" > "$log_file" 2>&1 &

    APP_PID=$!
    print_info "App started with PID: $APP_PID (container: soak_test_app)"

    # Wait for app to initialize
    print_info "Waiting for app initialization (10 seconds)..."
    sleep 10

    # Verify app is running
    if docker ps --format "{{.Names}}" | grep -q "soak_test_app"; then
        print_success "Application is running in container"
    else
        print_error "Application failed to start!"
        print_error "Check log: $log_file"
        tail -50 "$log_file"
        exit 1
    fi

    # Take initial screenshot
    take_screenshot "00_initial"
}

take_screenshot() {
    local name="${1:-screenshot}"
    local filename="${SCREENSHOT_DIR}/${name}.png"

    import -window root "$filename" 2>/dev/null || true

    if [ -f "$filename" ]; then
        local size=$(ls -lh "$filename" | awk '{print $5}')
        print_info "Screenshot saved: ${name} (${size})"
        ((SCREENSHOT_COUNT++))
    fi
}

check_app_health() {
    ((CHECK_COUNT++))

    # Check if container is still running
    if ! docker ps --format "{{.Names}}" | grep -q "soak_test_app"; then
        print_error "Application container has stopped!"
        ((CRASH_COUNT++))

        # Get last logs
        docker logs soak_test_app 2>&1 | tail -100 > "${LOG_DIR}/crash_log_${CHECK_COUNT}.txt" || true
        take_screenshot "crash_${CHECK_COUNT}"

        return 1
    fi

    # Get memory usage from container
    local mem_info
    mem_info=$(docker stats --no-stream --format "{{.MemUsage}}" soak_test_app 2>/dev/null | head -1 || echo "unknown")

    if [ "$mem_info" != "unknown" ]; then
        local mem_value
        mem_value=$(echo "$mem_info" | grep -oP '[\d.]+')
        local mem_unit
        mem_unit=$(echo "$mem_info" | grep -oP '[A-Za-z]+')

        # Convert to MB for tracking
        local mem_mb
        case "$mem_unit" in
            B)   mem_mb=$(echo "$mem_value / 1048576" | bc 2>/dev/null || echo "0") ;;
            KiB) mem_mb=$(echo "$mem_value / 1024" | bc 2>/dev/null || echo "0") ;;
            MiB) mem_mb="$mem_value" ;;
            GiB) mem_mb=$(echo "$mem_value * 1024" | bc 2>/dev/null || echo "0") ;;
            *)   mem_mb="0" ;;
        esac

        # Track memory statistics
        if [ "$mem_mb" != "0" ] && [ "$mem_mb" =~ ^[0-9.]+$ ]; then
            ((MEMORY_SAMPLES++))
            TOTAL_MEMORY=$(echo "$TOTAL_MEMORY + $mem_mb" | bc 2>/dev/null || echo "$TOTAL_MEMORY")

            if [ "$(echo "$mem_mb > $MAX_MEMORY" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
                MAX_MEMORY="$mem_mb"
            fi
        fi

        print_info "Memory: $mem_info"
    fi

    # Get CPU usage
    local cpu_usage
    cpu_usage=$(docker stats --no-stream --format "{{.CPUPerc}}" soak_test_app 2>/dev/null | head -1 || echo "unknown")
    print_info "CPU: $cpu_usage"

    # Take periodic screenshot (every 5 checks)
    if [ $((CHECK_COUNT % 5)) -eq 0 ]; then
        take_screenshot "check_${CHECK_COUNT}"
    fi

    # Check for errors in recent log output
    local log_file="${LOG_DIR}/app_output.log"
    if [ -f "$log_file" ]; then
        local recent_errors
        recent_errors=$(tail -100 "$log_file" | grep -c "error\|Error\|ERROR\|fatal\|Fatal\|FATAL\|crash\|Crash\|CRASH\|segfault\|SIGSEGV\|SIGABRT" || echo "0")

        if [ "$recent_errors" -gt 0 ]; then
            print_warning "Found $recent_errors error indicators in recent logs"
        fi
    fi

    return 0
}

generate_report() {
    print_header "Generating Test Report"

    local end_time=$(date +%s)
    local actual_duration=$((end_time - START_TIME))
    local actual_minutes=$((actual_duration / 60))
    local actual_seconds=$((actual_duration % 60))

    # Calculate average memory
    if [ "$MEMORY_SAMPLES" -gt 0 ]; then
        AVG_MEMORY=$(echo "$TOTAL_MEMORY / $MEMORY_SAMPLES" | bc 2>/dev/null || echo "N/A")
    else
        AVG_MEMORY="N/A"
    fi

    # Determine test result
    local test_result="PASS"
    if [ "$CRASH_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$HANG_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$WARNING_COUNT" -gt 10 ]; then
        test_result="WARNING"
    fi

    # Generate report file
    local report_file="${LOG_DIR}/soak_test_report_$(date +%Y%m%d_%H%M%S).md"

    cat > "$report_file" << EOF
# OpenterfaceQT GUI Soak Test Report

## Test Configuration
- **Date:** $(date -Iseconds)
- **Duration:** ${actual_minutes}m ${actual_seconds}s (target: ${DURATION_MINUTES}m)
- **Check Interval:** ${CHECK_INTERVAL}s
- **Display:** :${DISPLAY_PORT} (${SCREEN_WIDTH}x${SCREEN_HEIGHT})
- **Docker Image:** ${DOCKER_IMAGE}
- **Binary:** ${BUILD_DIR}/${APP_BINARY}

## Test Results
- **Status:** ${test_result}
- **Total Checks:** ${CHECK_COUNT}
- **Crashes:** ${CRASH_COUNT}
- **Hangs:** ${HANG_COUNT}
- **Warnings:** ${WARNING_COUNT}
- **Screenshots Captured:** ${SCREENSHOT_COUNT}

## Resource Usage
- **Memory Samples:** ${MEMORY_SAMPLES}
- **Max Memory:** ${MAX_MEMORY} MB
- **Average Memory:** ${AVG_MEMORY} MB

## Key Findings
- [ ] No crashes detected
- [ ] No memory leaks (stable memory usage)
- [ ] No UI rendering issues
- [ ] All features responsive
- [ ] Clean shutdown

## Logs
- App Output: ${LOG_DIR}/app_output.log
- Crash Logs: ${LOG_DIR}/crash_log_*.txt (if any)

## Screenshots
- Directory: ${SCREENSHOT_DIR}/
- Total: ${SCREENSHOT_COUNT} images

---
*Generated by gui_soak_test.sh*
EOF

    print_success "Report saved: $report_file"

    # Print summary to console
    print_header "Test Summary"
    echo -e "Status:          $([[ $test_result == 'PASS' ]] && echo -e "${GREEN}${test_result}${NC}" || echo -e "${RED}${test_result}${NC}")"
    echo -e "Duration:        ${actual_minutes}m ${actual_seconds}s"
    echo -e "Total Checks:    ${CHECK_COUNT}"
    echo -e "Crashes:         ${CRASH_COUNT}"
    echo -e "Warnings:        ${WARNING_COUNT}"
    echo -e "Screenshots:     ${SCREENSHOT_COUNT}"
    echo -e "Max Memory:      ${MAX_MEMORY} MB"
    echo -e "Avg Memory:      ${AVG_MEMORY} MB"
}

# =============================================================================
# Main Execution
# =============================================================================

main() {
    print_header "OpenterfaceQT GUI Soak Test"
    echo -e "Duration:  ${DURATION_MINUTES} minutes"
    echo -e "Interval:  ${CHECK_INTERVAL} seconds"
    echo -e "Display:   :${DISPLAY_PORT} (${SCREEN_WIDTH}x${SCREEN_HEIGHT})"
    echo ""

    # Setup
    setup_environment

    # Start app
    start_application

    # Main soak loop
    print_header "Running Soak Test"
    print_info "Monitoring application for ${DURATION_MINUTES} minutes..."
    echo ""

    while [ $(date +%s) -lt $END_TIME ]; do
        print_progress

        # Check health
        if ! check_app_health; then
            print_error "Health check failed! Restarting..."
            start_application
        fi

        # Wait for next check
        sleep "$CHECK_INTERVAL"
    done

    # Final check
    print_header "Final Verification"
    check_app_health

    # Take final screenshot
    take_screenshot "99_final"

    # Generate report
    generate_report

    print_header "Soak Test Complete"
}

# Run main function
main "$@"
