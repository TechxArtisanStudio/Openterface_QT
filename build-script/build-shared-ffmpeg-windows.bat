@echo off
REM ============================================================================
REM Shared FFmpeg Build Script for Windows (MSYS2 MinGW)
REM This script builds FFmpeg with shared libraries using MSYS2 MinGW
REM toolchain and MSYS2 Bash.
REM ============================================================================

setlocal enabledelayedexpansion

REM Configuration (use BACKSLASHES for Windows paths)
set "FFMPEG_VERSION=6.1.1"
set "LIBJPEG_TURBO_VERSION=3.0.4"
set "FFMPEG_INSTALL_PREFIX=C:\ffmpeg-shared"
set "BUILD_DIR=%cd%\ffmpeg-build-temp"
set "SCRIPT_DIR=%~dp0"

REM Optional environment variables:
REM   EXTERNAL_MINGW=C:\msys64\mingw64  -> Path to MinGW toolchain (default MSYS2)
REM   ENABLE_NVENC=1              -> Enable NVENC support
REM   NVENC_SDK_PATH=...          -> Path to NVENC SDK (optional)

echo ============================================================================
echo FFmpeg Shared Build Script for Windows
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
set "SCRIPT_PATH=%SCRIPT_DIR%build-shared-ffmpeg-windows.sh"
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
echo Starting FFmpeg build (this will take 30-60 minutes)...
echo.

REM URLs and versions (keep as before)
set "DOWNLOAD_URL=https://ffmpeg.org/releases/ffmpeg-%FFMPEG_VERSION%.tar.bz2"
set "LIBJPEG_TURBO_URL=https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/%LIBJPEG_TURBO_VERSION%.tar.gz"

REM Number of CPU cores
set "NUM_CORES=%NUMBER_OF_PROCESSORS%"

echo ============================================================================
echo FFmpeg Shared Build - Windows (external MinGW)
echo ============================================================================
echo FFmpeg Version: %FFMPEG_VERSION%
echo libjpeg-turbo Version: %LIBJPEG_TURBO_VERSION%
echo Install Prefix: %FFMPEG_INSTALL_PREFIX%
echo Build Directory: %BUILD_DIR%
echo CPU Cores: %NUM_CORES%
echo ============================================================================

REM Check for required tools
echo Checking for required tools...
where gcc >nul 2>&1 || (echo ERROR: gcc not found on PATH & exit /b 1)
where cmake >nul 2>&1 || (echo ERROR: cmake not found on PATH & exit /b 1)
where mingw32-make >nul 2>&1 || where make >nul 2>&1 || (echo ERROR: make or mingw32-make not found on PATH & exit /b 1)
where nasm >nul 2>&1 || where yasm >nul 2>&1 || (echo ERROR: nasm or yasm not found on PATH & exit /b 1)
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

REM === Convert install prefix to forward slashes for build tools ===
REM (Removed - cmake handles Windows paths fine)

REM Build libjpeg-turbo
echo Building libjpeg-turbo %LIBJPEG_TURBO_VERSION% ^(shared^)...

set "TURBO_INSTALLED=0"
if exist "%FFMPEG_INSTALL_PREFIX%\bin\libturbojpeg.dll" set "TURBO_INSTALLED=1"
if exist "%FFMPEG_INSTALL_PREFIX%\lib\libturbojpeg.dll" set "TURBO_INSTALLED=1"

if "%TURBO_INSTALLED%"=="1" goto :skip_libjpeg_turbo

echo Downloading libjpeg-turbo...
if not exist "libjpeg-turbo.tar.gz" (
    curl -L "%LIBJPEG_TURBO_URL%" -o "libjpeg-turbo.tar.gz"
)
echo Extracting libjpeg-turbo...
tar -xzf "libjpeg-turbo.tar.gz" >nul 2>&1
cd "libjpeg-turbo-%LIBJPEG_TURBO_VERSION%"
if errorlevel 1 (exit /b 1)
if not exist "build" mkdir build
cd build
if errorlevel 1 (exit /b 1)
echo Configuring libjpeg-turbo ^(shared^)...
cmake .. -G "MinGW Makefiles" ^
  -DCMAKE_INSTALL_PREFIX="%FFMPEG_INSTALL_PREFIX%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DENABLE_SHARED=ON ^
  -DENABLE_STATIC=OFF ^
  -DWITH_JPEG8=ON ^
  -DWITH_TURBOJPEG=ON ^
  -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON
echo Building libjpeg-turbo with %NUM_CORES% cores...
mingw32-make -j%NUM_CORES% || make -j%NUM_CORES%
if errorlevel 1 (exit /b 1)
echo Installing libjpeg-turbo...
mingw32-make install || make install
if errorlevel 1 (exit /b 1)
cd "%BUILD_DIR%"
rmdir /s /q "libjpeg-turbo-%LIBJPEG_TURBO_VERSION%"
echo libjpeg-turbo built and installed ^(shared^).

:skip_libjpeg_turbo
echo.

dir "%FFMPEG_INSTALL_PREFIX%\lib\libturbojpeg*"

REM Set extra flags (use UNIX-style paths for compiler/linker)
set "EXTRA_CFLAGS=-I%FFMPEG_INSTALL_PREFIX%\include"
set "EXTRA_LDFLAGS=-L%FFMPEG_INSTALL_PREFIX%\lib -lz -lbz2 -llzma -lwinpthread"
set "NVENC_ARG=--disable-nvenc"
set "CUDA_ARG=--enable-decoder=mjpeg"

