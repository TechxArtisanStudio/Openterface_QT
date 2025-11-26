@echo off
REM ============================================================================
REM Static FFmpeg Build Script for Windows using MSYS2
REM Now uses an external MinGW (default C:\mingw64) for building; MSYS2 only manages dependencies
REM ============================================================================

setlocal enabledelayedexpansion

REM Configuration
if not defined MSYS2_ROOT set MSYS2_ROOT=C:\msys64
if not defined EXTERNAL_MINGW set EXTERNAL_MINGW=C:\mingw64
if not defined FFMPEG_VERSION set FFMPEG_VERSION=6.1.1
if not defined LIBJPEG_TURBO_VERSION set LIBJPEG_TURBO_VERSION=3.0.4
if not defined FFMPEG_INSTALL_PREFIX set FFMPEG_INSTALL_PREFIX=C:\ffmpeg-static
set BUILD_DIR=%cd%\ffmpeg-build-temp
set SCRIPT_DIR=%~dp0

REM Colors for output (Windows console codes)
echo [92m============================================================================[0m
echo [92mFFmpeg Static Build Script for Windows (using external MinGW)[0m
echo [92m============================================================================[0m

REM Check if MSYS2 is installed
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo [91mError: MSYS2 not found at %MSYS2_ROOT%[0m
    echo Please install MSYS2 from https://www.msys2.org/
    echo Or update MSYS2_ROOT variable in this script if installed elsewhere
    exit /b 1
)

REM Check external MinGW
if not exist "%EXTERNAL_MINGW%\bin\gcc.exe" (
    echo [91mError: External MinGW not found at %EXTERNAL_MINGW%[0m
    REM Avoid parentheses inside a parenthesized block which breaks cmd parsing
    echo Please install MinGW-w64 or update the EXTERNAL_MINGW variable to point to your toolchain.
    exit /b 1
)

echo [92mFound MSYS2 at: %MSYS2_ROOT%[0m
echo [92mFound external MinGW at: %EXTERNAL_MINGW%[0m

REM Check if build script exists
if not exist "%SCRIPT_DIR%build-static-ffmpeg-windows.sh" (
    echo [91mError: Build script not found at %SCRIPT_DIR%build-static-ffmpeg-windows.sh[0m
    exit /b 1
)

REM Convert Windows paths to MSYS2 path format (C:\path\to -> /c/path/to)
set SCRIPT_PATH=%SCRIPT_DIR%build-static-ffmpeg-windows.sh
set SCRIPT_PATH_MSYS=%SCRIPT_PATH:\=/%
set SCRIPT_PATH_MSYS=%SCRIPT_PATH_MSYS::=%
set SCRIPT_PATH_MSYS=/%SCRIPT_PATH_MSYS%

set EXTERNAL_MINGW_MSYS=%EXTERNAL_MINGW:\=/%
set EXTERNAL_MINGW_MSYS=%EXTERNAL_MINGW_MSYS::=%
set EXTERNAL_MINGW_MSYS=/%EXTERNAL_MINGW_MSYS%

set FFMPEG_INSTALL_PREFIX_MSYS=%FFMPEG_INSTALL_PREFIX:\=/%
set FFMPEG_INSTALL_PREFIX_MSYS=%FFMPEG_INSTALL_PREFIX_MSYS::=%
set FFMPEG_INSTALL_PREFIX_MSYS=/%FFMPEG_INSTALL_PREFIX_MSYS%

echo [92mLaunching MSYS2 (MSYS) environment...[0m
echo [93mUsing external MinGW: %EXTERNAL_MINGW%[0m
echo [93mThis will take some time (30-60 minutes depending on your system)[0m
echo.

REM Launch MSYS2 MSYS shell and run the build script while exporting the external MinGW path
"%MSYS2_ROOT%\msys2_shell.cmd" -msys -defterm -no-start -here -c "bash -lc \"export EXTERNAL_MINGW=%EXTERNAL_MINGW_MSYS%; export FFMPEG_INSTALL_PREFIX=%FFMPEG_INSTALL_PREFIX_MSYS%; export FFMPEG_VERSION=%FFMPEG_VERSION%; export LIBJPEG_TURBO_VERSION=%LIBJPEG_TURBO_VERSION%; bash '%SCRIPT_PATH_MSYS%'\""

if %errorlevel% equ 0 (
    echo.
    echo [92m============================================================================[0m
    echo [92mFFmpeg build completed successfully![0m
    echo [92m============================================================================[0m
    echo Installation directory: %FFMPEG_INSTALL_PREFIX%
    echo.
    echo To use this FFmpeg build in your project, set the environment variable:
    echo   set FFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX%
    echo.
    echo Or in CMake:
    echo   cmake -DFFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX% ...
    echo [92m============================================================================[0m
) else (
    echo.
    echo [91m============================================================================[0m
    echo [91mFFmpeg build failed![0m
    echo [91m============================================================================[0m
    echo Check the output above for errors
    echo [91m============================================================================[0m
    exit /b 1
fi

exit /b 0