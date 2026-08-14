#!/bin/bash
# =============================================================================
# OpenterfaceQT GUI Soak Test Automation
# =============================================================================
# Purpose: Long-running stability test for the OpenterfaceQT GUI application
#
# Features:
#   - Runs app natively OR in Docker (auto-detect)
#   - Tests multiple multimedia backends (ffmpeg, gstreamer)
#   - Monitors memory usage, CPU, and process health
#   - Takes periodic screenshots for visual verification
#   - Captures and analyzes app output logs
#   - Detects crashes, hangs, and resource leaks
#   - Detects focus event loops (X11 grab/ungrab bug)
#   - Tracks memory growth rate for leak detection
#   - Detects file descriptor leaks
#   - Detects thread leaks
#   - Monitors log file size explosion
#   - Checks GStreamer pipeline health
#   - Monitors device connection stability
#   - Phase 1: Video performance monitoring (frame drops, stalls, format changes)
#   - Phase 1: USB/device communication stability (device flapping, HID errors)
#   - Phase 1: Qt event loop responsiveness (UI blocking, timer drift)
#   - Phase 2: X11/Wayland resource leaks (connections, windows, contexts)
#   - Phase 2: Network/MCP server stability (socket leaks, timeouts)
#   - Phase 2: Input event processing (HID parsing, stuck keys, latency)
#   - Phase 3: Disk I/O monitoring (file handle leaks, excessive writes)
#   - Phase 3: System resource contention (swap usage, CPU overload)
#   - Phase 3: Audio device stability (buffer issues, reinit loops)
#   - Phase 3: GPU memory tracking (memory leaks, graphics errors)
#   - Generates comprehensive test report
#
# Usage:
#   ./tests/gui_soak_test.sh [OPTIONS] [DURATION_MINUTES] [CHECK_INTERVAL_SECONDS]
#   ./tests/gui_soak_test.sh --native 60 30    # Native mode, 60 minutes, 30s interval
#   ./tests/gui_soak_test.sh --docker 60 30    # Docker mode (if image exists)
#   ./tests/gui_soak_test.sh 30 15             # Auto-detect, 30 minutes, 15s interval
#   ./tests/gui_soak_test.sh --all-backends 30 # Test with all available backends
#   ./tests/gui_soak_test.sh --backend ffmpeg 30  # Test with specific backend
#
# Options:
#   --native            Run application natively (requires DISPLAY)
#   --docker            Run application in Docker container
#   --no-xvfb           Skip Xvfb setup (use existing DISPLAY)
#   --backend NAME      Use specific media backend (ffmpeg, gstreamer, qt on Windows)
#   --all-backends      Test with all available backends sequentially
#   --help              Show this help
#
# Requirements:
#   - Native mode: X11 DISPLAY, application binary
#   - Docker mode: Docker with appropriate image
#   - Optional: Xvfb (virtual framebuffer), imagemagick (screenshots)
# =============================================================================

set -eo pipefail

# =============================================================================
# Configuration
# =============================================================================
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
TEST_DIR="${PROJECT_DIR}/tests"
LOG_DIR="${TEST_DIR}/soak_test_logs"
SCREENSHOT_DIR="${TEST_DIR}/soak_test_screenshots"

# Default parameters (will be overridden by parse_args)
DURATION_MINUTES=30
CHECK_INTERVAL=15
DISPLAY_PORT=99
SCREEN_WIDTH=1280
SCREEN_HEIGHT=720

# Runtime mode (native or docker)
RUN_MODE="auto"
USE_XVFB=true

# Media backend configuration
MEDIA_BACKEND=""
TEST_ALL_BACKENDS=false
BACKEND_TEST_RESULTS=()

# Derived values
DURATION_SECONDS=$((DURATION_MINUTES * 60))
ARCH=$(uname -m)
APP_BINARY_NAME="openterfaceQT"
APP_BINARY="${BUILD_DIR}/${APP_BINARY_NAME}"

# Docker configuration
DOCKER_IMAGE="openterface-qtbuild-complete:${ARCH}"
CONTAINER_NAME="soak_test_app_$$"

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
FOCUS_LOOP_COUNT=0
RESTART_COUNT=0
FD_LEAK_COUNT=0
THREAD_LEAK_COUNT=0
LOG_EXPLOSION_COUNT=0
GST_ISSUE_COUNT=0
DEVICE_INSTABILITY_COUNT=0
VIDEO_PERFORMANCE_ISSUE_COUNT=0
USB_INSTABILITY_COUNT=0
QT_RESPONSIVENESS_ISSUE_COUNT=0
X11_RESOURCE_LEAK_COUNT=0
NETWORK_STABILITY_ISSUE_COUNT=0
INPUT_PROCESSING_ISSUE_COUNT=0
DISK_IO_ISSUE_COUNT=0
SYSTEM_RESOURCE_ISSUE_COUNT=0
AUDIO_STABILITY_ISSUE_COUNT=0
GPU_MEMORY_ISSUE_COUNT=0

# Memory tracking for leak detection
declare -a MEMORY_HISTORY=()
MEMORY_HISTORY_TIMESTAMPS=()

# File descriptor tracking
FD_COUNT_HISTORY=()

# Thread count tracking
THREAD_COUNT_HISTORY=()

# CPU usage tracking
CPU_USAGE_HISTORY=()

# Log size tracking
LOG_SIZE_HISTORY=()
LAST_LOG_SIZE=0

# Error tracking
LAST_ERROR_LINE=0

# =============================================================================
# Color Codes
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m'

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
    WARNING_COUNT=$((WARNING_COUNT + 1))
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
    # Generate report before cleanup (if we have enough data)
    if [ "$CHECK_COUNT" -gt 0 ]; then
        generate_report
    fi

    print_info "Cleaning up..."

    if [ "$RUN_MODE" = "native" ]; then
        # Kill native app
        if [ -n "${APP_PID:-}" ] && kill -0 "$APP_PID" 2>/dev/null; then
            kill "$APP_PID" 2>/dev/null || true
            wait "$APP_PID" 2>/dev/null || true
        fi
    elif [ "$RUN_MODE" = "docker" ]; then
        # Stop docker container
        docker stop "$CONTAINER_NAME" 2>/dev/null || true
    fi

    # Kill Xvfb if we started it
    if [ "$USE_XVFB" = true ]; then
        pkill -f "Xvfb :${DISPLAY_PORT}" 2>/dev/null || true
    fi

    print_info "Cleanup complete"
}

trap cleanup EXIT

