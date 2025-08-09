#!/bin/bash

set -e

# Default settings
QT_VERSION=${QT_VERSION:-"6.6.3"}
QT_MAJOR_VERSION="6.6"
QT_MODULES=${QT_MODULES:-"qtbase qtdeclarative qtsvg qtshadertools qtmultimedia qtsvg qtserialport qttools"}
QT_TARGET_DIR=${QT_TARGET_DIR:-"/opt/Qt6-arm64"}
CROSS_COMPILE=${CROSS_COMPILE:-"aarch64-linux-gnu-"}
FFMPEG_VERSION="6.1.1"
GSTREAMER_VERSION="1.22.11"
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/submodules"

echo "Starting Qt ${QT_VERSION}, FFmpeg ${FFMPEG_VERSION}, and GStreamer ${GSTREAMER_VERSION} build for ARM64..."

# Install cross-compilation dependencies
echo "Installing cross-compilation dependencies..."
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  ninja-build \
  cmake \
  pkg-config \
  gcc-aarch64-linux-gnu \
  g++-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu \
  nasm \
  yasm \
  libass-dev \
  git \
  wget \
  unzip \
  meson \
  flex \
  bison \
  gettext \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-wheel \
  libglib2.0-dev \
  libxml2-dev \
  zlib1g-dev \
  libdbus-1-dev

# Create build directory
WORK_DIR="${HOME}/qt-arm64-build"
mkdir -p "${WORK_DIR}"
echo "Working directory: ${WORK_DIR}"
cd "${WORK_DIR}"

# Build FFmpeg
echo "Building static FFmpeg for ARM64..."
mkdir -p ffmpeg_sources ffmpeg_build
cd ffmpeg_sources

# Download FFmpeg
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
  echo "Downloading FFmpeg ${FFMPEG_VERSION}..."
  wget -O ffmpeg-${FFMPEG_VERSION}.tar.bz2 https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2
  echo "Extracting FFmpeg ${FFMPEG_VERSION}..."
  tar xjf ffmpeg-${FFMPEG_VERSION}.tar.bz2
  echo "Removing FFmpeg package to save space..."
  rm ffmpeg-${FFMPEG_VERSION}.tar.bz2
else
  echo "FFmpeg ${FFMPEG_VERSION} directory already exists, skipping download and extraction."
fi

cd ffmpeg-${FFMPEG_VERSION}

# Configure FFmpeg for ARM64 cross-compilation
if [ ! -f "${WORK_DIR}/ffmpeg_build/lib/libavcodec.a" ]; then
  echo "Configuring and building FFmpeg for ARM64..."
  PKG_CONFIG_PATH="${WORK_DIR}/ffmpeg_build/lib/pkgconfig" \
  ./configure \
    --prefix="${WORK_DIR}/ffmpeg_build" \
    --pkg-config="pkg-config" \
    --cross-prefix=${CROSS_COMPILE} \
    --arch=aarch64 \
    --target-os=linux \
    --enable-cross-compile \
    --enable-static \
    --disable-shared \
    --disable-doc \
    --disable-programs \
    --disable-autodetect \
    --disable-everything \
    --enable-encoder=aac,opus,h264,hevc \
    --enable-decoder=aac,mp3,opus,h264,hevc \
    --enable-parser=aac,mpegaudio,opus,h264,hevc \
    --enable-small \
    --disable-debug

  # Build FFmpeg
  make -j$(nproc)
  make install
else
  echo "FFmpeg already built, skipping build."
fi

# Add ffmpeg install dir to PKG_CONFIG_PATH
export PKG_CONFIG_PATH="${WORK_DIR}/ffmpeg_build/lib/pkgconfig:${PKG_CONFIG_PATH}"
cd "${WORK_DIR}"

# Build GStreamer
echo "Building static GStreamer for ARM64..."
mkdir -p gstreamer_sources gstreamer_build
cd gstreamer_sources

