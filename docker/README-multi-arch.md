# Multi-Architecture Build Guide

This guide explains how to build Openterface Qt environments for different architectures (amd64/arm64) and Ubuntu versions.

## Overview

The Openterface Qt project now supports building Docker images for multiple architectures and Ubuntu versions:

- **Architectures**: `linux/amd64`, `linux/arm64`
- **Ubuntu Versions**: `20.04`, `22.04`, `24.04`
- **Build Types**: `static` (multi-stage), `dynamic` (single image)

## Quick Start

### Using the Build Script

The easiest way to build multi-architecture images is using the provided build script:

```bash
# Build base image for amd64 with Ubuntu 24.04 (default)
./build-script/build-multi-arch.sh --type base

# Build for both amd64 and arm64 with Ubuntu 22.04
./build-script/build-multi-arch.sh \
  --type base \
  --architecture linux/amd64,linux/arm64 \
  --ubuntu-version 22.04 \
  --push

# Build complete static environment for arm64 only
./build-script/build-multi-arch.sh \
  --type complete \
  --architecture linux/arm64 \
  --ubuntu-version 24.04
```

### Using GitHub Actions

You can trigger builds via GitHub Actions with custom parameters:

1. Go to the "Actions" tab in your repository
2. Select "Build Qt Environments (Static & Dynamic)"
3. Click "Run workflow"
4. Configure the parameters:
   - **Environment Type**: `static`, `dynamic`, or `both`
   - **Architecture**: `linux/amd64`, `linux/arm64`, or `linux/amd64,linux/arm64`
   - **Ubuntu Version**: `20.04`, `22.04`, or `24.04`
   - **Step**: For static builds, choose specific step or `all`

## Manual Docker Build

### Prerequisites

Set up Docker buildx for multi-architecture builds:

```bash
# Create and use a new builder instance
docker buildx create --name multiarch-builder --use --bootstrap

# Verify platform support
docker buildx inspect --bootstrap
```

### Building Base Images

#### For AMD64 (default):
```bash
docker buildx build \
  --platform linux/amd64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-amd64 \
  --load \
  .
```

#### For ARM64:
```bash
docker buildx build \
  --platform linux/arm64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-arm64 \
  --load \
  .
```

#### For Multi-Architecture (push to registry):
```bash
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-base:latest \
  --push \
  .
```

### Building Static Environment (Multi-Stage)

The static environment builds in stages: base → ffmpeg → gstreamer → complete

```bash
# 1. Build base
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag myregistry/openterface-qt-base:ubuntu-24.04 \
  --push .

# 2. Build FFmpeg (depends on base)
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-ffmpeg \
  --build-arg BASE_IMAGE=myregistry/openterface-qt-base:ubuntu-24.04 \
  --tag myregistry/openterface-qt-ffmpeg:ubuntu-24.04 \
  --push .

# 3. Build GStreamer (depends on ffmpeg)
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-gstreamer \
  --build-arg BASE_IMAGE=myregistry/openterface-qt-ffmpeg:ubuntu-24.04 \
  --tag myregistry/openterface-qt-gstreamer:ubuntu-24.04 \
  --push .

# 4. Build Qt Complete (depends on gstreamer)
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-complete \
  --build-arg BASE_IMAGE=myregistry/openterface-qt-gstreamer:ubuntu-24.04 \
  --tag myregistry/openterface-qt-complete:ubuntu-24.04 \
  --push .
```

### Building Dynamic Environment

```bash
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-dynamic \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-dynamic:ubuntu-24.04 \
  --push \
  .
```

## Architecture-Specific Considerations

### ARM64 Builds

- **Build Time**: ARM64 builds typically take longer than AMD64 builds
- **Emulation**: When building on AMD64 hardware, ARM64 builds run through emulation
- **Memory**: ARM64 builds may require more memory, especially for Qt compilation
- **Testing**: Always test ARM64 builds on actual ARM64 hardware when possible

### Cross-Platform Building

When building for multiple architectures simultaneously:

```bash
# This builds for both architectures in parallel
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --push \
  .
```

**Note**: Multi-platform builds require pushing to a registry (can't use `--load`).

## Ubuntu Version Compatibility

### Ubuntu 20.04 LTS
- **Qt Version**: Best compatibility with Qt 5.x and Qt 6.x
- **Package Availability**: Some newer packages may not be available
- **Use Case**: Legacy systems, maximum compatibility

### Ubuntu 22.04 LTS
- **Qt Version**: Good balance of modern packages and stability
- **Package Availability**: Most packages available
- **Use Case**: Production environments, balanced approach

### Ubuntu 24.04 LTS (Default)
- **Qt Version**: Latest Qt 6.x with newest features
- **Package Availability**: Latest packages available
- **Use Case**: Latest features, development environments

## Environment Variables

The Dockerfiles support these build arguments:

- `UBUNTU_VERSION`: Ubuntu base image version (default: 24.04)
- `TARGETARCH`: Target architecture set automatically by Docker buildx
- `BASE_IMAGE`: Base image for multi-stage builds

## Troubleshooting

### Common Issues

1. **Buildx not found**:
   ```bash
   docker buildx install
   ```

2. **Platform not supported**:
   ```bash
   docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
   ```

3. **Out of disk space during ARM64 build**:
   - ARM64 builds can be large; ensure sufficient disk space
   - Use multi-stage builds to reduce final image size

4. **Build timeout**:
   - ARM64 builds take longer; increase timeout values
   - Consider building stages separately for very large builds

### Performance Tips

1. **Use BuildKit cache**:
   ```bash
   --cache-from type=gha,scope=build-cache \
   --cache-to type=gha,mode=max,scope=build-cache
   ```

2. **Build specific architecture first**:
   ```bash
   # Build AMD64 first (faster), then ARM64
   docker buildx build --platform linux/amd64 ...
   docker buildx build --platform linux/arm64 ...
   ```

3. **Use registry cache**:
   ```bash
   --cache-from type=registry,ref=myregistry/cache \
   --cache-to type=registry,ref=myregistry/cache,mode=max
   ```

## Image Naming Convention

Images are tagged with the following convention:

- `registry/openterface-qt-{type}:ubuntu-{version}` - Version-specific
- `registry/openterface-qt-{type}:latest` - Latest/default version

Examples:
- `ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04`
- `ghcr.io/kevinzjpeng/openterface-qt-complete:ubuntu-22.04`
- `ghcr.io/kevinzjpeng/openterface-qt-dynamic:latest`

## Usage Examples

### Development on ARM64 Mac

```bash
# Build for native ARM64
./build-script/build-multi-arch.sh \
  --type dynamic \
  --architecture linux/arm64 \
  --ubuntu-version 24.04

# Use the image
docker run -it --rm \
  -v $(pwd):/workspace/src \
  ghcr.io/kevinzjpeng/openterface-qt-dynamic:ubuntu-24.04 \
  bash
```

### CI/CD Pipeline

```bash
# Build for production deployment (both architectures)
./build-script/build-multi-arch.sh \
  --type complete \
  --architecture linux/amd64,linux/arm64 \
  --ubuntu-version 22.04 \
  --registry myregistry.com/myorg \
  --push
```

### Testing on Raspberry Pi

```bash
# Build ARM64 image for Raspberry Pi
./build-script/build-multi-arch.sh \
  --type dynamic \
  --architecture linux/arm64 \
  --ubuntu-version 22.04 \
  --push

# On Raspberry Pi
docker pull ghcr.io/kevinzjpeng/openterface-qt-dynamic:ubuntu-22.04
```
