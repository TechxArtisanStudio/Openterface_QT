param(
    [Parameter(Mandatory=$true)]
    [string]$DockerImage,

    [Parameter(Mandatory=$true)]
    [string]$DockerTag,

    [Parameter(Mandatory=$true)]
    [string]$InstallType,

    [Parameter(Mandatory=$true)]
    [string]$TestDescription,

    [Parameter(Mandatory=$true)]
    [string]$MatrixInstallType,

    [Parameter(Mandatory=$true)]
    [string]$GitHubRunId,

    [Parameter(Mandatory=$false)]
    [string]$GitHubToken = ""
)

Write-Host "[TEST] Testing Windows container"
Write-Host "============================"
Write-Host ""
Write-Host "Test Configuration:"
Write-Host "  Image: ${DockerImage}:${DockerTag}"
Write-Host "  Install Type: $InstallType"
Write-Host "  Description: $TestDescription"
Write-Host ""

$containerName = "openterface-test-windows-${MatrixInstallType}-${GitHubRunId}"
$logFile = "C:\tmp\container-test-${MatrixInstallType}.log"

Write-Host "Preparing volume mount..."

# Check if build artifacts exist
$volumeMount = ""
if (Test-Path "build") {
    $buildFiles = Get-ChildItem -Path "build" -Recurse -Include "*.exe", "*.msi", "*.zip" -ErrorAction SilentlyContinue
    if ($buildFiles.Count -gt 0) {
        $volumeMount = "-v $(Convert-Path build):C:\tmp\build-artifacts"
        Write-Host "[SUCCESS] Volume mount prepared: $volumeMount"
    } else {
        Write-Host "[WARNING] No installer files found in build directory"
    }
} else {
    Write-Host "[WARNING] Build directory not found"
}

Write-Host ""
Write-Host "Running container in background..."

# Build docker run command
# Note: Don't use -it (interactive + TTY) on GitHub Actions Windows runners
$dockerArgs = @(
    "-d",
    "--name", $containerName,
    "-e", "INSTALL_TYPE=$InstallType",
    "-e", "GITHUB_TOKEN=$GitHubToken"
)

if ($volumeMount) {
    $dockerArgs += @("-v", "$(Convert-Path build):C:\tmp\build-artifacts")
}

          $dockerArgs += "${DockerImage}:${DockerTag}"Write-Host "Docker arguments:"
$dockerArgs | ForEach-Object { Write-Host "  $_" }
Write-Host ""

