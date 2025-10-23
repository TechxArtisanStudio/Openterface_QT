#!/bin/bash
# Openterface QT Shared Build Script
# Uses Docker image with static Qt6 build

set -e

# Configuration
DOCKER_IMAGE="ghcr.io/techxartisanstudio/static-qtbuild-complete:ubuntu-22.04-amd64"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "=== Openterface QT Static Build Script ==="
echo "Project directory: ${PROJECT_DIR}"
echo "Build directory: ${BUILD_DIR}"
echo "Docker image: ${DOCKER_IMAGE}"
echo

# Pull the Docker image
echo "Pulling Docker image..."
docker pull "${DOCKER_IMAGE}"
echo

# Clean build directory
echo "Cleaning build directory..."
sudo rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
echo

# Run cmake inside Docker
echo "Running cmake..."
docker run --rm \
    -v "${PROJECT_DIR}:/workspace/src" \
    -w "/workspace/src/build" \
    "${DOCKER_IMAGE}" \
    bash -c 'cmake -DCMAKE_PREFIX_PATH=/opt/Qt6 -DBUILD_SHARED_LIBS=ON -DOPENTERFACE_BUILD_STATIC=OFF ..'
echo

# Run make inside Docker
echo "Running make..."
docker run --rm \
    -v "${PROJECT_DIR}:/workspace/src" \
    -w "/workspace/src/build" \
    "${DOCKER_IMAGE}" \
    bash -c 'make -j$(nproc)'
echo

echo "Build completed successfully!"
echo "Binary location: ${BUILD_DIR}/openterfaceQT"