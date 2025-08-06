#!/bin/bash

# Test script for ARM64 Linux Docker build
set -e

echo "🚀 Starting ARM64 Linux Docker build test..."

# Build the Docker image
echo "📦 Building Docker image..."
docker build -f docker/Dockerfile.arm64 -t openterface-arm64-test .

echo "✅ Docker build completed successfully!"

# Test extracting the binary
echo "📄 Extracting binary from Docker image..."
docker create --name temp-extract openterface-arm64-test
docker cp temp-extract:/app/build/openterfaceQT ./openterfaceQT-arm64-test
docker rm temp-extract

echo "🔍 Checking binary..."
file ./openterfaceQT-arm64-test
ls -la ./openterfaceQT-arm64-test

echo "✅ Test completed successfully!"
echo "Binary saved as: ./openterfaceQT-arm64-test"
