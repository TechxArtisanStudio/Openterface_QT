#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev libxrandr-dev libegl-dev libegl-mesa0 libegl1 libgl-dev \
    libgl1-mesa-dev libgles-dev libgles1 libgles2 libglu1-mesa libglu1-mesa-dev libglvnd-core-dev \
    libglvnd-dev libglx-dev libopengl-dev libopengl0 libxcb-cursor-dev \
    libxcb-cursor0 libxcb-icccm4 libxcb-icccm4-dev libxcb-image0 \
    libxcb-image0-dev libxcb-keysyms1 libxcb-keysyms1-dev libxcb-render-util0 \
    libxcb-render-util0-dev libxcb-render0-dev libxcb-shm0-dev libxcb-util1 \
    libxfixes-dev libxi-dev libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev \
    libdbus-1-dev libfontconfig1-dev libfreetype-dev libxkbcommon-dev libxkbcommon-x11-dev libxrandr2 libxrandr-dev \
    libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
    libxrender-dev libxcb1-dev libxcb-glx0-dev libxcb-xfixes0-dev \
    libxcb-xinerama0-dev libxcb-xkb-dev libxcb-util-dev \
    libdrm-dev libgbm-dev libatspi2.0-dev \
    libvulkan-dev libssl-dev \
    libpulse-dev \
    clang-16 llvm-16-dev libclang-16-dev\
    yasm nasm # Dependencies for FFmpeg compilation

QT_VERSION=6.6.3
QT_MAJOR_VERSION=6.6
LIBUSB_VERSION=1.0.26
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
FFMPEG_PREFIX=/opt/Qt6
FFMPEG_VERSION="6.1.1"
GSTREAMER_VERSION="1.22.11"
WORK_DIR="${HOME}/qt-amd64-build"
# Update module list to include qtdeclarative (which provides Qt Quick)
MODULES=("qtbase" "qtshadertools" "qtdeclarative" "qtmultimedia" "qtsvg" "qtserialport" "qttools")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Create the build directory first
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
# Build or Install libusb from source
if $BUILD_ENABLED; then
    echo "Building libusb $LIBUSB_VERSION from source..."
    if [ ! -d "libusb" ]; then
        curl -L -o libusb.tar.bz2 "https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2"
        tar xf libusb.tar.bz2
        mv "libusb-${LIBUSB_VERSION}" libusb
        rm libusb.tar.bz2
    fi

    cd libusb
    ./configure --prefix="$FFMPEG_PREFIX" --enable-static --disable-shared --disable-udev
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libusb $LIBUSB_VERSION..."
    cd "$BUILD_DIR"/libusb
    sudo make install
fi
cd "$BUILD_DIR"

# Build FFmpeg statically
echo "Building FFmpeg statically..."
cd "$BUILD_DIR"
if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
    curl -L -o ffmpeg.tar.bz2 "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.bz2"
    tar xjf ffmpeg.tar.bz2
    rm ffmpeg.tar.bz2
fi

cd "ffmpeg-$FFMPEG_VERSION"
./configure \
    --prefix="$FFMPEG_PREFIX" \
    --enable-static \
    --disable-shared \
    --disable-doc \
    --disable-programs \
    --disable-outdevs \
    --enable-pic \
    --enable-libpulse \
    --disable-debug

make -j$(nproc)
make install


# Build GStreamer
echo "Building static GStreamer for ARM64..."
mkdir -p gstreamer_sources gstreamer_build
cd gstreamer_sources

# Download GStreamer core components
echo "Downloading GStreamer core..."
if [ ! -d "gstreamer-${GSTREAMER_VERSION}" ]; then
  wget https://gstreamer.freedesktop.org/src/gstreamer/gstreamer-${GSTREAMER_VERSION}.tar.xz
  tar xvf gstreamer-${GSTREAMER_VERSION}.tar.xz
  rm gstreamer-${GSTREAMER_VERSION}.tar.xz
else
  echo "GStreamer core directory already exists, skipping download."
fi

echo "Downloading gst-plugins-base..."
if [ ! -d "gst-plugins-base-${GSTREAMER_VERSION}" ]; then
  wget https://gstreamer.freedesktop.org/src/gst-plugins-base/gst-plugins-base-${GSTREAMER_VERSION}.tar.xz
  tar xf gst-plugins-base-${GSTREAMER_VERSION}.tar.xz
  rm gst-plugins-base-${GSTREAMER_VERSION}.tar.xz
else
  echo "gst-plugins-base directory already exists, skipping download."
fi

