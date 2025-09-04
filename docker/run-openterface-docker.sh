#!/bin/bash
# =============================================================================
# Openterface QT Docker GUI Launcher Script
# =============================================================================
#
# This script simplifies running the Openterface QT Docker container with
# proper GUI support and hardware access.
#
# Usage:
#   ./run-openterface-docker.sh [OPTIONS]
#
# Options:
#   --no-hardware    Run without hardware access (testing only)
#   --shell          Start bash shell instead of the application
#   --help           Show this help message
#
# =============================================================================

set -e

# Configuration
DOCKER_IMAGE="openterface-test"
CONTAINER_NAME="openterface-qt-$(date +%s)"

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
    echo "Openterface QT Docker GUI Launcher"
    echo "=================================="
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --no-hardware    Run without hardware access (testing only)"
    echo "  --shell          Start bash shell instead of the application"
    echo "  --help           Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Run with full GUI and hardware support"
    echo "  $0 --shell           # Start interactive shell for debugging"
    echo "  $0 --no-hardware     # Run without hardware access"
    echo ""
}

check_prerequisites() {
    print_info "Checking prerequisites..."
    
    # Check if Docker is installed
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    # Check if Docker is running
    if ! docker info &> /dev/null; then
        print_error "Docker is not running or you don't have permission to access it"
        print_info "Try: sudo systemctl start docker"
        exit 1
    fi
    
    # Check if image exists
    if ! docker image inspect "$DOCKER_IMAGE" &> /dev/null; then
        print_error "Docker image '$DOCKER_IMAGE' not found"
        print_info "Please build the image first with:"
        print_info "  docker build -f Dockerfile.openterface-qt -t $DOCKER_IMAGE ."
        exit 1
    fi
    
    print_success "Prerequisites check passed"
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
    
    print_success "X11 setup completed"
}

build_docker_command() {
    local cmd="docker run -it --rm"
    
    # Add container name
    cmd+=" --name $CONTAINER_NAME"
    
    # Detect if we're in a remote environment
    local is_remote=false
    if [ -n "$SSH_CLIENT" ] || [ -n "$SSH_TTY" ] || [ -n "$SSH_CONNECTION" ]; then
        is_remote=true
    fi
    
    # Add X11 support - different approach for remote vs local
    if $is_remote; then
        print_info "Configuring for remote environment..."
        # For remote environments, use network access and more permissive settings
        cmd+=" --network host"
        cmd+=" -e DISPLAY=$DISPLAY"
        cmd+=" -e QT_X11_NO_MITSHM=1"
        cmd+=" -e QT_QPA_PLATFORM=xcb"
        
        # Add X11 authority and runtime directory
        if [ -f "$HOME/.Xauthority" ]; then
            cmd+=" -v $HOME/.Xauthority:/home/openterface/.Xauthority:ro"
            cmd+=" -e XAUTHORITY=/home/openterface/.Xauthority"
        fi
        
        # Also mount X11 socket as backup
        cmd+=" -v /tmp/.X11-unix:/tmp/.X11-unix:rw"
    else
        print_info "Configuring for local environment..."
        # For local environments, use socket mounting
        cmd+=" -v /tmp/.X11-unix:/tmp/.X11-unix:rw"
        cmd+=" -e DISPLAY=$DISPLAY"
        cmd+=" -e QT_X11_NO_MITSHM=1"
        cmd+=" -e QT_QPA_PLATFORM=xcb"
        
        # Add X11 authority if it exists
        if [ -f "$HOME/.Xauthority" ]; then
            cmd+=" -v $HOME/.Xauthority:/home/openterface/.Xauthority:ro"
        fi
    fi
    
    # Add hardware access unless disabled
    if [ "$NO_HARDWARE" != "true" ]; then
        cmd+=" --privileged"
        cmd+=" -v /dev:/dev"
        print_info "Hardware access enabled"
    else
        print_warning "Hardware access disabled"
    fi
    
    # Add image name
    cmd+=" $DOCKER_IMAGE"
    
    # Add command override if needed
    if [ "$START_SHELL" = "true" ]; then
        cmd+=" /bin/bash"
        print_info "Starting interactive shell"
    else
        print_info "Starting Openterface QT application"
    fi
    
    echo "$cmd"
}

