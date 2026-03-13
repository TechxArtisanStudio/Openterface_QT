## Openterface QT - CMake notes

This document explains the CMake variables recently added to help control static vs dynamic linking and to expose the detected target architecture.

### Key CMake cache variables

- `OPENTERFACE_BUILD_STATIC` (BOOL, default OFF)
  - When ON, CMake will prefer static libraries (`.a`) when searching for dependencies. Use to request a static-preferred configure.

- `OPENTERFACE_ARCH` (STRING, cached)
  - One of `arm64`, `amd64`, or the raw `CMAKE_SYSTEM_PROCESSOR` string when unknown.
  - Can be overridden at configure time to force a specific value.

- `OPENTERFACE_IS_ARM64` / `OPENTERFACE_IS_AMD64` (BOOL, cached)
  - Convenience booleans derived from `OPENTERFACE_ARCH` for conditional logic in CMake files.

### How to configure

Normal configure (auto-detect architecture):

```bash
cmake -S /path/to/Openterface_QT -B /path/to/Openterface_QT/build
```

Prefer static libraries when available:

```bash
cmake -S /path/to/Openterface_QT -B /path/to/Openterface_QT/build -DOPENTERFACE_BUILD_STATIC=ON
```

Force or override detected architecture:

```bash
cmake -S /path/to/Openterface_QT -B /path/to/Openterface_QT/build -DOPENTERFACE_ARCH=amd64
```

### Notes and caveats

- `OPENTERFACE_BUILD_STATIC` prefers static libs but does not automatically force static linking for every dependency. Some backends (FFmpeg, GStreamer, Qt) require per-library handling which is already partially implemented in `cmake/FFmpeg.cmake` and `cmake/GStreamer.cmake`.
- If you need a strict static-only or shared-only build, consider adding per-backend cache switches and adapting the `find_library`/`find_package` tactics.

### Next steps

- If you want, a short `doc/BUILD.md` can be added with example build flows for ARM64 vs AMD64 and packaging notes.
