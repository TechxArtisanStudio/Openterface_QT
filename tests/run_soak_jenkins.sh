#!/bin/bash
# =============================================================================
# Jenkins Soak Test Runner for NanoPi Agent
# =============================================================================
# Invoked by Jenkins job "Openterface_Soak_Test_arm64"
# Runs gui_soak_test.sh under Xvfb, archives the report
#
# Env vars (set in Jenkins job):
#   SOAK_DURATION_MIN  - Test duration in minutes (default 30)
#   SOAK_CHECK_INTERVAL - Check interval in seconds (default 30)
#   SOAK_BACKEND       - Media backend: ffmpeg, gstreamer, or empty for default
# =============================================================================

set -euo pipefail

PROJECT_DIR="${WORKSPACE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SOAK_SCRIPT="${PROJECT_DIR}/tests/gui_soak_test.sh"
LOG_DIR="${PROJECT_DIR}/tests/soak_test_logs"
BUILD_DIR="${PROJECT_DIR}/build"

DURATION="${SOAK_DURATION_MIN:-30}"
INTERVAL="${SOAK_CHECK_INTERVAL:-30}"
BACKEND="${SOAK_BACKEND:-}"

# The soak test job's own workspace doesn't include build artifacts.
# The binary lives in the build job's workspace on the same agent.
BUILD_JOB_WORKSPACE="/home/pi/jenkins/workspace/Openterface_Shared_arm64/build"

echo "=== OpenterfaceQT Soak Test Runner ==="
echo "Project:   ${PROJECT_DIR}"
echo "Duration:  ${DURATION} min"
echo "Interval:  ${INTERVAL} s"
echo "Backend:   ${BACKEND:-default}"
echo

# ---------------------------------------------------------------------------
# Pre-flight: ensure binary exists
# ---------------------------------------------------------------------------
if [ ! -f "${BUILD_DIR}/openterfaceQT" ]; then
    if [ -f "${BUILD_JOB_WORKSPACE}/openterfaceQT" ]; then
        echo "Binary not in this workspace — symlinking from build job workspace:"
        echo "  ${BUILD_JOB_WORKSPACE}/openterfaceQT"
        mkdir -p "${BUILD_DIR}"
        # Symlink the whole build output so runtime libs (Qt, FFmpeg) resolve too
        for f in "${BUILD_JOB_WORKSPACE}"/*; do
            [ -e "${BUILD_DIR}/$(basename "$f")" ] && continue
            ln -sf "$f" "${BUILD_DIR}/$(basename "$f")"
        done
        # Copy launcher script and translations (small, not symlinked)
        echo "  Symlinked $(ls "${BUILD_DIR}" | wc -l) entries into ${BUILD_DIR}"
    else
        echo "Binary not found in this workspace OR build job workspace."
        echo "  Expected: ${BUILD_DIR}/openterfaceQT"
        echo "      Or:   ${BUILD_JOB_WORKSPACE}/openterfaceQT"
        echo "Trigger Openterface_Shared_arm64 first."
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Install Xvfb + imagemagick if missing (one-time, cached across builds)
# ---------------------------------------------------------------------------
if ! command -v Xvfb &>/dev/null; then
    echo "Installing Xvfb + imagemagick..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get install -y xvfb imagemagick
    else
        echo "apt-get not found — cannot install Xvfb. Exiting."
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Clean up any leftover Xvfb / app instances from prior failed runs
# ---------------------------------------------------------------------------
pkill -9 -f "Xvfb :99" 2>/dev/null || true
pkill -9 -f "openterfaceQT" 2>/dev/null || true
sleep 1

# ---------------------------------------------------------------------------
# Run the soak test
# ---------------------------------------------------------------------------
mkdir -p "${LOG_DIR}"

# ---------------------------------------------------------------------------
# Set up Qt/library paths from the AppDir bundled by the build job.
# The shared build produces a dynamically-linked binary that needs Qt/FFmpeg
# libs at runtime; on the NanoPi agent these aren't installed system-wide —
# they live in the AppImage staging area.
# ponytail: relies on the build job having populated appimage/AppDir. If the
# build layout changes, this block needs updating (or switch to running the
# AppImage directly via --appimage-extract).
# ---------------------------------------------------------------------------
APPDIR="${BUILD_DIR}/appimage/AppDir"
if [ -d "${APPDIR}/usr/lib" ]; then
    echo "Setting LD_LIBRARY_PATH from AppDir: ${APPDIR}/usr/lib"
    export LD_LIBRARY_PATH="${APPDIR}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
    export QML2_IMPORT_PATH="${APPDIR}/usr/qml"
    # The AppDir binary (openterfaceQT.bin) is stripped & smaller; prefer it
    # because the raw build binary (100MB+) has debug symbols and the same
    # runtime deps. If the AppDir binary isn't there, fall back to raw.
    if [ -x "${APPDIR}/usr/bin/openterfaceQT.bin" ]; then
        echo "Using AppDir binary: ${APPDIR}/usr/bin/openterfaceQT.bin"
        cp "${APPDIR}/usr/bin/openterfaceQT.bin" "${BUILD_DIR}/openterfaceQT"
    fi
else
    echo "WARNING: AppDir not found at ${APPDIR} — relying on system Qt libs"
fi

echo "Starting soak test..."
ARGS=("--native" "${DURATION}" "${INTERVAL}")
if [ -n "${BACKEND}" ]; then
    ARGS=("--backend" "${BACKEND}" "${DURATION}" "${INTERVAL}")
fi

# Run in foreground; the script itself handles cleanup via EXIT trap
bash "${SOAK_SCRIPT}" "${ARGS[@]}"
RC=$?

# ---------------------------------------------------------------------------
# Report result summary for Jenkins console
# ---------------------------------------------------------------------------
REPORT=$(ls -t "${LOG_DIR}"/soak_test_report_*.md 2>/dev/null | head -1)
if [ -n "${REPORT}" ]; then
    echo
    echo "=== Soak Test Report: ${REPORT} ==="
    # Extract key lines for Jenkins console summary
    grep -E 'Status:|Total Checks:|Crashes:|Max Memory:|Average Memory:|Focus Loops:|FD Leaks:|Thread Leaks:' "${REPORT}" || true
    echo "=== End Report ==="
fi

if [ ${RC} -ne 0 ]; then
    echo "Soak test script exited with code ${RC}"
    exit ${RC}
fi

# Jenkins: mark build UNSTABLE if the report contains FAIL
if [ -n "${REPORT}" ] && grep -q "Status:.*FAIL" "${REPORT}"; then
    echo "Soak test reported FAIL — marking build UNSTABLE"
    exit 1
fi

echo "Soak test completed successfully."
exit 0
