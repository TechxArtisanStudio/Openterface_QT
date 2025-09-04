param(
    [string]$BuildType = "Release",
    [string]$Architecture = "x64",
    [string]$SourceDir = "C:\workspace"
)

Write-Host "Building Openterface QT with shared libraries (qmake)..." -ForegroundColor Green
Write-Host "Build Type: $BuildType" -ForegroundColor Yellow
Write-Host "Architecture: $Architecture" -ForegroundColor Yellow
Write-Host "Source Directory: $SourceDir" -ForegroundColor Yellow

# Set environment variables
$env:PACKAGE_DIR = "package_shared"
$env:EXE_NAME = "openterfaceQT.exe"

# Create build directory
$BuildDir = "build-windows-shared-$BuildType"
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir
Set-Location $BuildDir

Write-Host "Updating and releasing translations..." -ForegroundColor Cyan

# Update translations with lupdate
Write-Host "Running lupdate..."
lupdate "$SourceDir\openterfaceQT.pro"
if ($LASTEXITCODE -ne 0) {
    Write-Error "lupdate failed!"
    exit $LASTEXITCODE
}

# Generate .qm files with lrelease
Write-Host "Running lrelease..."
lrelease "$SourceDir\openterfaceQT.pro"
if ($LASTEXITCODE -ne 0) {
    Write-Error "lrelease failed!"
    exit $LASTEXITCODE
}

Write-Host "Building with qmake..." -ForegroundColor Cyan

# Configure with qmake
qmake -v
qmake -r "$SourceDir\openterfaceQT.pro"
if ($LASTEXITCODE -ne 0) {
    Write-Error "qmake configuration failed!"
    exit $LASTEXITCODE
}

Write-Host "Building with mingw32-make..." -ForegroundColor Cyan

# Build with mingw32-make
mingw32-make -j2
if ($LASTEXITCODE -ne 0) {
    Write-Error "mingw32-make failed!"
    exit $LASTEXITCODE
}

Write-Host "Checking build output..." -ForegroundColor Cyan
Get-ChildItem release

if (-not (Test-Path "release\openterfaceQT.exe")) {
    Write-Error "Failed to build openterfaceQT.exe"
    exit 1
}

Write-Host "Creating package directory..." -ForegroundColor Cyan

# Create package structure
New-Item -ItemType Directory -Force -Path $env:PACKAGE_DIR
New-Item -ItemType Directory -Force -Path "$env:PACKAGE_DIR\driver"
New-Item -ItemType Directory -Force -Path "$env:PACKAGE_DIR\driver\windows"
New-Item -ItemType Directory -Force -Path "$env:PACKAGE_DIR\config\languages"
New-Item -ItemType Directory -Force -Path "$env:PACKAGE_DIR\config\keyboards"

# Copy files
Copy-Item "release\openterfaceQT.exe" "$env:PACKAGE_DIR\$env:EXE_NAME"
Copy-Item "$SourceDir\LICENSE" "$env:PACKAGE_DIR\"
Copy-Item "$SourceDir\driver\windows\*" "$env:PACKAGE_DIR\driver\windows\"

Write-Host "Copying translation & keyboard layout files..." -ForegroundColor Cyan
Copy-Item "$SourceDir\config\keyboards\*.json" "$env:PACKAGE_DIR\config\keyboards\"
Copy-Item "$SourceDir\config\languages\*.qm" "$env:PACKAGE_DIR\config\languages\"

Write-Host "Running windeployqt..." -ForegroundColor Cyan
Set-Location $env:PACKAGE_DIR
windeployqt --qmldir $SourceDir $env:EXE_NAME --compiler-runtime --multimedia

Set-Location ..

Write-Host "Shared build completed successfully in $BuildDir\$env:PACKAGE_DIR directory" -ForegroundColor Green
Write-Host "Packaged application is ready for distribution" -ForegroundColor Yellow
