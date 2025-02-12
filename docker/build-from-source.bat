@echo off
REM To install OpenTerface QT, you can run this script as an administrator.

setlocal enabledelayedexpansion

REM Configuration
set QT_VERSION=6.5.3
set QT_MAJOR_VERSION=6.5
set INSTALL_PREFIX=C:\Qt6
set BUILD_DIR=%cd%\qt-build
set MODULES=qtbase qtshadertools qtmultimedia qtsvg qtserialport
set DOWNLOAD_BASE_URL=https://download.qt.io/archive/qt/%QT_MAJOR_VERSION%/%QT_VERSION%/submodules

REM Update and install dependencies
REM Note: You may need to install dependencies manually or use a package manager like vcpkg or Chocolatey.

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
                    -DFEATURE_ffmpeg=ON ^
                    -DFFmpeg_DIR="%BUILD_DIR%\FFmpeg-n6.1.1" ^
                    -DFEATURE_pulseaudio=ON ^
                    ..
            ) else (
                cmake -G "Ninja" ^
                    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
                    -DCMAKE_PREFIX_PATH="%INSTALL_PREFIX%" ^
                    ..
            )
            
            ninja
            ninja install
        )
    )
)

REM Download latest release and build openterfaceQT from source
set PATH=%INSTALL_PREFIX%\bin;%PATH%

cd "%BUILD_DIR%"
if not exist "Openterface_QT" (
    git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
)

cd Openterface_QT
mkdir build
cd build
qmake ..
make
make install

REM Clean up all the build folder
echo Cleaning the build folder...
rmdir /s /q "%BUILD_DIR%"

REM Print instructions for running the program
echo.
echo ==========================================================================
echo Build completed successfully! 
echo.
echo To run OpenTerface QT:
echo 1. First, ensure you have the necessary permissions:
echo    - Add yourself to the dialout group (for serial port access):
echo      net localgroup dialout %USERNAME% /add
echo.
echo    - Set up hidraw permissions (for USB device access):
echo      echo KERNEL== "hidraw*", SUBSYSTEM=="hidraw", MODE="0666" > C:\Windows\System32\config\systemprofile\51-openterface.rules
echo.
echo 2. You may need to log out and log back in for the group changes to take effect.
echo.
echo 3. You can now run OpenTerface QT by typing:
echo    openterfaceQT
echo.
echo Note: If you experience issues controlling mouse and keyboard:
echo - Try removing brltty: choco uninstall brltty
echo - Unplug and replug your OpenTerface device
echo - Check if the serial port is recognized: dir /b \\.\ttyUSB*
echo ==========================================================================