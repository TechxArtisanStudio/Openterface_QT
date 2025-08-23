#!/bin/bash
# Multi-architecture build script for Openterface Qt environments
# This script demonstrates how to build for different architectures and Ubuntu versions

set -e

# Default values
UBUNTU_VERSION=${UBUNTU_VERSION:-"24.04"}
ARCHITECTURE=${ARCHITECTURE:-"linux/amd64"}
BUILD_TYPE=${BUILD_TYPE:-"base"}
REGISTRY=${REGISTRY:-"ghcr.io/kevinzjpeng"}
PUSH=${PUSH:-"false"}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -u, --ubuntu-version VERSION    Ubuntu version (20.04, 22.04, 24.04) [default: 24.04]"
    echo "  -a, --architecture ARCH         Target architecture(s) [default: linux/amd64]"
    echo "                                   Examples: linux/amd64, linux/arm64, linux/amd64,linux/arm64"
    echo "  -t, --type TYPE                  Build type (base, dynamic, ffmpeg, gstreamer, complete, all) [default: base]"
    echo "  -r, --registry REGISTRY         Container registry [default: ghcr.io/kevinzjpeng]"
    echo "  -p, --push                       Push to registry after build"
    echo "  -h, --help                       Show this help message"
    echo ""
    echo "Examples:"
    echo "  # Build base image for amd64 with Ubuntu 24.04"
    echo "  $0 --type base --architecture linux/amd64 --ubuntu-version 24.04"
    echo ""
    echo "  # Build for both amd64 and arm64 with Ubuntu 22.04"
    echo "  $0 --type base --architecture linux/amd64,linux/arm64 --ubuntu-version 22.04"
    echo ""
    echo "  # Build complete static environment for arm64"
    echo "  $0 --type complete --architecture linux/arm64 --ubuntu-version 24.04 --push"
}

log() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -u|--ubuntu-version)
            UBUNTU_VERSION="$2"
            shift 2
            ;;
        -a|--architecture)
            ARCHITECTURE="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -r|--registry)
            REGISTRY="$2"
            shift 2
            ;;
        -p|--push)
            PUSH="true"
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            ;;
    esac
done

# Validate Ubuntu version
if [[ ! "$UBUNTU_VERSION" =~ ^(20\.04|22\.04|24\.04)$ ]]; then
    error "Invalid Ubuntu version: $UBUNTU_VERSION. Must be one of: 20.04, 22.04, 24.04"
fi

# Validate build type
if [[ ! "$BUILD_TYPE" =~ ^(base|dynamic|ffmpeg|gstreamer|complete|all)$ ]]; then
    error "Invalid build type: $BUILD_TYPE. Must be one of: base, dynamic, ffmpeg, gstreamer, complete, all"
fi

# Set up Docker buildx for multi-architecture builds
log "Setting up Docker buildx for multi-architecture builds..."
docker buildx create --name multiarch-builder --use --bootstrap 2>/dev/null || docker buildx use multiarch-builder

# Function to build a specific image
build_image() {
    local dockerfile="$1"
    local image_name="$2"
    local build_args="$3"
    local base_image="$4"

    log "Building $image_name for $ARCHITECTURE with Ubuntu $UBUNTU_VERSION..."
    
    local docker_args="docker buildx build"
    docker_args+=" --platform $ARCHITECTURE"
    docker_args+=" --file $dockerfile"
    docker_args+=" --tag $REGISTRY/openterface-qt-$image_name:ubuntu-$UBUNTU_VERSION"
    docker_args+=" --tag $REGISTRY/openterface-qt-$image_name:latest"
    
    # Add build arguments
    docker_args+=" --build-arg UBUNTU_VERSION=$UBUNTU_VERSION"
    if [[ -n "$base_image" ]]; then
        docker_args+=" --build-arg BASE_IMAGE=$base_image"
    fi
    if [[ -n "$build_args" ]]; then
        docker_args+=" $build_args"
    fi
    
    # Add push flag if requested
    if [[ "$PUSH" == "true" ]]; then
        docker_args+=" --push"
    else
        docker_args+=" --load"
    fi
    
    docker_args+=" ."
    
    echo -e "${BLUE}[CMD]${NC} $docker_args"
    eval $docker_args
    
    if [[ $? -eq 0 ]]; then
        log "Successfully built $image_name"
    else
        error "Failed to build $image_name"
    fi
}

# Navigate to repository root
cd "$(dirname "$0")/.."

# Build based on type
case "$BUILD_TYPE" in
    base)
        build_image "docker/Dockerfile.qt-base" "base"
        ;;
    dynamic)
        build_image "docker/Dockerfile.qt-dynamic" "dynamic"
        ;;
    ffmpeg)
        BASE_IMAGE="$REGISTRY/openterface-qt-base:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-ffmpeg" "ffmpeg" "" "$BASE_IMAGE"
        ;;
    gstreamer)
        BASE_IMAGE="$REGISTRY/openterface-qt-ffmpeg:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-gstreamer" "gstreamer" "" "$BASE_IMAGE"
        ;;
    complete)
        BASE_IMAGE="$REGISTRY/openterface-qt-gstreamer:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-complete" "complete" "" "$BASE_IMAGE"
        ;;
    all)
        log "Building all static images in sequence..."
        build_image "docker/Dockerfile.qt-base" "base"
        
        BASE_IMAGE="$REGISTRY/openterface-qt-base:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-ffmpeg" "ffmpeg" "" "$BASE_IMAGE"
        
        BASE_IMAGE="$REGISTRY/openterface-qt-ffmpeg:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-gstreamer" "gstreamer" "" "$BASE_IMAGE"
        
        BASE_IMAGE="$REGISTRY/openterface-qt-gstreamer:ubuntu-$UBUNTU_VERSION"
        build_image "docker/Dockerfile.qt-complete" "complete" "" "$BASE_IMAGE"
        
        log "Building dynamic image..."
        build_image "docker/Dockerfile.qt-dynamic" "dynamic"
        
        log "All images built successfully!"
        ;;
esac

log "Build completed successfully!"

# Show available images
if [[ "$PUSH" != "true" ]]; then
    log "Available local images:"
    docker images | grep "openterface-qt" | grep "ubuntu-$UBUNTU_VERSION"
fi