# Run container in detached mode (background)
Write-Host "[RUN] Starting container in background (detached mode)..."
& docker run @dockerArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "[SUCCESS] Container started successfully in background"
    Write-Host ""

    # Wait for container to become ready
    Write-Host "[WAIT] Waiting for container initialization (15 seconds)..."
    Start-Sleep -Seconds 15

    # First, list available installers
    Write-Host ""
    Write-Host "[DEBUG] Checking available installers..."
    docker exec $containerName powershell -Command {
        Write-Host "[LIST] Contents of C:\tmp\build-artifacts:"
        Get-ChildItem -Path "C:\tmp\build-artifacts" -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "  - $($_.Name) ($($_.Length) bytes)"
        }
    }

    # Install the application
    Write-Host ""
    Write-Host "[INSTALL] Installing the application..."
    Write-Host "Executing installation inside container..."

    $installCommand = {
        # List current directory contents for debugging
        Write-Host "[DEBUG] Current directory: $(Get-Location)"
        Write-Host "[DEBUG] Artifacts directory contents:"
        Get-ChildItem -Path "C:\tmp\build-artifacts" -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "  - $($_.Name)"
        }

        # Check installation type and install accordingly
        if ($env:INSTALL_TYPE -eq "exe") {
            Write-Host "[INSTALLER] Using EXE installer..."
            # Look for exe installer in artifacts
            $exeFiles = Get-ChildItem -Path "C:\tmp\build-artifacts" -Filter "*.exe" -ErrorAction SilentlyContinue
            Write-Host "[DEBUG] Found $($exeFiles.Count) EXE files"

            if ($exeFiles) {
                $exeFile = $exeFiles | Select-Object -First 1
                Write-Host "[INSTALL] Running: $($exeFile.FullName)"
                Write-Host "[INSTALL] File size: $($exeFile.Length) bytes"

                # Create log file for installation
                $logFile = "C:\tmp\install.log"
                Write-Host "[INSTALL] Installation log will be saved to: $logFile"

                # Check available installer options
                Write-Host "[DEBUG] Checking installer signature and type..."
                $fileInfo = Get-Item $exeFile.FullName
                Write-Host "[DEBUG] File signature check: $($fileInfo.Extension)"

                # Scan for possible installation directories before installation
                Write-Host "[DEBUG] Scanning common installation directories BEFORE installation:"
                $commonPaths = @(
                    "C:\Program Files\Openterface*",
                    "C:\Program Files (x86)\Openterface*",
                    "C:\Users\ContainerAdministrator\AppData\Local\Openterface*",
                    "C:\Users\ContainerAdministrator\AppData\Roaming\Openterface*"
                )
                foreach ($path in $commonPaths) {
                    $exists = Get-Item $path -ErrorAction SilentlyContinue
                    if ($exists) {
                        Write-Host "[DEBUG]   Found: $($exists.FullName)"
                    }
                }

                # Run installer with logging and capture output
                Write-Host "[INSTALL] Starting installation with silent mode..."
                Write-Host "[INSTALL] Command: $($exeFile.FullName) /S /D=`"C:\Program Files\Openterface Mini-KVM`""

                # Capture installation output
                $installOutput = & $exeFile.FullName /S /D="C:\Program Files\Openterface Mini-KVM" 2>&1 | Tee-Object -Variable installLogs
                $installExitCode = $LASTEXITCODE

                # Save installation output to log file
                $installOutput | Out-File -FilePath $logFile -Encoding UTF8

                Write-Host "[INSTALL] Installer exit code: $installExitCode"

                if ($installExitCode -eq 0) {
                    Write-Host "[SUCCESS] EXE installation completed successfully"
                } else {
                    Write-Host "[WARNING] Installer exited with code: $installExitCode"
                }

                # Show installation output if any
                if ($installOutput) {
                    Write-Host "[INSTALL] Installer output:"
                    $installOutput | ForEach-Object { Write-Host "    $_" }
                }

                # Wait for installation to finalize
                Write-Host "[WAIT] Waiting for installation to finalize (5 seconds)..."
                Start-Sleep -Seconds 5

                # Comprehensive directory scan AFTER installation
                Write-Host "[DEBUG] Scanning ALL possible installation locations AFTER installation:"

                # Check expected location
                if (Test-Path "C:\Program Files\Openterface Mini-KVM") {
                    Write-Host "[VERIFY] ✅ Installation directory created: C:\Program Files\Openterface Mini-KVM"
                    Write-Host "[VERIFY] Contents:"
                    Get-ChildItem -Path "C:\Program Files\Openterface Mini-KVM" -Recurse | ForEach-Object { Write-Host "    $($_.FullName)" }
                } else {
                    Write-Host "[WARNING] ❌ Expected directory not found: C:\Program Files\Openterface Mini-KVM"
                }

                # Search for any Openterface-related directories
                Write-Host "[DEBUG] Searching for any Openterface directories..."
                foreach ($path in $commonPaths) {
                    $found = Get-Item $path -ErrorAction SilentlyContinue
                    if ($found) {
                        Write-Host "[FOUND] ✅ $($found.FullName)"
                        Write-Host "[FOUND]   Contents:"
                        Get-ChildItem -Path $found.FullName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 20 | ForEach-Object { Write-Host "      $($_.FullName)" }
                    }
                }

                # Search entire Program Files for any .exe that might be the app
                Write-Host "[DEBUG] Searching for Openterface executables in Program Files..."
                $foundExes = Get-ChildItem -Path "C:\Program Files" -Filter "*openterface*.exe" -Recurse -ErrorAction SilentlyContinue
                if ($foundExes) {
                    Write-Host "[FOUND] ✅ Found $($foundExes.Count) executable(s):"
                    $foundExes | ForEach-Object { Write-Host "    $($_.FullName)" }
                } else {
                    Write-Host "[WARNING] ❌ No Openterface executables found in C:\Program Files"
                }

                # Also check Program Files (x86)
                Write-Host "[DEBUG] Searching for Openterface executables in Program Files (x86)..."
                $foundExes86 = Get-ChildItem -Path "C:\Program Files (x86)" -Filter "*openterface*.exe" -Recurse -ErrorAction SilentlyContinue
                if ($foundExes86) {
                    Write-Host "[FOUND] ✅ Found $($foundExes86.Count) executable(s):"
                    $foundExes86 | ForEach-Object { Write-Host "    $($_.FullName)" }
                }

                # Check if any installer logs were created
                Write-Host "[DEBUG] Checking for NSIS installer logs..."
                $nsisLogs = Get-ChildItem -Path "C:\Users\ContainerAdministrator\AppData\Local\Temp" -Filter "*nsis*.log" -ErrorAction SilentlyContinue
                if ($nsisLogs) {
                    Write-Host "[FOUND] ✅ NSIS log files:"
                    $nsisLogs | ForEach-Object {
                        Write-Host "    $($_.FullName)"
                        Write-Host "    Contents:"
                        Get-Content $_.FullName -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "      $_" }
                    }
                }
            } else {
                Write-Host "[ERROR] No EXE installer found in C:\tmp\build-artifacts"
            }
        } elseif ($env:INSTALL_TYPE -eq "zip") {
            Write-Host "[INSTALLER] Using portable/ZIP installation..."
            # Look for portable exe or zip file in artifacts
            $portableFiles = Get-ChildItem -Path "C:\tmp\build-artifacts" -Filter "*portable*" -ErrorAction SilentlyContinue
            $zipFiles = Get-ChildItem -Path "C:\tmp\build-artifacts" -Filter "*.zip" -ErrorAction SilentlyContinue
            $exeFiles = Get-ChildItem -Path "C:\tmp\build-artifacts" -Filter "*.exe" -ErrorAction SilentlyContinue

            Write-Host "[DEBUG] Found $($portableFiles.Count) portable files, $($zipFiles.Count) ZIP files, $($exeFiles.Count) EXE files"

            if ($zipFiles) {
                $zipFile = $zipFiles | Select-Object -First 1
                Write-Host "[INSTALL] Extracting ZIP: $($zipFile.FullName)"
                Write-Host "[INSTALL] File size: $($zipFile.Length) bytes"

                # Create target directory
                New-Item -ItemType Directory -Path "C:\Program Files\Openterface Mini-KVM" -Force | Out-Null

                # Extract with error checking
                try {
                    Expand-Archive -Path $zipFile.FullName -DestinationPath "C:\Program Files\Openterface Mini-KVM" -Force
                    Write-Host "[SUCCESS] ZIP extraction completed"

                    # Verify extraction
                    Start-Sleep -Seconds 2
                    if (Test-Path "C:\Program Files\Openterface Mini-KVM") {
                        Write-Host "[VERIFY] Extraction directory contents:"
                        Get-ChildItem -Path "C:\Program Files\Openterface Mini-KVM" -Recurse | ForEach-Object { Write-Host "    $($_.FullName)" }
                    }
                } catch {
                    Write-Host "[ERROR] ZIP extraction failed: $_"
                }
            } elseif ($portableFiles -or $exeFiles) {
                # Portable build - just copy to Program Files
                $portableExe = $portableFiles | Select-Object -First 1
                if (-not $portableExe) {
                    $portableExe = $exeFiles | Select-Object -First 1
                }

                Write-Host "[INSTALL] Using portable EXE: $($portableExe.FullName)"
                Write-Host "[INSTALL] File size: $($portableExe.Length) bytes"

                # Create target directory
                $targetDir = "C:\Program Files\Openterface Mini-KVM"
                New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

                # Copy portable exe
                Copy-Item -Path $portableExe.FullName -Destination "$targetDir\openterfaceQT-portable.exe" -Force
                Write-Host "[SUCCESS] Portable EXE copied to: $targetDir"

                # Verify
                if (Test-Path "$targetDir\openterfaceQT-portable.exe") {
                    Write-Host "[VERIFY] Installation directory contents:"
                    Get-ChildItem -Path $targetDir -Recurse | ForEach-Object { Write-Host "    $($_.FullName)" }
                }
            } else {
                Write-Host "[ERROR] No ZIP or portable EXE file found in C:\tmp\build-artifacts"
            }
        }
    }

    docker exec $containerName powershell -Command $installCommand

    if ($LASTEXITCODE -eq 0) {
        Write-Host "[SUCCESS] Installation step completed"
    } else {
        Write-Host "[WARNING] Installation step had exit code: $LASTEXITCODE"
    }

    # Start the application
    Write-Host ""
    Write-Host "[START] Starting the Openterface application..."

    $startCommand = {
        # Check multiple possible paths
        $possiblePaths = @(
            "C:\Program Files\Openterface Mini-KVM\Openterface.exe",
            "C:\Program Files\Openterface Mini-KVM\bin\openterfaceqt.exe",
            "C:\Program Files\Openterface Mini-KVM\openterfaceqt.exe",
            "C:\Program Files\Openterface Mini-KVM\OpenTerface.exe"
        )

        $appPath = $null
        foreach ($path in $possiblePaths) {
            if (Test-Path $path) {
                $appPath = $path
                break
            }
        }

        if ($appPath) {
            Write-Host "[START] Launching: $appPath"
            Start-Process -FilePath $appPath -NoNewWindow -PassThru | Out-Null
            Write-Host "[SUCCESS] Application launched"
            Start-Sleep -Seconds 5
        } else {
            Write-Host "[WARNING] Application executable not found"
            Write-Host "[DEBUG] Checking C:\Program Files\Openterface Mini-KVM contents:"
            if (Test-Path "C:\Program Files\Openterface Mini-KVM") {
                Get-ChildItem -Path "C:\Program Files\Openterface Mini-KVM" -Recurse -File | Select-Object -First 20 | ForEach-Object { Write-Host "  - $($_.FullName)" }
            } else {
                Write-Host "[ERROR] Installation directory does not exist!"
            }
        }
    }

    docker exec $containerName powershell -Command $startCommand

    # Wait for application and desktop to stabilize
    Write-Host ""
    Write-Host "[WAIT] Waiting for application to stabilize (15 seconds)..."
    Start-Sleep -Seconds 15

    # Take a screenshot with better method
    Write-Host ""
    Write-Host "[SCREENSHOT] Taking screenshot of the desktop..."

    $screenshotCommand = {
        try {
            # Add required assemblies
            Add-Type -AssemblyName System.Windows.Forms
            Add-Type -AssemblyName System.Drawing

            # Get screen dimensions
            $screen = [System.Windows.Forms.Screen]::PrimaryScreen
            $bounds = $screen.Bounds

            Write-Host "[DEBUG] Screen resolution: $($bounds.Width)x$($bounds.Height)"

            # Create bitmap with correct dimensions
            $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

            # Capture screen to bitmap
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
            $graphics.Dispose()

            # Save as PNG with high quality
            $screenshotPath = "C:\tmp\screenshot.png"
            $bitmap.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()

            Write-Host "[SUCCESS] Screenshot saved to: $screenshotPath"
            if (Test-Path $screenshotPath) {
                $fileSize = (Get-Item $screenshotPath).Length
                $fileSizeKB = [Math]::Round($fileSize / 1KB, 2)
                Write-Host "[INFO] File size: $fileSize bytes ($fileSizeKB KB)"
                if ($fileSize -lt 50000) {
                    Write-Host "[WARNING] Screenshot file seems small, may not capture full desktop"
                }
            }
        } catch {
            Write-Host "[ERROR] Screenshot capture failed: $_"
        }
    }

    docker exec $containerName powershell -Command $screenshotCommand

    if ($LASTEXITCODE -eq 0) {
        Write-Host "[SUCCESS] Screenshot captured"
    } else {
        Write-Host "[WARNING] Screenshot capture had issues"
    }

    # Wait a bit more for container to stabilize
    Write-Host ""
    Write-Host "[WAIT] Waiting for container to stabilize (optional: waiting for completion)..."
    Start-Sleep -Seconds 5

} else {
    Write-Host "[ERROR] Failed to start container"
}

# Get container logs
Write-Host ""
Write-Host "Container logs:"
Write-Host "==============="
docker logs $containerName 2>&1 | Tee-Object -FilePath "container-logs-${MatrixInstallType}.txt"