@echo on
REM To install OpenTerface QT, you can run this script as an administrator.

setlocal enabledelayedexpansion

REM Configuration
set QT_VERSION=6.5.3
set QT_MAJOR_VERSION=6.5
set INSTALL_PREFIX=C:\Qt6
set BUILD_DIR=%cd%\qt-build
set MODULES=qtbase qtshadertools qtmultimedia qtsvg qtserialport
set DOWNLOAD_BASE_URL=https://download.qt.io/archive/qt/%QT_MAJOR_VERSION%/%QT_VERSION%/submodules

set PATH=C:\ProgramData\chocolatey\bin;C:\ProgramData\chocolatey\lib\ninja\tools;C:\ProgramData\chocolatey\lib\mingw\tools\mingw64\bin;%PATH%

REM Check for Ninja
where ninja >nul 2>nul
if %errorlevel% neq 0 (
    echo Ninja is not installed. Please install Ninja and ensure it is in your PATH.
    exit /b 1
)

REM Create build directory
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

REM Download and extract modules
for %%m in (%MODULES%) do (
    if not exist "%%m" (
        curl -L -o "%%m.zip" "%DOWNLOAD_BASE_URL%/%%m-everywhere-src-%QT_VERSION%.zip"
        powershell -command "Expand-Archive -Path %%m.zip -DestinationPath ."
        move "%%m-everywhere-src-%QT_VERSION%" "%%m"
        del "%%m.zip"
    )
)

REM Build qtbase first
cd "%BUILD_DIR%\qtbase"
mkdir build
cd build
cmake -G "Ninja" ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DFEATURE_dbus=ON ^
    -DFEATURE_sql=OFF ^
    -DFEATURE_testlib=OFF ^
    -DFEATURE_icu=OFF ^
    -DFEATURE_opengl=ON ^
    ..

ninja
ninja install

REM Build qtshadertools
cd "%BUILD_DIR%\qtshadertools"
mkdir build
cd build
cmake -G "Ninja" ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
    -DCMAKE_PREFIX_PATH="%INSTALL_PREFIX%" ^
    -DBUILD_SHARED_LIBS=OFF ^
    ..

ninja
ninja install

REM Build other modules
for %%m in (%MODULES%) do (
    if /I not "%%m"=="qtbase" (
        if /I not "%%m"=="qtshadertools" (
            cd "%BUILD_DIR%\%%m"
            mkdir build
            cd build
            
            if /I "%%m"=="qtmultimedia" (
                REM Special configuration for qtmultimedia to enable FFmpeg
                cmake -G "Ninja" ^
                    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
                    -DCMAKE_PREFIX_PATH="%INSTALL_PREFIX%" ^
                    -DBUILD_SHARED_LIBS=OFF ^
                    -DFEATURE_ffmpeg=ON ^
                    -DFFmpeg_DIR="%BUILD_DIR%\FFmpeg-n6.1.1" ^
                    -DFEATURE_pulseaudio=ON ^
                    ..
            ) else (
                cmake -G "Ninja" ^
                    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
                    -DCMAKE_PREFIX_PATH="%INSTALL_PREFIX%" ^
                    -DBUILD_SHARED_LIBS=OFF ^
                    ..
            )
            
            ninja
            ninja install
        )
    )
)