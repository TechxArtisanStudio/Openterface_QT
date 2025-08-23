# Docker-based Qt Static Build Environment

This directory contains Docker configurations for building a complete Qt static build environment based on the original `build-static-qt-from-source.sh` script.

## Overview

The Docker setup is divided into multiple stages to enable efficient caching and step-by-step building:

1. **qt-base**: Ubuntu base with all build tools and development libraries
2. **qt-ffmpeg**: Base + libusb + FFmpeg static libraries
3. **qt-gstreamer**: FFmpeg + GStreamer components and static libraries
4. **qt-complete**: Complete Qt 6.6.3 static build environment

## Quick Start

### Option 1: Using GitHub Actions (Recommended)

1. Go to your repository's Actions tab
2. Select "Build Qt Environment (Step by Step)"
3. Run workflow with:
   - Step: "all" (builds all components)
   - Push to registry: true

### Option 2: Pull from Registry

```bash
docker pull ghcr.io/kevinzjpeng/openterface-qt-complete:latest
```

## Building Your Application

Once you have the Qt environment, you can build your application:

```bash
# Manually run the complete image to build your application
docker run --rm \
   -v $(pwd):/workspace/src \
   -w /workspace/src \
   ghcr.io/kevinzjpeng/openterface-qt-complete:latest \
   bash -c "
      mkdir -p build && cd build
      cmake -DCMAKE_PREFIX_PATH=/opt/Qt6 \
               -DCMAKE_BUILD_TYPE=Release \
               -DBUILD_SHARED_LIBS=OFF \
               ..
      make -j\$(nproc)
   "
```

## Docker Images

### Base Image (`qt-base`)
- **Size**: ~2GB
- **Contents**: Ubuntu 24.04 with build tools, X11 libraries, development headers
- **Use case**: Foundation for Qt builds

### FFmpeg Image (`qt-ffmpeg`) 
- **Size**: ~2.5GB
- **Contents**: Base + libusb 1.0.26 + libjpeg-turbo + FFmpeg 6.1.1 static libraries
- **Use case**: Multimedia support foundation

### GStreamer Image (`qt-gstreamer`)
- **Size**: ~3GB  
- **Contents**: FFmpeg + GStreamer 1.22.11 + ORC static libraries
- **Use case**: Advanced multimedia pipeline support

### Complete Image (`qt-complete`)
- **Size**: ~4GB
- **Contents**: Full Qt 6.6.3 static build with all modules
- **Use case**: Ready-to-use Qt development environment

## Environment Variables

The complete image sets these environment variables:

```bash
QT_VERSION=6.6.3
INSTALL_PREFIX=/opt/Qt6
PATH=/opt/Qt6/bin:$PATH
PKG_CONFIG_PATH=/opt/Qt6/lib/pkgconfig:$PKG_CONFIG_PATH
```

## Qt Modules Included

- **qtbase**: Core Qt functionality
- **qtshadertools**: Shader compilation tools
- **qtdeclarative**: Qt Quick/QML support
- **qtmultimedia**: Audio/video support (with FFmpeg backend)
- **qtsvg**: SVG support
- **qtserialport**: Serial port communication
- **qttools**: Development tools (linguist, etc.)

## Build Features

### Enabled Features
- Static linking (`-DBUILD_SHARED_LIBS=OFF`)
- FFmpeg multimedia backend
- PulseAudio support
- X11/XCB support
- OpenGL support
- D-Bus integration
- SVG rendering

### Disabled Features
- GStreamer backend (FFmpeg is used instead)
- SQL modules
- Test frameworks
- ICU (to reduce size)

## Troubleshooting

### Build Failures

1. **Package installation errors**: Some packages may not be available on all Ubuntu variants. The Dockerfile includes fallback options.

2. **Memory issues**: Qt compilation is memory-intensive. Ensure Docker has at least 4GB RAM allocated.

3. **Cache issues**: If builds fail unexpectedly, clear Docker cache:
   ```bash
   docker builder prune -a
   ```

### Application Build Issues

1. **CMake configuration errors**: Ensure all Qt module paths are correctly specified:
   ```bash
   -DQt6_DIR="/opt/Qt6/lib/cmake/Qt6"
   -DQt6Multimedia_DIR="/opt/Qt6/lib/cmake/Qt6Multimedia"
   # ... etc
   ```

2. **Missing libraries**: Verify the environment with:
   ```bash
   docker run --rm ghcr.io/kevinzjpeng/openterface-qt-complete:latest \
     /opt/Qt6/bin/verify-qt-installation.sh
   ```

3. **Runtime dependencies**: Static builds should have minimal dependencies. Check with:
   ```bash
   ldd ./build/your-application
   ```

## Development Workflow

### Step-by-step Testing

The repository no longer provides local helper scripts for incremental builds. To test components without rebuilding everything:

- Use the GitHub Actions workflow with the individual step selected (e.g. `base`, `ffmpeg`, `gstreamer`, `qt-complete`) to build and validate a single stage.
- Or pull the relevant pre-built image from the registry and run quick verification commands inside the container.

For example, verify the complete image environment:

```bash
docker run --rm ghcr.io/kevinzjpeng/openterface-qt-complete:latest \
   /opt/Qt6/bin/verify-qt-installation.sh
```

### GitHub Actions Integration

The workflow supports building individual steps:

- `base`: Just the base Ubuntu image with tools
- `ffmpeg`: Base + FFmpeg libraries  
- `gstreamer`: FFmpeg + GStreamer
- `qt-complete`: Full Qt environment
- `all`: Build everything in sequence

This allows you to debug build issues step by step without rebuilding everything.

## Authentication with GitHub Container Registry

Before you can push images to GitHub Container Registry (ghcr.io), you need to authenticate properly.

### Step 1: Create a GitHub Personal Access Token (PAT)

1. Go to your GitHub account settings → Developer settings → Personal access tokens
2. Click on "Generate new token" (classic)
3. Give it a descriptive name like "OpenTerface Docker Push"
4. Select the following scopes:
   - `repo` (Full control of private repositories)
   - `write:packages` (Upload packages to GitHub Package Registry)
   - `read:packages` (Download packages from GitHub Package Registry)
5. Click "Generate token"
6. **Important:** Copy the token immediately and keep it safe - you won't be able to see it again!

### Step 2: Login to GitHub Container Registry

Use your token to authenticate with GitHub Container Registry:

```bash
export GITHUB_TOKEN=your_github_personal_access_token
export GITHUB_USERNAME=your_github_username
echo $GITHUB_TOKEN | docker login ghcr.io -u $GITHUB_USERNAME --password-stdin
```

## Comparison with Original Script

| Aspect | Original Script | Docker Approach |
|--------|----------------|-----------------|
| **Caching** | No caching | Layer-based caching |
| **Reproducibility** | Environment-dependent | Containerized |
| **Debugging** | Full rebuild needed | Step-by-step builds |
| **CI/CD** | Manual setup | GitHub Actions ready |
| **Distribution** | Script sharing | Container registry |

## Performance Notes

- **Build time**: Initial build ~2-3 hours, subsequent builds with cache ~30-60 minutes
- **Size**: Final image ~4GB (reasonable for a complete Qt environment)
- **Memory**: Requires 4GB+ RAM during build, 2GB+ during use

## Contributing

When modifying the Docker setup:

1. Test each stage individually
2. Update version numbers in environment variables
3. Ensure backward compatibility
4. Update documentation
