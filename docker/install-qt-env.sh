#!/bin/bash
set -e  # Exit on error
set -x  # Show commands

# Configuration
QT_VERSION="6.5.3"
QT_MAJOR_VERSION="6.5"
INSTALL_PREFIX="/opt/Qt6"
BUILD_DIR="$PWD/qt-build"
MODULES=("qtbase" "qtshadertools" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/${QT_MAJOR_VERSION}/${QT_VERSION}/submodules"

# Update and install dependencies with handling for held packages
sudo apt update -y
sudo apt install -y --allow-change-held-packages \
    build-essential \
    libdbus-1-dev \
    libgl1-mesa-dev \
    libasound2-dev \
    ninja-build \
    cmake \
    wget \
    unzip

# Clean up unused packages (optional)
sudo apt autoremove -y

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Download and extract modules
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        wget "${DOWNLOAD_BASE_URL}/${module}-everywhere-src-${QT_VERSION}.zip"
        unzip "${module}-everywhere-src-${QT_VERSION}.zip"
        mv "${module}-everywhere-src-${QT_VERSION}" "${module}"
        rm "${module}-everywhere-src-${QT_VERSION}.zip"
    fi
done

# Build qtbase first
cd "$BUILD_DIR/qtbase"
mkdir -p build && cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    ..
ninja
sudo ninja install

# Build qtshadertools
cd "$BUILD_DIR/qtshadertools"
mkdir -p build && cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    ..
ninja
sudo ninja install

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir -p build && cd build
        cmake -GNinja \
            -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
            -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
            ..
        ninja
        sudo ninja install
    fi
done

# Verify installation
echo "Qt installation complete. Installation details:"
"$INSTALL_PREFIX/bin/qmake6" -query

echo "Listing all installed Qt modules:"
"$INSTALL_PREFIX/bin/qmake6" -query QT_INSTALL_LIBS

echo "To use the installed Qt tools, please add the following path to your system's PATH environment variable:"
echo "\$INSTALL_PREFIX/bin/"
echo "You can do this by following these steps:"
echo "1. Open your terminal."
echo "2. Depending on your shell, you will need to edit the appropriate configuration file:"
echo "   - For bash, edit ~/.bashrc or ~/.bash_profile"
echo "   - For zsh, edit ~/.zshrc"
echo "   - For fish, edit ~/.config/fish/config.fish"
echo "3. Add the following line to the end of the file:"
echo "   export PATH=\$INSTALL_PREFIX/bin:\$PATH"
echo "4. Save the file and exit the editor."
echo "5. To apply the changes, run the following command:"
echo "   source ~/.bashrc  # or source ~/.zshrc or source ~/.config/fish/config.fish"
echo "6. Verify that the path has been added by running:"
echo "   echo \$PATH"
