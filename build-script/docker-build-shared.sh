#!/bin/bash
set -e

# Wrapper for shared (dynamic) build packaging
export OPENTERFACE_BUILD_STATIC=OFF
export USE_GSTREAMER_STATIC_PLUGINS=OFF

# Allow optional overrides via environment
# - DEB_DEPENDS: override Debian Depends string
# - SKIP_APPIMAGE=1 to skip AppImage packaging

# Check for pre-downloaded AppImage runtime
echo "Checking AppImage build environment..."
case "$(uname -m)" in
    x86_64) APPIMAGE_ARCH=x86_64;;
    aarch64) APPIMAGE_ARCH=aarch64;;
    armv7l) APPIMAGE_ARCH=armhf;;
    *) APPIMAGE_ARCH=x86_64;;
esac

DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"

if [ -f "${DOCKER_RUNTIME_FILE}" ]; then
    echo "✓ Pre-downloaded AppImage runtime found: ${DOCKER_RUNTIME_FILE}"
    ls -lh "${DOCKER_RUNTIME_FILE}"
else
    echo "⚠ Pre-downloaded AppImage runtime not found at: ${DOCKER_RUNTIME_FILE}"
    echo "Will pre-download runtime to optimize build process..."
    
    # Pre-download the runtime to avoid network downloads during AppImage creation
    mkdir -p /opt/appimage-runtime
    RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${APPIMAGE_ARCH}"
    echo "Downloading runtime from: ${RUNTIME_URL}"
    
    if command -v curl >/dev/null 2>&1; then
        if curl -fL "${RUNTIME_URL}" -o "${DOCKER_RUNTIME_FILE}"; then
            chmod +x "${DOCKER_RUNTIME_FILE}"
            echo "✓ Runtime downloaded successfully: ${DOCKER_RUNTIME_FILE}"
            ls -lh "${DOCKER_RUNTIME_FILE}"
            
            # Cache for future builds if in Jenkins
            if [ -n "${CACHE_DIR}" ]; then
                cp "${DOCKER_RUNTIME_FILE}" "${CACHED_RUNTIME}"
                echo "✓ Runtime cached for future builds: ${CACHED_RUNTIME}"
            fi
        else
            echo "⚠ Failed to download runtime, build will download it automatically"
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -qO "${DOCKER_RUNTIME_FILE}" "${RUNTIME_URL}"; then
            chmod +x "${DOCKER_RUNTIME_FILE}"
            echo "✓ Runtime downloaded successfully: ${DOCKER_RUNTIME_FILE}"
            ls -lh "${DOCKER_RUNTIME_FILE}"
            
            # Cache for future builds if in Jenkins
            if [ -n "${CACHE_DIR}" ]; then
                cp "${DOCKER_RUNTIME_FILE}" "${CACHED_RUNTIME}"
                echo "✓ Runtime cached for future builds: ${CACHED_RUNTIME}"
            fi
        else
            echo "⚠ Failed to download runtime, build will download it automatically"
        fi
    else
        echo "⚠ Neither curl nor wget found for downloading runtime"
    fi
fi

# Check for linuxdeploy tools
if command -v linuxdeploy >/dev/null 2>&1; then
    echo "✓ linuxdeploy found: $(command -v linuxdeploy)"
else
    echo "⚠ linuxdeploy not found, will be downloaded during build"
fi

if command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
    echo "✓ linuxdeploy-plugin-qt found: $(command -v linuxdeploy-plugin-qt)"
else
    echo "⚠ linuxdeploy-plugin-qt not found, will be downloaded during build"
fi


# Print a detailed failure report on any command error
err_report() {
    local exit_code=$?
    local cmd="${BASH_COMMAND:-<unknown>}"
    echo >&2
    echo "========== ERROR ==========" >&2
    echo "Script: $0" >&2
    echo "Exit code: ${exit_code}" >&2
    echo "Failed command: ${cmd}" >&2
    echo "Location (top of stack): ${BASH_LINENO[0]:-<unknown>}" >&2
    echo "Call stack:" >&2
    local i=0
    while caller $i; do ((i++)); done >&2
    echo "===========================" >&2
    exit "${exit_code}"
}
trap 'err_report' ERR

# Enable debug tracing if DEBUG environment variable is set (useful for diagnostics)
if [ "${DEBUG:-0}" != "0" ]; then
    export PS4='+ ${BASH_SOURCE}:${LINENO}:${FUNCNAME[0]}: '
    set -x
fi

# Configuration
BUILD_DIR="/workspace/build"
SRC_DIR="/workspace/src"

# Verify Qt version
echo "Using Qt version: $(qmake -query QT_VERSION)"
echo "Qt installation prefix: $(qmake -query QT_INSTALL_PREFIX)"

cd /workspace/build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DOPENTERFACE_BUILD_STATIC=${OPENTERFACE_BUILD_STATIC} \
      -DUSE_GSTREAMER_STATIC_PLUGINS=${USE_GSTREAMER_STATIC_PLUGINS} \
      -DCMAKE_PREFIX_PATH="/opt/Qt6" \
      -DQt6_DIR="/opt/Qt6/lib/cmake/Qt6" \
      /workspace/src
make -j4
echo "Build with Qt 6.6.3 complete."

# Determine version from resources/version.h
VERSION_H="/workspace/src/resources/version.h"
if [ -f "${VERSION_H}" ]; then
    VERSION=$(grep -Po '^#define APP_VERSION\s+"\K[0-9]+(\.[0-9]+)*' "${VERSION_H}" | head -n1)
fi
if [ -z "${VERSION}" ]; then
    VERSION="0.0.1"
fi
export VERSION
echo "Packaging OpenterfaceQT version: ${VERSION}"

bash /workspace/src/build-script/docker-build-appimage.sh

bash /workspace/src/build-script/docker-build-deb.sh

bash /workspace/src/build-script/docker-build-rpm.sh