# Download GStreamer core components
echo "Downloading GStreamer core..."
if [ ! -d "gstreamer-${GSTREAMER_VERSION}" ]; then
  wget https://gstreamer.freedesktop.org/src/gstreamer/gstreamer-${GSTREAMER_VERSION}.tar.xz
  tar xf gstreamer-${GSTREAMER_VERSION}.tar.xz
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

# Set up cross-compilation environment for GStreamer
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"
export AR="${CROSS_COMPILE}ar"
export STRIP="${CROSS_COMPILE}strip"
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig"

# Install ARM64 development libraries for cross-compilation
echo "Installing ARM64 development libraries for GStreamer and Qt..."
sudo apt-get install -y \
  libglib2.0-dev:arm64 \
  libgobject-2.0-dev:arm64 \
  libgmodule-2.0-dev:arm64 \
  libc6-dev:arm64 \
  linux-libc-dev:arm64 \
  libudev-dev:arm64 \
  libxkbcommon-dev:arm64 \
  libxkbcommon-x11-dev:arm64 \
  libxcb1-dev:arm64 \
  libxcb-xlib0-dev:arm64 \
  libxcb-util-dev:arm64 \
  libxcb-keysyms1-dev:arm64 \
  libxcb-image0-dev:arm64 \
  libxcb-shm0-dev:arm64 \
  libxcb-icccm4-dev:arm64 \
  libxcb-sync-dev:arm64 \
  libxcb-xfixes0-dev:arm64 \
  libxcb-shape0-dev:arm64 \
  libxcb-randr0-dev:arm64 \
  libxcb-render-util0-dev:arm64 \
  libxcb-render0-dev:arm64 \
  libxcb-glx0-dev:arm64 \
  libxcb-xinerama0-dev:arm64 \
  libxcb-xinput-dev:arm64 || echo "Some ARM64 libraries installation failed, continuing with available libraries"

# Set up cross-compilation sysroot
SYSROOT="/usr/aarch64-linux-gnu"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"

# Build GStreamer core
echo "Building GStreamer core..."
cd gstreamer-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/libgstreamer-1.0.a" ]; then
  meson setup build \
    --cross-file /dev/stdin <<EOF
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${STRIP}'
pkgconfig = 'pkg-config'

[properties]
sys_root = '${SYSROOT}'
pkg_config_libdir = '${SYSROOT}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
c_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
cpp_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
EOF

meson configure build \
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
    --cross-file /dev/stdin <<EOF
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${STRIP}'
pkgconfig = 'pkg-config'

[properties]
sys_root = '${SYSROOT}'
pkg_config_libdir = '${SYSROOT}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${WORK_DIR}/gstreamer_build/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
c_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
cpp_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
EOF

meson configure build \
  --prefix="${WORK_DIR}/gstreamer_build" \
  --libdir=lib \
  --default-library=static \
  -Dexamples=disabled \
  -Dtests=disabled \
  -Ddoc=disabled \
  -Dtools=disabled \
  -Dalsa=disabled \
  -Dcdparanoia=disabled \
  -Dlibvisual=disabled \
  -Dorc=disabled \
  -Dtremor=disabled \
  -Dvorbis=disabled \
  -Dx11=disabled \
  -Dxshm=disabled \
  -Dxvideo=disabled \
  -Dnls=disabled

ninja -C build
ninja -C build install
else
  echo "gst-plugins-base already built, skipping build."
fi
cd ..

# Build gst-plugins-good
echo "Building gst-plugins-good..."
cd gst-plugins-good-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstvideotestsrc.a" ]; then
  meson setup build \
    --cross-file /dev/stdin <<EOF
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${STRIP}'
pkgconfig = 'pkg-config'

[properties]
sys_root = '${SYSROOT}'
pkg_config_libdir = '${SYSROOT}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${WORK_DIR}/gstreamer_build/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
c_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
cpp_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
EOF