echo "Downloading gst-plugins-good..."
if [ ! -d "gst-plugins-good-${GSTREAMER_VERSION}" ]; then
  wget https://gstreamer.freedesktop.org/src/gst-plugins-good/gst-plugins-good-${GSTREAMER_VERSION}.tar.xz
  tar xf gst-plugins-good-${GSTREAMER_VERSION}.tar.xz
  rm gst-plugins-good-${GSTREAMER_VERSION}.tar.xz
else
  echo "gst-plugins-good directory already exists, skipping download."
fi

echo "Downloading gst-plugins-bad..."
if [ ! -d "gst-plugins-bad-${GSTREAMER_VERSION}" ]; then
  wget https://gstreamer.freedesktop.org/src/gst-plugins-bad/gst-plugins-bad-${GSTREAMER_VERSION}.tar.xz
  tar xf gst-plugins-bad-${GSTREAMER_VERSION}.tar.xz
  rm gst-plugins-bad-${GSTREAMER_VERSION}.tar.xz
else
  echo "gst-plugins-bad directory already exists, skipping download."
fi

# Set up build environment for GStreamer
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig"

# Install development libraries for GStreamer and Qt (including GLib)
echo "Installing development libraries for GStreamer and Qt..."
sudo apt-get install -y \
  libglib2.0-dev \
  libgobject-2.0-dev \
  libgio-2.0-dev \
  libc6-dev \
  linux-libc-dev \
  libudev-dev \
  libxkbcommon-dev \
  libxkbcommon-x11-dev \
  libxcb1-dev \
  libxcb-util-dev \
  libxcb-keysyms1-dev \
  libxcb-image0-dev \
  libxcb-shm0-dev \
  libxcb-icccm4-dev \
  libxcb-sync-dev \
  libxcb-xfixes0-dev \
  libxcb-shape0-dev \
  libxcb-randr0-dev \
  libxcb-render-util0-dev \
  libxcb-render0-dev \
  libxcb-glx0-dev \
  libxcb-xinerama0-dev \
  libxcb-xinput-dev \
  libx11-dev \
  libxext-dev \
  libxv-dev \
  libgl1-mesa-dev \
  libgles2-mesa-dev \
  libegl1-mesa-dev \
  liborc-0.4-dev \
  libpcre2-dev \
  libffi-dev \
  libmount-dev \
  libblkid-dev \
  libselinux1-dev \
  libvorbis-dev \
  libvorbisenc2 \
  libtheora-dev \
  zlib1g-dev || echo "Some libraries installation failed, continuing with available libraries"

# Build static ORC library (required for GStreamer static linking)
echo "Building static ORC library..."
cd "${WORK_DIR}"
mkdir -p orc_sources orc_build
cd orc_sources

# Download ORC if not already present
if [ ! -d "orc-0.4.33" ]; then
  echo "Downloading ORC 0.4.33..."
  wget https://gstreamer.freedesktop.org/src/orc/orc-0.4.33.tar.xz
  tar -xf orc-0.4.33.tar.xz
  rm orc-0.4.33.tar.xz
else
  echo "ORC source directory already exists, skipping download."
fi

# Build static ORC library
cd orc-0.4.33
if [ ! -f "/opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a" ]; then
  echo "Configuring and building static ORC library..."
  meson setup build --prefix=/opt/orc-static --default-library=static
  ninja -C build
  sudo ninja -C build install
  
  # Verify the static library was created
  if [ -f "/opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a" ]; then
    echo "✓ Static ORC library successfully built and installed"
    ls -la /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a
  else
    echo "✗ Warning: Static ORC library not found after installation"
  fi
else
  echo "Static ORC library already built, skipping build."
fi

cd "${WORK_DIR}"/gstreamer_sources

# Build GStreamer core
echo "Building GStreamer core..."
cd gstreamer-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/libgstreamer-1.0.a" ]; then
  meson setup build \
    --prefix="${WORK_DIR}/gstreamer_build" \
    --libdir=lib \
    --default-library=static \
    -Dexamples=disabled \
    -Dtests=disabled \
    -Dbenchmarks=disabled \
    -Dtools=disabled \
    -Ddoc=disabled \
    -Dgst_debug=false \
    -Dnls=disabled

  ninja -C build
  ninja -C build install
else
  echo "GStreamer core already built, skipping build."
fi
cd ..