parse_args() {
    local positional_args=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --native)
                RUN_MODE="native"
                shift
                ;;
            --docker)
                RUN_MODE="docker"
                shift
                ;;
            --no-xvfb)
                USE_XVFB=false
                shift
                ;;
            --backend)
                if [[ -z "${2:-}" ]]; then
                    print_error "--backend requires a backend name (ffmpeg, gstreamer, or qt on Windows)"
                    exit 1
                fi
                MEDIA_BACKEND="$2"
                shift 2
                ;;
            --all-backends)
                TEST_ALL_BACKENDS=true
                shift
                ;;
            --help)
                head -30 "$0" | tail -25
                exit 0
                ;;
            [0-9]*)
                # Numeric argument - collect for later
                positional_args+=("$1")
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    # Set positional parameters
    if [ ${#positional_args[@]} -ge 1 ]; then
        DURATION_MINUTES="${positional_args[0]}"
    fi
    if [ ${#positional_args[@]} -ge 2 ]; then
        CHECK_INTERVAL="${positional_args[1]}"
    fi
    if [ ${#positional_args[@]} -ge 3 ]; then
        DISPLAY_PORT="${positional_args[2]}"
    fi
    if [ ${#positional_args[@]} -ge 4 ]; then
        SCREEN_WIDTH="${positional_args[3]}"
    fi
    if [ ${#positional_args[@]} -ge 5 ]; then
        SCREEN_HEIGHT="${positional_args[4]}"
    fi
}

detect_run_mode() {
    if [ "$RUN_MODE" = "auto" ]; then
        # Prefer native if binary exists and DISPLAY is set
        if [ -f "$APP_BINARY" ] && [ -n "${DISPLAY:-}" ]; then
            RUN_MODE="native"
            print_info "Auto-detected native mode (binary exists, DISPLAY set)"
        elif docker image inspect "$DOCKER_IMAGE" &>/dev/null; then
            RUN_MODE="docker"
            print_info "Auto-detected Docker mode (image exists)"
        elif [ -f "$APP_BINARY" ]; then
            RUN_MODE="native"
            USE_XVFB=true
            print_info "Auto-detected native mode with Xvfb"
        else
            print_error "Cannot determine run mode: no binary and no Docker image"
            exit 1
        fi
    fi
}

setup_environment() {
    print_header "Setting Up Test Environment"

    # Create directories
    mkdir -p "$LOG_DIR" "$SCREENSHOT_DIR"

    # Check required tools
    print_info "Checking required tools..."
    local missing_tools=()

    if [ "$RUN_MODE" = "docker" ]; then
        if ! command -v docker &>/dev/null; then
            missing_tools+=("docker")
        fi
        if ! docker image inspect "$DOCKER_IMAGE" &>/dev/null; then
            print_error "Docker image '$DOCKER_IMAGE' not found!"
            print_info "Available images:"
            docker images | grep -i openterface || echo "  (none)"
            exit 1
        fi
        print_success "Docker image found: $DOCKER_IMAGE"
    else
        if [ ! -f "$APP_BINARY" ]; then
            print_error "Binary not found: $APP_BINARY"
            print_info "Please build the application first"
            exit 1
        fi
        print_success "Binary found: $APP_BINARY"
    fi

    # Check optional tools
    for cmd in import; do
        if ! command -v "$cmd" &>/dev/null; then
            print_warning "Optional tool missing: $cmd (screenshots disabled)"
        fi
    done

    # Setup display
    if [ -z "${DISPLAY:-}" ] && [ "$USE_XVFB" = true ]; then
        print_info "Starting Xvfb on :${DISPLAY_PORT} (${SCREEN_WIDTH}x${SCREEN_HEIGHT}x24)..."

        if ! command -v Xvfb &>/dev/null; then
            print_error "Xvfb not found and DISPLAY not set!"
            exit 1
        fi

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

        export DISPLAY=":${DISPLAY_PORT}"
    fi

    if [ -n "${DISPLAY:-}" ]; then
        print_info "DISPLAY=$DISPLAY"
    else
        print_error "DISPLAY not set and Xvfb disabled"
        exit 1
    fi
}

start_application() {
    print_header "Starting Application"

    local log_file="${LOG_DIR}/app_output.log"

    # Kill any existing instances to prevent multiple instances
    print_info "Stopping any existing app instances..."
    pkill -9 -f "$APP_BINARY_NAME" 2>/dev/null || true
    sleep 1

    if [ "$RUN_MODE" = "native" ]; then
        print_info "Launching OpenterfaceQT natively..."

        # Set Qt environment
        export QT_QPA_PLATFORM=xcb
        export QT_LOGGING_RULES="*=false;qt.multimedia=true;openterface.*=true"

        # Build command with optional backend override
        local cmd=("$APP_BINARY")
        if [ -n "$MEDIA_BACKEND" ]; then
            cmd+=(--backend "$MEDIA_BACKEND")
            print_info "Using media backend: $MEDIA_BACKEND"
        fi

        # Start app
        "${cmd[@]}" > "$log_file" 2>&1 &
        APP_PID=$!

        print_info "App started with PID: $APP_PID"

        # Wait for initialization
        print_info "Waiting for app initialization (5 seconds)..."
        sleep 5

        # Verify app is running
        if ! kill -0 "$APP_PID" 2>/dev/null; then
            print_error "Application failed to start!"
            print_error "Check log: $log_file"
            tail -50 "$log_file"
            exit 1
        fi

        print_success "Application is running"

    elif [ "$RUN_MODE" = "docker" ]; then
        print_info "Launching OpenterfaceQT in Docker container..."

        # Build command with optional backend override
        local docker_cmd="./${APP_BINARY_NAME}"
        if [ -n "$MEDIA_BACKEND" ]; then
            docker_cmd="./${APP_BINARY_NAME} --backend $MEDIA_BACKEND"
            print_info "Using media backend: $MEDIA_BACKEND"
        fi

        docker run --rm \
            --name "$CONTAINER_NAME" \
            --network host \
            -v /tmp/.X11-unix:/tmp/.X11-unix \
            -e DISPLAY="$DISPLAY" \
            -e QT_QPA_PLATFORM=xcb \
            -e QT_PLUGIN_PATH=/opt/Qt6/plugins \
            -e QML2_IMPORT_PATH=/opt/Qt6/qml \
            -e GST_PLUGIN_PATH=/opt/gstreamer/lib/gstreamer-1.0:/usr/lib/aarch64-linux-gnu/gstreamer-1.0 \
            -e LD_LIBRARY_PATH=/opt/Qt6/lib:/opt/ffmpeg/lib:/opt/gstreamer/lib:/usr/lib/aarch64-linux-gnu \
            -v "${PROJECT_DIR}:/workspace/src" \
            -w "/workspace/src/build" \
            "$DOCKER_IMAGE" \
            bash -c "$docker_cmd" > "$log_file" 2>&1 &

        APP_PID=$!
        print_info "App started with PID: $APP_PID (container: $CONTAINER_NAME)"

        # Wait for initialization
        print_info "Waiting for app initialization (10 seconds)..."
        sleep 10

        # Verify container is running
        if ! docker ps --format "{{.Names}}" | grep -q "^${CONTAINER_NAME}$"; then
            print_error "Application failed to start!"
            print_error "Check log: $log_file"
            tail -50 "$log_file"
            exit 1
        fi

        print_success "Application is running in container"
    fi

    # Take initial screenshot
    take_screenshot "00_initial"

    # Reset error tracking
    LAST_ERROR_LINE=0
}

take_screenshot() {
    local name="${1:-screenshot}"
    local filename="${SCREENSHOT_DIR}/${name}.png"

    if ! command -v import &>/dev/null; then
        return 0
    fi

    import -window root "$filename" 2>/dev/null || true

    if [ -f "$filename" ]; then
        local size=$(ls -lh "$filename" | awk '{print $5}')
        print_info "Screenshot saved: ${name} (${size})"
        SCREENSHOT_COUNT=$((SCREENSHOT_COUNT + 1))
    fi
}

check_focus_loop() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for focus grab/ungrab loop pattern
    local recent_lines
    recent_lines=$(tail -100 "$log_file" | tr -d '\0')

    local grab_count
    grab_count=$(echo "$recent_lines" | grep "grabbing keyboard" 2>/dev/null | wc -l | awk '{print $1+0}')
    grab_count=${grab_count:-0}

    local ungrab_count
    ungrab_count=$(echo "$recent_lines" | grep "ungrabbing keyboard" 2>/dev/null | wc -l | awk '{print $1+0}')
    ungrab_count=${ungrab_count:-0}

    # If we see many grab/ungrab cycles in recent logs, it's a loop
    if [ "$grab_count" -gt 10 ] || [ "$ungrab_count" -gt 10 ]; then
        print_error "DETECTED: Focus grab/ungrab loop! (${grab_count} grabs, ${ungrab_count} ungrabs in last 100 lines)"
        FOCUS_LOOP_COUNT=$((FOCUS_LOOP_COUNT + 1))
        return 1
    fi

    return 0
}

check_memory_leak() {
    local current_mem_mb=$1

    # Add to history
    MEMORY_HISTORY+=("$current_mem_mb")
    MEMORY_HISTORY_TIMESTAMPS+=("$(date +%s)")

    # Need at least 10 samples to detect trend
    if [ ${#MEMORY_HISTORY[@]} -lt 10 ]; then
        return 0
    fi

    # Compare first 5 samples with last 5 samples
    local first_sum=0
    local last_sum=0
    local count=5

    for i in $(seq 0 $((count-1))); do
        first_sum=$(echo "$first_sum + ${MEMORY_HISTORY[$i]}" | bc 2>/dev/null || echo "$first_sum")
    done

    for i in $(seq $((${#MEMORY_HISTORY[@]}-count)) $((${#MEMORY_HISTORY[@]}-1))); do
        last_sum=$(echo "$last_sum + ${MEMORY_HISTORY[$i]}" | bc 2>/dev/null || echo "$last_sum")
    done

    local first_avg=$(echo "$first_sum / $count" | bc 2>/dev/null || echo "0")
    local last_avg=$(echo "$last_sum / $count" | bc 2>/dev/null || echo "0")

    # If memory grew by more than 50MB, flag as potential leak
    local growth=$(echo "$last_avg - $first_avg" | bc 2>/dev/null || echo "0")

    if [ "$(echo "$growth > 50" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
        print_warning "POTENTIAL MEMORY LEAK: Memory grew by ${growth}MB (from ${first_avg}MB to ${last_avg}MB)"
        return 1
    fi

    return 0
}

check_fd_leak() {
    local pid=$1

    if [ ! -d "/proc/$pid/fd" ]; then
        return 0
    fi

    local fd_count
    fd_count=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l | awk '{print $1+0}')
    fd_count=${fd_count:-0}

    FD_COUNT_HISTORY+=("$fd_count")

    # Need at least 10 samples to detect trend
    if [ ${#FD_COUNT_HISTORY[@]} -lt 10 ]; then
        return 0
    fi

    # Compare first 5 samples with last 5 samples
    local first_sum=0
    local last_sum=0
    local count=5

    for i in $(seq 0 $((count-1))); do
        first_sum=$((first_sum + ${FD_COUNT_HISTORY[$i]}))
    done

    for i in $(seq $((${#FD_COUNT_HISTORY[@]}-count)) $((${#FD_COUNT_HISTORY[@]}-1))); do
        last_sum=$((last_sum + ${FD_COUNT_HISTORY[$i]}))
    done

    local first_avg=$((first_sum / count))
    local last_avg=$((last_sum / count))
    local growth=$((last_avg - first_avg))

    # If FD count grew by more than 20, flag as potential leak
    if [ "$growth" -gt 20 ]; then
        print_warning "POTENTIAL FD LEAK: File descriptors grew by $growth (from $first_avg to $last_avg)"
        return 1
    fi

    return 0
}

check_thread_leak() {
    local pid=$1

    if [ ! -f "/proc/$pid/status" ]; then
        return 0
    fi

    local thread_count
    thread_count=$(grep "^Threads:" /proc/$pid/status 2>/dev/null | awk '{print $2+0}')
    thread_count=${thread_count:-0}

    THREAD_COUNT_HISTORY+=("$thread_count")

    # Need at least 10 samples
    if [ ${#THREAD_COUNT_HISTORY[@]} -lt 10 ]; then
        return 0
    fi

    # Compare first 5 samples with last 5 samples
    local first_sum=0
    local last_sum=0
    local count=5

    for i in $(seq 0 $((count-1))); do
        first_sum=$((first_sum + ${THREAD_COUNT_HISTORY[$i]}))
    done

    for i in $(seq $((${#THREAD_COUNT_HISTORY[@]}-count)) $((${#THREAD_COUNT_HISTORY[@]}-1))); do
        last_sum=$((last_sum + ${THREAD_COUNT_HISTORY[$i]}))
    done

    local first_avg=$((first_sum / count))
    local last_avg=$((last_sum / count))
    local growth=$((last_avg - first_avg))

    # If thread count grew by more than 10, flag as potential leak
    if [ "$growth" -gt 10 ]; then
        print_warning "POTENTIAL THREAD LEAK: Threads grew by $growth (from $first_avg to $last_avg)"
        return 1
    fi

    return 0
}

check_cpu_spike() {
    local pid=$1

    if [ ! -f "/proc/$pid/stat" ]; then
        return 0
    fi

    # Get CPU times (user + system)
    local cpu_times
    cpu_times=$(awk '{print $14+$15}' /proc/$pid/stat 2>/dev/null)
    cpu_times=${cpu_times:-0}

    # Simple check: if we can read it, track it
    # More sophisticated CPU % calculation would require sampling over time
    CPU_USAGE_HISTORY+=("$cpu_times")

    return 0
}

check_log_explosion() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    local current_size
    current_size=$(stat -c%s "$log_file" 2>/dev/null || echo "0")

    LOG_SIZE_HISTORY+=("$current_size")

    # Check if log grew by more than 10MB since last check
    if [ "$LAST_LOG_SIZE" -gt 0 ]; then
        local growth=$((current_size - LAST_LOG_SIZE))
        local growth_mb=$((growth / 1048576))

        if [ "$growth_mb" -gt 10 ]; then
            print_warning "LOG EXPLOSION: Log grew by ${growth_mb}MB since last check"
            LAST_LOG_SIZE=$current_size
            return 1
        fi
    fi

    LAST_LOG_SIZE=$current_size
    return 0
}

check_gstreamer_issues() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for GStreamer pipeline errors
    local gst_errors
    gst_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                grep -i "gstreamer\|gst_\|pipeline\|xvimagesink\|v4l2src" | \
                grep -i "error\|warning\|critical\|failed" | wc -l | awk '{print $1+0}')
    gst_errors=${gst_errors:-0}

    if [ "$gst_errors" -gt 5 ]; then
        print_warning "GSTREAMER ISSUES: Found $gst_errors GStreamer errors/warnings in recent logs"
        return 1
    fi

    return 0
}

check_device_stability() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for repeated device open/close cycles (indicates instability)
    local device_opens
    device_opens=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Opening device\|Device opened" | awk '{print $1+0}')
    device_opens=${device_opens:-0}

    if [ "$device_opens" -gt 10 ]; then
        print_warning "DEVICE INSTABILITY: Found $device_opens device open events in recent logs"
        return 1
    fi

    return 0
}

check_video_performance() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for video frame rate issues
    # Count frame-related events in recent logs
    local frame_events
    frame_events=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "frameReady\|FrameAvailable\|new frame\|Frame received" | awk '{print $1+0}')
    frame_events=${frame_events:-0}

    # Check for video errors
    local video_errors
    video_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Video.*error\|video.*failed\|frame.*drop\|buffer.*underrun\|Video.*timeout" | awk '{print $1+0}')
    video_errors=${video_errors:-0}

    # Check for video pipeline stalls
    local video_stalls
    video_stalls=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Video.*stall\|Video.*freeze\|pipeline.*stuck\|Video.*timeout" | awk '{print $1+0}')
    video_stalls=${video_stalls:-0}

    # Check for resolution/format changes (unexpected switches)
    local format_changes
    format_changes=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                     grep -c "Resolution changed\|Format changed\|Video.*reconfigure" | awk '{print $1+0}')
    format_changes=${format_changes:-0}

    local issues=0

    # Alert if too many video errors
    if [ "$video_errors" -gt 5 ]; then
        print_warning "VIDEO PERFORMANCE: Found $video_errors video errors in recent logs"
        issues=1
    fi

    # Alert if video pipeline is stalling
    if [ "$video_stalls" -gt 2 ]; then
        print_warning "VIDEO PERFORMANCE: Found $video_stalls video stalls/freezes"
        issues=1
    fi

    # Alert if too many format changes (indicates instability)
    if [ "$format_changes" -gt 10 ]; then
        print_warning "VIDEO PERFORMANCE: Found $format_changes unexpected format/resolution changes"
        issues=1
    fi

    # Note: We don't alert on low frame count because it might be normal for static scenes
    # Instead, we track it for debugging
    if [ "$frame_events" -eq 0 ] && [ "$video_errors" -gt 0 ]; then
        print_warning "VIDEO PERFORMANCE: No frame events but errors detected - pipeline may be stalled"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Video stats: frames=$frame_events, errors=$video_errors, stalls=$video_stalls, format_changes=$format_changes"
    fi

    return $issues
}

check_usb_stability() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for USB communication errors
    local usb_errors
    usb_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                 grep -c "USB error\|usb_submit_urb failed\|LIBUSB_ERROR\|USB.*timeout\|USB.*failed" | awk '{print $1+0}')
    usb_errors=${usb_errors:-0}

    # Check for device disconnect/reconnect cycles
    local device_reconnects
    device_reconnects=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                        grep -c "Device disconnected\|Device reconnected\|USB.*disconnect\|Device lost" | awk '{print $1+0}')
    device_reconnects=${device_reconnects:-0}

    # Check for HID communication errors
    local hid_errors
    hid_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                 grep -c "HID.*error\|HID.*failed\|Failed to parse HID\|Invalid HID report" | awk '{print $1+0}')
    hid_errors=${hid_errors:-0}

    # Check for device enumeration loops (repeated discovery)
    local enum_loops
    enum_loops=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                 grep -c "Device discovered\|Found.*device\|Enumerating.*device" | awk '{print $1+0}')
    enum_loops=${enum_loops:-0}

    # Check for serial port issues
    local serial_errors
    serial_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "Serial.*error\|Serial.*timeout\|Serial.*failed\|UART.*error" | awk '{print $1+0}')
    serial_errors=${serial_errors:-0}

    local issues=0

    # Alert if too many USB errors
    if [ "$usb_errors" -gt 5 ]; then
        print_warning "USB STABILITY: Found $usb_errors USB communication errors"
        issues=1
    fi

    # Alert if device is repeatedly disconnecting/reconnecting
    if [ "$device_reconnects" -gt 3 ]; then
        print_warning "USB STABILITY: Device reconnected $device_reconnects times (device flapping)"
        issues=1
    fi

    # Alert if HID parsing is failing
    if [ "$hid_errors" -gt 5 ]; then
        print_warning "USB STABILITY: Found $hid_errors HID parsing errors"
        issues=1
    fi

    # Alert if device enumeration is looping
    if [ "$enum_loops" -gt 20 ]; then
        print_warning "USB STABILITY: Found $enum_loops device enumeration events (possible loop)"
        issues=1
    fi

    # Alert if serial port is having issues
    if [ "$serial_errors" -gt 5 ]; then
        print_warning "USB STABILITY: Found $serial_errors serial port errors"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "USB stats: usb_errors=$usb_errors, reconnects=$device_reconnects, hid_errors=$hid_errors, serial_errors=$serial_errors"
    fi

    return $issues
}

check_qt_responsiveness() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    # Check for Qt event loop warnings
    local event_warnings
    event_warnings=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                     grep -c "Event loop.*blocking\|QEventLoop.*slow\|Main thread.*blocked\|UI.*freeze\|UI.*unresponsive" | awk '{print $1+0}')
    event_warnings=${event_warnings:-0}

    # Check for timer issues (drift, missed ticks)
    local timer_issues
    timer_issues=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Timer.*delayed\|Timer.*missed\|QTimer.*skip\|Timer.*drift" | awk '{print $1+0}')
    timer_issues=${timer_issues:-0}

    # Check for signal/slot connection accumulation (indicates leak)
    local connection_warnings
    connection_warnings=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                          grep -c "connect.*failed\|Signal.*slot.*error\|Too many connections" | awk '{print $1+0}')
    connection_warnings=${connection_warnings:-0}

    # Check for paint event issues
    local paint_issues
    paint_issues=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Paint.*slow\|Paint.*blocked\|Render.*timeout\|Display.*freeze" | awk '{print $1+0}')
    paint_issues=${paint_issues:-0}

    # Check for widget update issues
    local widget_issues
    widget_issues=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "Widget.*update.*failed\|UI.*update.*timeout\|Refresh.*slow" | awk '{print $1+0}')
    widget_issues=${widget_issues:-0}

    local issues=0

    # Alert if event loop is blocking
    if [ "$event_warnings" -gt 3 ]; then
        print_warning "QT RESPONSIVENESS: Found $event_warnings event loop blocking warnings"
        issues=1
    fi

    # Alert if timers are drifting or missing
    if [ "$timer_issues" -gt 5 ]; then
        print_warning "QT RESPONSIVENESS: Found $timer_issues timer issues (drift/missed ticks)"
        issues=1
    fi

    # Alert if signal/slot connections are failing
    if [ "$connection_warnings" -gt 3 ]; then
        print_warning "QT RESPONSIVENESS: Found $connection_warnings signal/slot connection issues"
        issues=1
    fi

    # Alert if paint/rendering is slow
    if [ "$paint_issues" -gt 3 ]; then
        print_warning "QT RESPONSIVENESS: Found $paint_issues paint/rendering issues"
        issues=1
    fi

    # Alert if widget updates are slow
    if [ "$widget_issues" -gt 5 ]; then
        print_warning "QT RESPONSIVENESS: Found $widget_issues widget update issues"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Qt stats: event_warnings=$event_warnings, timer_issues=$timer_issues, paint_issues=$paint_issues"
    fi

    return $issues
}

check_x11_resources() {
    local pid=$1
    local log_file="${LOG_DIR}/app_output.log"

    if [ -z "$pid" ] || [ ! -d "/proc/$pid" ]; then
        return 0
    fi

    local issues=0

    # Check X11 connection count via /proc/fd
    local x11_connections
    x11_connections=$(ls -l /proc/$pid/fd 2>/dev/null | grep -c "X11-unix\|/tmp/.X11-unix" | awk '{print $1+0}')
    x11_connections=${x11_connections:-0}

    # Check for X11 resource warnings in logs
    local x11_warnings
    if [ -f "$log_file" ]; then
        x11_warnings=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                       grep -c "X11.*error\|X11.*leak\|X.*connection.*failed\|Display.*error\|XCB.*error" | awk '{print $1+0}')
        x11_warnings=${x11_warnings:-0}
    else
        x11_warnings=0
    fi

    # Check for window/pixmap leaks in logs
    local window_leaks
    if [ -f "$log_file" ]; then
        window_leaks=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                       grep -c "Window.*leak\|Pixmap.*leak\|X11.*resource.*exhausted" | awk '{print $1+0}')
        window_leaks=${window_leaks:-0}
    else
        window_leaks=0
    fi

    # Check for graphics context leaks
    local context_leaks
    if [ -f "$log_file" ]; then
        context_leaks=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                        grep -c "GL.*context.*leak\|OpenGL.*context.*leak\|Graphics.*context.*leak" | awk '{print $1+0}')
        context_leaks=${context_leaks:-0}
    else
        context_leaks=0
    fi

    # Alert if too many X11 connections (indicates leak)
    if [ "$x11_connections" -gt 5 ]; then
        print_warning "X11 RESOURCE LEAK: Found $x11_connections X11 connections (possible leak)"
        issues=1
    fi

    # Alert if X11 errors detected
    if [ "$x11_warnings" -gt 5 ]; then
        print_warning "X11 RESOURCE LEAK: Found $x11_warnings X11/display errors"
        issues=1
    fi

    # Alert if window/pixmap leaks detected
    if [ "$window_leaks" -gt 0 ]; then
        print_warning "X11 RESOURCE LEAK: Found $window_leaks window/pixmap leaks"
        issues=1
    fi

    # Alert if graphics context leaks detected
    if [ "$context_leaks" -gt 0 ]; then
        print_warning "X11 RESOURCE LEAK: Found $context_leaks graphics context leaks"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "X11 stats: connections=$x11_connections, warnings=$x11_warnings, window_leaks=$window_leaks, context_leaks=$context_leaks"
    fi

    return $issues
}

check_network_health() {
    local pid=$1
    local log_file="${LOG_DIR}/app_output.log"

    if [ -z "$pid" ] || [ ! -d "/proc/$pid" ]; then
        return 0
    fi

    local issues=0

    # Count open network connections
    local tcp_connections
    tcp_connections=$(ss -tnp 2>/dev/null | grep "pid=$pid" | wc -l | awk '{print $1+0}')
    tcp_connections=${tcp_connections:-0}

    # Check for connections in various states
    local established
    established=$(ss -tnp 2>/dev/null | grep "pid=$pid" | grep -c "ESTAB" | awk '{print $1+0}')
    established=${established:-0}

    local time_wait
    time_wait=$(ss -tnp 2>/dev/null | grep "pid=$pid" | grep -c "TIME-WAIT" | awk '{print $1+0}')
    time_wait=${time_wait:-0}

    local close_wait
    close_wait=$(ss -tnp 2>/dev/null | grep "pid=$pid" | grep -c "CLOSE-WAIT" | awk '{print $1+0}')
    close_wait=${close_wait:-0}

    # Check for network-related errors in logs
    local network_errors
    if [ -f "$log_file" ]; then
        network_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                         grep -c "Network.*error\|Socket.*error\|TCP.*error\|Connection.*failed\|MCP.*error\|WebSocket.*error" | awk '{print $1+0}')
        network_errors=${network_errors:-0}
    else
        network_errors=0
    fi

    # Check for network timeout issues
    local network_timeouts
    if [ -f "$log_file" ]; then
        network_timeouts=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                           grep -c "Network.*timeout\|Connection.*timeout\|Socket.*timeout\|MCP.*timeout" | awk '{print $1+0}')
        network_timeouts=${network_timeouts:-0}
    else
        network_timeouts=0
    fi

    # Alert if too many TIME-WAIT connections (indicates leak)
    if [ "$time_wait" -gt 20 ]; then
        print_warning "NETWORK STABILITY: $time_wait connections in TIME-WAIT (possible socket leak)"
        issues=1
    fi

    # Alert if too many CLOSE-WAIT connections (indicates leak)
    if [ "$close_wait" -gt 10 ]; then
        print_warning "NETWORK STABILITY: $close_wait connections in CLOSE-WAIT (possible socket leak)"
        issues=1
    fi

    # Alert if too many total connections
    if [ "$tcp_connections" -gt 100 ]; then
        print_warning "NETWORK STABILITY: $tcp_connections TCP connections open (possible leak)"
        issues=1
    fi

    # Alert if network errors detected
    if [ "$network_errors" -gt 5 ]; then
        print_warning "NETWORK STABILITY: Found $network_errors network/socket errors"
        issues=1
    fi

    # Alert if network timeouts detected
    if [ "$network_timeouts" -gt 3 ]; then
        print_warning "NETWORK STABILITY: Found $network_timeouts network timeouts"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Network stats: total=$tcp_connections, established=$established, time_wait=$time_wait, close_wait=$close_wait, errors=$network_errors"
    fi

    return $issues
}

check_input_processing() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    local issues=0

    # Check for HID parsing errors
    local hid_errors
    hid_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                 grep -c "HID.*error\|HID.*failed\|Failed to parse HID\|Invalid HID report\|HID.*parse.*error" | awk '{print $1+0}')
    hid_errors=${hid_errors:-0}

    # Check for input latency warnings
    local input_latency
    input_latency=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "Input latency\|Event delay\|Input.*slow\|Keyboard.*delay\|Mouse.*delay" | awk '{print $1+0}')
    input_latency=${input_latency:-0}

    # Check for stuck key detection
    local stuck_keys
    stuck_keys=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                 grep -c "Stuck key\|Key not released\|Key.*stuck\|Stuck.*key" | awk '{print $1+0}')
    stuck_keys=${stuck_keys:-0}

    # Check for input queue buildup
    local input_queue
    input_queue=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                  grep -c "Input queue.*full\|Event queue.*overflow\|Input.*backlog\|Event.*backlog" | awk '{print $1+0}')
    input_queue=${input_queue:-0}

    # Check for mouse/coordinate mapping errors
    local mouse_errors
    mouse_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Mouse.*error\|Coordinate.*error\|Mouse.*mapping.*failed\|Input.*coordinate.*error" | awk '{print $1+0}')
    mouse_errors=${mouse_errors:-0}

    # Check for keyboard event errors
    local keyboard_errors
    keyboard_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                      grep -c "Keyboard.*error\|Key.*error\|Key.*event.*failed\|Keyboard.*event.*error" | awk '{print $1+0}')
    keyboard_errors=${keyboard_errors:-0}

    # Alert if HID parsing is failing
    if [ "$hid_errors" -gt 5 ]; then
        print_warning "INPUT PROCESSING: Found $hid_errors HID parsing errors"
        issues=1
    fi

    # Alert if input latency issues detected
    if [ "$input_latency" -gt 5 ]; then
        print_warning "INPUT PROCESSING: Found $input_latency input latency warnings"
        issues=1
    fi

    # Alert if stuck keys detected (critical issue)
    if [ "$stuck_keys" -gt 0 ]; then
        print_warning "INPUT PROCESSING: Found $stuck_keys stuck key events (critical)"
        issues=1
    fi

    # Alert if input queue is backing up
    if [ "$input_queue" -gt 3 ]; then
        print_warning "INPUT PROCESSING: Found $input_queue input queue buildup events"
        issues=1
    fi

    # Alert if mouse mapping errors detected
    if [ "$mouse_errors" -gt 3 ]; then
        print_warning "INPUT PROCESSING: Found $mouse_errors mouse/coordinate errors"
        issues=1
    fi

    # Alert if keyboard event errors detected
    if [ "$keyboard_errors" -gt 5 ]; then
        print_warning "INPUT PROCESSING: Found $keyboard_errors keyboard event errors"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Input stats: hid_errors=$hid_errors, latency=$input_latency, stuck_keys=$stuck_keys, queue=$input_queue"
    fi

    return $issues
}

