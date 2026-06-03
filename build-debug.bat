@echo off
REM ============================================================================
REM Windows CMake Build - choose Static or Shared FFmpeg
REM Usage: build-debug.bat [static|shared]
REM        default is static if no argument provided
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
if /I "%~1"=="static" set USE_SHARED=0
if /I "%~1"=="-static" set USE_SHARED=0
if /I "%~1"=="/static" set USE_SHARED=0
if /I "%~1"=="t" set USE_SHARED=0

REM Set default FFmpeg prefix based on selection (can be overridden by environment)
if "%USE_SHARED%"=="1" (
    if not defined FFMPEG_PREFIX set FFMPEG_PREFIX=C:\ffmpeg-shared
) else (
    if not defined FFMPEG_PREFIX set FFMPEG_PREFIX=C:\ffmpeg-static
)

REM Set Qt path (used by windeployqt)
if not defined QT_DIR (
    if exist "E:\Qt\6.5.3\mingw_64\bin\windeployqt.exe" (
        set QT_DIR=E:\Qt\6.5.3\mingw_64
    ) else if exist "E:\Qt\6.6.3\mingw_64\bin\windeployqt.exe" (
        set QT_DIR=E:\Qt\6.6.3\mingw_64
    )
)

REM Set MinGW path (adjust if Qt is installed elsewhere)
REM If the environment variable is already defined, keep it; otherwise use default
if not defined MINGW_PATH set MINGW_PATH=E:\Qt\Tools\mingw1120_64
if not defined CMAKE_PATH set CMAKE_PATH=E:\Qt\Tools\CMake_64\bin

REM Align with CI workflow: always prefer Qt-compatible MinGW 11.2.0 toolchain.
if exist "E:\Qt\Tools\mingw1120_64\bin\gcc.exe" set MINGW_PATH=E:\Qt\Tools\mingw1120_64

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
    goto :CHECK_SHARED_COMPAT
)

rem Try to find DLLs under bin
set FOUND_DLLS=0
dir /b "%FFMPEG_PREFIX%\bin\avformat-*.dll" >nul 2>nul
if errorlevel 0 set FOUND_DLLS=1
if "%FOUND_DLLS%"=="1" (
    echo ✓ Found FFmpeg DLL(s) under %FFMPEG_PREFIX%\bin
    goto :CHECK_SHARED_COMPAT
) else (
    echo Error: FFmpeg shared libraries not found at %FFMPEG_PREFIX%
    echo.
    echo Please install or build shared FFmpeg (e.g. into %FFMPEG_PREFIX%)
    echo.
    exit /b 1
)

:CHECK_SHARED_COMPAT
rem Guard against Qt MinGW + MSYS2 shared FFmpeg runtime mismatch.
rem If avutil needs clock_gettime64, shared mode usually requires newer
rem libwinpthread that conflicts with Qt6Multimedia (__emutls). Fallback to static.
set NEEDS_64TIME=0
for %%F in ("%FFMPEG_PREFIX%\bin\avutil-*.dll") do (
    "%MINGW_PATH%\bin\objdump.exe" -p "%%~fF" | findstr /I "clock_gettime64" >nul 2>nul
    if not errorlevel 1 set NEEDS_64TIME=1
)
if "%NEEDS_64TIME%"=="1" (
    echo.
    echo ============================================================================
    echo WARNING: Detected shared FFmpeg requiring clock_gettime64.
    echo This conflicts with Qt MinGW runtime used by Qt6Multimedia (__emutls).
    echo Auto-fallback to STATIC FFmpeg mode to avoid startup entry-point errors.
    echo ============================================================================
    set USE_SHARED=0
    set FFMPEG_PREFIX=C:\ffmpeg-static
    goto :CHECK_STATIC
)
goto :CHECK_LIBUSB

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
goto :CHECK_LIBUSB

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
cmake --build "%OUTPUT_DIR%" --config Debug -- -j 4

if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

