#!/bin/bash
echo "============================================"
        tar -xf "qt-everywhere-src-${QT_VERSION}.tar.xz" -C "${SOURCE_DIR}"
#!/bin/bash

# Qt 6.6.3 Source Installation Script
# Supports modes: download | build | all

set -euo pipefail
IFS=$'\n\t'

# Configuration
QT_VERSION="6.6.3"
QT_MAJOR_VERSION="6.6"
QT_INSTALL_PREFIX="/opt/qt6.6.3"
BUILD_DIR="/tmp/qt-build"
SOURCE_DIR="/tmp/qt-source"
PARALLEL_JOBS=$(nproc)

# Script modes: download | build | all
MODE="all"
if [ "$#" -ge 1 ]; then
    case "$1" in
        download|build|all)
            MODE="$1" ;;
        *)
            echo "Usage: $0 [download|build|all]" >&2
            exit 2 ;;
    esac
fi

log() { echo "[install-qt] $*"; }

# Download only: fetch and extract sources

    log "Preparing directories"
    mkdir -p "${BUILD_DIR}" "${SOURCE_DIR}"

    log "Downloading Qt ${QT_VERSION} source to ${SOURCE_DIR} (if not present)"
    cd "${SOURCE_DIR}"
    if [ ! -f "qt-everywhere-src-${QT_VERSION}.tar.xz" ]; then
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

        # Script modes: download | build | all
        MODE="all"
        if [ "$#" -ge 1 ]; then
            case "$1" in
                download|build|all)
                    MODE="$1" ;;
                *)
                    echo "Usage: $0 [download|build|all]" >&2
                    exit 2 ;;
            esac
        fi

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

        do_build() {
            if [ ! -d "${SOURCE_DIR}/qt-everywhere-src-${QT_VERSION}" ]; then
                echo "Source directory not found: ${SOURCE_DIR}/qt-everywhere-src-${QT_VERSION}" >&2
                echo "Run '$0 download' first or mount sources into ${SOURCE_DIR}." >&2
                exit 1
            fi

            mkdir -p "${BUILD_DIR}"
            cd "${BUILD_DIR}"

            log "Configuring Qt build"
            "${SOURCE_DIR}/qt-everywhere-src-${QT_VERSION}/configure" \
                -prefix "${QT_INSTALL_PREFIX}" \
                -opensource \
                -confirm-license \
                -release \
                -shared \
                -optimized-qmake \
                -strip \
                -reduce-relocations \
                -force-pkg-config \
                -qt-zlib \
                -qt-pcre \
                -openssl-linked \
                -system-libpng \
                -system-libjpeg \
                -skip qt3d \
                -skip qtsensors \
                -skip qtwebengine \
                -nomake examples \
                -nomake tests

            log "Configuration done. Starting build (this may take 30+ minutes)"
            nice -n 10 make -j"${PARALLEL_JOBS}"

            log "Installing to ${QT_INSTALL_PREFIX}"
            make install

            log "Creating setup script"
            mkdir -p "${QT_INSTALL_PREFIX}"
            cat > "${QT_INSTALL_PREFIX}/setup-qt-env.sh" <<'EOF'
        #!/bin/bash
        export QTDIR="${QT_INSTALL_PREFIX}"
        export QT_INSTALL_PREFIX="${QT_INSTALL_PREFIX}"
        export PATH="${QT_INSTALL_PREFIX}/bin:${PATH}"
        export LD_LIBRARY_PATH="${QT_INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"
        export PKG_CONFIG_PATH="${QT_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
        export QT_PLUGIN_PATH="${QT_INSTALL_PREFIX}/plugins"
        export QML_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
        export QML2_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
        EOF
            chmod +x "${QT_INSTALL_PREFIX}/setup-qt-env.sh"

            log "Creating global profile /etc/profile.d/qt6.6.3.sh"
            cat > "/etc/profile.d/qt6.6.3.sh" <<'EOF'
        # Qt 6.6.3 Environment Variables
        export QTDIR="${QT_INSTALL_PREFIX}"
        export QT_INSTALL_PREFIX="${QT_INSTALL_PREFIX}"
        export PATH="${QT_INSTALL_PREFIX}/bin:${PATH}"
        export LD_LIBRARY_PATH="${QT_INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"
        export PKG_CONFIG_PATH="${QT_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
        export QT_PLUGIN_PATH="${QT_INSTALL_PREFIX}/plugins"
        export QML_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
        export QML2_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
        EOF

            log "Cleaning up build directory (keeping source archive)"
            rm -rf "${BUILD_DIR}"
        }

        case "${MODE}" in
            download)
                do_download
                ;;
            build)
                do_build
                ;;
            all)
                do_download
                do_build
                ;;
        esac

        log "Done."
        exit 0
export PATH="${QT_INSTALL_PREFIX}/bin:${PATH}"
export LD_LIBRARY_PATH="${QT_INSTALL_PREFIX}/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="${QT_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
export QT_PLUGIN_PATH="${QT_INSTALL_PREFIX}/plugins"
export QML_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
export QML2_IMPORT_PATH="${QT_INSTALL_PREFIX}/qml"
EOF

    echo "Qt ${QT_VERSION} installed successfully."
}

# Run according to chosen mode
case "$MODE" in
    download)
        do_download
        ;;
    build)
        do_build
        ;;
    all)
        do_download
        do_build
        ;;
esac

chmod +x "${QT_INSTALL_PREFIX}/setup-qt-env.sh"

# Create global environment setup
cat > "/etc/profile.d/qt6.6.3.sh" << EOF
# Qt 6.6.3 Environment Variables
export QTDIR="/opt/qt6.6.3"
export QT_INSTALL_PREFIX="/opt/qt6.6.3"
export PATH="/opt/qt6.6.3/bin:\${PATH}"
export LD_LIBRARY_PATH="/opt/qt6.6.3/lib:\${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="/opt/qt6.6.3/lib/pkgconfig:\${PKG_CONFIG_PATH}"
export QT_PLUGIN_PATH="/opt/qt6.6.3/plugins"
export QML_IMPORT_PATH="/opt/qt6.6.3/qml"
export QML2_IMPORT_PATH="/opt/qt6.6.3/qml"
EOF

# Cleanup build directories
echo "Cleaning up build directories..."
rm -rf "${BUILD_DIR}" "${SOURCE_DIR}"

echo ""
echo "============================================"
echo "Qt ${QT_VERSION} installation completed!"
echo "============================================"
echo "Installation directory: ${QT_INSTALL_PREFIX}"
echo "To use Qt, source the environment:"
echo "  source ${QT_INSTALL_PREFIX}/setup-qt-env.sh"
echo "Or restart your shell to use global environment"
echo ""