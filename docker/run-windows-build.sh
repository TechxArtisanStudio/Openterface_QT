#!/bin/bash

# Windows Docker Build Runner Script for Openterface QT
# This script provides easy commands to build with both shared and static configurations

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_help() {
    echo "Windows Docker Build Runner for Openterface QT"
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  build-shared     Build with shared libraries using Docker"
    echo "  build-static     Build with static libraries (portable) using Docker"
    echo "  build-both       Build both shared and static versions"
    echo "  shell-shared     Open interactive shell in shared build environment"
    echo "  shell-static     Open interactive shell in static build environment"
    echo "  clean            Clean build artifacts and Docker volumes"
    echo "  rebuild          Rebuild Docker images from scratch"
    echo ""
    echo "Options:"
    echo "  --build-type     Specify build type (Release|Debug) [default: Release]"
    echo "  --help, -h       Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 build-shared --build-type Release"
    echo "  $0 build-static"
    echo "  $0 shell-shared"
}

# Parse command line arguments
COMMAND=""
BUILD_TYPE="Release"

while [[ $# -gt 0 ]]; do
    case $1 in
        build-shared|build-static|build-both|shell-shared|shell-static|clean|rebuild)
            COMMAND="$1"
            shift
            ;;
        --build-type)
            BUILD_TYPE="$2"
            shift 2
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

if [[ -z "$COMMAND" ]]; then
    print_error "No command specified"
    show_help
    exit 1
fi

# Change to docker directory
cd "$SCRIPT_DIR"

case $COMMAND in
    build-shared)
        print_info "Building Openterface QT with shared libraries..."
        docker-compose -f docker-compose.windows.yml build windows-shared-build
        docker-compose -f docker-compose.windows.yml run --rm windows-shared-build powershell -File "C:/build-scripts/build-shared.ps1" -BuildType "$BUILD_TYPE"
        print_success "Shared build completed!"
        ;;
    
    build-static)
        print_info "Building Openterface QT with static libraries (portable)..."
        docker-compose -f docker-compose.windows.yml build windows-static-build
        docker-compose -f docker-compose.windows.yml run --rm windows-static-build powershell -File "C:/build-scripts/build-static.ps1" -BuildType "$BUILD_TYPE"
        print_success "Static build completed!"
        ;;
    
    build-both)
        print_info "Building both shared and static versions..."
        docker-compose -f docker-compose.windows.yml build
        
        print_info "Building shared version..."
        docker-compose -f docker-compose.windows.yml run --rm windows-shared-build powershell -File "C:/build-scripts/build-shared.ps1" -BuildType "$BUILD_TYPE"
        
        print_info "Building static version..."
        docker-compose -f docker-compose.windows.yml run --rm windows-static-build powershell -File "C:/build-scripts/build-static.ps1" -BuildType "$BUILD_TYPE"
        
        print_success "Both builds completed!"
        ;;
    
    shell-shared)
        print_info "Opening interactive shell in shared build environment..."
        docker-compose -f docker-compose.windows.yml run --rm windows-shared-build powershell
        ;;
    
    shell-static)
        print_info "Opening interactive shell in static build environment..."
        docker-compose -f docker-compose.windows.yml run --rm windows-static-build powershell
        ;;
    
    clean)
        print_info "Cleaning build artifacts and Docker volumes..."
        docker-compose -f docker-compose.windows.yml down -v
        docker volume prune -f
        print_success "Cleanup completed!"
        ;;
    
    rebuild)
        print_info "Rebuilding Docker images from scratch..."
        docker-compose -f docker-compose.windows.yml down
        docker-compose -f docker-compose.windows.yml build --no-cache
        print_success "Rebuild completed!"
        ;;
    
    *)
        print_error "Unknown command: $COMMAND"
        show_help
        exit 1
        ;;
esac
