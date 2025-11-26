@echo off
REM ============================================================================
REM Static FFmpeg Build Script for Windows using External MinGW + MSYS2 (MinGW64)
REM Uses a wrapper script to avoid quoting issues in GitHub Actions
REM ============================================================================

setlocal enabledelayedexpansion

REM === Configuration ===
if not defined MSYS2_ROOT set MSYS2_ROOT=C:\msys64
if not defined EXTERNAL_MINGW set EXTERNAL_MINGW=C:\mingw64
if not defined FFMPEG_VERSION set FFMPEG_VERSION=6.1.1
if not defined LIBJPEG_TURBO_VERSION set LIBJPEG_TURBO_VERSION=3.0.4
if not defined FFMPEG_INSTALL_PREFIX set FFMPEG_INSTALL_PREFIX=C:\ffmpeg-static

set BUILD_DIR=%cd%\ffmpeg-build-temp
set SCRIPT_DIR=%~dp0

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [92m============================================================================[0m
echo [92mFFmpeg Static Build with QSV + CUDA Support[0m
echo [92mUsing External MinGW and MSYS2 MinGW64 Environment[0m
echo [92m============================================================================[0m

REM === Validate paths ===
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo [91mError: MSYS2 not found at %MSYS2_ROOT%[0m
    exit /b 1
)
if not exist "%EXTERNAL_MINGW%\bin\gcc.exe" (
    echo [91mError: External MinGW not found at %EXTERNAL_MINGW%[0m
    exit /b 1
)
if not exist "%SCRIPT_DIR%build-static-ffmpeg-windows.sh" (
    echo [91mError: Bash script not found[0m
    exit /b 1
)

echo [92m✓ MSYS2: %MSYS2_ROOT%[0m
echo [92m✓ External MinGW: %EXTERNAL_MINGW%[0m
echo [92m✓ Install Prefix: %FFMPEG_INSTALL_PREFIX%[0m

REM === Convert to MSYS path format (/c/...) ===
call :win_to_msys "%SCRIPT_DIR%build-static-ffmpeg-windows.sh" MAIN_SCRIPT_MSYS
call :win_to_msys "%EXTERNAL_MINGW%" EXTERNAL_MINGW_MSYS
call :win_to_msys "%FFMPEG_INSTALL_PREFIX%" FFMPEG_INSTALL_PREFIX_MSYS

REM === Create wrapper script ===
set WRAPPER=%BUILD_DIR%\wrapper.sh
(
    echo #!/bin/bash
    echo export EXTERNAL_MINGW="%EXTERNAL_MINGW_MSYS%"
    echo export FFMPEG_INSTALL_PREFIX="%FFMPEG_INSTALL_PREFIX_MSYS%"
    echo export FFMPEG_VERSION="%FFMPEG_VERSION%"
    echo export LIBJPEG_TURBO_VERSION="%LIBJPEG_TURBO_VERSION%"
    echo exec bash "%MAIN_SCRIPT_MSYS%"
) > "%WRAPPER%"

call :win_to_msys "%WRAPPER%" WRAPPER_MSYS

echo [92mLaunching MSYS2 MinGW64 environment...[0m
echo [93mThis may take 45-90 minutes.[0m

REM IMPORTANT: Use -mingw64, NOT -msys!
"%MSYS2_ROOT%\msys2_shell.cmd" -mingw64 -defterm -no-start -here -c "bash %WRAPPER_MSYS%"

if %errorlevel% equ 0 (
    echo.
    echo [92m============================================================================[0m
    echo [92m✅ FFmpeg build succeeded![0m
    echo [92mInstallation: %FFMPEG_INSTALL_PREFIX%[0m
    echo [92m============================================================================[0m
) else (
    echo [91m❌ FFmpeg build failed! Exit code: %errorlevel%[0m
    exit /b 1
)
exit /b 0

:win_to_msys
set "p=%~1"
set "p=%p:\=/%"
for /f "tokens=1,2 delims=:" %%a in ("%p%") do set "res=/%%a%%b"
set "res=%res:A=a%"
set "res=%res:B=b%"
set "res=%res:C=c%"
set "res=%res:D=d%"
set "res=%res:E=e%"
set "res=%res:F=f%"
endlocal & set "%2=%res%"
goto :eof