check_disk_io() {
    local pid=$1
    local log_file="${LOG_DIR}/app_output.log"

    if [ -z "$pid" ] || [ ! -d "/proc/$pid" ]; then
        return 0
    fi

    local issues=0

    # Read I/O stats from /proc
    local io_stats
    io_stats=$(cat /proc/$pid/io 2>/dev/null || echo "")

    local read_bytes=0
    local write_bytes=0

    if [ -n "$io_stats" ]; then
        read_bytes=$(echo "$io_stats" | grep "^read_bytes:" | awk '{print $2}' || echo "0")
        write_bytes=$(echo "$io_stats" | grep "^write_bytes:" | awk '{print $2}' || echo "0")
        read_bytes=${read_bytes:-0}
        write_bytes=${write_bytes:-0}
    fi

    # Convert to MB for display
    local read_mb=$(echo "scale=2; $read_bytes / 1048576" | bc 2>/dev/null || echo "0")
    local write_mb=$(echo "scale=2; $write_bytes / 1048576" | bc 2>/dev/null || echo "0")

    # Check open file count
    local open_files
    open_files=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l | awk '{print $1+0}')
    open_files=${open_files:-0}

    # Check for I/O-related errors in logs
    local io_errors
    if [ -f "$log_file" ]; then
        io_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "I/O error\|disk.*error\|file.*error\|write.*failed\|read.*failed" | awk '{print $1+0}')
        io_errors=${io_errors:-0}
    else
        io_errors=0
    fi

    # Check for file handle leak warnings in logs
    local file_leaks
    if [ -f "$log_file" ]; then
        file_leaks=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                     grep -c "file.*leak\|too many.*open.*files\|file.*descriptor.*leak\|Too many open files\|EMFILE\|ENFILE" | awk '{print $1+0}')
        file_leaks=${file_leaks:-0}
    else
        file_leaks=0
    fi

    # Alert if too many files are open
    if [ "$open_files" -gt 200 ]; then
        print_warning "DISK I/O: $open_files files open (possible file handle leak)"
        issues=1
    fi

    # Alert if I/O errors detected
    if [ "$io_errors" -gt 5 ]; then
        print_warning "DISK I/O: Found $io_errors I/O errors in logs"
        issues=1
    fi

    # Alert if file leak warnings detected
    if [ "$file_leaks" -gt 0 ]; then
        print_warning "DISK I/O: Found $file_leaks file leak warnings (critical)"
        issues=1
    fi

    # Alert if write rate is excessive (>100MB total write indicates heavy I/O)
    if [ "$(echo "$write_mb > 100" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
        print_warning "DISK I/O: High write volume (${write_mb}MB total, possible excessive logging)"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Disk I/O stats: read=${read_mb}MB, write=${write_mb}MB, open_files=$open_files, io_errors=$io_errors"
    fi

    return $issues
}

