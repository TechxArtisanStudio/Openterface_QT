#!/bin/bash

# Firmware Flashing Test Runner
# This script builds and runs the firmware flashing test suite

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="test_firmware_flashing"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Firmware Flashing Test Suite${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Function to print step
print_step() {
    echo -e "${YELLOW}Step $1:${NC} $2"
}

# Function to print success
print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

# Function to print error
print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Check if Qt6 is available
print_step 1 "Checking Qt6 installation..."
if ! command -v qmake6 &> /dev/null; then
    if ! command -v qmake &> /dev/null; then
        print_error "Qt6 not found. Please install Qt6 with SerialPort module."
        echo ""
        echo "Installation instructions:"
        echo "  Ubuntu/Debian: sudo apt install qt6-base-dev qt6-serialport-dev"
        echo "  Fedora:        sudo dnf install qt6-qtbase-devel qt6-qtserialport-devel"
        echo "  Arch:          sudo pacman -S qt6-base qt6-serialport"
        exit 1
    fi
fi
print_success "Qt6 found"

# Check CMake
print_step 2 "Checking CMake installation..."
if ! command -v cmake &> /dev/null; then
    print_error "CMake not found. Please install CMake 3.16 or later."
    exit 1
fi

CMAKE_VERSION=$(cmake --version | grep -oP '\d+\.\d+' | head -1)
CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 16 ]); then
    print_error "CMake version $CMAKE_VERSION found, but 3.16 or later is required."
    exit 1
fi
print_success "CMake $CMAKE_VERSION found"

# Create build directory
print_step 3 "Creating build directory..."
mkdir -p "${BUILD_DIR}"
print_success "Build directory ready: ${BUILD_DIR}"

# Run CMake
print_step 4 "Configuring build with CMake..."
cd "${BUILD_DIR}"
if ! cmake .. > cmake_output.log 2>&1; then
    print_error "CMake configuration failed. Check ${BUILD_DIR}/cmake_output.log"
    tail -20 cmake_output.log
    exit 1
fi
print_success "CMake configuration successful"

# Build the test
print_step 5 "Building test suite..."
if ! make ${TEST_NAME} > build_output.log 2>&1; then
    print_error "Build failed. Check ${BUILD_DIR}/build_output.log"
    tail -20 build_output.log
    exit 1
fi
print_success "Build successful"

# Check if test executable exists
if [ ! -f "${TEST_NAME}" ]; then
    print_error "Test executable not found after build"
    exit 1
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Ask user what to do next
echo "What would you like to do?"
echo "  1) Run all tests"
echo "  2) Run specific test"
echo "  3) Run with verbose output"
echo "  4) Run with valgrind (memory check)"
echo "  5) Exit"
echo ""

read -p "Enter choice [1-5]: " choice

case $choice in
    1)
        echo ""
        echo -e "${YELLOW}Running all tests...${NC}"
        echo ""
        ./${TEST_NAME}
        ;;
    2)
        echo ""
        echo "Available tests:"
        echo "  testCompleteFlashWorkflow"
        echo "  testUnstableUSBDuringFlash"
        echo "  testDelayedReenumeration"
        echo "  testGetInfoCommandStructure"
        echo "  testChecksumValidation"
        echo "  testMultipleFlashCycles"
        echo "  testPortChainTracking"
        echo "  testMultipleDevicesDuringFlash"
        echo "  testVIDPIDTransition"
        echo "  testFlashWithSerialCommunication"
        echo ""
        read -p "Enter test name: " test_name
        if [ -n "$test_name" ]; then
            echo ""
            echo -e "${YELLOW}Running test: ${test_name}${NC}"
            echo ""
            ./${TEST_NAME} ${test_name}
        else
            print_error "No test name provided"
            exit 1
        fi
        ;;
    3)
        echo ""
        echo -e "${YELLOW}Running tests with verbose output...${NC}"
        echo ""
        ./${TEST_NAME} -v2
        ;;
    4)
        if ! command -v valgrind &> /dev/null; then
            print_error "valgrind not found. Please install valgrind."
            exit 1
        fi
        echo ""
        echo -e "${YELLOW}Running tests with valgrind...${NC}"
        echo ""
        valgrind --leak-check=full --show-leak-kinds=all ./${TEST_NAME}
        ;;
    5)
        echo ""
        echo "Exiting. To run tests later:"
        echo "  cd ${BUILD_DIR}"
        echo "  ./${TEST_NAME}"
        ;;
    *)
        print_error "Invalid choice"
        exit 1
        ;;
esac

echo ""
print_success "Done!"
