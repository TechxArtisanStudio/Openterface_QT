#!/bin/bash
# =============================================================================
# Openterface QT Static Docker GUI Launcher Script
# =============================================================================
#
# This script simplifies running the Openterface QT Static Docker container with
# proper GUI support and hardware access. Optimized for static builds.
#
# Usage:
#   ./run-openterface-static-docker.sh [OPTIONS]
#
# Options:
#   --no-hardware    Run without hardware access (testing only)
#   --shell          Start bash shell instead of the application
#   --build          Build the image before running
#   --help           Show this help message
#
# =============================================================================

set -e

# Configuration
DOCKER_IMAGE="openterface-test-static"
CONTAINER_NAME="openterface-qt-static-$(date +%s)"
DOCKERFILE="Dockerfile.openterface-test-static"

# Colors for output (safer approach)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Functions
print_info() {
    printf "${BLUE}ℹ️  %s${NC}\n" "$1"
}

print_success() {
    printf "${GREEN}✅ %s${NC}\n" "$1"
}

print_warning() {
    printf "${YELLOW}⚠️  %s${NC}\n" "$1"
}

print_error() {
    printf "${RED}❌ %s${NC}\n" "$1"
}

show_help() {
    cat << EOF
Openterface QT Static Docker Launcher

Usage: $0 [OPTIONS]

Options:
  --no-hardware    Run without hardware access (GUI testing only)
  --shell          Start bash shell instead of the application
  --build          Build the Docker image before running
  --help           Show this help message

Environment:
  This script sets up GUI forwarding and hardware access for the
  statically-linked Openterface QT application in a Docker container.

Examples:
  $0                    # Run the static application with full hardware access
  $0 --shell            # Start a shell in the container for debugging
  $0 --build            # Build image and run the application
  $0 --no-hardware      # Run without hardware access (GUI only)

Hardware Support:
  - USB HID devices (Openterface Mini-KVM)
  - USB serial devices (CH340 chip)
  - Audio devices
  - Video devices

EOF
}

check_dependencies() {
    print_info "Checking dependencies..."
    
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        print_error "Docker daemon is not running or user lacks permissions"
        print_info "Try: sudo systemctl start docker"
        print_info "Or add user to docker group: sudo usermod -aG docker $USER"
        exit 1
    fi
    
    print_success "Docker is available and accessible"
}

setup_x11() {
    print_info "Setting up X11 display..."
    
    # Detect if we're in a remote environment
    local is_remote=false
    if [ -n "$SSH_CLIENT" ] || [ -n "$SSH_TTY" ] || [ -n "$SSH_CONNECTION" ]; then
        is_remote=true
        print_info "Remote SSH environment detected"
    fi
    
    # Check if DISPLAY is set
    if [ -z "$DISPLAY" ]; then
        if $is_remote; then
            print_warning "DISPLAY not set in remote environment"
            print_info "Setting DISPLAY to :0 (local X server on remote machine)"
            export DISPLAY=:0
        else
            print_warning "DISPLAY environment variable not set, trying :0"
            export DISPLAY=:0
        fi
    fi
    
    print_info "Using DISPLAY: $DISPLAY"
    
    # For remote environments, use different approach
    if $is_remote; then
        print_info "Configuring for remote environment..."
        # Set broader X11 permissions for remote access
        if command -v xhost &> /dev/null; then
            sudo xhost +local:root &> /dev/null || print_warning "Could not run 'xhost +local:root'"
            sudo xhost +local: &> /dev/null || print_warning "Could not run 'xhost +local:'"
            # For Docker containers, sometimes we need broader access
            sudo xhost +localhost &> /dev/null || true
        else
            print_warning "xhost not found, X11 forwarding may not work"
        fi
    else
        # Check if X11 socket exists for local environment
        if [ ! -S "/tmp/.X11-unix/X${DISPLAY#*:}" ]; then
            print_warning "X11 socket not found, GUI may not work"
            print_info "Make sure X11 is running and DISPLAY is correct"
        fi
        
        # Allow local connections to X server
        print_info "Allowing Docker containers to access X11..."
        if command -v xhost &> /dev/null; then
            xhost +local: &> /dev/null || print_warning "Could not run 'xhost +local:'"
        else
            print_warning "xhost not found, X11 forwarding may not work"
        fi
    fi
    
    X11_ARGS="--env DISPLAY=$DISPLAY"
    
    # Mount X11 socket for local environments
    if [ ! "$is_remote" = true ] && [ -S "/tmp/.X11-unix/X${DISPLAY#*:}" ]; then
        X11_ARGS="$X11_ARGS --volume /tmp/.X11-unix:/tmp/.X11-unix:rw"
        print_success "X11 socket will be mounted"
    else
        print_info "Using network-based X11 forwarding"
    fi
}

