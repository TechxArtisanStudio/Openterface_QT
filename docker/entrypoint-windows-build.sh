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
VCPKG_INSTALL_OK=1
if [[ -x "${VCPKG}/vcpkg" ]]; then
  # only attempt to install minimal deps declared in vcpkg.json if it exists
  if [[ -f "${SRC_DIR}/vcpkg.json" ]]; then
    echo "[container] Found vcpkg.json, installing packages (triplet x64-mingw-static)"
    set +e
    ${VCPKG}/vcpkg install --triplet x64-mingw-static --clean-after-build 2>&1 | sed -n '1,200p'
    VCPKG_RC=$?
    set -e
    if [[ ${VCPKG_RC} -ne 0 ]]; then
      echo "[container] vcpkg install returned ${VCPKG_RC} — continuing but CMake will skip auto-install"
      VCPKG_INSTALL_OK=0
    else
      echo "[container] vcpkg install succeeded"
      VCPKG_INSTALL_OK=1
    fi
  else
    echo "[container] No vcpkg.json in repo, skipping automatic vcpkg installs"
  fi
else
  echo "[container] vcpkg binary not present, skipping vcpkg installation"
fi

echo "[container] Configuring project with CMake for cross compile to Windows (x64)"
# Detect a local build program (prefer ninja)
GENERATOR="Ninja"
MAKE_PROG=""
if command -v ninja >/dev/null 2>&1; then
  MAKE_PROG=$(command -v ninja)
  echo "[container] Using generator: Ninja (program: ${MAKE_PROG})"
elif command -v make >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
  MAKE_PROG=$(command -v make)
  echo "[container] Ninja not found - falling back to Make (program: ${MAKE_PROG})"
else
  echo "ERROR: neither ninja nor make are available in container PATH"
  exit 7
fi

# If vcpkg manifest install previously failed, tell CMake not to attempt autoinstall so configure can proceed
VCPKG_CMAKE_FLAG="-DVCPKG_MANIFEST_INSTALL=ON"
if [[ ${VCPKG_INSTALL_OK} -eq 0 ]]; then
  VCPKG_CMAKE_FLAG="-DVCPKG_MANIFEST_INSTALL=OFF"
fi

cmake -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=${VCPKG}/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
  -DOPENTERFACE_BUILD_STATIC=ON \
  -DCMAKE_C_COMPILER=/usr/bin/x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/x86_64-w64-mingw32-g++ \
  -DCMAKE_RC_COMPILER=/usr/bin/x86_64-w64-mingw32-windres \
  "${SRC_DIR}"

echo "[container] Building (parallel)"
if [[ "${GENERATOR}" == "Ninja" ]]; then
  ${MAKE_PROG} -j$(nproc || echo 2)
else
  # unix makefiles uses 'make'
  ${MAKE_PROG} -j$(nproc || echo 2)
fi

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