meson configure build \
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
  -Drtp=disabled \
  -Drtpmanager=disabled \
  -Drtsp=disabled \
  -Dshapewipe=disabled \
  -Dsmpte=disabled \
  -Dspectrum=disabled \
  -Dudp=disabled \
  -Dvideobox=disabled \
  -Dvideocrop=disabled \
  -Dvideofilter=disabled \
  -Dvideomixer=disabled \
  -Dwavenc=disabled \
  -Dwavparse=disabled \
  -Dy4m=disabled \
  -Doss=disabled \
  -Doss4=disabled \
  -Dv4l2=disabled \
  -Dximagesrc=disabled \
  -Dnls=disabled

ninja -C build
ninja -C build install
else
  echo "gst-plugins-good already built, skipping build."
fi
cd ..

# Build gst-plugins-bad
echo "Building gst-plugins-bad..."
cd gst-plugins-bad-${GSTREAMER_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/libgstvideoparsersbad.a" ]; then
  meson setup build \
    --cross-file /dev/stdin <<EOF
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${STRIP}'
pkgconfig = 'pkg-config'

[properties]
sys_root = '${SYSROOT}'
pkg_config_libdir = '${SYSROOT}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${WORK_DIR}/gstreamer_build/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
c_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
cpp_link_args = ['-L${SYSROOT}/lib', '-L${SYSROOT}/usr/lib', '-L${SYSROOT}/usr/lib/aarch64-linux-gnu']
EOF

meson configure build \
  --prefix="${WORK_DIR}/gstreamer_build" \
  --libdir=lib \
  --default-library=static \
  -Dexamples=disabled \
  -Dtests=disabled \
  -Ddoc=disabled \
  -Dnls=disabled

ninja -C build
ninja -C build install
else
  echo "gst-plugins-bad already built, skipping build."
fi
cd ..

# Update PKG_CONFIG_PATH to include GStreamer
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${PKG_CONFIG_PATH}"
cd "${WORK_DIR}"

# Install host Qt for cross-compilation tools
echo "Installing host Qt for cross-compilation tools..."
HOST_QT_DIR="${WORK_DIR}/qt6_host"
if [ ! -f "${HOST_QT_DIR}/bin/moc" ]; then
    echo "Installing Qt ${QT_VERSION} for host (native)..."
    # Try to install Qt6 packages with fallback options
    sudo apt-get install -y qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-base-dev-tools || true
    
    # Try alternative D-Bus package names
    sudo apt-get install -y libqt6dbus6-dev || \
    sudo apt-get install -y qt6-base-dev || \
    echo "D-Bus packages not available, will build minimal host Qt with D-Bus support"
    
    # Find system Qt installation
    QT_HOST_PATH=$(dpkg -L qt6-base-dev 2>/dev/null | grep -E '/usr/lib/[^/]*/qt6$' | head -1)
    if [ -z "$QT_HOST_PATH" ]; then
        QT_HOST_PATH="/usr/lib/qt6"
    fi
    
    # Check if we have the necessary D-Bus tools
    DBUS_TOOLS_AVAILABLE=false
    if [ -f "${QT_HOST_PATH}/bin/qdbuscpp2xml" ] || [ -f "/usr/bin/qdbuscpp2xml" ] || [ -f "/usr/lib/qt6/libexec/qdbuscpp2xml" ]; then
        DBUS_TOOLS_AVAILABLE=true
        echo "D-Bus tools found in system Qt installation"
    fi
    
    # If system Qt is not available or doesn't have D-Bus tools, build a minimal host Qt
    if [ ! -f "${QT_HOST_PATH}/bin/moc" ] || [ "$DBUS_TOOLS_AVAILABLE" = "false" ]; then
        echo "Building minimal host Qt with D-Bus support..."
        mkdir -p qt6_host_build
        cd qt6_host_build
        
        # Download qtbase for host build
        if [ ! -f "qtbase-everywhere-src-${QT_VERSION}.zip" ]; then
            wget "${DOWNLOAD_BASE_URL}/qtbase-everywhere-src-${QT_VERSION}.zip"
            unzip "qtbase-everywhere-src-${QT_VERSION}.zip"
        fi
        
        cd "qtbase-everywhere-src-${QT_VERSION}"
        mkdir -p build && cd build
        
        # Configure with D-Bus support but minimal GUI
        cmake -GNinja \
            -DCMAKE_INSTALL_PREFIX="${HOST_QT_DIR}" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=ON \
            -DFEATURE_sql=OFF \
            -DFEATURE_testlib=OFF \
            -DFEATURE_icu=OFF \
            -DFEATURE_opengl=OFF \
            -DFEATURE_cups=OFF \
            -DFEATURE_printer=OFF \
            -DFEATURE_accessibility=OFF \
            -DFEATURE_dbus=ON \
            -DFEATURE_widgets=OFF \
            -DFEATURE_gui=OFF \
            -DFEATURE_network=ON \
            -DFEATURE_concurrent=ON \
            -DFEATURE_xml=ON \
            -DFEATURE_libudev=OFF \
            -DFEATURE_xkbcommon=OFF \
            -DFEATURE_xkbcommon_x11=OFF \
            -DFEATURE_evdev=OFF \
            -DFEATURE_libinput=OFF \
            -DFEATURE_tslib=OFF \
            -DFEATURE_xcb=OFF \
            -DFEATURE_xcb_xlib=OFF \
            -DFEATURE_eglfs=OFF \
            -DFEATURE_directfb=OFF \
            -DFEATURE_linuxfb=OFF \
            -DFEATURE_vnc=OFF \
            -DQT_QMLJS_TESTS=OFF \
            ..
        
        # Build core tools and D-Bus tools
        ninja -j$(nproc) qmake moc rcc uic qlalr
        
        # Try to build D-Bus tools (may fail if dependencies missing)
        ninja qdbuscpp2xml qdbusxml2cpp || echo "D-Bus tools build failed, continuing without them"
        
        ninja install
        cd "${WORK_DIR}"
        
        QT_HOST_PATH="${HOST_QT_DIR}"
    fi
else
    echo "Host Qt already available."
fi

# Download Qt sources
echo "Downloading Qt modules..."
mkdir -p qt6_modules
cd qt6_modules

# Download and extract each module
for module in $QT_MODULES; do
    if [ ! -d "$module" ]; then
        echo "Downloading $module..."
        wget "${DOWNLOAD_BASE_URL}/${module}-everywhere-src-${QT_VERSION}.zip"
        unzip "${module}-everywhere-src-${QT_VERSION}.zip"
        mv "${module}-everywhere-src-${QT_VERSION}" "${module}"
        rm "${module}-everywhere-src-${QT_VERSION}.zip"
    else
        echo "$module already exists, skipping download."
    fi
done

# Build Qt modules
echo "Building Qt modules for ARM64..."

# Set up cross-compilation environment for Qt
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"
export AR="${CROSS_COMPILE}ar"
export STRIP="${CROSS_COMPILE}strip"
export QT_HOST_PATH="${QT_HOST_PATH}"

# Get absolute paths for cross-compilation tools
CC_ABS=$(which ${CROSS_COMPILE}gcc)
CXX_ABS=$(which ${CROSS_COMPILE}g++)
AR_ABS=$(which ${CROSS_COMPILE}ar)
STRIP_ABS=$(which ${CROSS_COMPILE}strip)
RANLIB_ABS=$(which ${CROSS_COMPILE}ranlib)

# Verify tools exist
echo "Cross-compilation tools:"
echo "CC: ${CC_ABS}"
echo "CXX: ${CXX_ABS}"  
echo "AR: ${AR_ABS}"
echo "STRIP: ${STRIP_ABS}"
echo "RANLIB: ${RANLIB_ABS}"

# Critical: Set up pkg-config for cross-compilation to find ARM64 libraries
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR=""

# Create a cross-compilation pkg-config wrapper
CROSS_PKG_CONFIG="/tmp/${CROSS_COMPILE}pkg-config"
cat > "${CROSS_PKG_CONFIG}" <<EOF
#!/bin/bash
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="\${PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR=""
exec pkg-config "\$@"
EOF
chmod +x "${CROSS_PKG_CONFIG}"

# Create a CMake toolchain file for cross-compilation
TOOLCHAIN_FILE="/tmp/aarch64-toolchain.cmake"
cat > "${TOOLCHAIN_FILE}" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Set cross-compilation tools with absolute paths
set(CMAKE_C_COMPILER "${CC_ABS}")
set(CMAKE_CXX_COMPILER "${CXX_ABS}")
set(CMAKE_AR "${AR_ABS}")
set(CMAKE_RANLIB "${RANLIB_ABS}")
set(CMAKE_STRIP "${STRIP_ABS}")

# Prevent CMake from overriding our tool paths
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_AR_FORCED TRUE)
set(CMAKE_RANLIB_FORCED TRUE)
set(CMAKE_STRIP_FORCED TRUE)