# Build gst-plugins-base
echo "Building gst-plugins-base..."
cd gst-plugins-base-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/libgstbase-1.0.a" ]; then
  meson setup build \
    --prefix="${WORK_DIR}/gstreamer_build" \
    --libdir=lib \
    --default-library=static \
    -Dexamples=disabled \
    -Dtests=disabled \
    -Ddoc=disabled \
    -Dtools=disabled \
    -Dalsa=enabled \
    -Dcdparanoia=disabled \
    -Dlibvisual=disabled \
    -Dorc=enabled \
    -Dtremor=disabled \
    -Dvorbis=enabled \
    -Dx11=enabled \
    -Dxshm=enabled \
    -Dxvideo=enabled \
    -Dgl=enabled \
    -Dgl_platform=glx \
    -Dgl_winsys=x11 \
    -Dvideotestsrc=enabled \
    -Dvideoconvert=enabled \
    -Dvideoscale=enabled \
    -Dapp=enabled \
    -Daudioconvert=enabled \
    -Daudioresample=enabled \
    -Dtypefind=enabled \
    -Dplayback=enabled \
    -Dsubparse=enabled \
    -Dencoding=enabled \
    -Dcompositor=enabled \
    -Doverlaycomposition=enabled \
    -Dpbtypes=enabled \
    -Ddmabuf=enabled \
    -Dvideo=enabled \
    -Daudio=enabled \
    -Dvideooverlay=enabled \
    -Drtp=enabled \
    -Dtag=enabled \
    -Dpbutils=enabled \
    -Dnls=disabled

  ninja -C build
  ninja -C build install
  
  # Copy additional headers and libraries that might be needed
  echo "Copying additional GStreamer headers to Qt installation..."
  sudo mkdir -p ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video
  sudo mkdir -p ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/audio
  sudo mkdir -p ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/rtp
  sudo mkdir -p ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/pbutils
  sudo mkdir -p ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/tag
  
  # Copy source headers
  sudo cp gst-libs/gst/video/*.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/ 2>/dev/null || true
  sudo cp gst-libs/gst/audio/*.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/audio/ 2>/dev/null || true
  sudo cp gst-libs/gst/rtp/*.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/rtp/ 2>/dev/null || true
  sudo cp gst-libs/gst/pbutils/*.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/pbutils/ 2>/dev/null || true
  sudo cp gst-libs/gst/tag/*.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/tag/ 2>/dev/null || true
  
  # Copy generated headers
  sudo cp build/gst-libs/gst/video/video-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/ 2>/dev/null || true
  sudo cp build/gst-libs/gst/video/video-orc.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/ 2>/dev/null || true
  sudo cp build/gst-libs/gst/audio/audio-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/audio/ 2>/dev/null || true
  sudo cp build/gst-libs/gst/rtp/gstrtp-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/rtp/ 2>/dev/null || true
  sudo cp build/gst-libs/gst/pbutils/pbutils-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/pbutils/ 2>/dev/null || true
  sudo cp build/gst-libs/gst/tag/tag-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/tag/ 2>/dev/null || true
  
  # Copy all static libraries
  echo "Copying all GStreamer static libraries..."
  find build -name "libgst*.a" -exec sudo cp {} ${QT_TARGET_DIR}/lib/ \; 2>/dev/null || true
else
  echo "gst-plugins-base already built, skipping build."
fi
cd ..

# Build gst-plugins-good
echo "Building gst-plugins-good..."
cd gst-plugins-good-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstvideotestsrc.a" ]; then
  meson setup build \
    --prefix="${WORK_DIR}/gstreamer_build" \
    --libdir=lib \
    --default-library=static \
    -Dexamples=disabled \
    -Dtests=disabled \
    -Ddoc=disabled \
    -Dqt5=disabled \
    -Dqt6=disabled \
    -Dalpha=disabled \
    -Dapetag=disabled \
    -Daudiofx=disabled \
    -Dcutter=disabled \
    -Ddebugutils=disabled \
    -Ddeinterlace=disabled \
    -Ddtmf=disabled \
    -Deffectv=disabled \
    -Dequalizer=disabled \
    -Dgoom=disabled \
    -Dgoom2k1=disabled \
    -Dgtk3=disabled \
    -Dicydemux=disabled \
    -Dimagefreeze=disabled \
    -Dinterleave=disabled \
    -Disomp4=disabled \
    -Dlaw=disabled \
    -Dlevel=disabled \
    -Dmatroska=disabled \
    -Dmonoscope=disabled \
    -Dmultifile=disabled \
    -Dmultipart=disabled \
    -Dreplaygain=disabled \
    -Drtp=enabled \
    -Drtpmanager=enabled \
    -Drtsp=enabled \
    -Dshapewipe=disabled \
    -Dsmpte=disabled \
    -Dspectrum=disabled \
    -Dudp=enabled \
    -Dvideobox=disabled \
    -Dvideocrop=enabled \
    -Dvideofilter=enabled \
    -Dvideomixer=disabled \
    -Dwavenc=disabled \
    -Dwavparse=disabled \
    -Dy4m=disabled \
    -Doss=disabled \
    -Doss4=disabled \
    -Dv4l2=enabled \
    -Dximagesrc=disabled \
    -Dnls=disabled

  ninja -C build
  ninja -C build install
  
  # Copy plugin static libraries to Qt target directory
  echo "Copying gst-plugins-good static libraries to Qt installation..."
  sudo mkdir -p ${QT_TARGET_DIR}/lib/gstreamer-1.0
  find build -name "libgst*.a" -exec sudo cp {} ${QT_TARGET_DIR}/lib/gstreamer-1.0/ \; 2>/dev/null || true
else
  echo "gst-plugins-good already built, skipping build."
fi
cd ..

# Build gst-plugins-bad
echo "Building gst-plugins-bad..."
cd gst-plugins-bad-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstvideoparsersbad.a" ]; then
  meson setup build \
    --prefix="${WORK_DIR}/gstreamer_build" \
    --libdir=lib \
    --default-library=static \
    -Dexamples=disabled \
    -Dtests=disabled \
    -Ddoc=disabled \
    -Dnls=disabled

  ninja -C build
  ninja -C build install
  
  # Copy plugin static libraries to Qt target directory
  echo "Copying gst-plugins-bad static libraries to Qt installation..."
  find build -name "libgst*.a" -exec sudo cp {} ${QT_TARGET_DIR}/lib/gstreamer-1.0/ \; 2>/dev/null || true
else
  echo "gst-plugins-bad already built, skipping build."
fi
cd ..

# Update PKG_CONFIG_PATH to include GStreamer
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${PKG_CONFIG_PATH}"
cd "${WORK_DIR}"


# Download and extract modules
cd "$BUILD_DIR"
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Define common CMake flags to suppress warnings
CMAKE_COMMON_FLAGS="-Wno-dev -DCMAKE_POLICY_DEFAULT_CMP0177=NEW -DCMAKE_POLICY_DEFAULT_CMP0174=NEW"

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    -DFEATURE_xlib=ON \
    -DFEATURE_xcb_xlib=ON \
    -DFEATURE_xkbcommon=ON \
    -DFEATURE_xkbcommon_x11=ON \
    -DTEST_xcb_syslibs=ON \
    -DQT_FEATURE_clang=OFF \
    -DFEATURE_clang=ON \
    ..

ninja
sudo ninja install

# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-lfontconfig -lfreetype" \
    ..

ninja
sudo ninja install

# Build qtdeclarative (Qt Quick) before qtmultimedia
echo "Building qtdeclarative..."
cd "$BUILD_DIR/qtdeclarative"
mkdir -p build
cd build
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    ..

ninja
sudo ninja install



# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" && "$module" != "qtdeclarative" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir -p build
        cd build
        echo "Building $module..."

        # Add specific flags for qtmultimedia to enable FFmpeg and PulseAudio but disable GStreamer
        if [[ "$module" == "qtmultimedia" ]]; then
            PKG_CONFIG_PATH="$FFMPEG_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH" \
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX;$FFMPEG_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_gstreamer=OFF \
                -DINPUT_gstreamer=OFF \
                -DFEATURE_pulseaudio=ON \
                -DFEATURE_ffmpeg=ON \
                -DINPUT_ffmpeg=ON \
                -DFEATURE_avfoundation=OFF \
                -DCMAKE_FIND_ROOT_PATH="$FFMPEG_PREFIX" \
                -DCMAKE_EXE_LINKER_FLAGS="-L$FFMPEG_PREFIX/lib" \
                -DFFMPEG_PATH="$FFMPEG_PREFIX" \
                ..
        elif [[ "$module" == "qttools" ]]; then
            echo "Building $module..."
            CLANG_PREFIX="/usr/lib/llvm-16"
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_linguist=ON \
                -DFEATURE_lupdate=ON \
                -DFEATURE_lrelease=ON \
                -DFEATURE_designer=OFF \
                -DFEATURE_assistant=OFF \
                -DFEATURE_qtattributionsscanner=OFF \
                -DFEATURE_qtdiag=OFF \
                -DFEATURE_qtplugininfo=OFF \
                -DFEATURE_clang=ON \
                -DFEATURE_clangcpp=ON \
                -DLLVM_INSTALL_DIR="$CLANG_PREFIX" \
                -DLLVM_CMAKE_DIR="$CLANG_PREFIX/cmake" \
                ..

        else
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        fi
        
        ninja
        sudo ninja install
    fi
done


# Verify architecture of built libraries
echo "Verifying architecture of built libraries..."

echo "FFmpeg library architecture:"
if [ -f "${WORK_DIR}/ffmpeg_build/lib/libavcodec.a" ]; then
    file "${WORK_DIR}/ffmpeg_build/lib/libavcodec.a" | head -1
else
    echo "Warning: FFmpeg libavcodec.a not found"
fi

echo "GStreamer library architecture:"
if [ -f "${WORK_DIR}/gstreamer_build/lib/libgstreamer-1.0.a" ]; then
    file "${WORK_DIR}/gstreamer_build/lib/libgstreamer-1.0.a" | head -1
else
    echo "Warning: GStreamer libgstreamer-1.0.a not found"
fi

echo "Qt Core library architecture:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6Core.a" ]; then
    file "${QT_TARGET_DIR}/lib/libQt6Core.a" | head -1
else
    echo "Warning: Qt libQt6Core.a not found"
fi

echo "Verifying specific Qt modules..."
echo "Qt Multimedia:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6Multimedia.a" ]; then
    echo "  ✓ libQt6Multimedia.a found"
    file "${QT_TARGET_DIR}/lib/libQt6Multimedia.a" | head -1
else
    echo "  ✗ libQt6Multimedia.a not found"
fi

echo "Qt MultimediaWidgets:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6MultimediaWidgets.a" ]; then
    echo "  ✓ libQt6MultimediaWidgets.a found"
    file "${QT_TARGET_DIR}/lib/libQt6MultimediaWidgets.a" | head -1
else
    echo "  ✗ libQt6MultimediaWidgets.a not found"
fi

echo "Qt SerialPort:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6SerialPort.a" ]; then
    echo "  ✓ libQt6SerialPort.a found"
    file "${QT_TARGET_DIR}/lib/libQt6SerialPort.a" | head -1
else
    echo "  ✗ libQt6SerialPort.a not found"
fi

echo "Qt Svg:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6Svg.a" ]; then
    echo "  ✓ libQt6Svg.a found"
    file "${QT_TARGET_DIR}/lib/libQt6Svg.a" | head -1
else
    echo "  ✗ libQt6Svg.a not found"
fi

echo "Qt SvgWidgets:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6SvgWidgets.a" ]; then
    echo "  ✓ libQt6SvgWidgets.a found"
    file "${QT_TARGET_DIR}/lib/libQt6SvgWidgets.a" | head -1
else
    echo "  ✗ libQt6SvgWidgets.a not found"
fi

# Copy FFmpeg and GStreamer libraries to the Qt target directory
echo "Copying FFmpeg libraries to ${QT_TARGET_DIR}..."
sudo mkdir -p ${QT_TARGET_DIR}/lib
sudo mkdir -p ${QT_TARGET_DIR}/include
sudo mkdir -p ${QT_TARGET_DIR}/bin
sudo cp -a ${WORK_DIR}/ffmpeg_build/lib/. ${QT_TARGET_DIR}/lib/
sudo cp -a ${WORK_DIR}/ffmpeg_build/include/. ${QT_TARGET_DIR}/include/

echo "Copying GStreamer libraries to ${QT_TARGET_DIR}..."
sudo cp -a ${WORK_DIR}/gstreamer_build/lib/. ${QT_TARGET_DIR}/lib/
sudo cp -a ${WORK_DIR}/gstreamer_build/include/. ${QT_TARGET_DIR}/include/

# Verify GStreamer installation completeness
echo "Verifying GStreamer installation..."
echo "Checking critical GStreamer components:"

# Check for required headers
REQUIRED_HEADERS="gst/video/videooverlay.h gst/video/video-enumtypes.h gst/audio/audio-enumtypes.h gst/rtp/gstrtp-enumtypes.h gst/pbutils/pbutils-enumtypes.h gst/tag/tag-enumtypes.h"

for header in $REQUIRED_HEADERS; do
  if [ -f "${QT_TARGET_DIR}/include/gstreamer-1.0/${header}" ]; then
    echo "  ✓ ${header} found"
  else
    echo "  ✗ ${header} missing"
  fi
done

# Check for required libraries
REQUIRED_LIBS="libgstvideo-1.0.a libgstaudio-1.0.a libgsttag-1.0.a libgstrtp-1.0.a libgstpbutils-1.0.a libgstbase-1.0.a libgstreamer-1.0.a"

for lib in $REQUIRED_LIBS; do
  if [ -f "${QT_TARGET_DIR}/lib/${lib}" ]; then
    echo "  ✓ ${lib} found"
  else
    echo "  ✗ ${lib} missing"
  fi
done

# Create GStreamer verification script
# Ensure the bin directory exists
sudo mkdir -p "${QT_TARGET_DIR}/bin"

# Create the comprehensive verification script using sudo tee
sudo tee "${QT_TARGET_DIR}/bin/verify-gstreamer.sh" > /dev/null << 'EOF'
#!/bin/bash
echo "GStreamer Installation Verification"
echo "===================================="

QT_TARGET_DIR="/opt/Qt6-arm64"

# Test for required header file
VIDEO_OVERLAY_HEADER="${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/videooverlay.h"
if [ -f "$VIDEO_OVERLAY_HEADER" ]; then
    echo "✓ gst/video/videooverlay.h found"
else
    echo "✗ gst/video/videooverlay.h NOT found"
    echo "  Expected at: $VIDEO_OVERLAY_HEADER"
fi

# Test for required libraries
echo -e "\nChecking GStreamer core static libraries:"
CORE_LIBS="libgstvideo-1.0.a libgstaudio-1.0.a libgstpbutils-1.0.a libgstrtp-1.0.a libgsttag-1.0.a libgstbase-1.0.a libgstreamer-1.0.a"
for lib in $CORE_LIBS; do
    if [ -f "${QT_TARGET_DIR}/lib/$lib" ]; then
        echo "✓ $lib found"
    else
        echo "✗ $lib NOT found"
    fi
done

# Test for GStreamer core headers
GSTREAMER_H="${QT_TARGET_DIR}/include/gstreamer-1.0/gst/gst.h"
if [ -f "$GSTREAMER_H" ]; then
    echo "✓ GStreamer core headers found"
else
    echo "✗ GStreamer core headers NOT found"
fi

# Check for GStreamer plugin libraries
echo -e "\nChecking GStreamer plugin libraries:"
echo "Looking for plugins in: ${QT_TARGET_DIR}/lib/gstreamer-1.0/"

if [ -d "${QT_TARGET_DIR}/lib/gstreamer-1.0" ]; then
    echo "Plugin directory exists"
    
    # Check for specific plugins we enabled
    PLUGIN_LIBS="
    libgstv4l2.a
    libgstrtp.a
    libgstrtpmanager.a
    libgstrtsp.a
    libgstudp.a
    libgstvideocrop.a
    libgstvideofilter.a
    libgstvideotestsrc.a
    "
    
    for plugin in $PLUGIN_LIBS; do
        if [ -f "${QT_TARGET_DIR}/lib/gstreamer-1.0/$plugin" ]; then
            echo "✓ Plugin: $plugin found"
        else
            echo "✗ Plugin: $plugin NOT found"
        fi
    done
    
    echo -e "\nAll available plugins:"
    ls -la "${QT_TARGET_DIR}/lib/gstreamer-1.0/" 2>/dev/null | grep "\.a$" || echo "No .a files found in plugin directory"
    
else
    echo "✗ Plugin directory ${QT_TARGET_DIR}/lib/gstreamer-1.0/ does not exist"
    echo "  Checking alternative locations..."
    
    # Check if plugins are in main lib directory
    echo -e "\nChecking for plugins in main lib directory:"
    find "${QT_TARGET_DIR}/lib" -name "libgstv4l2*" -type f 2>/dev/null || echo "No v4l2 plugins found"
    find "${QT_TARGET_DIR}/lib" -name "libgst*rtp*" -type f 2>/dev/null || echo "No RTP plugins found"
    
    echo -e "\nAll GStreamer-related files in lib directory:"
    find "${QT_TARGET_DIR}/lib" -name "libgst*" -type f 2>/dev/null | head -20
fi

# Check build directory for verification
WORK_DIR="${HOME}/qt-arm64-build"
if [ -d "${WORK_DIR}/gstreamer_build" ]; then
    echo -e "\n=== Checking original build directory ==="
    echo "Build directory: ${WORK_DIR}/gstreamer_build"
    
    if [ -d "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0" ]; then
        echo -e "\nPlugins in build directory:"
        ls -la "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/" 2>/dev/null | grep "\.a$" || echo "No .a files in build plugin directory"
        
        # Specifically check for v4l2
        if [ -f "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstv4l2.a" ]; then
            echo "✓ v4l2 plugin found in build directory"
            file "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstv4l2.a"
        else
            echo "✗ v4l2 plugin NOT found in build directory"
        fi
    else
        echo "No plugin directory in build area"
    fi
fi

# Test if we can find v4l2 symbols in any static library
echo -e "\n=== Searching for v4l2 symbols ==="
echo "Searching for v4l2src symbol in static libraries..."

# Search in Qt target directory
V4L2_FOUND=false
for lib in $(find "${QT_TARGET_DIR}/lib" -name "*.a" -type f 2>/dev/null); do
    if nm "$lib" 2>/dev/null | grep -q "v4l2src\|gst_v4l2"; then
        echo "✓ v4l2 symbols found in: $(basename $lib)"
        V4L2_FOUND=true
    fi
done

if [ "$V4L2_FOUND" = "false" ]; then
    echo "✗ No v4l2 symbols found in any static library"
    
    # Check if v4l2 was actually built
    if [ -d "${WORK_DIR}/gstreamer_sources/gst-plugins-good-1.22.11" ]; then
        echo -e "\nChecking gst-plugins-good build configuration..."
        BUILD_DIR="${WORK_DIR}/gstreamer_sources/gst-plugins-good-1.22.11/build"
        if [ -f "${BUILD_DIR}/meson-info/intro-buildoptions.json" ]; then
            echo "Build options for v4l2:"
            cat "${BUILD_DIR}/meson-info/intro-buildoptions.json" | grep -A5 -B5 "v4l2" || echo "v4l2 option not found in build config"
        fi
        
        if [ -f "${BUILD_DIR}/meson-logs/meson-log.txt" ]; then
            echo -e "\nChecking build log for v4l2:"
            tail -50 "${BUILD_DIR}/meson-logs/meson-log.txt" | grep -i "v4l2" || echo "No v4l2 mentions in recent build log"
        fi
    fi
fi

echo -e "\n=== System V4L2 Check ==="
echo "Checking if V4L2 development headers are available:"
if [ -f "/usr/include/linux/videodev2.h" ]; then
    echo "✓ V4L2 system headers found at /usr/include/linux/videodev2.h"
elif [ -f "/usr/include/videodev2.h" ]; then
    echo "✓ V4L2 system headers found at /usr/include/videodev2.h"
else
    echo "✗ V4L2 system headers NOT found"
    echo "  Install with: sudo apt-get install linux-libc-dev"
fi

echo -e "\nChecking for V4L2 devices:"
if [ -d "/dev" ]; then
    ls -la /dev/video* 2>/dev/null || echo "No video devices found"
fi

echo -e "\nDone."
EOF

sudo chmod +x "${QT_TARGET_DIR}/bin/verify-gstreamer.sh"
echo "GStreamer verification script created at: ${QT_TARGET_DIR}/bin/verify-gstreamer.sh"

# Clean up
cd /
# sudo rm -rf "$WORK_DIR"

echo "Qt ${QT_VERSION}, FFmpeg ${FFMPEG_VERSION}, and GStreamer ${GSTREAMER_VERSION} for ARM64 build completed successfully!"
echo "Qt installed to: ${QT_TARGET_DIR}"
echo "FFmpeg libraries installed to: ${QT_TARGET_DIR}/lib"
echo "FFmpeg headers installed to: ${QT_TARGET_DIR}/include"
echo "GStreamer libraries installed to: ${QT_TARGET_DIR}/lib"
echo "GStreamer headers installed to: ${QT_TARGET_DIR}/include"
echo "Static ORC library installed to: /opt/orc-static/lib/aarch64-linux-gnu/"
echo "Verification script available at: ${QT_TARGET_DIR}/bin/verify-gstreamer.sh"

echo ""
echo "========================================================================================="
echo "BUILD INSTRUCTIONS FOR OPENTERFACE QT APPLICATION"
echo "========================================================================================="
echo ""
echo "This build environment includes:"
echo "  - Qt ${QT_VERSION} with multimedia support"
echo "  - FFmpeg ${FFMPEG_VERSION} static libraries"
echo "  - GStreamer ${GSTREAMER_VERSION} with video overlay support"
echo "  - Static ORC library 0.4.33 for GStreamer optimization"
echo "  - All necessary headers and enumtypes for GStreamer video components"
echo ""
echo "To build the static OpenTerface QT application using this environment, run:"
echo ""
echo "1. Navigate to the OpenTerface QT project directory:"
echo "   cd /path/to/Openterface_QT"
echo ""
echo "2. Create a build directory:"
echo "   mkdir -p build && cd build"
echo ""
echo "3. Set environment variables before configuring:"
echo "   export PKG_CONFIG_PATH=\"${QT_TARGET_DIR}/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig:\$PKG_CONFIG_PATH\""
echo ""
echo "4. Configure with CMake using the static Qt installation:"
echo "   cmake -DCMAKE_PREFIX_PATH=\"${QT_TARGET_DIR}\" \\"
echo "         -DCMAKE_BUILD_TYPE=Release \\"
echo "         -DBUILD_SHARED_LIBS=OFF \\"
echo "         -DQt6_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6\" \\"
echo "         -DQt6Multimedia_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6Multimedia\" \\"
echo "         -DQt6MultimediaWidgets_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6MultimediaWidgets\" \\"
echo "         -DQt6SerialPort_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6SerialPort\" \\"
echo "         -DQt6Svg_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6Svg\" \\"
echo "         .."
echo ""
echo "5. Build the application:"
echo "   make -j\$(nproc)"
echo ""
echo "5. The static binary will be available in the build directory."
echo ""
echo "Alternative using Ninja (if preferred):"
echo "   cmake -GNinja -DCMAKE_PREFIX_PATH=\"${QT_TARGET_DIR}\" \\"
echo "         -DCMAKE_BUILD_TYPE=Release \\"
echo "         -DBUILD_SHARED_LIBS=OFF \\"
echo "         -DQT_STATIC_BUILD=ON \\"
echo "         -DQT_TARGET_DIR=\"${QT_TARGET_DIR}\" \\"
echo "         -DQt6_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6\" \\"
echo "         -DQt6Multimedia_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6Multimedia\" \\"
echo "         -DQt6MultimediaWidgets_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6MultimediaWidgets\" \\"
echo "         -DQt6SerialPort_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6SerialPort\" \\"
echo "         -DQt6Svg_DIR=\"${QT_TARGET_DIR}/lib/cmake/Qt6Svg\" \\"
echo "         -DPKG_CONFIG_PATH=\"${QT_TARGET_DIR}/lib/pkgconfig\" \\"
echo "         .."
echo "   ninja"
echo ""
echo "Note: The resulting binary will be statically linked and can run on other ARM64"
echo "      systems without requiring Qt or multimedia libraries to be installed."
echo ""
echo "TROUBLESHOOTING:"
echo "If you encounter 'Could NOT find Qt6Multimedia' or similar errors:"
echo "1. Verify the Qt modules were built successfully by checking:"
echo "   ls -la ${QT_TARGET_DIR}/lib/cmake/"
echo "2. Ensure all required Qt module directories exist:"
echo "   ls -la ${QT_TARGET_DIR}/lib/cmake/Qt6*"
echo "3. Set environment variables before running cmake:"
echo "   export PKG_CONFIG_PATH=\"${QT_TARGET_DIR}/lib/pkgconfig:\$PKG_CONFIG_PATH\""
echo "   export CMAKE_PREFIX_PATH=\"${QT_TARGET_DIR}:\$CMAKE_PREFIX_PATH\""
echo ""
echo "If you encounter GStreamer-related build errors:"
echo "1. Run the GStreamer verification script:"
echo "   ${QT_TARGET_DIR}/bin/verify-gstreamer.sh"
echo "2. Check if videooverlay.h header is available:"
echo "   ls -la ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/videooverlay.h"
echo "3. Verify all required GStreamer libraries are present:"
echo "   ls -la ${QT_TARGET_DIR}/lib/libgst*.a | grep -E '(video|audio|tag|rtp|pbutils)'"
echo "4. If you encounter ORC library linking errors (undefined reference to orc_program_take_code):"
echo "   Verify the static ORC library is installed:"
echo "   ls -la /opt/orc-static/lib/aarch64-linux-gnu/liborc-0.4.a"
echo "   If missing, rebuild with the static ORC library section of this script"
echo ""
echo "If you encounter 'gst/video/video-enumtypes.h: No such file' errors:"
echo "1. The build script should have copied all generated headers automatically"
echo "2. If missing, they are available in the build directory:"
echo "   find ${WORK_DIR}/gstreamer_sources -name '*enumtypes.h'"
echo "3. Copy them manually if needed:"
echo "   sudo cp \${WORK_DIR}/gstreamer_sources/gst-plugins-base-${GSTREAMER_VERSION}/build/gst-libs/gst/video/video-enumtypes.h ${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/"
echo ""
echo "If you encounter 'PkgConfig::Libudev' target not found errors:"
echo "1. Install pkg-config and libudev development packages:"
echo "   sudo apt-get install -y pkg-config libudev-dev"
echo "2. Ensure PKG_CONFIG_PATH is set correctly before cmake:"
echo "   export PKG_CONFIG_PATH=\"${QT_TARGET_DIR}/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig:\$PKG_CONFIG_PATH\""
echo "3. Check if libudev.pc exists:"
echo "   pkg-config --exists libudev && echo 'libudev found' || echo 'libudev not found'"
echo ""
echo "4. If issues persist, try cleaning the build directory and reconfiguring."
echo "========================================================================================="


echo "OpenTerface QT $QT_VERSION has been successfully built and installed to $INSTALL_PREFIX"