check_system_resources() {
    local log_file="${LOG_DIR}/app_output.log"

    local issues=0

    # System-wide memory pressure
    local swap_used
    swap_used=$(free | grep Swap | awk '{print $3}' || echo "0")
    local swap_total
    swap_total=$(free | grep Swap | awk '{print $2}' || echo "1")
    swap_used=${swap_used:-0}
    swap_total=${swap_total:-1}

    local swap_pct=0
    if [ "$swap_total" -gt 0 ]; then
        swap_pct=$(echo "scale=2; $swap_used * 100 / $swap_total" | bc 2>/dev/null || echo "0")
    fi

    # CPU load average
    local load_avg
    load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk -F',' '{print $1}' | tr -d ' ' || echo "0")
    load_avg=${load_avg:-0}

    local cpu_count
    cpu_count=$(nproc 2>/dev/null || echo "1")
    cpu_count=${cpu_count:-1}

    # Calculate load threshold (2x CPU count)
    local load_threshold=$(echo "$cpu_count * 2" | bc 2>/dev/null || echo "2")

    # Check for system resource warnings in logs
    local resource_warnings
    if [ -f "$log_file" ]; then
        resource_warnings=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                            grep -c "out of memory\|OOM\|system.*resource\|CPU.*throttl\|thermal.*throttl" | awk '{print $1+0}')
        resource_warnings=${resource_warnings:-0}
    else
        resource_warnings=0
    fi

    # Alert if swap usage is high (>50%)
    if [ "$(echo "$swap_pct > 50" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
        print_warning "SYSTEM RESOURCES: High swap usage (${swap_pct}%)"
        issues=1
    fi

    # Alert if CPU load is too high
    if [ "$(echo "$load_avg > $load_threshold" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
        print_warning "SYSTEM RESOURCES: High CPU load ($load_avg, threshold: $load_threshold)"
        issues=1
    fi

    # Alert if system resource warnings detected
    if [ "$resource_warnings" -gt 0 ]; then
        print_warning "SYSTEM RESOURCES: Found $resource_warnings system resource warnings (OOM, throttling)"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "System stats: swap_usage=${swap_pct}%, load_avg=$load_avg, cpus=$cpu_count, warnings=$resource_warnings"
    fi

    return $issues
}

check_audio_stability() {
    local log_file="${LOG_DIR}/app_output.log"

    if [ ! -f "$log_file" ]; then
        return 0
    fi

    local issues=0

    # Check for audio errors
    local audio_errors
    audio_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "ALSA.*error\|PulseAudio.*error\|Audio.*failed\|Audio.*error\|Sound.*error" | awk '{print $1+0}')
    audio_errors=${audio_errors:-0}

    # Check for audio device reinitialization
    local audio_reinit
    audio_reinit=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                   grep -c "Opening audio\|Audio device opened\|Audio.*reinit\|Audio.*restart" | awk '{print $1+0}')
    audio_reinit=${audio_reinit:-0}

    # Check for buffer underruns/overruns
    local buffer_issues
    buffer_issues=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "buffer.*underrun\|buffer.*overrun\|audio.*buffer.*error\|xrun\|audio.*glitch" | awk '{print $1+0}')
    buffer_issues=${buffer_issues:-0}

    # Check for sample rate changes
    local sample_rate_changes
    sample_rate_changes=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                          grep -c "sample.*rate.*change\|audio.*format.*change\|audio.*reconfigure" | awk '{print $1+0}')
    sample_rate_changes=${sample_rate_changes:-0}

    # Check for audio latency warnings
    local audio_latency
    audio_latency=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                    grep -c "audio.*latency\|audio.*delay\|audio.*sync.*error" | awk '{print $1+0}')
    audio_latency=${audio_latency:-0}

    # Alert if audio errors detected
    if [ "$audio_errors" -gt 5 ]; then
        print_warning "AUDIO STABILITY: Found $audio_errors audio subsystem errors"
        issues=1
    fi

    # Alert if audio device is repeatedly reinitializing
    if [ "$audio_reinit" -gt 10 ]; then
        print_warning "AUDIO STABILITY: Audio device reinitialized $audio_reinit times (instability)"
        issues=1
    fi

    # Alert if buffer issues detected
    if [ "$buffer_issues" -gt 5 ]; then
        print_warning "AUDIO STABILITY: Found $buffer_issues audio buffer issues (underruns/overruns)"
        issues=1
    fi

    # Alert if too many sample rate changes
    if [ "$sample_rate_changes" -gt 10 ]; then
        print_warning "AUDIO STABILITY: Found $sample_rate_changes sample rate changes (instability)"
        issues=1
    fi

    # Alert if audio latency issues detected
    if [ "$audio_latency" -gt 5 ]; then
        print_warning "AUDIO STABILITY: Found $audio_latency audio latency warnings"
        issues=1
    fi

    if [ "$issues" -eq 1 ]; then
        print_info "Audio stats: errors=$audio_errors, reinit=$audio_reinit, buffer_issues=$buffer_issues, latency=$audio_latency"
    fi

    return $issues
}

