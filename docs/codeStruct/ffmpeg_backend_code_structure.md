# FFmpeg Backend — Code Structure & Technical Review ✅

This document explains the technical design and implementation details of the FFmpeg backend in this repository (files under `host/backend/ffmpeg`). It summarizes responsibilities, threading, FFmpeg integration, and notable implementation choices. Use this as a quick reference and review artifact.

---

## Table of Contents
- Overview
- Architecture & Data Flow
- Key Components
  - `CaptureThread`
  - `ICaptureFrameReader`
  - `FFmpegCaptureManager`
  - `FFmpegDeviceManager`
  - `FFmpegFrameProcessor`
  - `FFmpegHardwareAccelerator`
  - `FFmpegDeviceValidator`
  - `FFmpegHotplugHandler`
  - `FFmpegRecorder`
  - `FFmpegBackendHandler`
- FFmpeg integration & RAII helpers
- Threading, synchronization & signals
- Error handling & device recovery
- Performance, scaling & optimization notes
- Testability & suggestions
- Appendix: Relevant files

---

## Overview
The code implements a modular FFmpeg-based video capture and processing backend for a Qt application. The main goals are:
- Reliable capture from a variety of devices (hotplug aware)
- Resilient decoding: hardware-accelerated when available, software fallback otherwise
- Efficient frame processing and conversion for UI display
- Recording support and frame-level operations

Design emphasizes separation of concerns (device management, capture lifecycle, decoding/processing, recording, hotplug detection).

---

## Architecture & Data Flow
1. Device opening & decoder setup: `FFmpegDeviceManager` handles FFmpeg `AVFormatContext` & `AVCodecContext` and decoder choice (hardware vs software).
2. Capture loop: `CaptureThread` repeatedly calls `ICaptureFrameReader::readFrame()` (implemented by `FFmpegCaptureManager` or older `FFmpegBackendHandler`) and emits `frameAvailable()` when a frame is ready.
3. Frame processing: `FFmpegFrameProcessor` receives packets/frames and decodes/convert them to `QImage` or `QPixmap` for display, using turbojpeg/ffmpeg/hw decoders as appropriate.
4. Hotplug & validation: `FFmpegHotplugHandler` (with `FFmpegDeviceValidator`) watches system device events and ensures robust start/stop and retry behavior.
5. Recording: `FFmpegRecorder` encodes frames to disk using FFmpeg, supporting configuration such as codec, bitrate and format.


---

## Key Components
### CaptureThread (files: `capturethread.h`, `capturethread.cpp`) 🔧
- Purpose: background capture loop running on a dedicated `QThread`.
- Calls `m_frameReader->readFrame()` and emits `frameAvailable()` when a frame is read.
- Tracks consecutive failures and performs adaptive sleep and early device-disconnect handling.
- Notes:
  - Uses `QMutex` to protect the `m_running` state.
  - Uses `QPointer<QObject>` for `m_handler` to avoid dangling references.
  - Emits `readError()` and `deviceDisconnected()` when repeated failures occur.

Why this matters: separating the capturing loop into a dedicated thread ensures the UI thread remains responsive while keeping frame read lifetimes valid during processing.

---

### ICaptureFrameReader (`icapture_frame_reader.h`) 🧭
- A simple interface: `virtual bool readFrame() = 0;`
- Allows `CaptureThread` to be used with either `FFmpegBackendHandler` or `FFmpegCaptureManager` (polymorphism).

---

### FFmpegCaptureManager (`ffmpeg_capture_manager.*`) 📦
- Central lifecycle manager: start/stop capture, manage capture thread and packet storage.
- Implements `ICaptureFrameReader::readFrame()`; keeps latest `AVPacket`/packet pointers accessible for decoding by `FFmpegFrameProcessor`.
- Manages a `performance_timer_` and emits signals like `FrameAvailable`, `DeviceDisconnected` and `CaptureError`.
- Uses a `QTimer` and mutex for thread safety.

Notes:
- Encapsulates interrupt/timeouts using `InterruptCallback` and `kOperationTimeoutMs`.
- Responsible for allocation/cleanup and thread initialization.

---

### FFmpegDeviceManager (`ffmpeg_device_manager.*`) 🖥️
- Responsible for opening the device and configuring a decoder.
- Provides `OpenDevice`, `CloseDevice`, `FindVideoStream`, `SetupDecoder` and device capability queries.
- Contains interrupt logic (via `SetInterruptRequested` and `InterruptCallback`) to break long-blocking FFmpeg calls.

Important: This module deals directly with FFmpeg internals (`AVFormatContext`, `AVCodecContext`) and selects hardware decoders when available.

---

### FFmpegFrameProcessor (`ffmpeg_frame_processor.*`) 🖼️
- Handles decoding and converting `AVPacket` → `QImage/QPixmap`.
- Supports:
  - Hardware decoding (via `FFmpegHardwareAccelerator` / AVHW device context)
  - TurboJPEG MJPEG fast path (`HAVE_LIBJPEG_TURBO`) for MJPEG streams
  - FFmpeg software decoding and scaling (via `sws_context_`)
- Manages frame drop thresholds for display vs recording to keep UI responsive when resources are constrained.
- Uses thread-safe `QImage` conversion on worker threads and avoids `QPixmap` creation there (good practice).

Notes:
- Keeps a scaling context with lazy reconfiguration when size/pixel format changes.
- Stores a thread-safe `latest_frame_` for image capture.

---

### FFmpegHardwareAccelerator (`ffmpeg_hardware_accelerator.*`) ⚡
- Detects available HW decoders (e.g., `mjpeg_cuvid`, `mjpeg_qsv`) and optionally creates an `AVBufferRef` device context.
- Balances platform differences (Windows vs Linux) and supports manual preference override (e.g., `preferred_hw_accel_`).
- Exposes `TryHardwareDecoder()` to upstream code to select appropriate `AVCodec`.

