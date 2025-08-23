# Quick Fix for Architecture-Specific Image References

The error you're seeing happens because the workflow is trying to use `ghcr.io/kevinzjpeng/openterface-qt-base:latest` but we've implemented architecture-specific naming.

## The Problem
```
ERROR: no match for platform in manifest: not found
```

This occurs because:
1. We added architecture-specific suffixes to image names (e.g., `-amd64`, `-arm64`)
2. The FFmpeg build is still looking for the old `:latest` tag
3. The base image now has tags like `:ubuntu-24.04-amd64` instead of `:latest`

## Quick Solution

Update the BASE_IMAGE reference in the workflow to use the architecture-specific tag pattern:

**Before:**
```yaml
BASE_IMAGE=${{ env.REGISTRY }}/${{ env.IMAGE_NAME_LOWER }}-base:latest
```

**After:**
```yaml
BASE_IMAGE=${{ env.REGISTRY }}/${{ env.IMAGE_NAME_LOWER }}-base:ubuntu-${{ github.event.inputs.ubuntu_version || '24.04' }}${{ steps.arch-suffix.outputs.arch_tag_suffix }}
```

## What We Changed

1. **Dockerfile.qt-base**: Added architecture awareness with `TARGETARCH` variable
2. **Dockerfile.qt-dynamic**: Added architecture awareness
3. **Build script**: Updated to create architecture-specific tags
4. **GitHub Actions**: Partially updated to use architecture-specific naming

## Current Image Naming Pattern

With the new system, images are tagged as:
- `ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-amd64`
- `ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-arm64`
- `ghcr.io/kevinzjpeng/openterface-qt-base:latest-amd64`
- `ghcr.io/kevinzjpeng/openterface-qt-base:latest-arm64`

For multi-architecture manifests (when building both architectures):
- `ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04`
- `ghcr.io/kevinzjpeng/openterface-qt-base:latest`

## Manual Build Example

To build manually and avoid this issue:

```bash
# Build base image for AMD64
docker buildx build \
  --platform linux/amd64 \
  --file docker/Dockerfile.qt-base \
  --build-arg UBUNTU_VERSION=24.04 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-amd64 \
  --push .

# Build FFmpeg using the architecture-specific base
docker buildx build \
  --platform linux/amd64 \
  --file docker/Dockerfile.qt-ffmpeg \
  --build-arg BASE_IMAGE=ghcr.io/kevinzjpeng/openterface-qt-base:ubuntu-24.04-amd64 \
  --tag ghcr.io/kevinzjpeng/openterface-qt-ffmpeg:ubuntu-24.04-amd64 \
  --push .
```

## Using the Build Script

Our updated build script handles this automatically:

```bash
# Build complete stack for AMD64
./build-script/build-multi-arch.sh \
  --type all \
  --architecture linux/amd64 \
  --ubuntu-version 24.04 \
  --push

# Build complete stack for ARM64  
./build-script/build-multi-arch.sh \
  --type all \
  --architecture linux/arm64 \
  --ubuntu-version 24.04 \
  --push
```

## Next Steps

1. **Immediate fix**: Update all BASE_IMAGE references in the GitHub Actions workflow
2. **Complete the implementation**: Add architecture suffix logic to all build jobs
3. **Test**: Run the workflow with the new architecture-specific naming
4. **Cleanup**: Remove any old images without architecture suffixes

The key insight is that each architecture needs its own unique image tag to prevent conflicts in the registry.