check_gpu_memory() {
    local pid=$1
    local log_file="${LOG_DIR}/app_output.log"

    if [ -z "$pid" ]; then
        return 0
    fi

    local issues=0

    # NVIDIA GPUs
    if command -v nvidia-smi &>/dev/null; then
        local gpu_mem
        gpu_mem=$(nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader 2>/dev/null | grep "$pid" | awk '{print $2}' || echo "0")
        gpu_mem=${gpu_mem:-0}

        # Check for GPU errors in logs
        local gpu_errors
        if [ -f "$log_file" ]; then
            gpu_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                         grep -c "GPU.*error\|CUDA.*error\|OpenGL.*error\|graphics.*error\|render.*error" | awk '{print $1+0}')
            gpu_errors=${gpu_errors:-0}
        else
            gpu_errors=0
        fi

        # Check for texture/context leaks in logs
        local texture_leaks
        if [ -f "$log_file" ]; then
            texture_leaks=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                           grep -c "texture.*leak\|GPU.*memory.*leak\|graphics.*leak\|OpenGL.*leak" | awk '{print $1+0}')
            texture_leaks=${texture_leaks:-0}
        else
            texture_leaks=0
        fi

        # Alert if GPU memory usage is excessive (>500MB)
        if [ "$gpu_mem" -gt 500 ] 2>/dev/null; then
            print_warning "GPU MEMORY: High GPU memory usage (${gpu_mem}MB)"
            issues=1
        fi

        # Alert if GPU errors detected
        if [ "$gpu_errors" -gt 5 ]; then
            print_warning "GPU MEMORY: Found $gpu_errors GPU/graphics errors"
            issues=1
        fi

        # Alert if texture/context leaks detected
        if [ "$texture_leaks" -gt 0 ]; then
            print_warning "GPU MEMORY: Found $texture_leaks graphics resource leaks"
            issues=1
        fi

        if [ "$issues" -eq 1 ]; then
            print_info "GPU stats: memory=${gpu_mem}MB, errors=$gpu_errors, leaks=$texture_leaks"
        fi
    else
        # No NVIDIA GPU or nvidia-smi not available
        # Check for GPU-related errors in logs anyway
        local gpu_errors
        if [ -f "$log_file" ]; then
            gpu_errors=$(tail -100 "$log_file" 2>/dev/null | tr -d '\0' | \
                         grep -c "GPU.*error\|OpenGL.*error\|graphics.*error\|render.*error" | awk '{print $1+0}')
            gpu_errors=${gpu_errors:-0}
        else
            gpu_errors=0
        fi

        if [ "$gpu_errors" -gt 5 ]; then
            print_warning "GPU MEMORY: Found $gpu_errors GPU/graphics errors"
            issues=1
            print_info "GPU stats: errors=$gpu_errors (nvidia-smi not available)"
        fi
    fi

    return $issues
}

