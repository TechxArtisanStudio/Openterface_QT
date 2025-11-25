@echo on
REM To install OpenTerface QT with static OpenSSL support, you can run this script as an administrator.

setlocal enabledelayedexpansion

REM Configuration
set QT_VERSION=6.6.3
set QT_MAJOR_VERSION=6.6
set INSTALL_PREFIX=C:\Qt6
set BUILD_DIR=%cd%\qt-build
set MODULES=qtbase qtdeclarative qtshadertools qtmultimedia qtsvg qtserialport qttools
set DOWNLOAD_BASE_URL=https://download.qt.io/archive/qt/%QT_MAJOR_VERSION%/%QT_VERSION%/submodules
set VCPKG_DIR=C:\vcpkg
set OPENSSL_DIR=%VCPKG_DIR%\installed\x64-mingw-static
set OPENSSL_LIB_DIR=%OPENSSL_DIR%\lib
set OPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include

set PATH=C:\ProgramData\chocolatey\bin;C:\ProgramData\chocolatey\lib\ninja\tools;C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

REM Install dependencies with vcpkg
"%VCPKG_DIR%\vcpkg.exe" install openssl:x64-mingw-static zlib:x64-mingw-static

REM Check for Ninja
where ninja >nul 2>nul
if %errorlevel% neq 0 (
    echo Ninja is not installed. Please install Ninja and ensure it is in your PATH.
    exit /b 1
)

REM Check for OpenSSL static libraries
dir "%OPENSSL_LIB_DIR%"
if not exist "%OPENSSL_LIB_DIR%\libcrypto.a" (
    echo OpenSSL static library libcrypto.a not found in %OPENSSL_LIB_DIR%. Please install OpenSSL static libraries.
    exit /b 1
)
if not exist "%OPENSSL_LIB_DIR%\libssl.a" (
    echo OpenSSL static library libssl.a not found in %OPENSSL_LIB_DIR%. Please install OpenSSL static libraries.
    exit /b 1
)

REM Check for zlib static library
if not exist "%OPENSSL_LIB_DIR%\libzlib.a" (
    echo zlib static library libzlib.a not found in %OPENSSL_LIB_DIR%. Please install zlib static libraries.
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
    -DFEATURE_openssl=ON ^
    -DFEATURE_openssl_linked=ON ^
    -DFEATURE_static_runtime=ON ^
    -DFEATURE_zlib=system ^
    -DOPENSSL_ROOT_DIR="%OPENSSL_DIR%" ^
    -DOPENSSL_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
    -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIB_DIR%\libcrypto.a" ^
    -DOPENSSL_SSL_LIBRARY="%OPENSSL_LIB_DIR%\libssl.a" ^
    -DZLIB_LIBRARY="%OPENSSL_LIB_DIR%\libz.a" ^
    -DZLIB_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
    -DCMAKE_C_FLAGS="-I%OPENSSL_INCLUDE_DIR%" ^
    -DCMAKE_CXX_FLAGS="-I%OPENSSL_INCLUDE_DIR%" ^
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--subsystem,console -L%OPENSSL_LIB_DIR% -lws2_32 -lcrypt32 -ladvapi32 -lz" ^
    -DCMAKE_C_COMPILER=gcc ^
    -DCMAKE_CXX_COMPILER=g++ ^
    -DFEATURE_libb2=OFF ^
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
            -DFEATURE_static_runtime=ON ^
            -DFEATURE_zlib=system ^
            -DOPENSSL_ROOT_DIR="%OPENSSL_DIR%" ^
            -DOPENSSL_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
            -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIB_DIR%\libcrypto.a" ^
            -DOPENSSL_SSL_LIBRARY="%OPENSSL_LIB_DIR%\libssl.a" ^
            -DZLIB_LIBRARY="%OPENSSL_LIB_DIR%\libzlib.a" ^
            -DZLIB_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
            -DCMAKE_C_FLAGS="-mconsole -I%OPENSSL_INCLUDE_DIR%" ^
            -DCMAKE_CXX_FLAGS="-mconsole -I%OPENSSL_INCLUDE_DIR%" ^
            -DCMAKE_EXE_LINKER_FLAGS="-Wl,--subsystem,console -L%OPENSSL_LIB_DIR%" ^

            -DCMAKE_C_COMPILER=gcc ^

            -DCMAKE_CXX_COMPILER=g++ ^
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

REM Verify Qt configuration includes OpenSSL support
echo Verifying Qt OpenSSL configuration...
if exist "%INSTALL_PREFIX%\bin\qmake.exe" (
    "%INSTALL_PREFIX%\bin\qmake.exe" -query QT_INSTALL_LIBS
    echo Checking for OpenSSL feature in Qt...
    findstr /C:"openssl" "%INSTALL_PREFIX%\mkspecs\qconfig.pri" && echo "Qt built with OpenSSL support" || echo "Warning: OpenSSL support not detected in Qt configuration"
) else (
    echo Error: qmake.exe not found in %INSTALL_PREFIX%\bin
    exit /b 1
)

echo Qt static build with OpenSSL completed successfully!
