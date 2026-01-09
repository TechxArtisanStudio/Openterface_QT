@echo off
REM ============================================================================
REM LibUSB Shared Library Build and Install Script for Windows (MSYS2 MinGW)
REM This script builds and installs LibUSB shared library using MSYS2 MinGW
REM toolchain and MSYS2 Bash.
REM ============================================================================

setlocal enabledelayedexpansion

REM Configuration
set "LIBUSB_VERSION=1.0.27"
set "LIBUSB_INSTALL_PREFIX=C:/libusb"
set "BUILD_DIR=%cd%\libusb-build-temp"
set "SCRIPT_DIR=%~dp0"

echo ============================================================================
echo LibUSB Build and Install Script for Windows
echo ============================================================================

REM Locate MSYS2 Bash (prefer it over Git Bash and WSL bash)
set "GIT_BASH="
if exist "C:\msys64\usr\bin\bash.exe" (
    set "GIT_BASH=C:\msys64\usr\bin\bash.exe"
    goto :found
)
for /f "tokens=*" %%i in ('where bash 2^>nul') do (
    echo %%i | findstr /i "git" >nul
    if !errorlevel! == 0 (
        set "GIT_BASH=%%i"
        goto :found
    )
)
:found
if not defined GIT_BASH (
    REM Fallback to first available bash
    for /f "tokens=*" %%i in ('where bash 2^>nul') do (
        set "GIT_BASH=%%i"
        goto :fallback
    )
)
:fallback

if not defined GIT_BASH (
    echo ERROR: Bash not found. Please install MSYS2 or Git for Windows or ensure bash is in PATH.
    exit /b 1
)

echo Using Bash: !GIT_BASH!

REM Check build script exists
set "SCRIPT_PATH=%SCRIPT_DIR%install-libusb-windows.sh"
if not exist "!SCRIPT_PATH!" (
    echo ERROR: Build script not found at !SCRIPT_PATH!
    exit /b 1
)

echo Build script: !SCRIPT_PATH!

REM Determine MinGW path
if defined EXTERNAL_MINGW (
    set "MINGW_PATH=!EXTERNAL_MINGW!"
    REM Remove trailing backslash if present
    if "!MINGW_PATH:~-1!"=="\" set "MINGW_PATH=!MINGW_PATH:~0,-1!"
) else (
    set "MINGW_PATH=C:\msys64\mingw64"
    REM If default not found, search common drives
    if not exist "!MINGW_PATH!\bin\gcc.exe" (
        for %%d in (C D E F G) do (
            if exist "%%d:\msys64\mingw64\bin\gcc.exe" (
                set "MINGW_PATH=%%d:\msys64\mingw64"
                goto :mingw_found
            )
            if exist "%%d:\mingw64\bin\gcc.exe" (
                set "MINGW_PATH=%%d:\mingw64"
                goto :mingw_found
            )
        )
    )
)
:mingw_found

REM Validate MinGW toolchain
if not exist "!MINGW_PATH!\bin\gcc.exe" (
    echo ERROR: gcc.exe not found in MinGW directory: !MINGW_PATH!\bin
    echo Please set EXTERNAL_MINGW to a valid MinGW-w64 installation.
    exit /b 1
)

echo Using MinGW: !MINGW_PATH!

REM Set environment for the build
set "EXTERNAL_MINGW_MSYS=!MINGW_PATH!"
set "SKIP_MSYS_MINGW=1"

REM Update PATH to include MinGW bin
set "PATH=!MINGW_PATH!\bin;%PATH%"

REM Update PATH to include MSYS2 usr bin for tar
set "PATH=C:\msys64\usr\bin;%PATH%"

echo.
echo Starting LibUSB build and install...
echo.

REM URLs and versions
set "DOWNLOAD_URL=https://github.com/libusb/libusb/releases/download/v%LIBUSB_VERSION%/libusb-%LIBUSB_VERSION%.tar.bz2"

REM Number of CPU cores
set "NUM_CORES=%NUMBER_OF_PROCESSORS%"

echo ============================================================================
echo LibUSB Build and Install - Windows (external MinGW)
echo ============================================================================
echo LibUSB Version: %LIBUSB_VERSION%
echo Install Prefix: %LIBUSB_INSTALL_PREFIX%
echo Build Directory: %BUILD_DIR%
echo CPU Cores: %NUM_CORES%
echo ============================================================================

REM Check for required tools
echo Checking for required tools...
where gcc >nul 2>&1 || (echo ERROR: gcc not found on PATH & exit /b 1)
where mingw32-make >nul 2>&1 || where make >nul 2>&1 || (echo ERROR: make or mingw32-make not found on PATH & exit /b 1)
where tar >nul 2>&1 || (echo ERROR: tar not found on PATH & exit /b 1)
where curl >nul 2>&1 || (echo ERROR: curl not found on PATH & exit /b 1)
echo All required tools found.
echo.

REM Create build directory
echo Creating build directory...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
echo Build directory ready: %BUILD_DIR%
echo.

REM Call the bash script
"!GIT_BASH!" -c "SCRIPT_DIR='%SCRIPT_DIR%' LIBUSB_VERSION='%LIBUSB_VERSION%' LIBUSB_INSTALL_PREFIX='%LIBUSB_INSTALL_PREFIX%' BUILD_DIR='%BUILD_DIR%' NUM_CORES='%NUM_CORES%' DOWNLOAD_URL='%DOWNLOAD_URL%' '%SCRIPT_PATH%'"

if %errorlevel% neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo LibUSB build and install completed successfully!
echo Installed to: %LIBUSB_INSTALL_PREFIX%
echo.