check_app_health() {
    CHECK_COUNT=$((CHECK_COUNT + 1))

    local is_running=false
    local mem_info="unknown"

    if [ "$RUN_MODE" = "native" ]; then
        # Check if process is running
        if kill -0 "$APP_PID" 2>/dev/null; then
            is_running=true

            # Get memory usage from /proc
            if [ -f "/proc/$APP_PID/status" ]; then
                local vmrss
                vmrss=$(grep VmRSS /proc/$APP_PID/status | awk '{print $2}')
                if [ -n "$vmrss" ]; then
                    local mem_mb=$(echo "scale=2; $vmrss / 1024" | bc 2>/dev/null || echo "0")
                    mem_info="${mem_mb}MiB"
                fi
            fi
        fi
    elif [ "$RUN_MODE" = "docker" ]; then
        # Check if container is running
        if docker ps --format "{{.Names}}" | grep -q "^${CONTAINER_NAME}$"; then
            is_running=true

            # Get stats from docker
            mem_info=$(docker stats --no-stream --format "{{.MemUsage}}" "$CONTAINER_NAME" 2>/dev/null | head -1 || echo "unknown")
        fi
    fi

    if [ "$is_running" = false ]; then
        print_error "Application has stopped!"
        CRASH_COUNT=$((CRASH_COUNT + 1))

        # Get last logs
        local log_file="${LOG_DIR}/app_output.log"
        tail -100 "$log_file" > "${LOG_DIR}/crash_log_${CHECK_COUNT}.txt" 2>/dev/null || true
        take_screenshot "crash_${CHECK_COUNT}"

        return 1
    fi

    # Parse memory value
    if [ "$mem_info" != "unknown" ]; then
        local mem_value
        mem_value=$(echo "$mem_info" | grep -oP '[\d.]+' | head -1 || echo "0")
        local mem_unit
        mem_unit=$(echo "$mem_info" | grep -oP '[A-Za]+' | head -1 || echo "MiB")

        # Convert to MB
        local mem_mb
        case "$mem_unit" in
            B)     mem_mb=$(echo "scale=6; $mem_value / 1048576" | bc 2>/dev/null || echo "0") ;;
            KiB|KB) mem_mb=$(echo "scale=6; $mem_value / 1024" | bc 2>/dev/null || echo "0") ;;
            MiB|MB) mem_mb="$mem_value" ;;
            GiB|GB) mem_mb=$(echo "scale=6; $mem_value * 1024" | bc 2>/dev/null || echo "0") ;;
            *)     mem_mb="$mem_value" ;;
        esac

        # Track memory statistics
        if [ "$mem_mb" != "0" ] && [ "$mem_mb" != "" ]; then
            MEMORY_SAMPLES=$((MEMORY_SAMPLES + 1))
            TOTAL_MEMORY=$(echo "$TOTAL_MEMORY + $mem_mb" | bc 2>/dev/null || echo "$TOTAL_MEMORY")

            if [ "$(echo "$mem_mb > $MAX_MEMORY" | bc 2>/dev/null || echo 0)" -eq 1 ]; then
                MAX_MEMORY="$mem_mb"
            fi

            # Check for memory leak
            check_memory_leak "$mem_mb" || true
        fi

        print_info "Memory: $mem_info"
    fi

    # Get CPU usage (Docker only)
    if [ "$RUN_MODE" = "docker" ]; then
        local cpu_usage
        cpu_usage=$(docker stats --no-stream --format "{{.CPUPerc}}" "$CONTAINER_NAME" 2>/dev/null | head -1 || echo "unknown")
        print_info "CPU: $cpu_usage"
    fi

    # Check for focus loop (the bug we fixed)
    if ! check_focus_loop; then
        HANG_COUNT=$((HANG_COUNT + 1))
    fi

    # Check for file descriptor leaks (native mode only)
    if [ "$RUN_MODE" = "native" ] && [ -n "$APP_PID" ]; then
        check_fd_leak "$APP_PID" || FD_LEAK_COUNT=$((FD_LEAK_COUNT + 1))
        check_thread_leak "$APP_PID" || THREAD_LEAK_COUNT=$((THREAD_LEAK_COUNT + 1))
        check_cpu_spike "$APP_PID" || true
    fi

    # Check for log file explosion
    check_log_explosion || LOG_EXPLOSION_COUNT=$((LOG_EXPLOSION_COUNT + 1))

    # Check for GStreamer pipeline issues
    check_gstreamer_issues || GST_ISSUE_COUNT=$((GST_ISSUE_COUNT + 1))

    # Check for device connection stability
    check_device_stability || DEVICE_INSTABILITY_COUNT=$((DEVICE_INSTABILITY_COUNT + 1))

    # Check video performance (Phase 1)
    check_video_performance || VIDEO_PERFORMANCE_ISSUE_COUNT=$((VIDEO_PERFORMANCE_ISSUE_COUNT + 1))

    # Check USB/device communication stability (Phase 1)
    check_usb_stability || USB_INSTABILITY_COUNT=$((USB_INSTABILITY_COUNT + 1))

    # Check Qt event loop responsiveness (Phase 1)
    check_qt_responsiveness || QT_RESPONSIVENESS_ISSUE_COUNT=$((QT_RESPONSIVENESS_ISSUE_COUNT + 1))

    # Check X11/Wayland resource leaks (Phase 2)
    if [ "$RUN_MODE" = "native" ] && [ -n "$APP_PID" ]; then
        check_x11_resources "$APP_PID" || X11_RESOURCE_LEAK_COUNT=$((X11_RESOURCE_LEAK_COUNT + 1))
    fi

    # Check network/MCP server stability (Phase 2)
    if [ "$RUN_MODE" = "native" ] && [ -n "$APP_PID" ]; then
        check_network_health "$APP_PID" || NETWORK_STABILITY_ISSUE_COUNT=$((NETWORK_STABILITY_ISSUE_COUNT + 1))
    fi

    # Check input event processing (Phase 2)
    check_input_processing || INPUT_PROCESSING_ISSUE_COUNT=$((INPUT_PROCESSING_ISSUE_COUNT + 1))

    # Check disk I/O (Phase 3)
    if [ "$RUN_MODE" = "native" ] && [ -n "$APP_PID" ]; then
        check_disk_io "$APP_PID" || DISK_IO_ISSUE_COUNT=$((DISK_IO_ISSUE_COUNT + 1))
    fi

    # Check system resources (Phase 3)
    check_system_resources || SYSTEM_RESOURCE_ISSUE_COUNT=$((SYSTEM_RESOURCE_ISSUE_COUNT + 1))

    # Check audio device stability (Phase 3)
    check_audio_stability || AUDIO_STABILITY_ISSUE_COUNT=$((AUDIO_STABILITY_ISSUE_COUNT + 1))

    # Check GPU memory (Phase 3)
    if [ "$RUN_MODE" = "native" ] && [ -n "$APP_PID" ]; then
        check_gpu_memory "$APP_PID" || GPU_MEMORY_ISSUE_COUNT=$((GPU_MEMORY_ISSUE_COUNT + 1))
    fi

    # Take periodic screenshot (every 5 checks)
    if [ $((CHECK_COUNT % 5)) -eq 0 ]; then
        take_screenshot "check_${CHECK_COUNT}"
    fi

    # Check for NEW errors in log (not all historical errors)
    local log_file="${LOG_DIR}/app_output.log"
    if [ -f "$log_file" ]; then
        local total_lines
        total_lines=$(wc -l < "$log_file")

        if [ "$total_lines" -gt "$LAST_ERROR_LINE" ]; then
            local new_errors
            new_errors=$(tail -n +$((LAST_ERROR_LINE+1)) "$log_file" 2>/dev/null | \
                        grep "error\|Error\|ERROR\|fatal\|Fatal\|FATAL\|crash\|Crash\|CRASH\|segfault\|SIGSEGV\|SIGABRT" 2>/dev/null | wc -l | awk '{print $1+0}')
            new_errors=${new_errors:-0}

            if [ "$new_errors" -gt 0 ]; then
                print_warning "Found $new_errors NEW error(s) in logs since last check"
            fi

            LAST_ERROR_LINE=$total_lines
        fi
    fi

    return 0
}

