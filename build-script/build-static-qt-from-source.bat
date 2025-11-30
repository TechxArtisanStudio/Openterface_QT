@echo on
REM To install OpenTerface QT with static OpenSSL support, you can run this script as an administrator.

setlocal enabledelayedexpansion

REM Configuration
set QT_VERSION=6.5.3
set QT_MAJOR_VERSION=6.5
set INSTALL_PREFIX=C:\Qt6
set BUILD_DIR=%cd%\qt-build
set MODULES=qtbase qtshadertools qtmultimedia qtsvg qtserialport qttools
set DOWNLOAD_BASE_URL=https://download.qt.io/archive/qt/%QT_MAJOR_VERSION%/%QT_VERSION%/submodules
set VCPKG_DIR=D:\vcpkg

REM Allow openssl to come from either the central vcpkg installation or a
REM repo-local manifest install (vcpkg_installed). Prefer central vcpkg but
REM fall back to repo-local to avoid CI failures when vcpkg copies installs
REM into the repository instead of the shared vcpkg folder.
set OPENSSL_DIR=%VCPKG_DIR%\installed\x64-mingw-static
set OPENSSL_LIB_DIR=%OPENSSL_DIR%\lib
set OPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include

REM If the central vcpkg installed libraries are missing, try a repo-local manifest
if not exist "%OPENSSL_LIB_DIR%\" (
    echo INFO: Central vcpkg path "%OPENSSL_LIB_DIR%" not found.
    set "REPO_OPENSSL_DIR="
    if defined SOURCE_DIR (
        set "REPO_OPENSSL_DIR=%SOURCE_DIR%\vcpkg_installed\x64-mingw-static"
    ) else (
        REM fallback to current repository path if SOURCE_DIR isn't available
        set "REPO_OPENSSL_DIR=%CD%\vcpkg_installed\x64-mingw-static"
    )
    if exist "%REPO_OPENSSL_DIR%\lib\" (
        echo INFO: Found repo-local OpenSSL at "%REPO_OPENSSL_DIR%" - using it.
        set "OPENSSL_DIR=%REPO_OPENSSL_DIR%"
        set "OPENSSL_LIB_DIR=%OPENSSL_DIR%\lib"
        set "OPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include"
    ) else (
        echo INFO: Repo-local OpenSSL not found at "%REPO_OPENSSL_DIR%" either; will try to install later (or fail with diagnostics).
    )
)

set PATH=C:\ProgramData\chocolatey\bin;C:\ProgramData\chocolatey\lib\ninja\tools;%EXTERNAL_MINGW%\bin;%PATH%

REM Check for Ninja
where ninja >nul 2>nul
if %errorlevel% neq 0 (
    echo Ninja is not installed. Please install Ninja and ensure it is in your PATH.
    exit /b 1
)

REM Check for OpenSSL static libraries
echo Listing candidate OpenSSL library folder: %OPENSSL_LIB_DIR%
if not exist "%OPENSSL_LIB_DIR%\" (
    echo ERROR: OpenSSL library folder not found at %OPENSSL_LIB_DIR%
    echo Please ensure vcpkg installed OpenSSL for triplet x64-mingw-static or run vcpkg install --triplet=x64-mingw-static from the repository manifest.
    exit /b 1
)
dir "%OPENSSL_LIB_DIR%"

if not exist "%OPENSSL_LIB_DIR%\libcrypto.a" (
    echo WARNING: libcrypto.a not found in %OPENSSL_LIB_DIR% - attempting repo-local fallback
    if exist "%SOURCE_DIR%\vcpkg_installed\x64-mingw-static\lib\libcrypto.a" (
        echo Found libcrypto.a in repo-local vcpkg_installed; switching OPENSSL_DIR to repo-local path.
        set "OPENSSL_DIR=%SOURCE_DIR%\vcpkg_installed\x64-mingw-static"
        set "OPENSSL_LIB_DIR=%OPENSSL_DIR%\lib"
        set "OPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include"
    ) else (
        echo OpenSSL static library libcrypto.a not found in either central or repo-local locations.
        echo Please install OpenSSL static libraries (vcpkg install openssl --triplet x64-mingw-static) or let the workflow install/copy them into D:\vcpkg\installed\x64-mingw-static
        exit /b 1
    )
)

if not exist "%OPENSSL_LIB_DIR%\libssl.a" (
    echo ERROR: OpenSSL static library libssl.a not found in %OPENSSL_LIB_DIR%.
    echo Please install OpenSSL static libraries (vcpkg install openssl --triplet x64-mingw-static).
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

# Build qtbase first
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
    -DOPENSSL_ROOT_DIR="%OPENSSL_DIR%" ^
    -DOPENSSL_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
    -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIB_DIR%\libcrypto.a" ^
    -DOPENSSL_SSL_LIBRARY="%OPENSSL_LIB_DIR%\libssl.a" ^
    -DCMAKE_C_FLAGS="-I%OPENSSL_INCLUDE_DIR%" ^
    -DCMAKE_CXX_FLAGS="-I%OPENSSL_INCLUDE_DIR%" ^
    -DCMAKE_EXE_LINKER_FLAGS="-L%OPENSSL_LIB_DIR% -lws2_32 -lcrypt32 -ladvapi32" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static ^
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
            -DOPENSSL_ROOT_DIR="%OPENSSL_DIR%" ^
            -DOPENSSL_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%" ^
            -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIB_DIR%\libcrypto.a" ^
            -DOPENSSL_SSL_LIBRARY="%OPENSSL_LIB_DIR%\libssl.a" ^
            -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" ^
            -DVCPKG_TARGET_TRIPLET=x64-mingw-static ^
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