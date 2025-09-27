#!/usr/bin/env bash

set -euo pipefail
IFS=$'\n\t'

# Qt 6.6.3 Source Installation Script
# Supports modes: download | build | all

# Configuration
QT_VERSION="6.6.3"
QT_MAJOR_VERSION="6.6"
QT_INSTALL_PREFIX="/opt/qt6.6.3"
BUILD_DIR="/tmp/qt-build"
SOURCE_DIR="/tmp/qt-source"
PARALLEL_JOBS=$(nproc)

log() { echo "[install-qt] $*"; }

do_download() {
    log "Preparing directories"
    mkdir -p "${BUILD_DIR}" "${SOURCE_DIR}"

    log "Downloading Qt ${QT_VERSION} source to ${SOURCE_DIR} (if not present)"
    cd "${SOURCE_DIR}"
    if [ ! -f "qt-everywhere-src-${QT_VERSION}.tar.xz" ]; then
        wget -O "qt-everywhere-src-${QT_VERSION}.tar.xz" \
            "https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/single/qt-everywhere-src-${QT_VERSION}.tar.xz"
    else
        log "Archive already present, skipping download"
    fi

    log "Extracting source (if not already extracted)"
    if [ ! -d "${SOURCE_DIR}/qt-everywhere-src-${QT_VERSION}" ]; then
        tar -xf "qt-everywhere-src-${QT_VERSION}.tar.xz" -C "${SOURCE_DIR}"
    else
        log "Source directory already exists, skipping extract"
    fi
}

do_download