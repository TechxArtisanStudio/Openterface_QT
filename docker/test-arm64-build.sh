#!/bin/bash

# Test script for ARM64 Linux multi-stage Docker build
set -e

QT_VERSION="${QT_VERSION:-6.5.3}"

echo "🚀 Starting ARM64 Linux multi-stage Docker build test..."
echo "Qt Version: $QT_VERSION"

# Build the base environment
echo "📦 Building base environment..."
docker build -f docker/Dockerfile.arm64-base -t openterface-arm64-base:test ./docker

echo "✅ Base environment built successfully!"

# Build the Qt environment
echo "📦 Building Qt environment (this will take a while)..."
docker build -f docker/Dockerfile.arm64-qt -t openterface-arm64-qt:test ./docker \
  --build-arg BASE_IMAGE=openterface-arm64-base:test \
  --build-arg QT_VERSION=$QT_VERSION

echo "✅ Qt environment built successfully!"

# Build the application
echo "📦 Building application..."
docker build -f docker/Dockerfile.arm64 -t openterface-arm64-app:test . \
  --build-arg QT_IMAGE=openterface-arm64-qt:test \
  --build-arg QT_VERSION=$QT_VERSION

echo "✅ Application built successfully!"

# Test extracting the binary
echo "📄 Extracting binary from Docker image..."
docker create --name temp-extract openterface-arm64-app:test
docker cp temp-extract:/app/build/openterfaceQT ./openterfaceQT-arm64-test
docker rm temp-extract

echo "🔍 Checking binary..."
file ./openterfaceQT-arm64-test
ls -la ./openterfaceQT-arm64-test

echo "✅ Multi-stage test completed successfully!"
echo "Binary saved as: ./openterfaceQT-arm64-test"

# Optional cleanup
read -p "Do you want to clean up test images? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "🧹 Cleaning up test images..."
    docker rmi openterface-arm64-app:test openterface-arm64-qt:test openterface-arm64-base:test
    echo "✅ Cleanup completed!"
fi