rem After successful build, copy FFmpeg DLLs and Qt platform plugins when using shared FFmpeg
if "%USE_SHARED%"=="1" (
    echo.
    echo ============================================================================
    echo Copying required DLLs to %OUTPUT_DIR%...
    echo ============================================================================

    REM Copy all FFmpeg DLLs
    echo Copying FFmpeg DLLs...
    if exist "%FFMPEG_PREFIX%\bin\avcodec-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\avcodec-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\avformat-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\avformat-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\avutil-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\avutil-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\avdevice-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\avdevice-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\avfilter-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\avfilter-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\swscale-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\swscale-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\swresample-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\swresample-*.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\postproc-*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\postproc-*.dll" "%OUTPUT_DIR%\" >nul 2>nul

    REM Copy libjpeg-turbo DLLs if they exist
    echo Copying libjpeg-turbo DLLs if available...
    if exist "%FFMPEG_PREFIX%\bin\libturbojpeg.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\libturbojpeg.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%FFMPEG_PREFIX%\bin\*jpeg*.dll" xcopy /Y "%FFMPEG_PREFIX%\bin\*jpeg*.dll" "%OUTPUT_DIR%\" >nul 2>nul

    REM Copy libusb DLL if it exists
    echo Copying libusb DLL...
    if exist "C:\libusb\bin\libusb-1.0.dll" xcopy /Y "C:\libusb\bin\libusb-1.0.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "C:\libusb\lib\libusb-1.0.dll" xcopy /Y "C:\libusb\lib\libusb-1.0.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%CD%\lib\libusb-1.0.dll" xcopy /Y "%CD%\lib\libusb-1.0.dll" "%OUTPUT_DIR%\" >nul 2>nul

    REM Copy MSYS2/MinGW compression dependencies (non-C++ runtime)
    echo Copying compression and runtime libraries...
    if exist "C:\msys64\mingw64\bin\libiconv-2.dll" xcopy /Y "C:\msys64\mingw64\bin\libiconv-2.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "C:\msys64\mingw64\bin\libbz2-1.dll" xcopy /Y "C:\msys64\mingw64\bin\libbz2-1.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "C:\msys64\mingw64\bin\liblzma-5.dll" xcopy /Y "C:\msys64\mingw64\bin\liblzma-5.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "C:\msys64\mingw64\bin\zlib1.dll" xcopy /Y "C:\msys64\mingw64\bin\zlib1.dll" "%OUTPUT_DIR%\" >nul 2>nul

    REM Copy hardware acceleration libraries (Intel QSV, etc.)
    echo Copying hardware acceleration libraries...
    if exist "C:\msys64\mingw64\bin\libmfx-1.dll" xcopy /Y "C:\msys64\mingw64\bin\libmfx-1.dll" "%OUTPUT_DIR%\" >nul 2>nul

    REM Use Qt MinGW runtime to stay ABI/TLS-compatible with deployed Qt DLLs.
    echo Copying MinGW runtime DLLs from Qt MinGW - ABI matched to Qt DLLs...
    if exist "%MINGW_PATH%\bin\libwinpthread-1.dll" (
        xcopy /Y "%MINGW_PATH%\bin\libwinpthread-1.dll" "%OUTPUT_DIR%\" >nul 2>nul
        echo   Copied libwinpthread-1.dll from Qt MinGW
    )
    if exist "%MINGW_PATH%\bin\libgcc_s_seh-1.dll" (
        xcopy /Y "%MINGW_PATH%\bin\libgcc_s_seh-1.dll" "%OUTPUT_DIR%\" >nul 2>nul
        echo   Copied libgcc_s_seh-1.dll from Qt MinGW
    )
    if exist "%MINGW_PATH%\bin\libstdc++-6.dll" (
        xcopy /Y "%MINGW_PATH%\bin\libstdc++-6.dll" "%OUTPUT_DIR%\" >nul 2>nul
        echo   Copied libstdc++-6.dll from Qt MinGW
    )

    REM ============================================================
    REM Deploy Qt DLLs, platform plugins, and multimedia plugins
    REM This is the same step the CI workflow performs via windeployqt.
    REM Without it, Windows falls back to loading Qt DLLs from the
    REM Qt installation dir via PATH, which may not have the matching
    REM runtime DLLs in the same folder.
    REM ============================================================
    echo.
    echo ============================================================================
    echo Running windeployqt to deploy Qt DLLs and plugins...
    echo ============================================================================
    if exist "%QT_DIR%\bin\windeployqt.exe" (
        echo   Using windeployqt from %QT_DIR%\bin
        "%QT_DIR%\bin\windeployqt.exe" "%OUTPUT_DIR%\openterfaceQT.exe" --multimedia --network
        if errorlevel 1 (
            echo   WARNING: windeployqt returned error code %errorlevel%
        ) else (
            echo   windeployqt completed successfully
        )
    ) else (
        echo   WARNING: windeployqt.exe not found at %QT_DIR%\bin\windeployqt.exe
        echo   Qt DLLs will NOT be deployed. Application may fail to start if
        echo   Qt DLLs are not available via PATH or system directories.
        echo   Consider running windeployqt manually:
        echo     windeployqt %OUTPUT_DIR%\openterfaceQT.exe --multimedia --network
    )
    
    echo.
    echo ============================================================================
    echo Verifying copied DLLs...
    echo ============================================================================
    
    REM Verify critical FFmpeg DLLs are present
    set MISSING_DLLS=0
    dir /b "%OUTPUT_DIR%\avcodec-*.dll" >nul 2>nul
    if errorlevel 1 set MISSING_DLLS=1
    dir /b "%OUTPUT_DIR%\avformat-*.dll" >nul 2>nul
    if errorlevel 1 set MISSING_DLLS=1
    dir /b "%OUTPUT_DIR%\avutil-*.dll" >nul 2>nul
    if errorlevel 1 set MISSING_DLLS=1
    dir /b "%OUTPUT_DIR%\swscale-*.dll" >nul 2>nul
    if errorlevel 1 set MISSING_DLLS=1
    dir /b "%OUTPUT_DIR%\swresample-*.dll" >nul 2>nul
    if errorlevel 1 set MISSING_DLLS=1
    
    if "%MISSING_DLLS%"=="1" (
        echo.
        echo ============================================================================
        echo WARNING: Some required FFmpeg DLLs are missing from %OUTPUT_DIR%
        echo ============================================================================
        echo.
        echo Please verify that FFmpeg shared libraries are properly built at:
        echo   %FFMPEG_PREFIX%\bin
        echo.
        echo You can build shared FFmpeg by running:
        echo   .\build-script\build-shared-ffmpeg-windows.bat
        echo.
        echo Or download pre-built FFmpeg shared libraries from:
        echo   https://github.com/BtbN/FFmpeg-Builds/releases
        echo.
    ) else (
        echo All critical FFmpeg DLLs copied successfully
    )
    
    echo.
    echo ============================================================================
    echo DLL Dependency Summary
    echo ============================================================================
    echo.
    echo FFmpeg DLLs:
    dir /b "%OUTPUT_DIR%\av*.dll" 2>nul || echo   None found
    dir /b "%OUTPUT_DIR%\sw*.dll" 2>nul || echo   None found
    dir /b "%OUTPUT_DIR%\postproc*.dll" 2>nul
    echo.
    echo Compression Libraries:
    dir /b "%OUTPUT_DIR%\libiconv*.dll" 2>nul
    dir /b "%OUTPUT_DIR%\libbz2*.dll" 2>nul
    dir /b "%OUTPUT_DIR%\liblzma*.dll" 2>nul
    dir /b "%OUTPUT_DIR%\zlib*.dll" 2>nul
    echo.
    echo USB Libraries:
    dir /b "%OUTPUT_DIR%\libusb*.dll" 2>nul || echo   libusb not found - USB functions may not work
    echo.
    echo Hardware Acceleration:
    dir /b "%OUTPUT_DIR%\libmfx*.dll" 2>nul || echo   libmfx not found - Intel QSV hardware acceleration unavailable
    dir /b "%OUTPUT_DIR%\nvcuvid.dll" 2>nul
    echo.
    echo MinGW Runtime:
    dir /b "%OUTPUT_DIR%\libwinpthread*.dll" 2>nul
    dir /b "%OUTPUT_DIR%\libgcc*.dll" 2>nul
    dir /b "%OUTPUT_DIR%\libstdc++*.dll" 2>nul
    echo.
    echo JPEG Libraries:
    dir /b "%OUTPUT_DIR%\*jpeg*.dll" 2>nul
    echo.
    echo Total DLL count: 
    dir /b "%OUTPUT_DIR%\*.dll" 2>nul | find /c ".dll" || echo   0
    
    echo.
)

