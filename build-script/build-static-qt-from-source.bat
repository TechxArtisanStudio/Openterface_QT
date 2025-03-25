@echo on
REM To install OpenTerface QT, you can run this script as an administrator.

setlocal enabledelayedexpansion

REM Configuration
set QT_VERSION=6.5.3
set QT_MAJOR_VERSION=6.5
set INSTALL_PREFIX=C:\Qt6
set BUILD_DIR=%cd%\qt-build
set MODULES=qtbase qtshadertools qtmultimedia qtsvg qtserialport qttools
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

REM Build other modules (including qttools)
for %%m in (%MODULES%) do (
    if /I not "%%m"=="qtbase" (
        cd "%BUILD_DIR%\%%m"
        mkdir build
        cd build
        cmake -G "Ninja" ^
            -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
            -DCMAKE_PREFIX_PATH="%INSTALL_PREFIX%" ^
            -DBUILD_SHARED_LIBS=OFF ^
            ..
        ninja
        ninja install
    )
)

REM Quick fix: Add -loleaut32 to qnetworklistmanager.prl
set PRL_FILE=%INSTALL_PREFIX%\plugins\networkinformation\qnetworklistmanager.prl
if exist "%PRL_FILE%" (
    echo Updating %PRL_FILE% to include -loleaut32...
    echo QMAKE_PRL_LIBS += -loleaut32 >> "%PRL_FILE%"
) else (
    echo Warning: %PRL_FILE% not found. Please check the build process.
)

REM Verify lupdate
if exist "%INSTALL_PREFIX%\bin\lupdate.exe" (
    echo lupdate.exe successfully built!
) else (
    echo Error: lupdate.exe not found in %INSTALL_PREFIX%\bin
    exit /b 1
)