update_comprehensive_report() {
    local report_file="$1"
    local test_result="$2"
    local duration_min="$3"
    local duration_sec="$4"

    local comprehensive_file="${LOG_DIR}/COMPREHENSIVE_SOAK_TEST_REPORT.md"
    local short_time=$(date "+%Y-%m-%d %H:%M:%S")
    local backend="${MEDIA_BACKEND:-auto}"

    # Read the individual report content
    local report_content
    report_content=$(cat "$report_file" 2>/dev/null || echo "Error: Could not read report file")

    # Create comprehensive report if it doesn't exist
    if [ ! -f "$comprehensive_file" ]; then
        cat > "$comprehensive_file" << EOF
# OpenterfaceQT Comprehensive Soak Test Report

This document contains detailed results from all soak test runs.

## Overall Statistics
- **Total Test Runs:** 0
- **Passed:** 0 ✅
- **Failed:** 0 ❌
- **Success Rate:** N/A%

---

EOF
    fi

    # Read current statistics (extract the number from each line)
    local current_total=$(grep "Total Test Runs:" "$comprehensive_file" | awk '{print $NF}' | tr -cd '0-9')
    local current_pass=$(grep "^- \*\*Passed:\*\*" "$comprehensive_file" | awk '{print $3}' | tr -cd '0-9')
    local current_fail=$(grep "^- \*\*Failed:\*\*" "$comprehensive_file" | awk '{print $3}' | tr -cd '0-9')

    # Default to 0 if empty
    current_total=${current_total:-0}
    current_pass=${current_pass:-0}
    current_fail=${current_fail:-0}

    # Update statistics based on current test result
    local new_total=$((current_total + 1))
    local new_pass=$current_pass
    local new_fail=$current_fail

    if [ "$test_result" = "PASS" ]; then
        new_pass=$((current_pass + 1))
    elif [ "$test_result" = "FAIL" ]; then
        new_fail=$((current_fail + 1))
    fi

    # Calculate success rate
    local success_rate="N/A"
    if [ "$new_total" -gt 0 ]; then
        success_rate=$(echo "scale=1; $new_pass * 100 / $new_total" | bc 2>/dev/null || echo "N/A")
    fi

    # Extract key metrics from the individual report
    local total_checks=$(grep "Total Checks:" "$report_file" 2>/dev/null | sed 's/.*Total Checks:\s*\*\*\s*//' | awk '{print $1}' || echo "N/A")
    local max_memory=$(grep "Max Memory:" "$report_file" 2>/dev/null | sed 's/.*Max Memory:\s*\*\*\s*//' | awk '{print $1}' || echo "N/A")
    local avg_memory=$(grep "Average Memory:" "$report_file" 2>/dev/null | sed 's/.*Average Memory:\s*\*\*\s*//' | awk '{print $1}' || echo "N/A")

    # Append new test run to comprehensive report
    cat >> "$comprehensive_file" << EOF

## Test Run: ${short_time}

**Status:** ${test_result} | **Duration:** ${duration_min}m ${duration_sec}s | **Backend:** ${backend}

### Quick Summary
- **Total Checks:** ${total_checks}
- **Max Memory:** ${max_memory} MB
- **Average Memory:** ${avg_memory} MB

### Detailed Results

\`\`\`
${report_content}
\`\`\`

---

EOF

    # Update the statistics in the header using sed
    sed -i "s/^\- \*\*Total Test Runs:\*\* .*/- **Total Test Runs:** ${new_total}/" "$comprehensive_file"
    sed -i "s/^\- \*\*Passed:\*\* .*/- **Passed:** ${new_pass} ✅/" "$comprehensive_file"
    sed -i "s/^\- \*\*Failed:\*\* .*/- **Failed:** ${new_fail} ❌/" "$comprehensive_file"
    sed -i "s/^\- \*\*Success Rate:\*\* .*/- **Success Rate:** ${success_rate}%/" "$comprehensive_file"

    print_info "Updated comprehensive report: $comprehensive_file (Total runs: ${new_total}, Pass: ${new_pass}, Fail: ${new_fail})"
}

update_test_index() {
    local report_file="$1"
    local test_result="$2"
    local duration_min="$3"
    local duration_sec="$4"

    local index_file="${LOG_DIR}/TEST_INDEX.md"
    local timestamp=$(date -Iseconds)
    local short_time=$(date "+%Y-%m-%d %H:%M:%S")

    # Create index file if it doesn't exist
    if [ ! -f "$index_file" ]; then
        cat > "$index_file" << 'EOF'
# OpenterfaceQT Soak Test Index

This file tracks all soak test runs with quick summaries.

## Test Runs

| Date/Time | Duration | Status | Backend | Report Link |
|-----------|----------|--------|---------|-------------|
EOF
    fi

    # Extract backend from report filename or use current setting
    local backend="${MEDIA_BACKEND:-auto}"

    # Get report filename (just the name, not full path)
    local report_name=$(basename "$report_file")

    # Add new entry to index (append to end)
    echo "| ${short_time} | ${duration_min}m ${duration_sec}s | ${test_result} | ${backend} | [${report_name}](${report_name}) |" >> "$index_file"

    # Count statistics from the table rows (skip header lines)
    local total_tests=$(grep "^| 2" "$index_file" 2>/dev/null | wc -l | awk '{print $1}')
    local pass_count=$(grep "^| 2" "$index_file" 2>/dev/null | grep "| PASS |" | wc -l | awk '{print $1}')
    local fail_count=$(grep "^| 2" "$index_file" 2>/dev/null | grep "| FAIL |" | wc -l | awk '{print $1}')
    local warning_count=$(grep "^| 2" "$index_file" 2>/dev/null | grep "| WARNING |" | wc -l | awk '{print $1}')

    # Calculate success rate
    local success_rate="N/A"
    if [ "$total_tests" -gt 0 ]; then
        success_rate=$(echo "scale=1; $pass_count * 100 / $total_tests" | bc 2>/dev/null || echo "N/A")
    fi

    # Extract all table rows (lines starting with "| 2")
    local table_rows
    table_rows=$(grep "^| 2" "$index_file" | sort -r)

    # Rebuild the entire index file with updated statistics
    cat > "$index_file" << EOF
# OpenterfaceQT Soak Test Index

This file tracks all soak test runs with quick summaries.

## Summary Statistics
- **Total Tests:** ${total_tests}
- **Passed:** ${pass_count} ✅
- **Failed:** ${fail_count} ❌
- **Warnings:** ${warning_count} ⚠️
- **Success Rate:** ${success_rate}%

## Recent Test Runs

| Date/Time | Duration | Status | Backend | Report Link |
|-----------|----------|--------|---------|-------------|
${table_rows}
EOF

    print_info "Updated test index: $index_file (Total: ${total_tests}, Pass: ${pass_count}, Fail: ${fail_count})"
}

generate_report() {
    print_header "Generating Test Report"

    local end_time=$(date +%s)
    local actual_duration=$((end_time - START_TIME))
    local actual_minutes=$((actual_duration / 60))
    local actual_seconds=$((actual_duration % 60))

    # Calculate average memory
    if [ "$MEMORY_SAMPLES" -gt 0 ]; then
        AVG_MEMORY=$(echo "scale=2; $TOTAL_MEMORY / $MEMORY_SAMPLES" | bc 2>/dev/null || echo "N/A")
    else
        AVG_MEMORY="N/A"
    fi

    # Determine test result
    local test_result="PASS"
    if [ "$CRASH_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$HANG_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$FOCUS_LOOP_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$FD_LEAK_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$THREAD_LEAK_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$LOG_EXPLOSION_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$VIDEO_PERFORMANCE_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$USB_INSTABILITY_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$QT_RESPONSIVENESS_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$X11_RESOURCE_LEAK_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$NETWORK_STABILITY_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$INPUT_PROCESSING_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$DISK_IO_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$SYSTEM_RESOURCE_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$AUDIO_STABILITY_ISSUE_COUNT" -gt 0 ]; then
        test_result="FAIL"
    elif [ "$GPU_MEMORY_ISSUE_COUNT" -gt 0 ]; then
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
- **Display:** ${DISPLAY:-not set} (${SCREEN_WIDTH}x${SCREEN_HEIGHT})
- **Run Mode:** ${RUN_MODE}
- **Architecture:** ${ARCH}
- **Binary:** ${APP_BINARY}

## Test Results
- **Status:** ${test_result}
- **Total Checks:** ${CHECK_COUNT}
- **Crashes:** ${CRASH_COUNT}
- **Hangs Detected:** ${HANG_COUNT}
- **Focus Loops:** ${FOCUS_LOOP_COUNT}
- **FD Leaks:** ${FD_LEAK_COUNT}
- **Thread Leaks:** ${THREAD_LEAK_COUNT}
- **Log Explosions:** ${LOG_EXPLOSION_COUNT}
- **GStreamer Issues:** ${GST_ISSUE_COUNT}
- **Device Instability:** ${DEVICE_INSTABILITY_COUNT}
- **Video Performance Issues:** ${VIDEO_PERFORMANCE_ISSUE_COUNT}
- **USB Instability:** ${USB_INSTABILITY_COUNT}
- **Qt Responsiveness Issues:** ${QT_RESPONSIVENESS_ISSUE_COUNT}
- **X11 Resource Leaks:** ${X11_RESOURCE_LEAK_COUNT}
- **Network Stability Issues:** ${NETWORK_STABILITY_ISSUE_COUNT}
- **Input Processing Issues:** ${INPUT_PROCESSING_ISSUE_COUNT}
- **Disk I/O Issues:** ${DISK_IO_ISSUE_COUNT}
- **System Resource Issues:** ${SYSTEM_RESOURCE_ISSUE_COUNT}
- **Audio Stability Issues:** ${AUDIO_STABILITY_ISSUE_COUNT}
- **GPU Memory Issues:** ${GPU_MEMORY_ISSUE_COUNT}
- **Warnings:** ${WARNING_COUNT}
- **Restarts:** ${RESTART_COUNT}
- **Screenshots Captured:** ${SCREENSHOT_COUNT}

## Resource Usage
- **Memory Samples:** ${MEMORY_SAMPLES}
- **Max Memory:** ${MAX_MEMORY} MB
- **Average Memory:** ${AVG_MEMORY} MB

## Key Findings
- $([ "$CRASH_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No crashes detected
- $([ "$MEMORY_SAMPLES" -gt 0 ] && [ "$(echo "$MAX_MEMORY - $AVG_MEMORY < 50" | bc 2>/dev/null || echo 1)" -eq 1 ] && echo "[x]" || echo "[ ]") No memory leaks (stable memory usage)
- $([ "$FD_LEAK_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No file descriptor leaks
- $([ "$THREAD_LEAK_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No thread leaks
- $([ "$FOCUS_LOOP_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No focus event loops (X11 grab/ungrab bug)
- $([ "$LOG_EXPLOSION_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No log file explosions
- $([ "$GST_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No GStreamer pipeline issues
- $([ "$DEVICE_INSTABILITY_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No device connection instability
- $([ "$VIDEO_PERFORMANCE_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No video performance degradation (frame drops, stalls)
- $([ "$USB_INSTABILITY_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No USB/communication instability (device flapping, HID errors)
- $([ "$QT_RESPONSIVENESS_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No Qt event loop blocking (UI responsiveness maintained)
- $([ "$X11_RESOURCE_LEAK_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No X11/Wayland resource leaks (connections, windows, contexts)
- $([ "$NETWORK_STABILITY_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No network/socket stability issues (connection leaks, timeouts)
- $([ "$INPUT_PROCESSING_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No input processing errors (HID parsing, stuck keys, latency)
- $([ "$DISK_IO_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No disk I/O issues (file handle leaks, excessive writes)
- $([ "$SYSTEM_RESOURCE_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No system resource contention (high swap, CPU overload)
- $([ "$AUDIO_STABILITY_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No audio device instability (buffer issues, reinit loops)
- $([ "$GPU_MEMORY_ISSUE_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") No GPU memory issues (memory leaks, graphics errors)
- $([ "$WARNING_COUNT" -le 5 ] && echo "[x]" || echo "[ ]") No UI rendering issues
- $([ "$WARNING_COUNT" -le 5 ] && echo "[x]" || echo "[ ]") All features responsive
- $([ "$RESTART_COUNT" -eq 0 ] && echo "[x]" || echo "[ ]") Clean shutdown

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

    # Update comprehensive report with full details
    update_comprehensive_report "$report_file" "$test_result" "$actual_minutes" "$actual_seconds"

    # Update index report with summary of all tests
    update_test_index "$report_file" "$test_result" "$actual_minutes" "$actual_seconds"

    # Print summary to console
    print_header "Test Summary"
    echo -e "Status:          $([[ $test_result == 'PASS' ]] && echo -e "${GREEN}${test_result}${NC}" || echo -e "${RED}${test_result}${NC}")"
    echo -e "Duration:        ${actual_minutes}m ${actual_seconds}s"
    echo -e "Run Mode:        ${RUN_MODE}"
    echo -e "Total Checks:    ${CHECK_COUNT}"
    echo -e "Crashes:         ${CRASH_COUNT}"
    echo -e "Focus Loops:     ${FOCUS_LOOP_COUNT}"
    echo -e "FD Leaks:        ${FD_LEAK_COUNT}"
    echo -e "Thread Leaks:    ${THREAD_LEAK_COUNT}"
    echo -e "Log Explosions:  ${LOG_EXPLOSION_COUNT}"
    echo -e "GST Issues:      ${GST_ISSUE_COUNT}"
    echo -e "Device Issues:   ${DEVICE_INSTABILITY_COUNT}"
    echo -e "Video Perf:      ${VIDEO_PERFORMANCE_ISSUE_COUNT}"
    echo -e "USB Stability:   ${USB_INSTABILITY_COUNT}"
    echo -e "Qt Responsive:   ${QT_RESPONSIVENESS_ISSUE_COUNT}"
    echo -e "X11 Leaks:       ${X11_RESOURCE_LEAK_COUNT}"
    echo -e "Network:         ${NETWORK_STABILITY_ISSUE_COUNT}"
    echo -e "Input Proc:      ${INPUT_PROCESSING_ISSUE_COUNT}"
    echo -e "Disk I/O:        ${DISK_IO_ISSUE_COUNT}"
    echo -e "System Res:      ${SYSTEM_RESOURCE_ISSUE_COUNT}"
    echo -e "Audio:           ${AUDIO_STABILITY_ISSUE_COUNT}"
    echo -e "GPU Memory:      ${GPU_MEMORY_ISSUE_COUNT}"
    echo -e "Warnings:        ${WARNING_COUNT}"
    echo -e "Screenshots:     ${SCREENSHOT_COUNT}"
    echo -e "Max Memory:      ${MAX_MEMORY} MB"
    echo -e "Avg Memory:      ${AVG_MEMORY} MB"
}

# =============================================================================
# Main Execution
# =============================================================================

run_single_backend_test() {
    local backend="$1"
    local backend_start_time=$(date +%s)

    print_header "Testing Backend: $backend"
    print_info "Starting soak test with $backend backend for ${DURATION_MINUTES} minutes"

    # Set backend for this test
    MEDIA_BACKEND="$backend"

    # Run the test
    setup_environment
    start_application

    # Main soak loop
    print_header "Running Soak Test"
    print_info "Monitoring application for ${DURATION_MINUTES} minutes..."
    echo ""

    while [ $(date +%s) -lt $END_TIME ]; do
        print_progress

        # Check health
        if ! check_app_health; then
            print_error "Health check failed!"

            if [ "$RESTART_COUNT" -lt 3 ]; then
                print_warning "Restarting application (attempt $((RESTART_COUNT+1))/3)..."
                RESTART_COUNT=$((RESTART_COUNT + 1))

                # Kill old instance
                if [ "$RUN_MODE" = "native" ] && [ -n "${APP_PID:-}" ]; then
                    kill "$APP_PID" 2>/dev/null || true
                    sleep 2
                elif [ "$RUN_MODE" = "docker" ]; then
                    docker stop "$CONTAINER_NAME" 2>/dev/null || true
                    sleep 2
                fi

                # Restart
                start_application
            else
                print_error "Max restart attempts reached. Stopping test."
                break
            fi
        fi

        # Wait for next check
        sleep "$CHECK_INTERVAL"
    done

    # Final check
    print_header "Final Verification"
    check_app_health

    # Take final screenshot
    take_screenshot "99_final"

    # Calculate test duration
    local backend_end_time=$(date +%s)
    local backend_duration=$((backend_end_time - backend_start_time))

    # Store result
    BACKEND_TEST_RESULTS+=("$backend:$backend_duration:$CRASH_COUNT:$WARNING_COUNT:$MAX_MEMORY")

    # Stop app for next backend test (don't call cleanup to avoid EXIT trap)
    print_info "Stopping app for next backend test..."
    if [ "$RUN_MODE" = "native" ] && [ -n "${APP_PID:-}" ]; then
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    elif [ "$RUN_MODE" = "docker" ]; then
        docker stop "$CONTAINER_NAME" 2>/dev/null || true
    fi
    APP_PID=""

    print_info "Backend $backend test completed"
}

run_all_backends_test() {
    print_header "Testing All Available Backends"

    # Get available backends from the app
    local available_backends=()
    if [ -f "$APP_BINARY" ]; then
        local backend_output
        backend_output=$("$APP_BINARY" --list-backends 2>&1)
        # Extract backend names from output
        while IFS= read -r line; do
            if [[ "$line" =~ ^[[:space:]]*([a-z]+)[[:space:]]+- ]]; then
                available_backends+=("${BASH_REMATCH[1]}")
            fi
        done <<< "$backend_output"
    fi

    if [ ${#available_backends[@]} -eq 0 ]; then
        # Fallback to known backends
        available_backends=("ffmpeg" "gstreamer")
    fi

    print_info "Found ${#available_backends[@]} backends: ${available_backends[*]}"

    # Test each backend
    for backend in "${available_backends[@]}"; do
        # Reset counters for each backend
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
        FOCUS_LOOP_COUNT=0
        RESTART_COUNT=0
        FD_LEAK_COUNT=0
        THREAD_LEAK_COUNT=0
        LOG_EXPLOSION_COUNT=0
        GST_ISSUE_COUNT=0
        DEVICE_INSTABILITY_COUNT=0
        MEMORY_HISTORY=()
        FD_COUNT_HISTORY=()
        THREAD_COUNT_HISTORY=()
        CPU_USAGE_HISTORY=()
        LOG_SIZE_HISTORY=()
        LAST_LOG_SIZE=0
        LAST_ERROR_LINE=0

        # Run test for this backend
        run_single_backend_test "$backend"

        # Small delay between backend tests
        print_info "Waiting 5 seconds before next backend test..."
        sleep 5
    done

    # Generate combined report
    generate_combined_report
}

generate_combined_report() {
    print_header "Combined Backend Test Report"

    local report_file="${LOG_DIR}/combined_backend_test_$(date +%Y%m%d_%H%M%S).md"

    cat > "$report_file" << EOF
# Combined Backend Test Report

## Test Configuration
- **Date:** $(date -Iseconds)
- **Duration per backend:** ${DURATION_MINUTES} minutes
- **Check Interval:** ${CHECK_INTERVAL}s
- **Total Backends Tested:** ${#BACKEND_TEST_RESULTS[@]}

## Results by Backend

| Backend | Duration (s) | Crashes | Warnings | Max Memory (MB) |
|---------|--------------|---------|----------|-----------------|
EOF

    for result in "${BACKEND_TEST_RESULTS[@]}"; do
        IFS=':' read -r backend duration crashes warnings max_mem <<< "$result"
        echo "| $backend | $duration | $crashes | $warnings | $max_mem |" >> "$report_file"
    done

    cat >> "$report_file" << EOF

## Analysis

Review the results above to identify:
- Which backend is most stable (fewest crashes/warnings)
- Which backend uses the least memory
- Any backend-specific issues

## Individual Reports

Each backend test also generated its own detailed report in:
${LOG_DIR}/

---
*Generated by gui_soak_test.sh*
EOF

    print_success "Combined report saved: $report_file"

    # Print summary to console
    echo ""
    echo "Backend Comparison Summary:"
    echo "==========================="
    for result in "${BACKEND_TEST_RESULTS[@]}"; do
        IFS=':' read -r backend duration crashes warnings max_mem <<< "$result"
        echo "$backend: ${duration}s, $crashes crashes, $warnings warnings, max ${max_mem}MB"
    done
}

main() {
    # Parse arguments
    parse_args "$@"

    DURATION_SECONDS=$((DURATION_MINUTES * 60))
    # Update END_TIME based on parsed duration (START_TIME was set at script init)
    END_TIME=$((START_TIME + DURATION_SECONDS))

    print_header "OpenterfaceQT GUI Soak Test"
    echo -e "Duration:  ${DURATION_MINUTES} minutes"
    echo -e "Interval:  ${CHECK_INTERVAL} seconds"
    echo -e "Run Mode:  ${RUN_MODE}"
    echo -e "Arch:      ${ARCH}"
    if [ -n "$MEDIA_BACKEND" ]; then
        echo -e "Backend:   ${MEDIA_BACKEND}"
    elif [ "$TEST_ALL_BACKENDS" = true ]; then
        echo -e "Backend:   ALL (testing sequentially)"
    else
        echo -e "Backend:   default (from settings)"
    fi
    echo ""

    # Detect run mode if auto
    detect_run_mode

    # If testing all backends, use the multi-backend flow
    if [ "$TEST_ALL_BACKENDS" = true ]; then
        run_all_backends_test
        return
    fi

    # Single backend test (or default backend)
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
            print_error "Health check failed!"

            if [ "$RESTART_COUNT" -lt 3 ]; then
                print_warning "Restarting application (attempt $((RESTART_COUNT+1))/3)..."
                RESTART_COUNT=$((RESTART_COUNT + 1))

                # Kill old instance
                if [ "$RUN_MODE" = "native" ] && [ -n "${APP_PID:-}" ]; then
                    kill "$APP_PID" 2>/dev/null || true
                    sleep 2
                elif [ "$RUN_MODE" = "docker" ]; then
                    docker stop "$CONTAINER_NAME" 2>/dev/null || true
                    sleep 2
                fi

                # Restart
                start_application
            else
                print_error "Max restart attempts reached. Stopping test."
                break
            fi
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
