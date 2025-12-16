@echo off
REM ============================================================================
REM Windows CMake Build with Static FFmpeg
REM ============================================================================

setlocal

REM Set FFmpeg prefix (adjust if you installed elsewhere)
set FFMPEG_PREFIX=C:\ffmpeg-static

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
set OUTPUT_DIR=build/Release

echo ============================================================================
echo Building Openterface_QT with Static FFmpeg
echo ============================================================================
echo FFmpeg Path: %FFMPEG_PREFIX%
echo Output Directory: %OUTPUT_DIR%
echo ============================================================================
echo.

REM Check if FFmpeg is installed
if not exist "%FFMPEG_PREFIX%\lib\libavformat.a" (
    echo Error: FFmpeg static libraries not found at %FFMPEG_PREFIX%
    echo.
    echo Please build FFmpeg first by running:
    echo   .\build-script\build-static-ffmpeg-windows.bat
    echo.
    exit /b 1
)

echo ✓ FFmpeg libraries found
echo.

REM Create build directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Run CMake from source directory
echo Running CMake configuration...
cmake -B "%OUTPUT_DIR%" -S . -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM=%CMAKE_MAKE_PROGRAM% ^
    -DCMAKE_C_COMPILER=%CMAKE_C_COMPILER% ^
    -DCMAKE_CXX_COMPILER=%CMAKE_CXX_COMPILER% ^
    -DFFMPEG_PREFIX=%FFMPEG_PREFIX%

if %errorlevel% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

echo.
echo CMake configuration successful!
echo.

REM Build the project
echo Building project...
cmake --build "%OUTPUT_DIR%" --config Release -j4

if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

echo.
echo ============================================================================
echo Build completed successfully!
echo ============================================================================
echo.
echo Executable location: %OUTPUT_DIR%\openterfaceQT.exe
echo.

exit /b 0