# Cross-compilation sysroot
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING TRUE)
EOF

# Build qtbase first (required by all other modules)
if [ ! -f "${QT_TARGET_DIR}/lib/libQt6Core.a" ]; then
  echo "Building qtbase..."
  cd qtbase
  mkdir -p build && cd build
  
  # Set environment variables to override CMake tool detection
  export CMAKE_C_COMPILER="${CC_ABS}"
  export CMAKE_CXX_COMPILER="${CXX_ABS}"
  export CMAKE_AR="${AR_ABS}"
  export CMAKE_RANLIB="${RANLIB_ABS}"
  export CMAKE_STRIP="${STRIP_ABS}"
  
  cmake -GNinja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_INSTALL_PREFIX="${QT_TARGET_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="${QT_HOST_PATH}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static=ON \
    -DFEATURE_shared=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=OFF \
    -DFEATURE_cups=OFF \
    -DFEATURE_printer=OFF \
    -DFEATURE_accessibility=OFF \
    -DFEATURE_future=OFF \
    -DFEATURE_regularexpression=OFF \
    -DFEATURE_xmlstream=OFF \
    -DFEATURE_sessionmanager=OFF \
    -DFEATURE_libudev=ON \
    -DFEATURE_xkbcommon=ON \
    -DFEATURE_xkbcommon_x11=ON \
    -DFEATURE_xcb=ON \
    -DFEATURE_xcb_xlib=ON \
    -DFEATURE_gui=ON \
    -DFEATURE_widgets=ON \
    ..
  
  ninja -j$(nproc)
  sudo ninja install
  cd ../..