cleanup() {
    print_info "Cleaning up..."
    
    # Remove container if it exists
    if docker ps -a --format "table {{.Names}}" | grep -q "^$CONTAINER_NAME$"; then
        docker rm -f "$CONTAINER_NAME" &> /dev/null || true
    fi
    
    # Restore X11 access controls (optional)
    if command -v xhost &> /dev/null; then
        xhost -local: &> /dev/null || true
    fi
}

# Parse command line arguments
NO_HARDWARE=false
START_SHELL=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --no-hardware)
            NO_HARDWARE=true
            shift
            ;;
        --shell)
            START_SHELL=true
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Main execution
main() {
    echo "🚀 Openterface QT Docker GUI Launcher"
    echo "====================================="
    echo ""
    
    # Set up cleanup trap
    trap cleanup EXIT
    
    # Run checks and setup
    check_prerequisites
    setup_x11
    
    # Build and display the command
    DOCKER_CMD=$(build_docker_command)
    echo ""
    print_info "Docker command:"
    echo "$DOCKER_CMD"
    echo ""
    
    # Ask for confirmation
    if [ "$START_SHELL" != "true" ]; then
        print_info "Starting Openterface QT application..."
        print_info "The application will open in a new window"
        print_info "Press Ctrl+C to stop the container"
        echo ""
    fi
    
    # Run the container
    print_success "Launching container..."
    
    # Get the Docker command and run it safely
    DOCKER_CMD=$(build_docker_command)
    
    # Extract components and run directly (safer than eval)
    if [ "$START_SHELL" = "true" ]; then
        if [ -n "$SSH_CLIENT" ] || [ -n "$SSH_TTY" ] || [ -n "$SSH_CONNECTION" ]; then
            # Remote environment
            exec docker run -it --rm \
                --name "$CONTAINER_NAME" \
                --network host \
                -e DISPLAY="$DISPLAY" \
                -e QT_X11_NO_MITSHM=1 \
                -e QT_QPA_PLATFORM=xcb \
                -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
                $([ -f "$HOME/.Xauthority" ] && echo "-v $HOME/.Xauthority:/home/openterface/.Xauthority:ro -e XAUTHORITY=/home/openterface/.Xauthority") \
                $([ "$NO_HARDWARE" != "true" ] && echo "--privileged -v /dev:/dev") \
                "$DOCKER_IMAGE" /bin/bash
        else
            # Local environment
            exec docker run -it --rm \
                --name "$CONTAINER_NAME" \
                -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
                -e DISPLAY="$DISPLAY" \
                -e QT_X11_NO_MITSHM=1 \
                -e QT_QPA_PLATFORM=xcb \
                $([ -f "$HOME/.Xauthority" ] && echo "-v $HOME/.Xauthority:/home/openterface/.Xauthority:ro") \
                $([ "$NO_HARDWARE" != "true" ] && echo "--privileged -v /dev:/dev") \
                "$DOCKER_IMAGE" /bin/bash
        fi
    else
        if [ -n "$SSH_CLIENT" ] || [ -n "$SSH_TTY" ] || [ -n "$SSH_CONNECTION" ]; then
            # Remote environment
            exec docker run -it --rm \
                --name "$CONTAINER_NAME" \
                --network host \
                -e DISPLAY="$DISPLAY" \
                -e QT_X11_NO_MITSHM=1 \
                -e QT_QPA_PLATFORM=xcb \
                -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
                $([ -f "$HOME/.Xauthority" ] && echo "-v $HOME/.Xauthority:/home/openterface/.Xauthority:ro -e XAUTHORITY=/home/openterface/.Xauthority") \
                $([ "$NO_HARDWARE" != "true" ] && echo "--privileged -v /dev:/dev") \
                "$DOCKER_IMAGE"
        else
            # Local environment
            exec docker run -it --rm \
                --name "$CONTAINER_NAME" \
                -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
                -e DISPLAY="$DISPLAY" \
                -e QT_X11_NO_MITSHM=1 \
                -e QT_QPA_PLATFORM=xcb \
                $([ -f "$HOME/.Xauthority" ] && echo "-v $HOME/.Xauthority:/home/openterface/.Xauthority:ro") \
                $([ "$NO_HARDWARE" != "true" ] && echo "--privileged -v /dev:/dev") \
                "$DOCKER_IMAGE"
        fi
    fi
}

# Handle Ctrl+C gracefully
trap 'print_info "Interrupted by user"; exit 130' INT

# Run main function
main "$@"
