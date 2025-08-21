# GStreamer Qt6 Camera Example

This directory contains a Qt6 example demonstrating live camera capture and display using GStreamer with native Qt widgets.

## Overview

This example shows how to integrate GStreamer camera input with a Qt6 QWidget application using video overlay rendering. The application captures live video from a camera device (`/dev/video0`) and displays it directly in a Qt widget window.

**Features:**
- Live camera streaming (1280x720 @ 30fps, JPEG format)
- Pure Qt6 QWidget interface with native controls
- Menu system with File and Help menus
- Start/Stop camera controls
- Status bar with real-time information
- Auto-stop demo timer (30 seconds)
- Camera availability checking

## Prerequisites

### System Requirements
- Qt6 development libraries
- GStreamer 1.0 development libraries
- V4L2-compatible camera device
- CMake build system

### Ubuntu/Debian Installation
```bash
# Install Qt6
sudo apt-get update
sudo apt-get install qt6-base-dev

# Install GStreamer
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
sudo apt-get install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
sudo apt-get install gstreamer1.0-tools

# Install build tools
sudo apt-get install cmake build-essential
```

### Raspberry Pi Additional Setup
```bash
# Enable camera (if using Raspberry Pi camera)
sudo raspi-config
# Navigate to: Interface Options -> Camera -> Enable

# Add user to video group for camera access
sudo usermod -a -G video $USER
# Log out and back in for group changes to take effect
```

## Building the Example

### Using CMake

```bash
# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# The executable will be created as: camera-example
```

## Running the Example

```bash
./build/camera-example
```

**What to expect:**
- A window opens with a black video area and control buttons
- Click "Start Camera" to begin live camera streaming
- Video should appear in the widget area within a few seconds
- Click "Stop Camera" to pause the stream
- The demo will automatically stop after 30 seconds

## Technical Details

### Application Architecture
- **Framework**: Qt6 QApplication with QMainWindow
- **Video Rendering**: GStreamer video overlay using `gst_video_overlay_set_window_handle()`
- **Camera Input**: V4L2 source (`v4l2src`) with JPEG decoding
- **Pipeline**: `v4l2src device=/dev/video0 ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegdec ! videoconvert ! xvimagesink`

### Key Components
- **VideoWidget**: Custom QWidget with native window attributes for video overlay
- **VideoWindow**: Main application window with menu, controls, and status bar
- **GStreamer Pipeline**: Handles camera capture, decoding, and video display
- **Error Handling**: Checks camera availability and pipeline creation

### Camera Settings
- **Device**: `/dev/video0` (configurable in source code)
- **Resolution**: 1280x720 pixels
- **Frame Rate**: 30 FPS
- **Format**: JPEG (automatically decoded)
- **Sink**: XV image sink with video overlay support

## Troubleshooting

### Common Issues

**Error: "No camera found at /dev/video0"**
```bash
# List available video devices
ls -la /dev/video*

# Test camera with GStreamer directly
gst-launch-1.0 v4l2src device=/dev/video0 ! autovideosink

# Check camera permissions
groups $USER  # Should include 'video' group
```

**Error: "Failed to create GStreamer pipeline"**
```bash
# Test basic GStreamer functionality
gst-launch-1.0 videotestsrc ! autovideosink

# Test V4L2 source specifically
gst-launch-1.0 v4l2src ! jpegdec ! autovideosink
```

**Error: "Failed to start video playback"**
- Ensure the camera is not being used by another application
- Check if the camera supports JPEG format:
  ```bash
  v4l2-ctl --device=/dev/video0 --list-formats
  ```
- Try a different resolution or format in the source code

**Video appears corrupted or distorted**
- Camera may not support the requested resolution (1280x720)
- Try changing the pipeline to use different formats:
  ```cpp
  // In widgets_main.cpp, replace the pipeline with:
  "v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480 ! videoconvert ! xvimagesink name=videosink"
  ```

### Debug Information

Enable GStreamer debug output:
```bash
export GST_DEBUG=2
./camera-example
```

For detailed pipeline information:
```bash
export GST_DEBUG=3
export GST_DEBUG_FILE=/tmp/gst-debug.log
./camera-example
# Check /tmp/gst-debug.log for detailed information
```

### Performance Tips

- **For Raspberry Pi**: Enable GPU memory split (≥128MB recommended)
  ```bash
  sudo raspi-config
  # Advanced Options -> Memory Split -> 128 or 256
  ```
- **For low-end hardware**: Reduce resolution in the pipeline
- **For better performance**: Use hardware acceleration if available (`vaapi`, `nvenc`)

### Alternative Camera Devices

If `/dev/video0` doesn't work, try:
```bash
# Find available cameras
ls /dev/video*

# Test different devices
gst-launch-1.0 v4l2src device=/dev/video1 ! autovideosink
```

Then modify the device path in `widgets_main.cpp`:
```cpp
// Change this line:
"v4l2src device=/dev/video0 ! ..."
// To:
"v4l2src device=/dev/video1 ! ..."
```

## File Structure

```
qmlsink/
├── README.md              # This documentation
├── CMakeLists.txt         # CMake build configuration
├── meson.build           # Meson build configuration
└── widgets_main.cpp      # Qt6 camera application source code
```

## Source Code Overview

The main application (`widgets_main.cpp`) contains:

- **VideoWidget class**: Custom QWidget configured for video overlay
  - Sets native window attributes (`WA_NativeWindow`, `WA_PaintOnScreen`)
  - Provides window ID for GStreamer video overlay
  - Shows instructional text when camera is not active

- **VideoWindow class**: Main application window (QMainWindow)
  - Menu bar with File and Help menus
  - Camera control buttons (Start/Stop)
  - Status bar for real-time information
  - GStreamer pipeline management
  - Camera availability checking

- **GStreamer Integration**:
  - V4L2 camera source with JPEG decoding
  - Video overlay rendering using `gst_video_overlay_set_window_handle()`
  - Pipeline state management (NULL/PLAYING/PAUSED)
  - Error handling and user feedback

## Development Notes

- **Qt6 API**: Uses modern Qt6 APIs and requires Qt 6.0 or later
- **GStreamer 1.0**: Compatible with GStreamer 1.0 API
- **Video Overlay**: Requires a windowing system (X11/Wayland) for video overlay
- **Camera Support**: Expects V4L2-compatible devices (Linux standard)
- **Cross-platform**: Should work on any Linux system with Qt6 and GStreamer

## Customization

### Changing Camera Settings

Edit the pipeline in `setupGStreamer()` function:
```cpp
// Current settings: 1280x720 @ 30fps, JPEG
pipeline = gst_parse_launch(
    "v4l2src device=/dev/video0 ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegdec ! videoconvert ! xvimagesink name=videosink", NULL);

// Example: Lower resolution for better performance
pipeline = gst_parse_launch(
    "v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480,framerate=15/1 ! videoconvert ! xvimagesink name=videosink", NULL);
```

### Removing Auto-Stop Timer

In `startVideo()` function, comment out or remove:
```cpp
// Remove this line to disable auto-stop:
// QTimer::singleShot(30000, this, &VideoWindow::stopVideo);
```

### Adding Recording Feature

You can extend the pipeline to save video:
```cpp
// Example: Add recording capability
"v4l2src device=/dev/video0 ! tee name=t ! queue ! jpegdec ! videoconvert ! xvimagesink name=videosink t. ! queue ! x264enc ! mp4mux ! filesink location=recording.mp4"
```

## License

This example is part of the GStreamer project and follows the same licensing terms as GStreamer (LGPL).