else
  echo "qtbase already built, skipping build."
fi

# Build qtshadertools (required by qtmultimedia)
if [ ! -f "${QT_TARGET_DIR}/lib/libQt6ShaderTools.a" ]; then
  echo "Building qtshadertools..."
  cd qtshadertools
  mkdir -p build && cd build
  
  # Set environment variables to override CMake tool detection
  export CMAKE_C_COMPILER="${CC_ABS}"
  export CMAKE_CXX_COMPILER="${CXX_ABS}"
  export CMAKE_AR="${AR_ABS}"
  export CMAKE_RANLIB="${RANLIB_ABS}"
  export CMAKE_STRIP="${STRIP_ABS}"
  
  cmake -GNinja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_INSTALL_PREFIX="${QT_TARGET_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_TARGET_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="${QT_HOST_PATH}" \
    -DBUILD_SHARED_LIBS=OFF \
    ..
  
  ninja -j$(nproc)
  sudo ninja install
  cd ../..
else
  echo "qtshadertools already built, skipping build."
fi

# Build other modules
for module in $QT_MODULES; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        if [ ! -f "${QT_TARGET_DIR}/lib/libQt6$(echo ${module#qt} | sed 's/.*/\u&/').a" ]; then
            echo "Building $module..."
            cd "$module"
            mkdir -p build && cd build
            
            # Set environment variables to override CMake tool detection
            export CMAKE_C_COMPILER="${CC_ABS}"
            export CMAKE_CXX_COMPILER="${CXX_ABS}"
            export CMAKE_AR="${AR_ABS}"
            export CMAKE_RANLIB="${RANLIB_ABS}"
            export CMAKE_STRIP="${STRIP_ABS}"
            
            if [[ "$module" == "qtmultimedia" ]]; then
                # Special configuration for qtmultimedia to enable FFmpeg and GStreamer
                cmake -GNinja \
                    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
                    -DCMAKE_INSTALL_PREFIX="${QT_TARGET_DIR}" \
                    -DCMAKE_PREFIX_PATH="${QT_TARGET_DIR}" \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DQT_HOST_PATH="${QT_HOST_PATH}" \
                    -DBUILD_SHARED_LIBS=OFF \
                    -DFEATURE_ffmpeg=ON \
                    -DFEATURE_gstreamer=ON \
                    -DPkgConfig_EXECUTABLE="${CROSS_PKG_CONFIG}" \
                    -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON \
                    ..
            else
                cmake -GNinja \
                    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
                    -DCMAKE_INSTALL_PREFIX="${QT_TARGET_DIR}" \
                    -DCMAKE_PREFIX_PATH="${QT_TARGET_DIR}" \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DQT_HOST_PATH="${QT_HOST_PATH}" \
                    -DBUILD_SHARED_LIBS=OFF \
                    ..
            fi
            
            ninja -j$(nproc)
            sudo ninja install
            cd ../..
        else
            echo "$module already built, skipping build."
        fi
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

