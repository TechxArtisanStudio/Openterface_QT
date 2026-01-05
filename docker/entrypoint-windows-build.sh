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
    # capture output so we can detect shallow-repo errors and retry
    VCPKG_OUTPUT="$(${VCPKG}/vcpkg install --triplet x64-mingw-static --clean-after-build 2>&1 || true)"
    VCPKG_RC=$?
    echo "${VCPKG_OUTPUT}" | sed -n '1,200p'
    # automatic recovery for shallow-clone problems: attempt to unshallow vcpkg repo and retry once
    if [[ ${VCPKG_RC} -ne 0 ]]; then
      if echo "${VCPKG_OUTPUT}" | grep -qi "shallow" || echo "${VCPKG_OUTPUT}" | grep -qi "shallow repository"; then
        echo "[container] Detected shallow vcpkg clone, attempting to fetch full history and retry"
        set +e
        git -C "${VCPKG}" fetch --unshallow --tags --prune || git -C "${VCPKG}" fetch --all --tags || true
        set -e
        echo "[container] Retry vcpkg install after unshallow"
        VCPKG_OUTPUT="$(${VCPKG}/vcpkg install --triplet x64-mingw-static --clean-after-build 2>&1 || true)"
        VCPKG_RC=$?
        echo "${VCPKG_OUTPUT}" | sed -n '1,200p'
      fi
    fi
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

# Decide on CMake prefix path for Qt — either provided by user, found at /opt/Qt6, or fail with helpful advice
if [[ -n "${QT_CMAKE_PATH:-}" ]]; then
  CMAKE_PREFIX_PATH="${QT_CMAKE_PATH}"
  echo "[container] Using Qt prefix from QT_CMAKE_PATH=${CMAKE_PREFIX_PATH}"
elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  echo "[container] Using existing CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
else
  # Common default location used in other dockerfiles in this repo
  if [[ -d "/opt/Qt6" ]]; then
    CMAKE_PREFIX_PATH="/opt/Qt6"
    echo "[container] Found Qt at /opt/Qt6 — using as CMAKE_PREFIX_PATH"
  else
    echo "\n[ERROR] Qt not found in container. CMake requires Qt (Qt5/Qt6) to build this project.\n"
    echo "Options to provide Qt for cross-compiling (pick one):"
    echo "  1) Mount a prebuilt cross-compiled Qt installation into the container at /opt/Qt6 and re-run. Example:" \
         "docker run --rm -v /path/to/your/qt-mingw-install:/opt/Qt6 -v \\$(pwd):/src openterface/windows-builder:latest"
    echo "  2) Set environment variable QT_CMAKE_PATH to a CMake Qt prefix (inside container) and re-run. Example:" \
         "docker run --rm -e QT_CMAKE_PATH=/some/qt/cmake/prefix -v \\$(pwd):/src openterface/windows-builder:latest"
    echo "  3) Use the repo's Qt builder docker images (docker/Dockerfile.qt-base-qtml or Dockerfile.qt-complete) to prepare a /opt/Qt6 installation, then re-run this container mounting /opt/Qt6."
    echo "  4) (Not recommended) Use vcpkg to build Qt inside the container (very long). See repository build-script/build-static-qt-from-source.sh for more info."
    exit 8
  fi
fi
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
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="x64-mingw-static" \
  -DOPENTERFACE_BUILD_STATIC=ON \
  -DCMAKE_C_COMPILER="/usr/bin/x86_64-w64-mingw32-gcc" \
  -DCMAKE_CXX_COMPILER="/usr/bin/x86_64-w64-mingw32-g++" \
  -DCMAKE_RC_COMPILER="/usr/bin/x86_64-w64-mingw32-windres" \
  -DCMAKE_MAKE_PROGRAM="${MAKE_PROG}" \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  ${VCPKG_CMAKE_FLAG} \
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
