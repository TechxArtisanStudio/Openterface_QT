# FFmpeg Capture Manager Extraction

## Overview
Extracted capture lifecycle management functionality from `FFmpegBackendHandler` into a dedicated `FFmpegCaptureManager` class.

## Date
2024

## Objective
Separate capture thread management, resource lifecycle, and frame reading logic from the main backend handler to improve modularity and maintainability.

## Changes Made

### New Files Created

#### 1. `host/backend/ffmpeg/ffmpeg_capture_manager.h` (147 lines)
- **Purpose**: Manages capture thread lifecycle and frame reading
- **Key Features**:
  - Capture start/stop with device path, resolution, and framerate
  - Thread management with safe cleanup
  - Frame reading with comprehensive error handling
  - Interrupt callback for FFmpeg timeout protection
  - Capability auto-detection from GlobalSetting

#### 2. `host/backend/ffmpeg/ffmpeg_capture_manager.cpp` (431 lines)
- **Implementation Details**:
  - `StartCapture()`: Auto-detects capabilities, opens device, creates capture thread
  - `StopCapture()`: Safely terminates thread with async cleanup for self-thread calls
  - `ReadFrame()`: Reads frames with device disconnection detection
  - `InterruptCallback()`: Static callback for FFmpeg operation timeouts
  - `GetMaxCameraCapability()`: Loads settings from GlobalSetting

### Modified Files

#### 3. `host/backend/ffmpeg/capturethread.h`
- **Changes**:
  - Added `ICaptureFrameReader` interface for polymorphic readFrame() calls
  - Added constructor overload for `FFmpegCaptureManager`
  - Changed `m_handler` from `QPointer<FFmpegBackendHandler>` to `QPointer<QObject>`
  - Added `m_frameReader` pointer for interface-based frame reading

#### 4. `host/backend/ffmpeg/capturethread.cpp`
- **Changes**:
  - Implemented both constructors (FFmpegBackendHandler and FFmpegCaptureManager)
  - Updated `run()` method to call `m_frameReader->readFrame()` instead of `m_handler->readFrame()`
  - Maintains full error handling and performance monitoring

#### 5. `host/backend/ffmpegbackendhandler.h`
- **Changes**:
  - Added forward declaration for `FFmpegCaptureManager`
  - Added `std::unique_ptr<FFmpegCaptureManager> m_captureManager` member
  - Removed `m_captureThread` (now managed by FFmpegCaptureManager)

#### 6. `host/backend/ffmpegbackendhandler.cpp`
- **Changes**:
  - Added `#include "ffmpeg/ffmpeg_capture_manager.h"`
  - Updated constructor to initialize `m_captureManager` with dependencies
  - Connected `FFmpegCaptureManager` signals to handler slots:
    - `FrameAvailable` → `processFrame()`
    - `DeviceDisconnected` → `handleDeviceDeactivation()`
    - `CaptureError` → `captureError()`
  - Delegated `startDirectCapture()` to `m_captureManager->StartCapture()`
  - Delegated `stopDirectCapture()` to `m_captureManager->StopCapture()`
  - Delegated `readFrame()` to `m_captureManager->ReadFrame()`

#### 7. `cmake/SourceFiles.cmake`
- **Changes**:
  - Added `host/backend/ffmpeg/ffmpeg_capture_manager.cpp` and `.h` to build

## Architecture

### Dependencies
```
FFmpegCaptureManager
├── FFmpegDeviceManager (opens device, manages AVFormatContext/AVCodecContext)
├── FFmpegHardwareAccelerator (initializes hardware decoding)
├── FFmpegDeviceValidator (checks capabilities, loads settings)
└── CaptureThread (background frame reading loop)
```

### Signal Flow
```
CaptureThread::run()
    └── calls m_frameReader->readFrame() (polymorphic)
        └── FFmpegCaptureManager::ReadFrame()
            └── reads AVPacket from device
            └── emits FrameAvailable signal
                └── FFmpegBackendHandler::processFrame()
                    └── decodes frame and emits frameReady(QPixmap)
```

## Implementation Details

### Thread Safety
- Mutex protection for capture state changes
- Mutex released before `thread->wait()` to avoid deadlocks
- Self-thread detection in `StopCapture()` to prevent blocking
- Async cleanup using `QTimer::singleShot()` when stopping from capture thread

### Error Handling
- Device disconnection detection through:
  - AVERROR_EOF, AVERROR(EIO), AVERROR(ENODEV), AVERROR(ENXIO)
  - Error message analysis (searches for "no such device", "vidioc", etc.)
- Consecutive failure tracking in CaptureThread
- Emits `DeviceDisconnected` signal for hotplug handling

### Capability Auto-Detection
- Loads resolution and framerate from GlobalSetting via `FFmpegDeviceValidator`
- Falls back to defaults (1920x1080 @ 30fps) if not found
- Restores FFmpeg logging level and error suppression flags

### Interrupt Handling
- Static callback registered with AVFormatContext
- Checks `m_interruptRequested` flag
- Enforces 5-second timeout on FFmpeg operations
- Prevents blocking on device operations

## Testing Notes

1. **CaptureThread Interface Pattern**:
   - Verify both `FFmpegBackendHandler` and `FFmpegCaptureManager` can call readFrame()
   - Check polymorphic dispatch through `ICaptureFrameReader`

2. **Thread Lifecycle**:
   - Test normal stop (from main thread)
   - Test self-stop (from capture thread during device disconnection)
   - Verify no deadlocks or crashes

3. **Device Reconnection**:
   - Unplug device during capture
   - Verify `DeviceDisconnected` signal emitted
   - Plug device back in
   - Verify hotplug handler restarts capture

4. **Performance**:
   - Ensure frame rates remain consistent after refactoring
   - Check CPU usage hasn't increased
   - Verify performance timer still reports FPS correctly

## Benefits

1. **Separation of Concerns**: Capture logic isolated from backend handler
2. **Reusability**: FFmpegCaptureManager can be used independently
3. **Maintainability**: Thread management in one place
4. **Testability**: Easier to unit test capture lifecycle
5. **Consistency**: Matches pattern of FFmpegDeviceValidator and FFmpegHotplugHandler

## Known Issues

- IntelliSense may report false errors about QObject/QString not found (configuration issue, not compilation error)
- Some unused includes in ffmpegbackendhandler.cpp (can be cleaned up later)

## Future Work

1. Remove `openInputDevice()`, `closeInputDevice()`, `cleanupFFmpegResources()` from FFmpegBackendHandler (now duplicated in FFmpegCaptureManager)
2. Remove `m_interruptRequested`, `m_operationStartTime` from FFmpegBackendHandler (now managed by FFmpegCaptureManager)
3. Consider extracting `processFrame()` to FFmpegFrameProcessor
4. Move `m_packet` management into FFmpegCaptureManager or FFmpegDeviceManager
