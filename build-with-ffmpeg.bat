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

echo ============================================================================
echo Building Openterface_QT with Static FFmpeg
echo ============================================================================
echo FFmpeg Path: %FFMPEG_PREFIX%
echo ============================================================================
echo.

REM Check if FFmpeg is installed
if not exist "%FFMPEG_PREFIX%\lib\libavformat.a" (
    echo [91mError: FFmpeg static libraries not found at %FFMPEG_PREFIX%[0m
    echo.
    echo Please build FFmpeg first by running:
    echo   .\build-script\build-static-ffmpeg-windows.bat
    echo.
    exit /b 1
)

echo [92m✓ FFmpeg libraries found[0m
echo.

REM Create build directory
if not exist "build" mkdir build
cd build

REM Run CMake
echo Running CMake configuration...
cmake .. -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM=%CMAKE_MAKE_PROGRAM% ^
    -DCMAKE_C_COMPILER=%CMAKE_C_COMPILER% ^
    -DCMAKE_CXX_COMPILER=%CMAKE_CXX_COMPILER% ^
    -DFFMPEG_PREFIX=%FFMPEG_PREFIX%

if %errorlevel% neq 0 (
    echo [91mCMake configuration failed![0m
    cd ..
    exit /b 1
)

echo.
echo [92mCMake configuration successful![0m
echo.

REM Build the project
echo Building project...
cmake --build . --config Release

if %errorlevel% neq 0 (
    echo [91mBuild failed![0m
    cd ..
    exit /b 1
)

echo.
echo [92m============================================================================[0m
echo [92mBuild completed successfully![0m
echo [92m============================================================================[0m
echo.
echo Executable location: build\openterfaceQT.exe
echo.

cd ..
exit /b 0
