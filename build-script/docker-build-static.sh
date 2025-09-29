#!/bin/bash
set -e

# Wrapper for static build packaging
export OPENTERFACE_BUILD_STATIC=ON
export USE_GSTREAMER_STATIC_PLUGINS=OFF

# Optional: set minimal deps for static .deb (can be overridden by DEB_DEPENDS)
: "${DEB_DEPENDS:=libc6}"
export DEB_DEPENDS

# Allow optional overrides via environment
# - SKIP_APPIMAGE=1 to skip AppImage packaging

exec bash /workspace/src/build-script/docker-build.sh
