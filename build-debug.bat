@echo off
REM ============================================================================
REM Windows CMake Build - choose Static or Shared FFmpeg
REM ============================================================================

setlocal

REM Usage: build-debug.bat [static|shared]
REM Default: static

REM Parse first argument to select FFmpeg type
set USE_SHARED=0
if /I "%~1"=="shared" set USE_SHARED=1
if /I "%~1"=="-shared" set USE_SHARED=1
if /I "%~1"=="/shared" set USE_SHARED=1
if /I "%~1"=="s" set USE_SHARED=1

REM Set default FFmpeg prefix based on selection (can be overridden by environment)
if "%USE_SHARED%"=="1" (
    if not defined FFMPEG_PREFIX set FFMPEG_PREFIX=C:\ffmpeg-shared
) else (
    if not defined FFMPEG_PREFIX set FFMPEG_PREFIX=C:\ffmpeg-static
)

REM Set MinGW path (adjust if Qt is installed elsewhere)
set MINGW_PATH=E:\Qt\Tools\mingw1120_64
set CMAKE_PATH=E:\Qt\Tools\CMake_64\bin

REM Add MinGW and CMake to PATH
set PATH=%MINGW_PATH%\bin;%CMAKE_PATH%;%PATH%

REM Set CMake generator and compiler
set CMAKE_MAKE_PROGRAM=%MINGW_PATH%\bin\mingw32-make.exe
set CMAKE_C_COMPILER=%MINGW_PATH%\bin\gcc.exe
set CMAKE_CXX_COMPILER=%MINGW_PATH%\bin\g++.exe

REM Set output directory (change this to your desired path)
set OUTPUT_DIR=build/Debug

if "%USE_SHARED%"=="1" (
    echo ============================================================================
    echo Building Openterface_QT with SHARED FFmpeg
    echo ============================================================================
) else (
    echo ============================================================================
    echo Building Openterface_QT with STATIC FFmpeg
    echo ============================================================================
)

echo FFmpeg Path: %FFMPEG_PREFIX%
echo Output Directory: %OUTPUT_DIR%
echo ============================================================================
echo.

REM Check if FFmpeg is installed (static vs shared)
if "%USE_SHARED%"=="1" goto :CHECK_SHARED
goto :CHECK_STATIC

:CHECK_SHARED
echo Checking for shared FFmpeg libraries...
if exist "%FFMPEG_PREFIX%\lib\libavformat.dll.a" (
    echo ✓ Found import lib: %FFMPEG_PREFIX%\lib\libavformat.dll.a
    goto :CHECK_LIBUSB
)

rem Try to find DLLs under bin
set FOUND_DLLS=0
dir /b "%FFMPEG_PREFIX%\bin\avformat-*.dll" >nul 2>nul
if errorlevel 0 set FOUND_DLLS=1
if "%FOUND_DLLS%"=="1" (
    echo ✓ Found FFmpeg DLL(s) under %FFMPEG_PREFIX%\bin
    goto :CHECK_LIBUSB
) else (
    echo Error: FFmpeg shared libraries not found at %FFMPEG_PREFIX%
    echo.
    echo Please install or build shared FFmpeg (e.g. into %FFMPEG_PREFIX%)
    echo.
    exit /b 1
)

:CHECK_STATIC
echo Checking for static FFmpeg libraries...
if not exist "%FFMPEG_PREFIX%\lib\libavformat.a" (
    echo Error: FFmpeg static libraries not found at %FFMPEG_PREFIX%
    echo.
    echo Please build FFmpeg first by running:
    echo   .\build-script\build-static-ffmpeg-windows.bat
    echo.
    exit /b 1
)
echo ✓ FFmpeg static libraries found

:CHECK_LIBUSB
echo Checking for libusb installation...
set LIBUSB_FOUND=0
if exist "%CD%\lib\libusb-1.0.a" set LIBUSB_FOUND=1
if exist "%CD%\lib\libusb-1.0.dll.a" set LIBUSB_FOUND=1
if exist "C:\libusb\lib\libusb-1.0.a" set LIBUSB_FOUND=1
if exist "C:\libusb\lib\libusb-1.0.dll.a" set LIBUSB_FOUND=1
if exist "C:\libusb\bin\libusb-1.0.dll" set LIBUSB_FOUND=1
if "%LIBUSB_FOUND%"=="1" (
    echo ✓ libusb found
    goto :AFTER_CHECK
)

echo libusb not found in project or C:\libusb. Attempting automatic install to C:\libusb...
call "%~dp0build-script\install-libusb-windows.bat" "C:\libusb"
if %errorlevel% neq 0 (
    echo Auto-install of libusb failed or aborted. Please install libusb to C:\libusb manually and re-run this script.
    exit /b 1
)

rem Re-check after install
if exist "C:\libusb\include\libusb-1.0\libusb.h" (
    echo ✓ libusb now installed at C:\libusb
) else (
    echo Failed to find libusb after installation. Please verify C:\libusb contains include\libusb-1.0 and lib/ or bin/ with libusb files.
    exit /b 1
)

:AFTER_CHECK

echo.

REM Create build directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Run CMake from source directory
echo Running CMake configuration...

REM Pass USE_SHARED_FFMPEG to CMake as ON/OFF
if "%USE_SHARED%"=="1" (
    set CM_USE_SHARED_FFMPEG=ON
) else (
    set CM_USE_SHARED_FFMPEG=OFF
)

cmake -B "%OUTPUT_DIR%" -S . -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DHAVE_LIBJPEG_TURBO=ON ^
    -DCMAKE_MAKE_PROGRAM=%CMAKE_MAKE_PROGRAM% ^
    -DCMAKE_C_COMPILER=%CMAKE_C_COMPILER% ^
    -DCMAKE_CXX_COMPILER=%CMAKE_CXX_COMPILER% ^
    -DFFMPEG_PREFIX=%FFMPEG_PREFIX% ^
    -DUSE_SHARED_FFMPEG=%CM_USE_SHARED_FFMPEG%

if %errorlevel% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo.
echo CMake configuration successful!
echo.

REM Build the project
echo Building project...
cmake --build "%OUTPUT_DIR%" --config Debug

if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

rem After successful build, copy FFmpeg DLLs and Qt platform plugins when using shared FFmpeg
if "%USE_SHARED%"=="1" (
    echo Copying FFmpeg shared DLLs to %OUTPUT_DIR%...
    xcopy /Y "%FFMPEG_PREFIX%\bin\avcodec-*.dll" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: avcodec DLLs not found
    xcopy /Y "%FFMPEG_PREFIX%\bin\avformat-*.dll" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: avformat DLLs not found
    xcopy /Y "%FFMPEG_PREFIX%\bin\avutil-*.dll" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: avutil DLLs not found
    xcopy /Y "%FFMPEG_PREFIX%\bin\avdevice-*.dll" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: avdevice DLLs not found
    xcopy /Y "%FFMPEG_PREFIX%\bin\sw*.dll" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: sw*.dll not found
)


rem Copy MinGW runtime DLLs that may be required for Qt/platform plugin compatibility
for %%d in (libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll) do (
    if exist "%MINGW_PATH%\bin\%%d" (
        echo Copying %%d to %OUTPUT_DIR%...
        xcopy /Y "%MINGW_PATH%\bin\%%d" "%OUTPUT_DIR%\" >nul 2>nul || echo Warning: couldn't copy %%d
    )
)

echo.
echo ============================================================================
echo Build completed successfully!
echo ============================================================================
echo.
echo Executable location: %OUTPUT_DIR%\openterfaceQT.exe
echo.

exit /b 0