Notes:
- Logs guidance to the user when HW acceleration isn't available.

---

### FFmpegDeviceValidator (`ffmpeg_device_validator.*`) 🔍
- Non-invasive device checks to determine availability and basic compatibility: uses `avformat_open_input` with very small frame size/framerate for quick tests.
- Has OS-specific checks for e.g., Windows exclusive access, and uses `GlobalSetting` as a fallback source of capability info.

Why: preventing aggressive checks on an already-in-use device reduces spurious failures and improves success rate during device handovers.

---

### FFmpegHotplugHandler (`ffmpeg_hotplug_handler.*`) 🔌
- Listens to the system `HotplugMonitor`, maps `DeviceInfo` events to application device activation/deactivation, and orchestrates retries and waiting timeouts.
- Maintains `waiting_for_device_` state and timer-based retries.

Notes:
- Integrates with `FFmpegDeviceValidator` to avoid unnecessary intrusive tests while waiting for devices.

---

### FFmpegRecorder (`ffmpeg_recorder.*`) 🎞️
- Encapsulates recording initialization, encoder configuration, writing frames to container and finalization.
- Accepts `RecordingConfig` (format, codec, bitrate, hardware accel flag).
- Uses `SwsContext` and `AVFrame`/`AVPacket` to feed the encoder.
- Tracks timing (start, pause, resume) and supports taking still images.

Notes:
- Careful cleanup and finalize steps ensure recorded files are coherent even on errors.

---

### FFmpegBackendHandler (`ffmpegbackendhandler.*`) 🧩
- Top-level glue between application UI, `FFmpegCaptureManager`, `FFmpegFrameProcessor`, `FFmpegHardwareAccelerator`, `FFmpegRecorder`, and `FFmpegHotplugHandler`.
- Manages UI-related objects (`QGraphicsVideoItem`, `VideoPane`) and translates device/hotplug events into start/stop capture requests.

Notes:
- Contains legacy code paths and both direct-capture and manager-based capture modes.

---

## FFmpeg Integration & RAII Helpers
- `ffmpegutils.h` wraps `AVFrame` and `AVPacket` into RAII-friendly `AvFramePtr` / `AvPacketPtr`, ensuring `av_frame_free()` / `av_packet_free()` usage.
- Conditional compilation macros like `HAVE_FFMPEG` and `HAVE_LIBJPEG_TURBO` gate availability-dependent implementations.

Why this matters: RAII owners in C++ reduce leaks and make cleanup simpler during early returns or exceptions.

---

## Threading, Synchronization & Signals
- `CaptureThread` runs the capture loop and emits Qt signals (`frameAvailable`, `deviceDisconnected`, `readError`) to communicate with owners on the main thread.
- `QMutex` is used in several classes to guard state (e.g., `capture_running_`, `latest_frame_`, `m_running`) and `QPointer` prevents use-after-free when the object owning the thread is destroyed.
- The code avoids creating `QPixmap` on worker threads (converts to `QImage` instead), which is correct for cross-thread GUI safety.

Potential improvement: consider using `std::atomic<bool>` for simple flags like `interrupt_requested_` and `capture_running_` to avoid mutex overhead and clarify intent.

---

## Error Handling & Device Recovery
- Capture loop uses `consecutiveFailures` and adaptive sleep strategy; on reaching thresholds the system notifies handlers about disconnection.
- `FFmpegDeviceManager` implements an `InterruptCallback` that checks timeouts and interrupt flags to cancel blocking FFmpeg calls.
- `FFmpegHotplugHandler` orchestrates waiting and retries to handle devices that take time to enumerate.

Notes:
- Logging is thorough (using `qCDebug`/`qCWarning`) which helps troubleshooting.

---

## Performance & Optimization Notes
- Performance logging in `CaptureThread` measures actual FPS and elapsed time for sanity checks.
- Frame drop thresholds are tuned separately for display and recording to prioritize UI responsiveness.
- TurboJPEG fast path accelerates MJPEG decoding when available; HW decoders are used for efficiency.

Tips:
- For high-rate capture (e.g., 60fps), consider moving performance-critical processing into a bounded worker-pool if you need parallelizable work besides decoding.
- Consider capping maximum consecutive sleep to avoid long delays during short transient glitches.

---

## Testability & Suggestions ✅
- Add unit tests or integration tests for:
  - `FFmpegDeviceValidator::CheckFFmpegCompatibility` with various virtual device inputs
  - `FFmpegFrameProcessor` conversions (sample packets → QImage)
  - Device hotplug integration (simulate `HotplugMonitor` events)

Refactor suggestions:
- Replace raw `volatile bool` and `QMutex` used as atomic flags with `std::atomic<bool>` where appropriate.
- Consider consolidating the legacy direct capture implementation in `FFmpegBackendHandler` with the newer `FFmpegCaptureManager` (if feasible) to reduce duplication.
- Add clearer documentation comments on the lifetime/ownership of packets and frames (e.g., who frees `AVPacket` after read).

---

## Appendix: Relevant Files (non-exhaustive)
- `capturethread.h` / `capturethread.cpp`
- `icapture_frame_reader.h`
- `ffmpeg_capture_manager.*`
- `ffmpeg_device_manager.*`
- `ffmpeg_frame_processor.*`
- `ffmpeg_hardware_accelerator.*`
- `ffmpeg_device_validator.*`
- `ffmpeg_hotplug_handler.*`
- `ffmpeg_recorder.*`
- `ffmpegutils.h`
- `ffmpegbackendhandler.*`

