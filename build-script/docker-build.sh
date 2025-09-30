#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

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

# Enhanced AppImage build script with comprehensive GStreamer plugin support
# This script builds an AppImage with all necessary GStreamer plugins for video capture

echo "Building Enhanced Openterface AppImage with GStreamer plugins..."

# Configuration
BUILD_DIR="/workspace/build"
SRC_DIR="/workspace/src"

# Build the application first
echo 'Building Openterface with Qt 6.6.3 and comprehensive GStreamer support...'

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
    VERSION="0.4.3.248"
fi