if defined ENABLE_NVENC (
    echo NVENC enabled
    set "NVENC_ARG=--enable-nvenc"
    set "CUDA_ARG=%CUDA_ARG% --enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-decoder=mjpeg_cuvid"
    if defined NVENC_SDK_PATH (
        set "NVENC_SDK_PATH_UNIX=%NVENC_SDK_PATH:\=/"
        set "EXTRA_CFLAGS=%EXTRA_CFLAGS% -I%NVENC_SDK_PATH_UNIX%/include"
    )
)

echo.

REM Download FFmpeg
echo Downloading FFmpeg %FFMPEG_VERSION%...
if not exist "ffmpeg-%FFMPEG_VERSION%.tar.bz2" (
    curl -L "%DOWNLOAD_URL%" -o "ffmpeg-%FFMPEG_VERSION%.tar.bz2"
    echo Downloaded FFmpeg source
) else (
    echo FFmpeg source already downloaded
)
echo.

REM Extract FFmpeg
echo Extracting source code...
if exist "ffmpeg-%FFMPEG_VERSION%" rmdir /s /q "ffmpeg-%FFMPEG_VERSION%"
tar -xjf "ffmpeg-%FFMPEG_VERSION%.tar.bz2" >nul 2>&1
echo Source extracted
cd "ffmpeg-%FFMPEG_VERSION%"
echo.

REM Build FFmpeg
echo Configuring FFmpeg for shared build...
set "PKG_CONFIG_PATH=%FFMPEG_INSTALL_PREFIX%\lib\pkgconfig;%PKG_CONFIG_PATH%"

%GIT_BASH% ./configure ^
--prefix="%FFMPEG_INSTALL_PREFIX%" ^
--arch=x86_64 ^
--target-os=mingw32 ^
--enable-shared --disable-static ^
--enable-gpl --enable-version3 --enable-nonfree ^
--disable-debug --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages --disable-outdevs ^
--enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale ^
--enable-avdevice --enable-avfilter --enable-postproc ^
--enable-network --enable-runtime-cpudetect --enable-pthreads --disable-w32threads ^
--enable-zlib --enable-bzlib --enable-lzma ^
--enable-dxva2 --enable-d3d11va --enable-hwaccels ^
%CUDA_ARG% %NVENC_ARG% ^
--pkg-config-flags="" --extra-cflags="%EXTRA_CFLAGS%" --extra-ldflags="%EXTRA_LDFLAGS%"

if errorlevel 1 (
    echo ERROR: FFmpeg configure failed.
    exit /b 1
)
echo Configuration complete
echo.

REM Build FFmpeg
echo Building FFmpeg...
echo Using 1 CPU core for compilation to avoid race conditions
mingw32-make clean || make clean
mingw32-make -j1 || make -j1
if errorlevel 1 (
    echo ERROR: FFmpeg build failed.
    exit /b 1
)
echo Build complete
echo.

REM Install FFmpeg
echo Installing FFmpeg to %FFMPEG_INSTALL_PREFIX%...
mingw32-make install || make install
if errorlevel 1 (
    echo ERROR: FFmpeg install failed.
    exit /b 1
)
echo Installation complete
echo.

REM Verify
echo ============================================================================
echo Verifying installation (shared)...
echo ============================================================================

dir "%FFMPEG_INSTALL_PREFIX%\bin\*.dll" >nul 2>&1 || dir "%FFMPEG_INSTALL_PREFIX%\lib\*.dll" >nul 2>&1
if %errorlevel% == 0 (
    echo FFmpeg shared libraries installed successfully!
    echo.
    echo Installed FFmpeg libraries (DLLs):
    dir "%FFMPEG_INSTALL_PREFIX%\bin\*.dll" 2>nul
    dir "%FFMPEG_INSTALL_PREFIX%\lib\*.dll" 2>nul
    echo.
    echo Installed libjpeg-turbo libraries (DLLs):
    dir "%FFMPEG_INSTALL_PREFIX%\bin\*jpeg*.dll" 2>nul
    dir "%FFMPEG_INSTALL_PREFIX%\lib\*jpeg*.dll" 2>nul
    echo.
    echo Include directories:
    dir /ad "%FFMPEG_INSTALL_PREFIX%\include\lib*" 2>nul
    echo.
    echo ============================================================================
    echo Installation Summary
    echo ============================================================================
    echo Install Path: %FFMPEG_INSTALL_PREFIX%
    echo Libraries (DLLs): %FFMPEG_INSTALL_PREFIX%\bin or %FFMPEG_INSTALL_PREFIX%\lib
    echo Headers: %FFMPEG_INSTALL_PREFIX%\include
    echo pkg-config: %FFMPEG_INSTALL_PREFIX%\lib\pkgconfig
    echo.
    echo Components installed (shared):
    echo   FFmpeg %FFMPEG_VERSION% (shared)
    echo   libjpeg-turbo %LIBJPEG_TURBO_VERSION% (shared)
    echo.
    echo ============================================================================
) else (
    echo Installation verification failed (no DLLs found)!
    exit /b 1
)

echo.
echo Build completed successfully!
echo.
echo To use this FFmpeg in your CMake project (shared):
echo   set FFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX%
echo   or pass -DFFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX% to cmake
echo.
echo Note: For runtime, ensure %FFMPEG_INSTALL_PREFIX%\bin is in your PATH or bundle the DLLs with your application.

cd /d "%SCRIPT_DIR%"

echo.
echo ============================================================================
echo FFmpeg build completed successfully!
echo ============================================================================
echo Installation directory: %FFMPEG_INSTALL_PREFIX%
echo.
echo To use this FFmpeg build, set in your environment:
echo   set FFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX%
echo.
echo Or in CMake:
echo   cmake -DFFMPEG_PREFIX=%FFMPEG_INSTALL_PREFIX% ...
echo ============================================================================

exit /b 0