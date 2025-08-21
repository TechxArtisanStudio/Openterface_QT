#!/bin/bash

# Run script for Openterface QT application
# Built with static Qt and FFmpeg libraries

# Set Qt plugin path to the static build location
export QT_PLUGIN_PATH="/opt/Qt6-arm64/plugins"

# Change to build directory
cd "$(dirname "$0")/build"

# Run the application
./openterfaceQT "$@"