echo "Qt library architecture:"
if [ -f "${QT_TARGET_DIR}/lib/libQt6Core.a" ]; then
    file "${QT_TARGET_DIR}/lib/libQt6Core.a" | head -1
else
    echo "Warning: Qt libQt6Core.a not found"
fi

# Copy FFmpeg and GStreamer libraries to the Qt target directory
echo "Copying FFmpeg libraries to ${QT_TARGET_DIR}..."
sudo mkdir -p ${QT_TARGET_DIR}/lib
sudo cp -a ${WORK_DIR}/ffmpeg_build/lib/. ${QT_TARGET_DIR}/lib/
sudo mkdir -p ${QT_TARGET_DIR}/include
sudo cp -a ${WORK_DIR}/ffmpeg_build/include/. ${QT_TARGET_DIR}/include/

echo "Copying GStreamer libraries to ${QT_TARGET_DIR}..."
sudo cp -a ${WORK_DIR}/gstreamer_build/lib/. ${QT_TARGET_DIR}/lib/
sudo cp -a ${WORK_DIR}/gstreamer_build/include/. ${QT_TARGET_DIR}/include/

# Clean up
cd /
sudo rm -rf "$WORK_DIR"
rm -f "${CROSS_PKG_CONFIG}"
rm -f "${TOOLCHAIN_FILE}"

echo "Qt ${QT_VERSION}, FFmpeg ${FFMPEG_VERSION}, and GStreamer ${GSTREAMER_VERSION} for ARM64 build completed successfully!"
echo "Qt installed to: ${QT_TARGET_DIR}"
echo "FFmpeg libraries installed to: ${QT_TARGET_DIR}/lib"
echo "FFmpeg headers installed to: ${QT_TARGET_DIR}/include"
echo "GStreamer libraries installed to: ${QT_TARGET_DIR}/lib"
echo "GStreamer headers installed to: ${QT_TARGET_DIR}/include"
