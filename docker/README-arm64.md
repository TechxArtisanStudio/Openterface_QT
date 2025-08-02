# Multi-Stage Docker Build for ARM64 GNU/Linux

This directory contains a multi-stage Docker configuration for building OpenterfaceQT for ARM64 GNU/Linux systems. The build is split into stages to improve caching and reduce build times.

## Architecture

### Stage 1: Base Environment (`Dockerfile.arm64-base`)
- Debian ARM64 base image
- Build dependencies (gcc, cmake, pkg-config, etc.)
- FFmpeg 6.0 (static libraries)
- System libraries (X11, audio, USB, etc.)

### Stage 2: Qt Environment (`Dockerfile.arm64-qt`)
- Built on top of base environment
- Qt 6.5.3 compiled from source
- Optimized for embedded/cross-compilation

### Stage 3: Application (`Dockerfile.arm64`)
- Built on top of Qt environment
- Compiles the OpenterfaceQT application
- Produces final executable

## Files

- `Dockerfile.arm64-base` - Base environment with dependencies and FFmpeg
- `Dockerfile.arm64-qt` - Qt compilation stage
- `Dockerfile.arm64` - Final application build
- `test-arm64-build.sh` - Local multi-stage testing script
- `.dockerignore` - Files to exclude from Docker build context

## GitHub Actions Workflows

### Individual Workflows
1. **`arm64-base-build.yml`** - Builds base environment (~30 min)
2. **`arm64-qt-build.yml`** - Builds Qt environment (~90 min first time, ~5 min cached)
3. **`arm64-linux-build.yml`** - Builds application (~5-10 min)

### Pipeline Workflow
- **`arm64-pipeline.yml`** - Orchestrates all three stages with proper dependencies

## Usage

### Automatic Builds (GitHub Actions)
The pipeline automatically triggers on:
- Pushes to `main` or `dev` branches
- Changes to relevant source files
- Manual workflow dispatch

**Manual Trigger:**
```bash
# Trigger via GitHub Actions UI with options:
# - Qt Version (default: 6.5.3)
# - Rebuild base environment (forces base rebuild)
# - Rebuild Qt (forces Qt rebuild)
```

### Local Testing
```bash
# From the project root directory
./docker/test-arm64-build.sh
```

### Manual Multi-Stage Build
```bash
# Stage 1: Base environment
docker build -f docker/Dockerfile.arm64-base -t openterface-base ./docker

# Stage 2: Qt environment  
docker build -f docker/Dockerfile.arm64-qt -t openterface-qt ./docker \
  --build-arg BASE_IMAGE=openterface-base \
  --build-arg QT_VERSION=6.5.3

# Stage 3: Application
docker build -f docker/Dockerfile.arm64 -t openterface-app . \
  --build-arg QT_IMAGE=openterface-qt \
  --build-arg QT_VERSION=6.5.3

# Extract binary
docker create --name temp openterface-app
docker cp temp:/app/build/openterfaceQT ./openterfaceQT-arm64
docker rm temp
```

## Build Times & Caching

### First Build (no cache)
- **Base Environment**: ~30 minutes
- **Qt Environment**: ~90 minutes  
- **Application**: ~5-10 minutes
- **Total**: ~2-2.5 hours

### Subsequent Builds (with cache)
- **Base Environment**: ~2 minutes (if unchanged)
- **Qt Environment**: ~5 minutes (if unchanged)
- **Application**: ~5-10 minutes
- **Total**: ~10-15 minutes

### Cache Strategy
- **GitHub Actions Cache**: Used for Docker layer caching
- **Container Registry**: Pre-built base and Qt images stored in GHCR
- **Smart Triggering**: Only rebuilds changed stages

## Troubleshooting

### Build Failures
1. **Qt compilation timeout**: Use the pipeline workflow which has proper timeouts
2. **Memory issues**: Qt compilation requires ~8GB RAM
3. **Cache corruption**: Use "Rebuild Qt" option in manual trigger

### Cache Management
```bash
# Clear local Docker cache
docker system prune -a

# Rebuild specific stage
docker build --no-cache -f docker/Dockerfile.arm64-base -t openterface-base ./docker
```

### Debug Mode
```bash
# Build with debug output
docker build --progress=plain -f docker/Dockerfile.arm64-base -t openterface-base ./docker
```

## Advantages of Multi-Stage Approach

1. **Faster Incremental Builds**: Only rebuild changed layers
2. **Better Caching**: Each stage cached independently  
3. **Parallel Development**: Multiple developers can share base images
4. **Reduced Timeouts**: Individual stages complete within GitHub Actions limits
5. **Resource Efficient**: Reuse expensive Qt compilation across builds
6. **Rollback Capability**: Can pin to specific base/Qt versions
