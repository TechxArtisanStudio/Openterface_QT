#!/bin/bash

# Qt 6.6.3 Source Installation Script
# This script downloads and compiles Qt 6.6.3 from source

set -e

# Configuration
QT_VERSION="6.6.3"
QT_MAJOR_VERSION="6.6"
QT_INSTALL_PREFIX="/opt/qt6.6.3"
BUILD_DIR="/tmp/qt-build"
SOURCE_DIR="/tmp/qt-source"
PARALLEL_JOBS=$(nproc)

echo "============================================"
echo "Installing Qt ${QT_VERSION} from source"
echo "============================================"
echo "Install prefix: ${QT_INSTALL_PREFIX}"
echo "Parallel jobs: ${PARALLEL_JOBS}"
echo ""

# Create directories
mkdir -p "${BUILD_DIR}" "${SOURCE_DIR}"

# Download Qt source
echo "Downloading Qt ${QT_VERSION} source..."
cd "${SOURCE_DIR}"
if [ ! -f "qt-everywhere-src-${QT_VERSION}.tar.xz" ]; then
    wget -O "qt-everywhere-src-${QT_VERSION}.tar.xz" \
        "https://download.qt.io/official_releases/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/single/qt-everywhere-src-${QT_VERSION}.tar.xz"
fi

# Extract source
echo "Extracting Qt source..."
if [ ! -d "qt-everywhere-src-${QT_VERSION}" ]; then
    tar -xf "qt-everywhere-src-${QT_VERSION}.tar.xz"
fi

# Configure build
echo "Configuring Qt build..."
cd "${BUILD_DIR}"

# Qt configuration options optimized for AppImage building
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
    -qt-libpng \
    -qt-libjpeg \
    -qt-freetype \
    -qt-harfbuzz \
    -qt-pcre \
    -openssl-linked \
    -system-sqlite \
    -xcb \
    -xcb-xlib \
    -bundled-xcb-xinput \
    -egl \
    -opengl desktop \
    -dbus-linked \
    -glib \
    -icu \
    -fontconfig \
    -system-freetype \
    -system-harfbuzz \
    -gui \
    -widgets \
    -feature-xcb \
    -feature-xcb-xlib \
    -feature-xlib \
    -feature-wayland \
    -feature-wayland-client \
    -feature-wayland-cursor \
    -feature-wayland-egl \
    -feature-im \
    -feature-accessibility \
    -feature-clipboard \
    -feature-draganddrop \
    -feature-imageformat_png \
    -feature-imageformat_jpeg \
    -feature-imageformat_svg \
    -feature-multimedia \
    -feature-gstreamer \
    -feature-pulseaudio \
    -feature-alsa \
    -feature-serialport \
    -feature-svg \
    -feature-xml \
    -feature-xmlstream \
    -feature-xmlstreamreader \
    -feature-xmlstreamwriter \
    -feature-printsupport \
    -feature-printdialog \
    -feature-printer \
    -feature-printpreviewwidget \
    -feature-printpreviewdialog \
    -feature-pdf \
    -feature-textodfwriter \
    -feature-cssparser \
    -feature-texthtmlparser \
    -feature-textmarkdownreader \
    -feature-textmarkdownwriter \
    -no-feature-concurrent \
    -no-feature-testlib \
    -no-feature-sql \
    -no-feature-network \
    -no-feature-networkproxy \
    -no-feature-ftp \
    -no-feature-http \
    -no-feature-udpsocket \
    -no-feature-tcpsocket \
    -no-feature-socks5 \
    -no-feature-networkinterface \
    -no-feature-networkdiskcache \
    -no-feature-bearermanagement \
    -no-feature-localserver \
    -no-feature-translation \
    -no-feature-big-codecs \
    -no-feature-iconv \
    -no-feature-codecs \
    -no-feature-textcodec \
    -no-compile-examples \
    -nomake examples \
    -nomake tests \
    -skip qt3d \
    -skip qtactiveqt \
    -skip qtcharts \
    -skip qtcoap \
    -skip qtconnectivity \
    -skip qtdatavis3d \
    -skip qtdoc \
    -skip qtlottie \
    -skip qtmqtt \
    -skip qtnetworkauth \
    -skip qtopcua \
    -skip qtpositioning \
    -skip qtquick3d \
    -skip qtquickeffectmaker \
    -skip qtquicktimeline \
    -skip qtremoteobjects \
    -skip qtscxml \
    -skip qtsensors \
    -skip qttranslations \
    -skip qtvirtualkeyboard \
    -skip qtwebchannel \
    -skip qtwebengine \
    -skip qtwebsockets \
    -skip qtwebview

echo ""
echo "Configuration completed. Starting build..."
echo "This will take a significant amount of time (30+ minutes)..."
echo ""

# Build Qt
echo "Building Qt with ${PARALLEL_JOBS} parallel jobs..."
make -j"${PARALLEL_JOBS}"

# Install Qt
echo "Installing Qt to ${QT_INSTALL_PREFIX}..."
make install

# Verify installation
echo "Verifying Qt installation..."
if [ -f "${QT_INSTALL_PREFIX}/bin/qmake" ]; then
    echo "✓ Qt installation successful!"
    echo "Qt version: $(${QT_INSTALL_PREFIX}/bin/qmake -query QT_VERSION)"
    echo "Qt installation prefix: $(${QT_INSTALL_PREFIX}/bin/qmake -query QT_INSTALL_PREFIX)"
    echo "Qt library path: $(${QT_INSTALL_PREFIX}/bin/qmake -query QT_INSTALL_LIBS)"
else
    echo "✗ Qt installation failed - qmake not found"
    exit 1
fi

# Set up environment
echo "Setting up Qt environment..."
cat > "${QT_INSTALL_PREFIX}/setup-qt-env.sh" << 'EOF'
#!/bin/bash
# Qt 6.6.3 Environment Setup

export QTDIR="/opt/qt6.6.3"
export QT_INSTALL_PREFIX="/opt/qt6.6.3"
export PATH="/opt/qt6.6.3/bin:${PATH}"
export LD_LIBRARY_PATH="/opt/qt6.6.3/lib:${LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="/opt/qt6.6.3/lib/pkgconfig:${PKG_CONFIG_PATH}"
export QT_PLUGIN_PATH="/opt/qt6.6.3/plugins"
export QML_IMPORT_PATH="/opt/qt6.6.3/qml"
export QML2_IMPORT_PATH="/opt/qt6.6.3/qml"

echo "Qt 6.6.3 environment activated"
echo "Qt version: $(qmake -query QT_VERSION)"
echo "Qt install prefix: $(qmake -query QT_INSTALL_PREFIX)"
EOF

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