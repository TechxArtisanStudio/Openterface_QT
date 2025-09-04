# Windows Docker Build Environments for Openterface QT

This directory contains Docker environments for building Openterface QT on Windows with both shared and static linking configurations.

## Structure

```
docker/
├── windows-shared/          # Shared libraries build environment
│   └── Dockerfile
├── windows-static/          # Static libraries build environment  
│   └── Dockerfile
├── windows-build-scripts/   # PowerShell build scripts
│   ├── build-shared.ps1
│   └── build-static.ps1
├── docker-compose.windows.yml  # Docker Compose configuration
└── run-windows-build.sh    # Convenience script for building
```

## Build Environments

### Windows Shared Build Environment

**Image**: `ghcr.io/techxartisanstudio/openterface-qtbuild-windows-shared:ltsc2022-windows`

- **Base**: Windows Server Core LTSC 2022
- **Qt Version**: 6.5.3 with shared libraries
- **Compiler**: Visual Studio 2022 Build Tools
- **Output**: Executable with Qt DLL dependencies

**Features**:
- Pre-built Qt 6.5.3 with shared libraries
- Visual Studio 2022 Build Tools
- CMake, Ninja, vcpkg for dependency management
- Translation tools (lupdate, lrelease)
- Optimized for fast builds with windeployqt

### Windows Static Build Environment

**Image**: `ghcr.io/techxartisanstudio/openterface-qtbuild-windows-static:ltsc2022-windows`

- **Base**: Windows Server Core LTSC 2022  
- **Qt Version**: 6.5.3 compiled from source with static linking
- **Compiler**: MinGW-w64 with static runtime
- **Output**: Portable executable with no external dependencies

**Features**:
- Qt 6.5.3 built from source with static configuration
- OpenSSL static libraries via vcpkg
- MinGW-w64 toolchain with static runtime
- Self-contained portable executable output
- Based on existing portable build workflow

## Usage

### Using the Convenience Script

The `run-windows-build.sh` script provides easy commands for building:

```bash
# Build with shared libraries
./run-windows-build.sh build-shared

# Build with static libraries (portable)
./run-windows-build.sh build-static

# Build both versions
./run-windows-build.sh build-both

# Open interactive shell in shared environment
./run-windows-build.sh shell-shared

# Open interactive shell in static environment  
./run-windows-build.sh shell-static

# Clean build artifacts
./run-windows-build.sh clean

# Rebuild Docker images from scratch
./run-windows-build.sh rebuild
```

### Using Docker Compose Directly

```bash
# Build the environments
docker-compose -f docker-compose.windows.yml build

# Run shared build
docker-compose -f docker-compose.windows.yml run --rm windows-shared-build \
  powershell -File "C:/build-scripts/build-shared.ps1" -BuildType Release

# Run static build  
docker-compose -f docker-compose.windows.yml run --rm windows-static-build \
  powershell -File "C:/build-scripts/build-static.ps1" -BuildType Release
```

### Using Docker Directly

**Shared Build**:
```cmd
docker run --rm -v %cd%:C:/workspace -w C:/workspace ^
  ghcr.io/techxartisanstudio/openterface-qtbuild-windows-shared:ltsc2022-windows ^
  powershell -File C:/build-scripts/build-shared.ps1 -BuildType Release
```

**Static Build**:
```cmd
docker run --rm -v %cd%:C:/workspace -w C:/workspace ^
  ghcr.io/techxartisanstudio/openterface-qtbuild-windows-static:ltsc2022-windows ^
  powershell -File C:/build-scripts/build-static.ps1 -BuildType Release
```

## Build Scripts

### build-shared.ps1

Builds Openterface QT with shared Qt libraries using the qmake build system, following the pattern from `windows-build.yaml`:

- Uses `qmake` and `mingw32-make` for building
- Updates translations with `lupdate` and `lrelease`
- Creates packaged output with `windeployqt`
- Includes all necessary files (drivers, translations, keyboards)

### build-static.ps1

Builds a portable Openterface QT executable with static libraries, following the pattern from `windows-portable-build.yaml`:

- Modifies `.pro` file for static linking
- Links with static OpenSSL libraries
- Uses MinGW with static runtime
- Strips debug symbols for smaller size
- Creates completely self-contained executable

## Output

### Shared Build Output

Located in `build-windows-shared-Release/package_shared/`:
- `openterfaceQT.exe` - Main executable
- Qt DLLs and dependencies
- Driver files
- Translation files
- Keyboard layouts

### Static Build Output

Located in `build-windows-static-Release/package/`:
- `openterfaceQT-portable.exe` - Self-contained portable executable

## GitHub Actions Integration

The environments are integrated into the GitHub Actions workflow `build-qt-environments.yml`:

```yaml
# Build Windows shared environment
environment_type: 'windows-shared'

# Build Windows static environment  
environment_type: 'windows-static'

# Build both Windows environments
environment_type: 'windows-both'

# Build all environments (Linux + Windows)
target_platform: 'both'
```

## Requirements

- Docker Desktop with Windows containers enabled
- At least 8GB RAM available to Docker
- Sufficient disk space (static build requires ~10GB for Qt compilation)

## Troubleshooting

### Common Issues

1. **Out of memory during Qt compilation**: Increase Docker memory limit to 8GB+
2. **Long build times**: Use pre-built images from registry instead of building locally
3. **Volume mounting issues**: Ensure Docker has access to the project directory

### Debug Mode

Run containers interactively for debugging:

```cmd
# Shared environment
docker run --rm -it -v %cd%:C:/workspace ^
  ghcr.io/techxartisanstudio/openterface-qtbuild-windows-shared:ltsc2022-windows ^
  powershell

# Static environment
docker run --rm -it -v %cd%:C:/workspace ^
  ghcr.io/techxartisanstudio/openterface-qtbuild-windows-static:ltsc2022-windows ^
  powershell
```

## Compatibility

- **Host OS**: Windows 10/11, Windows Server 2019+
- **Docker**: Docker Desktop with Windows containers
- **Architecture**: x64 only
- **Qt Version**: 6.5.3
- **Visual Studio**: 2022 Build Tools (shared build)
- **MinGW**: w64 latest (static build)
