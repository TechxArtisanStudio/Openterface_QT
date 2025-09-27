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

bash "${SRC_DIR}/build-script/docker-build-appimage.sh"
