################################################################################
# Openterface QT Installation Script (Windows)
################################################################################
#
# PURPOSE:
#   Installs the Openterface QT application on Windows from various package formats.
#   Handles EXE, MSI, and ZIP package types.
#   Sets up necessary permissions and device rules.
#
# USAGE:
#   .\install-openterface.ps1 -InstallType exe
#   .\install-openterface.ps1 -InstallType msi
#   .\install-openterface.ps1 -InstallType zip
#   INSTALL_TYPE=exe .\install-openterface.ps1
#
# PARAMETERS:
#   -InstallType: The type of installer (exe, msi, or zip)
#                 Can also be set via INSTALL_TYPE environment variable
#
################################################################################

param(
    [string]$InstallType = $env:INSTALL_TYPE
)

# Set error action preference
$ErrorActionPreference = "Continue"

# Color codes for output
$colors = @{
    Green  = "`e[32m"
    Red    = "`e[31m"
    Yellow = "`e[33m"
    Blue   = "`e[34m"
    Reset  = "`e[0m"
}

# Utility functions
function Write-Header {
    param([string]$Message)
    Write-Host ""
    Write-Host $colors.Blue "╔════════════════════════════════════════════════════════════╗" $colors.Reset
    Write-Host $colors.Blue "║ " $colors.Reset "$Message"
    Write-Host $colors.Blue "╚════════════════════════════════════════════════════════════╝" $colors.Reset
    Write-Host ""
}

function Write-Success {
    param([string]$Message)
    Write-Host $colors.Green "✅ $Message" $colors.Reset
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host $colors.Red "❌ $Message" $colors.Reset
}

function Write-Warning-Custom {
    param([string]$Message)
    Write-Host $colors.Yellow "⚠️  $Message" $colors.Reset
}

function Write-Info {
    param([string]$Message)
    Write-Host $colors.Blue "ℹ️  $Message" $colors.Reset
}

# Main script
Write-Header "Openterface QT Installation Script (Windows)"

# Validate install type
if ([string]::IsNullOrWhiteSpace($InstallType)) {
    Write-Error-Custom "INSTALL_TYPE not specified"
    Write-Host ""
    Write-Host "Usage: $PSCommandPath -InstallType <type>"
    Write-Host "  Supported types: exe, msi, zip"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  $PSCommandPath -InstallType exe"
    Write-Host "  $PSCommandPath -InstallType msi"
    Write-Host "  $PSCommandPath -InstallType zip"
    Write-Host ""
    exit 1
}

$InstallType = $InstallType.ToLower()
Write-Success "Installation type: $InstallType"

# Define installation paths
$installDir = "C:\Program Files\Openterface"
$tempDir = "C:\tmp\build-artifacts"
$exePath = Join-Path $installDir "openterfaceQT.exe"
$logPath = "C:\tmp\install.log"

# Create installation directory
if (-not (Test-Path $installDir)) {
    Write-Info "Creating installation directory: $installDir"
    New-Item -ItemType Directory -Path $installDir -Force -ErrorAction SilentlyContinue | Out-Null
    Write-Success "Installation directory created"
}

# Find packages in temp directory
$packages = @()
if (Test-Path $tempDir) {
    $packages = Get-ChildItem -Path $tempDir -ErrorAction SilentlyContinue
} else {
    Write-Warning-Custom "Temporary directory not found: $tempDir"
}

Write-Info "Searching for installer packages..."
if ($packages.Count -eq 0) {
    Write-Warning-Custom "No packages found in $tempDir"
} else {
    Write-Info "Found $($packages.Count) package(s):"
    $packages | ForEach-Object { Write-Host "  - $($_.Name) ($([Math]::Round($_.Length/1MB, 2)) MB)" }
}

# Installation logic based on type
Write-Host ""
Write-Host $colors.Blue "▶ Beginning Installation" $colors.Reset
Write-Host ""