rem Static mode deployment/cleanup: ensure no stale shared FFmpeg or mismatched runtime DLLs.
if "%USE_SHARED%"=="0" (
    echo.
    echo ============================================================================
    echo Static mode: cleaning stale shared FFmpeg DLLs and deploying Qt runtime...
    echo ============================================================================

    del /Q "%OUTPUT_DIR%\av*.dll" >nul 2>nul
    del /Q "%OUTPUT_DIR%\sw*.dll" >nul 2>nul
    del /Q "%OUTPUT_DIR%\postproc*.dll" >nul 2>nul

    if exist "%QT_DIR%\bin\windeployqt.exe" (
        "%QT_DIR%\bin\windeployqt.exe" "%OUTPUT_DIR%\openterfaceQT.exe" --multimedia --network
        if errorlevel 1 (
            echo WARNING: windeployqt returned error code %errorlevel% in static mode
        )
    )

    del /Q "%OUTPUT_DIR%\libwinpthread-1.dll" >nul 2>nul
    del /Q "%OUTPUT_DIR%\libgcc_s_seh-1.dll" >nul 2>nul
    del /Q "%OUTPUT_DIR%\libstdc++-6.dll" >nul 2>nul
    if exist "%MINGW_PATH%\bin\libwinpthread-1.dll" xcopy /Y "%MINGW_PATH%\bin\libwinpthread-1.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%MINGW_PATH%\bin\libgcc_s_seh-1.dll" xcopy /Y "%MINGW_PATH%\bin\libgcc_s_seh-1.dll" "%OUTPUT_DIR%\" >nul 2>nul
    if exist "%MINGW_PATH%\bin\libstdc++-6.dll" xcopy /Y "%MINGW_PATH%\bin\libstdc++-6.dll" "%OUTPUT_DIR%\" >nul 2>nul
)

echo.
echo ============================================================================
echo Build completed successfully!
echo ============================================================================
echo.
echo Executable location: %OUTPUT_DIR%\openterfaceQT.exe
echo.

exit /b 0