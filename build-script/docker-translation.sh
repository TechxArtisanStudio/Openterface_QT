#!/bin/bash
set -e

echo "Processing translations before CMake build..."
cd /workspace/src

# Update translation files with lupdate
echo "Running lupdate to extract translatable strings..."
if [ -f "/opt/Qt6/bin/lupdate" ]; then
  /opt/Qt6/bin/lupdate openterfaceQT.pro -no-obsolete || echo "lupdate failed, continuing..."
else
  echo "lupdate not found, skipping translation update"
fi

# Compile translation files with lrelease
echo "Running lrelease to compile .qm files..."
if [ -f "/opt/Qt6/bin/lrelease" ]; then
  /opt/Qt6/bin/lrelease openterfaceQT.pro || echo "lrelease failed, continuing..."
else
  echo "lrelease not found, skipping translation compilation"
fi

# Verify .qm files were created
echo "Checking for compiled translation files:"
ls -la config/languages/*.qm 2>/dev/null || echo "No .qm files found"

echo "Translation processing completed"
