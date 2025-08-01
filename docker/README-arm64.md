# Docker Build for ARM64 GNU/Linux

This directory contains the Docker configuration for building OpenterfaceQT for ARM64 GNU/Linux systems.

## Files

- `Dockerfile.arm64` - Multi-stage Dockerfile for building the application
- `test-arm64-build.sh` - Local testing script
- `.dockerignore` - Files to exclude from Docker build context

## GitHub Actions Integration

The build is automatically triggered by:
- Pushes to `main` or `dev` branches
- Changes to relevant source files
- Manual workflow dispatch

## Local Testing

To test the build locally:

```bash
# From the project root directory
./docker/test-arm64-build.sh
```

## Key Features

### Dependencies Included
- Qt 6.5.3 (compiled from source)
- FFmpeg 6.0 (static libraries)
- libusb-1.0
- All necessary X11 and audio libraries

### Build Optimizations
- Multi-core compilation (`-j$(nproc)`)
- Static linking for FFmpeg
- Stripped binary for smaller size
- Build verification steps

### GitHub Actions Features
- Docker layer caching
- Cross-platform ARM64 emulation via QEMU
- Artifact upload for built binaries
- Automatic versioning with Git SHA

## Troubleshooting

### Common Issues

1. **Build timeout**: The initial build can take 2-3 hours due to Qt compilation
2. **Out of memory**: Ensure sufficient RAM/swap space for compilation
3. **Missing dependencies**: Check CMakeLists.txt for any new requirements

### Build Requirements
- Docker with Buildx support
- QEMU for ARM64 emulation (in GitHub Actions)
- ~8GB RAM recommended for Qt compilation
- ~10GB disk space for build artifacts

## Manual Docker Commands

```bash
# Build the image
docker build -f docker/Dockerfile.arm64 -t openterface-arm64 .

# Run the container
docker run --rm openterface-arm64

# Extract binary
docker create --name temp openterface-arm64
docker cp temp:/app/build/openterfaceQT ./openterfaceQT-arm64
docker rm temp
```
