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

DURATION="${SOAK_DURATION_MIN:-30}"
INTERVAL="${SOAK_CHECK_INTERVAL:-30}"
BACKEND="${SOAK_BACKEND:-}"

echo "=== OpenterfaceQT Soak Test Runner ==="
echo "Project:   ${PROJECT_DIR}"
echo "Duration:  ${DURATION} min"
echo "Interval:  ${INTERVAL} s"
echo "Backend:   ${BACKEND:-default}"
echo

# ---------------------------------------------------------------------------
# Pre-flight: ensure binary exists (skip if Jenkins already built it upstream)
# ---------------------------------------------------------------------------
if [ ! -f "${PROJECT_DIR}/build/openterfaceQT" ]; then
    echo "Binary not found — build is required before soak test."
    echo "Trigger Openterface_Shared_arm64 first, or run cmake/make here."
    exit 1
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