setup_hardware_access() {
    print_info "Setting up hardware access..."
    
    HARDWARE_ARGS=""
    
    # USB device access
    HARDWARE_ARGS="$HARDWARE_ARGS --device /dev/bus/usb:/dev/bus/usb"
    
    # HID device access (more permissive for static builds)
    if ls /dev/hidraw* &> /dev/null; then
        for device in /dev/hidraw*; do
            HARDWARE_ARGS="$HARDWARE_ARGS --device $device:$device"
        done
        print_success "HID devices will be accessible"
    else
        print_warning "No HID devices found (hardware may not be connected)"
    fi
    
    # Serial device access
    if ls /dev/ttyUSB* &> /dev/null; then
        for device in /dev/ttyUSB*; do
            HARDWARE_ARGS="$HARDWARE_ARGS --device $device:$device"
        done
        print_success "USB serial devices will be accessible"
    else
        print_warning "No USB serial devices found"
    fi
    
    # Audio device access (minimal for static builds)
    if [ -d "/dev/snd" ]; then
        HARDWARE_ARGS="$HARDWARE_ARGS --device /dev/snd:/dev/snd"
        print_success "Audio devices will be accessible"
    fi
    
    # Add necessary privileges
    HARDWARE_ARGS="$HARDWARE_ARGS --privileged"
    
    print_success "Hardware access configured"
}

build_image() {
    print_info "Building Docker image: $DOCKER_IMAGE"
    
    if [ ! -f "$DOCKERFILE" ]; then
        print_error "Dockerfile not found: $DOCKERFILE"
        print_info "Make sure you're running this script from the docker directory"
        exit 1
    fi
    
    docker build -f "$DOCKERFILE" -t "$DOCKER_IMAGE" . || {
        print_error "Failed to build Docker image"
        exit 1
    }
    
    print_success "Docker image built successfully: $DOCKER_IMAGE"
}

check_image() {
    if ! docker image inspect "$DOCKER_IMAGE" &> /dev/null; then
        print_warning "Docker image $DOCKER_IMAGE not found"
        print_info "Building image automatically..."
        build_image
    else
        print_success "Docker image $DOCKER_IMAGE is available"
    fi
}

run_container() {
    local shell_mode=$1
    local hardware_access=$2
    
    print_info "Starting Openterface QT Static container..."
    
    # Build base command
    local docker_cmd="docker run -it --rm --name $CONTAINER_NAME"
    
    # Add X11 support
    docker_cmd="$docker_cmd $X11_ARGS"
    
    # Add hardware access if enabled
    if [ "$hardware_access" = true ]; then
        docker_cmd="$docker_cmd $HARDWARE_ARGS"
    else
        print_warning "Running without hardware access (GUI testing only)"
    fi
    
    # Add image
    docker_cmd="$docker_cmd $DOCKER_IMAGE"
    
    # Add command
    if [ "$shell_mode" = true ]; then
        docker_cmd="$docker_cmd bash"
        print_info "Starting interactive shell in container"
    else
        print_info "Starting Openterface QT static application"
    fi
    
    print_info "Docker command: $docker_cmd"
    print_info "Container name: $CONTAINER_NAME"
    
    # Execute the command
    eval "$docker_cmd"
}

cleanup() {
    print_info "Cleaning up..."
    
    # Remove X11 permissions
    if command -v xhost &> /dev/null; then
        xhost -local: &> /dev/null || true
    fi
    
    # Force remove container if it exists
    if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        print_info "Removing container: $CONTAINER_NAME"
        docker rm -f "$CONTAINER_NAME" &> /dev/null || true
    fi
}

# Main execution
main() {
    local shell_mode=false
    local hardware_access=true
    local build_image_flag=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --shell)
                shell_mode=true
                shift
                ;;
            --no-hardware)
                hardware_access=false
                shift
                ;;
            --build)
                build_image_flag=true
                shift
                ;;
            --help)
                show_help
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                print_info "Use --help for usage information"
                exit 1
                ;;
        esac
    done
    
    # Set up trap for cleanup
    trap cleanup EXIT
    
    print_info "🚀 Openterface QT Static Docker Launcher"
    print_info "========================================"
    
    # Check dependencies
    check_dependencies
    
    # Build image if requested
    if [ "$build_image_flag" = true ]; then
        build_image
    else
        check_image
    fi
    
    # Setup X11
    setup_x11
    
    # Setup hardware access if enabled
    if [ "$hardware_access" = true ]; then
        setup_hardware_access
    fi
    
    # Run the container
    run_container "$shell_mode" "$hardware_access"
    
    print_success "Container execution completed"
}

# Run main function with all arguments
main "$@"
