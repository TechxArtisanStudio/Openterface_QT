#!/bin/bash

# 设置环境变量
export ARTIFACT="openterfaceQT.linux.amd64.portable"
export SOURCE_DIR=$(dirname "$(pwd)")  # 设置为父目录（项目根目录）
export QT_VERSION="6.6.3"
export BUILD_DIR="$(pwd)/temp/build"  # 改为 build-script/temp/build

# 检查 SOURCE_DIR 是否正确
if [ ! -d "$SOURCE_DIR" ]; then
  echo "Error: SOURCE_DIR ($SOURCE_DIR) does not exist"
  exit 1
fi

# 检查必要的脚本是否存在
if [ ! -f "$SOURCE_DIR/build-script/build-static-ffmpeg.sh" ] || [ ! -f "$SOURCE_DIR/build-script/build-static-qt-from-source.sh" ]; then
  echo "Error: Required build scripts are missing"
  exit 1
fi

# 检查 driver 目录是否存在
if [ ! -d "$SOURCE_DIR/driver" ]; then
  echo "Error: $SOURCE_DIR/driver directory does not exist. Please specify the correct driver path."
  exit 1
fi

# 清理旧的构建目录
echo "Cleaning previous build directory..."
rm -rf "$BUILD_DIR"/*

# 1. 创建构建目录
mkdir -p "$BUILD_DIR"

# 2. 安装 OpenGL ES 和相关开发包（包括 Qt6 开发包）
sudo apt-get update
sudo apt-get install -y \
  libgles2-mesa-dev \
  libegl1-mesa-dev \
  libglib2.0-dev \
  libpcre2-dev \
  libxrandr2 \
  libxcb1-dev \
  '^libxcb.*-dev' \
  libx11-xcb-dev \
  libglu1-mesa-dev \
  libxrender-dev \
  libxi-dev \
  libxkbcommon-dev \
  libxkbcommon-x11-dev \
  libexpat-dev \
  libfreetype6-dev \
  libfontconfig1-dev \
  libx11-dev \
  libdbus-1-dev \
  libdbus-glib-1-dev \
  libpulse-dev \
  libsndfile1-dev \
  libxrandr-dev \
  libxrender-dev \
  libexpat1-dev \
  libdrm-dev \
  libgbm-dev \
  libatspi2.0-dev \
  upx-ucl \
  qt6-base-dev

# 3. 检查 FFmpeg 是否已构建并安装
if [ ! -f "/usr/local/lib/libavformat.a" ]; then
  echo "Building FFmpeg..."
  bash "$SOURCE_DIR/build-script/build-static-ffmpeg.sh"
  if [ ! -f "/usr/local/lib/libavformat.a" ]; then
    echo "Error: FFmpeg static libraries not found in /usr/local/lib/"
    echo "Please check build-static-ffmpeg.sh output or specify the correct path."
    exit 1
  fi
else
  echo "FFmpeg static libraries found in /usr/local/lib/"
fi

# 4. 分析原始二进制大小（如果存在）
if [ -f "$BUILD_DIR/openterfaceQT" ]; then
  ls -lh "$BUILD_DIR/openterfaceQT"
fi

# 5. 构建便携式可执行文件
cd "$BUILD_DIR" || exit 1

echo "Setting LD_LIBRARY_PATH..."
export LD_LIBRARY_PATH=/usr/lib:/usr/local/lib:$LD_LIBRARY_PATH

echo "Configuring with CMake..."
cmake -S "$SOURCE_DIR" -B . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6 \
  -DCMAKE_LIBRARY_PATH=/usr/local/lib \
  -DCMAKE_INSTALL_PREFIX=release \
  -DCMAKE_POLICY_DEFAULT_CMP0177=NEW \
  -DCMAKE_POLICY_DEFAULT_CMP0174=NEW \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DQT_DEBUG_TRY_PACKAGE=ON \
  -DCMAKE_CXX_FLAGS="-Os -ffunction-sections -fdata-sections" \
  -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -Wl,--strip-all" \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON

if [ $? -ne 0 ]; then
  echo "Error: CMake configuration failed"
  exit 1
fi

echo "Building with CMake..."
cmake --build . --verbose || (echo "CMake build failed. Please check the build configuration." && exit 1)

if [ ! -f "openterfaceQT" ]; then
  echo "Error: Failed to build openterfaceQT"
  exit 1
fi

echo "Creating portable package..."
mkdir -p package

echo "Analyzing binary size before compression..."
ls -lh openterfaceQT

echo "Compressing binary with UPX..."
upx --best --lzma openterfaceQT

echo "Analyzing binary size after compression..."
ls -lh openterfaceQT

cp openterfaceQT package/

# 6. 保存构建产物
echo "Build artifact saved at $BUILD_DIR/package/openterfaceQT"