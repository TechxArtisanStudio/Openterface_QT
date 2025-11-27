@echo off
REM ============================================================================
REM Static FFmpeg Build for Windows (MinGW-w64 standalone)
REM - Uses external MinGW (C:\mingw64) as compiler
REM - Does NOT use MSYS2's GCC or libraries
REM - Disables Intel QSV (libmfx) to avoid compatibility issues
REM - Enables CUDA/NVDEC via ffnvcodec headers
REM - Runs configure in cross-compile mode
REM ============================================================================

setlocal enabledelayedexpansion

REM === Configuration ===
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not defined EXTERNAL_MINGW set "EXTERNAL_MINGW=C:\mingw64"
if not defined FFMPEG_VERSION set "FFMPEG_VERSION=6.1.1"
if not defined LIBJPEG_TURBO_VERSION set "LIBJPEG_TURBO_VERSION=3.0.4"
if not defined FFMPEG_INSTALL_PREFIX set "FFMPEG_INSTALL_PREFIX=C:\ffmpeg-static"
if not defined VCPKG_DIR set "VCPKG_DIR=C:\vcpkg"

set "BUILD_DIR=%cd%\ffmpeg-build-temp"
set "SCRIPT_DIR=%~dp0"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo ==============================================================================
echo FFmpeg Static Build ^(NO QSV, CUDA via ffnvcodec^)
echo Using External MinGW: %EXTERNAL_MINGW%
echo Install Prefix: %FFMPEG_INSTALL_PREFIX%
echo ==============================================================================

REM === Validate required paths ===
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo [ERROR] MSYS2 not found at %MSYS2_ROOT%
    exit /b 1
)
if not exist "%EXTERNAL_MINGW%\bin\gcc.exe" (
    echo [ERROR] External MinGW not found at %EXTERNAL_MINGW%
    exit /b 1
)
if not exist "%VCPKG_DIR%\installed\x64-mingw-static\lib\libz.a" (
    echo [ERROR] vcpkg dependencies (zlib etc.) not installed. Please run vcpkg install first.
    exit /b 1
)
if not exist "%SCRIPT_DIR%build-static-ffmpeg-windows.sh" (
    echo [ERROR] Bash script 'build-static-ffmpeg-windows.sh' not found.
    exit /b 1
)

echo [INFO] ✓ MSYS2: %MSYS2_ROOT%
echo [INFO] ✓ External MinGW: %EXTERNAL_MINGW%
echo [INFO] ✓ vcpkg libs: %VCPKG_DIR%\installed\x64-mingw-static
echo [INFO] ✓ Output: %FFMPEG_INSTALL_PREFIX%

REM === Convert Windows paths to MSYS2 format (/c/...) ===
call :win_to_msys "%SCRIPT_DIR%build-static-ffmpeg-windows.sh" MAIN_SCRIPT_MSYS
call :win_to_msys "%EXTERNAL_MINGW%" EXTERNAL_MINGW_MSYS
call :win_to_msys "%FFMPEG_INSTALL_PREFIX%" FFMPEG_INSTALL_PREFIX_MSYS
call :win_to_msys "%VCPKG_DIR%" VCPKG_DIR_MSYS
call :win_to_msys "%BUILD_DIR%" BUILD_DIR_MSYS

REM === Create wrapper.sh to set env vars in bash ===
set "WRAPPER=%BUILD_DIR%\wrapper.sh"
(
    echo #!/bin/bash
    echo export EXTERNAL_MINGW="%EXTERNAL_MINGW_MSYS%"
    echo export FFMPEG_INSTALL_PREFIX="%FFMPEG_INSTALL_PREFIX_MSYS%"
    echo export VCPKG_DIR="%VCPKG_DIR_MSYS%"
    echo export FFMPEG_VERSION="%FFMPEG_VERSION%"
    echo export LIBJPEG_TURBO_VERSION="%LIBJPEG_TURBO_VERSION%"
    echo export BUILD_DIR="%BUILD_DIR_MSYS%"
    echo exec bash "%MAIN_SCRIPT_MSYS%"
) > "%WRAPPER%"

call :win_to_msys "%WRAPPER%" WRAPPER_MSYS

echo [INFO] Launching MSYS2 MinGW64 environment to run build script...
echo [WARN] This may take 45-90 minutes.

REM Launch in MinGW64 mode (but we won't use its toolchain!)
"%MSYS2_ROOT%\msys2_shell.cmd" -mingw64 -defterm -no-start -here -c "bash %WRAPPER_MSYS%"

if %errorlevel% equ 0 (
    echo.
    echo ==============================================================================
    echo ✅ FFmpeg static build succeeded!
    echo Libraries: %FFMPEG_INSTALL_PREFIX%\lib
    echo ==============================================================================
) else (
    echo.
    echo ❌ FFmpeg build failed! Exit code: %errorlevel%
    exit /b 1
)

exit /b 0

REM ========================
REM Helper: Convert C:\path to /c/path
REM ========================
:win_to_msys
set "p=%~1"
set "p=%p:\=/%"
for /f "tokens=1,2 delims=:" %%a in ("%p%") do set "res=/%%a%%b"
REM Lowercase drive letter
set "res=%res:A=a%"
set "res=%res:B=b%"
set "res=%res:C=c%"
set "res=%res:D=d%"
set "res=%res:E=e%"
set "res=%res:F=f%"
endlocal & set "%2=%res%"
goto :eof