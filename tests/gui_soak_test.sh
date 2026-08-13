#!/bin/bash
# =============================================================================
# OpenterfaceQT GUI Soak Test Automation
# =============================================================================
# Purpose: Long-running stability test for the OpenterfaceQT GUI application
#
# Features:
#   - Runs app natively OR in Docker (auto-detect)
#   - Monitors memory usage, CPU, and process health
#   - Takes periodic screenshots for visual verification
#   - Captures and analyzes app output logs
#   - Detects crashes, hangs, and resource leaks
#   - Detects focus event loops (the bug we just fixed)
#   - Tracks memory growth rate for leak detection
#   - Generates comprehensive test report
#
# Usage:
#   ./tests/gui_soak_test.sh [OPTIONS] [DURATION_MINUTES] [CHECK_INTERVAL_SECONDS]
#   ./tests/gui_soak_test.sh --native 60 30    # Native mode, 60 minutes, 30s interval
#   ./tests/gui_soak_test.sh --docker 60 30    # Docker mode (if image exists)
#   ./tests/gui_soak_test.sh 30 15             # Auto-detect, 30 minutes, 15s interval
#
# Options:
#   --native     Run application natively (requires DISPLAY)
#   --docker     Run application in Docker container
#   --no-xvfb    Skip Xvfb setup (use existing DISPLAY)
#   --help       Show this help
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

# Memory tracking for leak detection
declare -a MEMORY_HISTORY=()
MEMORY_HISTORY_TIMESTAMPS=()

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

        # Start app
        "$APP_BINARY" > "$log_file" 2>&1 &
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
            bash -c "./${APP_BINARY_NAME}" > "$log_file" 2>&1 &

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
- **Warnings:** ${WARNING_COUNT}
- **Restarts:** ${RESTART_COUNT}
- **Screenshots Captured:** ${SCREENSHOT_COUNT}

## Resource Usage
- **Memory Samples:** ${MEMORY_SAMPLES}
- **Max Memory:** ${MAX_MEMORY} MB
- **Average Memory:** ${AVG_MEMORY} MB

## Key Findings
- [ ] No crashes detected
- [ ] No memory leaks (stable memory usage)
- [ ] No focus event loops (X11 grab/ungrab bug)
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
    echo -e "Run Mode:        ${RUN_MODE}"
    echo -e "Total Checks:    ${CHECK_COUNT}"
    echo -e "Crashes:         ${CRASH_COUNT}"
    echo -e "Focus Loops:     ${FOCUS_LOOP_COUNT}"
    echo -e "Warnings:        ${WARNING_COUNT}"
    echo -e "Screenshots:     ${SCREENSHOT_COUNT}"
    echo -e "Max Memory:      ${MAX_MEMORY} MB"
    echo -e "Avg Memory:      ${AVG_MEMORY} MB"
}

# =============================================================================
# Main Execution
# =============================================================================

main() {
    # Parse arguments
    parse_args "$@"

    DURATION_SECONDS=$((DURATION_MINUTES * 60))

    print_header "OpenterfaceQT GUI Soak Test"
    echo -e "Duration:  ${DURATION_MINUTES} minutes"
    echo -e "Interval:  ${CHECK_INTERVAL} seconds"
    echo -e "Run Mode:  ${RUN_MODE}"
    echo -e "Arch:      ${ARCH}"
    echo ""

    # Detect run mode if auto
    detect_run_mode

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
