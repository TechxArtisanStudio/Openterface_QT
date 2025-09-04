param(
    [string]$BuildType = "Release",
    [string]$Architecture = "x64",
    [string]$SourceDir = "C:\workspace"
)

Write-Host "Building Openterface QT with static libraries (portable build)..." -ForegroundColor Green
Write-Host "Build Type: $BuildType" -ForegroundColor Yellow
Write-Host "Architecture: $Architecture" -ForegroundColor Yellow
Write-Host "Source Directory: $SourceDir" -ForegroundColor Yellow

# Environment setup based on portable build workflow
$env:VCPKG_DIR = "C:\vcpkg"
$env:OPENSSL_DIR = "C:\vcpkg\installed\x64-mingw-static"

# Create build directory
$BuildDir = "build-windows-static-$BuildType"
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir
Set-Location $BuildDir

Write-Host "Preparing .pro file for static build..." -ForegroundColor Cyan

# Copy the .pro file to build directory and modify it for static build
Copy-Item "$SourceDir\openterfaceQT.pro" "openterfaceQT.pro"

# Check if static configuration already exists
$proContent = Get-Content "openterfaceQT.pro"
if ($proContent -notcontains "CONFIG += static release") {
    Write-Host "Adding static build configuration to .pro file..."
    Add-Content "openterfaceQT.pro" ""
    Add-Content "openterfaceQT.pro" "CONFIG += static release"
    Add-Content "openterfaceQT.pro" "QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++ -static-libgfortran -L$env:OPENSSL_DIR\lib -lssl -lcrypto -lws2_32 -lcrypt32"
    Add-Content "openterfaceQT.pro" "QMAKE_CXXFLAGS += -static -static-libgcc -static-libstdc++ -I$env:OPENSSL_DIR\include"
    Add-Content "openterfaceQT.pro" "QMAKE_LINK = g++ -Wl,-subsystem,windows"
    Add-Content "openterfaceQT.pro" "CONFIG += static staticlib"
    Add-Content "openterfaceQT.pro" "INCLUDEPATH += $env:OPENSSL_DIR\include"
    Add-Content "openterfaceQT.pro" "LIBS += -L$env:OPENSSL_DIR\lib -lssl -lcrypto -lws2_32 -lcrypt32"
}

Write-Host "Preparing driver files..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path "$SourceDir\drivers"
New-Item -ItemType Directory -Force -Path "$SourceDir\drivers\windows"
if (Test-Path "$SourceDir\driver\windows") {
    Copy-Item "$SourceDir\driver\windows\*" "$SourceDir\drivers\windows\"
}

Write-Host "Updating and releasing translations..." -ForegroundColor Cyan

# Update translations
if (Test-Path "C:\Qt-static\bin\lupdate.exe") {
    Write-Host "Running lupdate..."
    & "C:\Qt-static\bin\lupdate.exe" "openterfaceQT.pro"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "lupdate failed!"
        exit $LASTEXITCODE
    }
    
    Write-Host "Running lrelease..."
    & "C:\Qt-static\bin\lrelease.exe" "openterfaceQT.pro"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "lrelease failed!"
        exit $LASTEXITCODE
    }
} else {
    Write-Warning "Qt tools not found in expected location, skipping translation update"
}

Write-Host "Building portable executable..." -ForegroundColor Cyan

# Set environment for static build
$env:PATH = "C:\Qt-static\bin;C:\mingw64\bin;$env:PATH"
$env:INCLUDE = "C:\Qt-static\include;$env:OPENSSL_DIR\include;$env:INCLUDE"
$env:MINGW_STATIC_BUILD = "1"

Write-Host "Checking Qt configuration..."
& "C:\Qt-static\bin\qmake" -query

Write-Host "Building with qmake..."
& "C:\Qt-static\bin\qmake" -r "openterfaceQT.pro" "CONFIG+=static staticlib" "QMAKE_LFLAGS+=-static -L$env:OPENSSL_DIR\lib -lssl -lcrypto -lws2_32 -lcrypt32"
if ($LASTEXITCODE -ne 0) {
    Write-Error "qmake failed!"
    exit $LASTEXITCODE
}

Write-Host "Building with mingw32-make..."
mingw32-make VERBOSE=1 -j2
if ($LASTEXITCODE -ne 0) {
    Write-Error "mingw32-make failed!"
    exit $LASTEXITCODE
}

if (-not (Test-Path "release\openterfaceQT.exe")) {
    Write-Error "Failed to build openterfaceQT.exe"
    exit 1
}

Write-Host "Stripping debug symbols from executable..." -ForegroundColor Cyan
strip -s "release\openterfaceQT.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Failed to strip debug symbols"
}

Write-Host "Creating portable package..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path "package"
Copy-Item "release\openterfaceQT.exe" "package\openterfaceQT-portable.exe"

Write-Host "Static build completed successfully in $BuildDir\package directory" -ForegroundColor Green
Write-Host "Portable executable is ready: openterfaceQT-portable.exe" -ForegroundColor Yellow