switch ($InstallType) {
    "exe" {
        Write-Info "Looking for EXE installer..."
        $exeFiles = $packages | Where-Object { $_.Extension -eq ".exe" }
        
        if ($exeFiles) {
            $exeFile = $exeFiles | Select-Object -First 1
            Write-Info "Found EXE installer: $($exeFile.Name)"
            Write-Info "Running installer with silent mode..."
            
            try {
                # Common silent install parameters for Windows installers
                # /S - Silent mode (NSIS)
                # /D - Installation directory
                & $exeFile.FullName /S "/D=$installDir" 2>&1 | Tee-Object -FilePath $logPath -Append
                
                Start-Sleep -Seconds 3
                
                if (Test-Path $exePath) {
                    Write-Success "Installation completed successfully"
                    Write-Host ""
                    Write-Success "Openterface QT installed at: $exePath"
                    exit 0
                } else {
                    Write-Error-Custom "Installation completed but executable not found"
                    exit 1
                }
            } catch {
                Write-Error-Custom "Error during installation: $_"
                exit 1
            }
        } else {
            Write-Error-Custom "No EXE installer found in $tempDir"
            Write-Warning-Custom "Expected to find *.exe package"
            exit 1
        }
    }
    
    "msi" {
        Write-Info "Looking for MSI installer..."
        $msiFiles = $packages | Where-Object { $_.Extension -eq ".msi" }
        
        if ($msiFiles) {
            $msiFile = $msiFiles | Select-Object -First 1
            Write-Info "Found MSI installer: $($msiFile.Name)"
            Write-Info "Running MSI installer..."
            
            try {
                # MSI silent installation
                # /i - Install
                # /quiet - Silent mode (no UI)
                # /passive - Show progress bar only
                # /qb - Show basic UI
                # INSTALLDIR - Installation directory (if supported by MSI)
                $process = Start-Process msiexec -ArgumentList "/i", "`"$($msiFile.FullName)`"", "/quiet", "INSTALLDIR=`"$installDir`"" -PassThru
                $process.WaitForExit()
                
                if ($process.ExitCode -eq 0) {
                    Write-Success "MSI installation completed successfully"
                    
                    if (Test-Path $exePath) {
                        Write-Success "Openterface QT installed at: $exePath"
                        exit 0
                    } else {
                        Write-Warning-Custom "MSI installed but executable not found at expected location"
                        Write-Info "Looking for executable in installation directory..."
                        $foundExe = Get-ChildItem -Path $installDir -Filter "openterfaceQT.exe" -Recurse -ErrorAction SilentlyContinue
                        if ($foundExe) {
                            Write-Success "Found at: $($foundExe.FullName)"
                            exit 0
                        }
                    }
                } else {
                    Write-Error-Custom "MSI installation failed with exit code: $($process.ExitCode)"
                    exit 1
                }
            } catch {
                Write-Error-Custom "Error during MSI installation: $_"
                exit 1
            }
        } else {
            Write-Error-Custom "No MSI installer found in $tempDir"
            Write-Warning-Custom "Expected to find *.msi package"
            exit 1
        }
    }
    
    "zip" {
        Write-Info "Looking for ZIP archive..."
        $zipFiles = $packages | Where-Object { $_.Extension -eq ".zip" }
        
        if ($zipFiles) {
            $zipFile = $zipFiles | Select-Object -First 1
            Write-Info "Found ZIP archive: $($zipFile.Name)"
            Write-Info "Extracting to: $installDir"
            
            try {
                Expand-Archive -Path $zipFile.FullName -DestinationPath $installDir -Force -ErrorAction Stop
                Write-Success "Archive extracted successfully"
                
                if (Test-Path $exePath) {
                    Write-Success "Openterface QT installed at: $exePath"
                    exit 0
                } else {
                    Write-Warning-Custom "Archive extracted but executable not found at expected location"
                    Write-Info "Looking for executable in installation directory..."
                    $foundExe = Get-ChildItem -Path $installDir -Filter "openterfaceQT.exe" -Recurse -ErrorAction SilentlyContinue
                    if ($foundExe) {
                        Write-Success "Found at: $($foundExe.FullName)"
                        exit 0
                    } else {
                        Write-Error-Custom "Could not locate openterfaceQT.exe after extraction"
                        exit 1
                    }
                }
            } catch {
                Write-Error-Custom "Error during extraction: $_"
                exit 1
            }
        } else {
            Write-Error-Custom "No ZIP archive found in $tempDir"
            Write-Warning-Custom "Expected to find *.zip package"
            exit 1
        }
    }
    
    default {
        Write-Error-Custom "Unsupported installation type: $InstallType"
        Write-Host ""
        Write-Host "Supported types:"
        Write-Host "  - exe : Windows EXE installer"
        Write-Host "  - msi : Windows MSI installer"
        Write-Host "  - zip : ZIP archive"
        Write-Host ""
        exit 1
    }
}
