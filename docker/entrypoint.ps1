# Windows Entrypoint Script for Openterface QT Testing Container
# This script prepares the container environment and optionally installs the application

$ErrorActionPreference = "Continue"

Write-Host "=== Openterface QT Windows Container Entrypoint ===" -ForegroundColor Cyan
Write-Host ""

# Log start time
$startTime = Get-Date
Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Container startup initiated" -ForegroundColor Green

# Set up logging
$logFile = "C:\tmp\container-startup.log"
$logDir = Split-Path $logFile
if (-not (Test-Path $logDir)) {
    mkdir -Force $logDir | Out-Null
}

"=== Openterface QT Windows Container Startup ===" | Tee-Object -FilePath $logFile -Append
"Start Time: $(Get-Date)" | Tee-Object -FilePath $logFile -Append
"" | Tee-Object -FilePath $logFile -Append

# Display environment info
Write-Host "Environment Information:" -ForegroundColor Yellow
Write-Host "  OS Version: $([System.Environment]::OSVersion)" 
Write-Host "  Install Type: $env:INSTALL_TYPE"
Write-Host "  Temp Directory: $env:TEMP"
Write-Host ""

"Environment:" | Tee-Object -FilePath $logFile -Append
"  OS: $([System.Environment]::OSVersion)" | Tee-Object -FilePath $logFile -Append
"  Install Type: $env:INSTALL_TYPE" | Tee-Object -FilePath $logFile -Append
"" | Tee-Object -FilePath $logFile -Append

# Check if installation script exists and run it
$installScript = "C:\docker\install-openterface.ps1"
if (Test-Path $installScript) {
    Write-Host "Running installation script..." -ForegroundColor Green
    "Installation Script Found: $installScript" | Tee-Object -FilePath $logFile -Append
    
    try {
        & $installScript
        "Installation completed successfully" | Tee-Object -FilePath $logFile -Append
    } catch {
        $errorMsg = $_.Exception.Message
        Write-Host "Error during installation: $errorMsg" -ForegroundColor Red
        "Installation Error: $errorMsg" | Tee-Object -FilePath $logFile -Append
    }
} else {
    Write-Host "Installation script not found at $installScript" -ForegroundColor Yellow
    "Warning: Installation script not found" | Tee-Object -FilePath $logFile -Append
}

# Check if application is installed
$appPath = "C:\Program Files\Openterface"
if (Test-Path $appPath) {
    Write-Host "Application found at $appPath" -ForegroundColor Green
    "Application Verification: FOUND" | Tee-Object -FilePath $logFile -Append
    Get-ChildItem -Path $appPath | Tee-Object -FilePath $logFile -Append
} else {
    Write-Host "Application not found at $appPath" -ForegroundColor Yellow
    "Application Verification: NOT FOUND" | Tee-Object -FilePath $logFile -Append
}

# Display completion info
$endTime = Get-Date
$duration = $endTime - $startTime
Write-Host ""
Write-Host "=== Container Startup Complete ===" -ForegroundColor Cyan
Write-Host "Duration: $($duration.TotalSeconds) seconds"
Write-Host "Status: Ready for testing"
Write-Host ""

"End Time: $(Get-Date)" | Tee-Object -FilePath $logFile -Append
"Duration: $($duration.TotalSeconds) seconds" | Tee-Object -FilePath $logFile -Append
"Status: Container ready for testing" | Tee-Object -FilePath $logFile -Append

# Keep container running by waiting for input
Write-Host "Container is ready. Press Ctrl+C to exit."
while ($true) {
    Start-Sleep -Seconds 60
}
