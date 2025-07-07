#!/bin/bash
set -e

# Create output directory
mkdir -p ./output
# Ensure output directory has correct permissions
chmod 777 ./output

echo "Starting Alpine ARM64 Qt Docker container build..."
docker-compose -f docker-compose.alpine.yml build

echo "Starting Alpine ARM64 Qt build process..."
docker-compose -f docker-compose.alpine.yml up

echo "Build complete!"
echo "Alpine Qt ARM64 build artifacts are in the ./output directory"
