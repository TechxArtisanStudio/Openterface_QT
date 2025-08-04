#!/bin/bash
set -e

echo "🔧 Installing dependencies for Kali Linux..."
sudo apt-get update -y --allow-releaseinfo-change --allow-unauthenticated || true

echo "📦 Installing build dependencies..."
sudo apt-get install -y --allow-unauthenticated \
    build-essential \
    qmake6 \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev \
    libusb-1.0-0-dev \
    qt6-tools-dev \
    libudev-dev

echo "👥 Setting up user permissions..."
sudo usermod -a -G dialout $USER
sudo usermod -a -G uucp $USER

echo "🔐 Setting up device permissions..."
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/51-openterface.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "📥 Cloning repository..."
if [ ! -d "Openterface_QT" ]; then
    git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
fi
cd Openterface_QT

echo "🌐 Generating language files..."
if [ -x "/usr/lib/qt6/bin/lrelease" ]; then
    /usr/lib/qt6/bin/lrelease openterfaceQT.pro
    echo "✅ Language files generated successfully"
else
    echo "⚠️  lrelease not found at ./lib/qt6/bin/lrelease, skipping..."
fi

echo "🏗️ Building project..."
mkdir -p build
cd build
qmake6 ..
make -j$(( $(nproc) - 1 ))

echo "✅ Build complete!"
echo "Run './openterfaceQT' to start the application."
echo "💡 If mouse/keyboard don't work, try: sudo apt remove brltty"
echo "⚠️  You may need to reboot for permission changes to take effect."