#!/usr/bin/env bash
set -euo pipefail

# Simple entrypoint for the windows-builder container
# Usage (from image):
#  - `docker run --rm -v $(pwd):/src <image>`  -> runs default build steps
#  - `docker run --rm -v $(pwd):/src <image> /bin/bash -lc "<your commands>"` -> override

echo "[container] Starting Windows cross-build helper"

# location of the project inside the container
SRC_DIR=/src
BUILD_DIR=${BUILD_DIR:-/src/build}
VCPKG=${VCPKG_ROOT:-/opt/vcpkg}

echo "[container] Source: ${SRC_DIR}"
echo "[container] Build: ${BUILD_DIR}"
echo "[container] vcpkg: ${VCPKG}"

if [[ ! -d ${SRC_DIR} ]]; then
  echo "ERROR: source directory ${SRC_DIR} not found or not mounted"
  exit 2
fi

cd ${SRC_DIR}

# ensure cross compiler present
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "ERROR: x86_64-w64-mingw32-g++ not found in PATH"
  exit 3
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[container] Running vcpkg install for x64-mingw-static (this is optional, may take a while)"
if [[ -x "${VCPKG}/vcpkg" ]]; then
  # only attempt to install minimal deps declared in vcpkg.json if it exists
  if [[ -f "${SRC_DIR}/vcpkg.json" ]]; then
    echo "[container] Found vcpkg.json, installing packages (triplet x64-mingw-static)"
    ${VCPKG}/vcpkg install --triplet x64-mingw-static --clean-after-build || echo "vcpkg install failed but continuing"
  else
    echo "[container] No vcpkg.json in repo, skipping automatic vcpkg installs"
  fi
else
  echo "[container] vcpkg binary not present, skipping vcpkg installation"
fi

echo "[container] Configuring project with CMake for cross compile to Windows (x64)"
cmake -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG}/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
  -DOPENTERFACE_BUILD_STATIC=ON \
  -DCMAKE_C_COMPILER=/usr/bin/x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=/usr/bin/x86_64-w64-mingw32-windres \
  "${SRC_DIR}"

echo "[container] Building (parallel)"
ninja -j$(nproc || echo 2)

echo "[container] Searching for built Windows executables"
EXE=$(find . -maxdepth 4 -type f -iname "openterfaceQT.exe" -print -quit || true)

if [[ -z "$EXE" ]]; then
  echo "ERROR: built executable not found"
  echo "Listing contents of build tree for debugging:"
  find . -maxdepth 3 -print | sed -n '1,200p'
  exit 5
fi

echo "[container] Found exe: ${EXE}"

mkdir -p package
cp -v "${EXE}" package/openterfaceQT-portable.exe

echo "[container] Created package/openterfaceQT-portable.exe"

echo "[container] Build finished — output in ${BUILD_DIR}/package"

exec "$@" || true
