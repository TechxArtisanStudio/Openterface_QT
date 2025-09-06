#!/bin/bash
# =============================================================================
# Local Docker Test Script
# =============================================================================
#
# This script performs local testing of the testos/Dockerfile.ubuntu-test-shared
# similar to what the GitHub Actions workflow does, but for local development
# and debugging purposes.
#
# Usage:
#   ./test-docker-local.sh [OPTIONS]
#
# Options:
#   --quick     Run only quick tests
#   --full      Run full test suite (default)
#   --clean     Clean up Docker resources after tests
#   --help      Show this help message
#
# =============================================================================

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCKER_IMAGE="openterface-test-local"
DOCKER_TAG="$(date +%Y%m%d-%H%M%S)"
FULL_IMAGE_NAME="${DOCKER_IMAGE}:${DOCKER_TAG}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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
    echo "Local Docker Test Script for Openterface QT"
    echo "==========================================="
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --quick     Run only quick tests"
    echo "  --full      Run full test suite (default)"
    echo "  --clean     Clean up Docker resources after tests"
    echo "  --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Run full test suite"
    echo "  $0 --quick           # Run quick tests only"
    echo "  $0 --full --clean    # Run full tests and clean up"
    echo ""
}

check_prerequisites() {
    print_info "Checking prerequisites..."
    
    # Check if Docker is installed and running
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        print_error "Docker is not running or you don't have permission to access it"
        exit 1
    fi
    
    # Check if we're in the right directory
    if [ ! -f "$PROJECT_ROOT/docker/testos/Dockerfile.ubuntu-test-shared" ]; then
        print_error "testos/Dockerfile.ubuntu-test-shared not found"
        print_info "Please run this script from the project root or docker directory"
        exit 1
    fi
    
    print_success "Prerequisites check passed"
}

build_docker_image() {
    print_info "Building Docker image: $FULL_IMAGE_NAME"
    
    cd "$PROJECT_ROOT"
    
    if docker build \
        -f docker/testos/Dockerfile.ubuntu-test-shared \
        -t "$FULL_IMAGE_NAME" \
        docker/; then
        print_success "Docker image built successfully"
    else
        print_error "Failed to build Docker image"
        exit 1
    fi
    
    # Show image information
    print_info "Image information:"
    docker images | grep "$DOCKER_IMAGE" | head -1
}

test_container_startup() {
    print_info "Testing container startup..."
    
    # Test container can start
    if CONTAINER_ID=$(docker run -d \
        --name "openterface-test-startup-$$" \
        "$FULL_IMAGE_NAME" \
        sleep 10); then
        
        print_success "Container started successfully (ID: ${CONTAINER_ID:0:12})"
        
        # Wait and check if container is still running
        sleep 2
        if docker ps | grep -q "$CONTAINER_ID"; then
            print_success "Container is running properly"
        else
            print_error "Container stopped unexpectedly"
            docker logs "$CONTAINER_ID"
        fi
        
        # Clean up
        docker stop "$CONTAINER_ID" &> /dev/null || true
        docker rm "$CONTAINER_ID" &> /dev/null || true
    else
        print_error "Failed to start container"
        exit 1
    fi
}

test_installation() {
    print_info "Testing installation components..."
    
    docker run --rm \
        --name "openterface-test-install-$$" \
        "$FULL_IMAGE_NAME" \
        bash -c "
            echo '🔍 Checking user setup...'
            whoami
            groups
            
            echo '🔍 Checking launcher script...'
            ls -la /usr/local/bin/start-openterface.sh || echo 'Launcher script not found'
            
            echo '🔍 Checking udev rules...'
            ls -la /etc/udev/rules.d/*openterface* || echo 'No udev rules found'
            
            echo '🔍 Checking for openterface binary...'
            which openterfaceQT || find /usr /opt -name 'openterfaceQT' -type f 2>/dev/null | head -1 || echo 'Binary not found'
            
            echo '✅ Installation test completed'
        "
}

test_dependencies() {
    print_info "Testing dependencies..."
    
    docker run --rm \
        --name "openterface-test-deps-$$" \
        "$FULL_IMAGE_NAME" \
        bash -c "
            echo '🔍 Checking Qt6 libraries...'
            dpkg -l | grep libqt6 | wc -l | xargs echo 'Qt6 packages found:'
            
            echo '🔍 Checking multimedia libraries...'
            dpkg -l | grep -E 'multimedia|ffmpeg|gstreamer' | wc -l | xargs echo 'Multimedia packages found:'
            
            echo '🔍 Checking hardware interface libraries...'
            dpkg -l | grep -E 'libusb|libudev' | wc -l | xargs echo 'Hardware interface packages found:'
            
            echo '✅ Dependencies test completed'
        "
}

test_quick() {
    print_info "Running quick test suite..."
    
    build_docker_image
    test_container_startup
    
    print_success "Quick test suite completed"
}

test_full() {
    print_info "Running full test suite..."
    
    build_docker_image
    test_container_startup
    test_installation
    test_dependencies
    
    print_info "Testing launcher script syntax..."
    if [ -f "$PROJECT_ROOT/docker/run-openterface-docker.sh" ]; then
        bash -n "$PROJECT_ROOT/docker/run-openterface-docker.sh" && \
            print_success "Launcher script syntax is valid" || \
            print_error "Launcher script has syntax errors"
    else
        print_warning "Launcher script not found"
    fi
    
    print_success "Full test suite completed"
}

cleanup_docker_resources() {
    print_info "Cleaning up Docker resources..."
    
    # Remove test containers
    docker ps -aq --filter "name=openterface-test-" | xargs -r docker rm -f &> /dev/null || true
    
    # Remove test image
    docker rmi "$FULL_IMAGE_NAME" &> /dev/null || true
    
    # Show remaining Docker resources
    print_info "Remaining Docker images:"
    docker images | grep openterface || echo "No openterface images found"
    
    print_success "Cleanup completed"
}

# Parse command line arguments
RUN_QUICK=false
RUN_FULL=true
CLEANUP=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            RUN_QUICK=true
            RUN_FULL=false
            shift
            ;;
        --full)
            RUN_FULL=true
            RUN_QUICK=false
            shift
            ;;
        --clean)
            CLEANUP=true
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
    echo "🧪 Local Docker Test Script"
    echo "==========================="
    echo ""
    
    # Set up cleanup trap if requested
    if [ "$CLEANUP" = "true" ]; then
        trap cleanup_docker_resources EXIT
    fi
    
    # Run checks
    check_prerequisites
    
    # Run tests based on options
    if [ "$RUN_QUICK" = "true" ]; then
        test_quick
    elif [ "$RUN_FULL" = "true" ]; then
        test_full
    fi
    
    # Manual cleanup if not using trap
    if [ "$CLEANUP" = "true" ]; then
        cleanup_docker_resources
    else
        echo ""
        print_info "Docker image created: $FULL_IMAGE_NAME"
        print_info "To clean up manually, run:"
        print_info "  docker rmi $FULL_IMAGE_NAME"
    fi
    
    echo ""
    print_success "Local Docker testing completed successfully!"
}

# Run main function
main "$@"
