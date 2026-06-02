@echo off
echo Building Openterface QT in Release mode...

REM Set Qt environment
set QT_DIR=E:\QT\6.5.3\mingw_64
set PATH=%QT_DIR%\bin;%PATH%

REM Set MinGW environment
set MINGW_DIR=E:\QT\Tools\mingw1120_64
set PATH=%MINGW_DIR%\bin;%PATH%

REM Navigate to project root
cd /d "%~dp0\.."

REM Clean and rebuild
qmake6 -makefile -o build\Desktop_Qt_6_5_3_MinGW_64_bit-Release\Makefile openterfaceQT.pro CONFIG+=release

REM Navigate to build directory
cd build\Desktop_Qt_6_5_3_MinGW_64_bit-Release

REM Build the project
jom || mingw32-make || nmake || make

REM Deploy Qt DLLs and dependencies using windeployqt
echo.
echo ============================================================================
echo Running windeployqt to deploy Qt DLLs...
echo ============================================================================
if exist "%QT_DIR%\bin\windeployqt.exe" (
    echo Deploying Qt dependencies for openterfaceQT.exe...
    "%QT_DIR%\bin\windeployqt.exe" openterfaceQT.exe --release --multimedia --network --no-compiler-runtime
    if errorlevel 1 (
        echo WARNING: windeployqt returned error code %errorlevel%
    ) else (
        echo windeployqt completed successfully
    )

    REM Ensure MinGW runtime DLLs from Qt MinGW are present (ABI-matched)
    echo.
    echo Ensuring MinGW runtime DLLs are present...
    if not exist "libstdc++-6.dll" if exist "%MINGW_DIR%\bin\libstdc++-6.dll" (
        copy "%MINGW_DIR%\bin\libstdc++-6.dll" . >nul
        echo   Copied libstdc++-6.dll from Qt MinGW
    )
    if not exist "libgcc_s_seh-1.dll" if exist "%MINGW_DIR%\bin\libgcc_s_seh-1.dll" (
        copy "%MINGW_DIR%\bin\libgcc_s_seh-1.dll" . >nul
        echo   Copied libgcc_s_seh-1.dll from Qt MinGW
    )
    if not exist "libwinpthread-1.dll" if exist "%MINGW_DIR%\bin\libwinpthread-1.dll" (
        copy "%MINGW_DIR%\bin\libwinpthread-1.dll" . >nul
        echo   Copied libwinpthread-1.dll from Qt MinGW
    )
    echo.
    echo Deployment complete. You can now run openterfaceQT.exe from:
    echo   %CD%
) else (
    echo WARNING: windeployqt.exe not found at %QT_DIR%\bin
    echo You may need to run it manually before running the executable:
    echo   windeployqt openterfaceQT.exe --release --multimedia --network
)

echo Build complete.
pause
