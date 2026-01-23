<#
PowerShell helper to run the FFmpeg cross-build script in a suitable Linux environment.
It will try (in order):
 - WSL (preferred if present)
 - Docker (if available)
If neither is present it prints instructions.
#>
param(
    [string] $Prefix = "/tmp/ffmpeg-static-windows",
    [string] $CrossPrefix = "x86_64-w64-mingw32-",
    [switch] $UseDocker
)

function Run-InWsl {
    Write-Host "Attempting to run inside WSL (ubuntu-22.04)..."
    $cmd = "cd \"$(wslpath -a .)\"; PREFIX=$Prefix CROSS_PREFIX=$CrossPrefix bash build-script/build-static-ffmpeg-cross.sh"
    wsl -d ubuntu-22.04 -- bash -lc $cmd
}

function Run-InDocker {
    Write-Host "Attempting to run inside Docker (ubuntu:22.04)..."
    $pwd = (Get-Location).Path
    docker run --rm -it -v "${pwd}:/work" -w /work ubuntu:22.04 bash -lc "apt-get update && apt-get install -y git make build-essential pkg-config nasm yasm mingw-w64 curl wget tar cmake && cd build-script && PREFIX=$Prefix CROSS_PREFIX=$CrossPrefix bash build-static-ffmpeg-cross.sh"
}

# Main
if ($UseDocker) {
    if (Get-Command docker -ErrorAction SilentlyContinue) {
        Run-InDocker
        exit $LASTEXITCODE
    } else {
        Write-Error "Docker not found on PATH"
        exit 1
    }
}

if (Get-Command wsl -ErrorAction SilentlyContinue) {
    try {
        Run-InWsl
        exit $LASTEXITCODE
    } catch {
        Write-Warning "WSL attempt failed: $_"
        if (Get-Command docker -ErrorAction SilentlyContinue) {
            Write-Host "Falling back to Docker..."
            Run-InDocker
            exit $LASTEXITCODE
        }
        exit 1
    }
} elseif (Get-Command docker -ErrorAction SilentlyContinue) {
    Run-InDocker
    exit $LASTEXITCODE
} else {
    Write-Host "Neither WSL nor Docker detected. Please install one of them or run the build on an Ubuntu runner."
    Write-Host "WSL (Ubuntu 22.04) example:"
    Write-Host "  wsl --install -d ubuntu-22.04"
    Write-Host "Then from PowerShell:"
    Write-Host "  wsl -d ubuntu-22.04 -- bash -lc 'cd /mnt/c/path/to/repo && PREFIX=/tmp/ffmpeg-static-windows CROSS_PREFIX=x86_64-w64-mingw32- bash build-script/build-static-ffmpeg-cross.sh'"
    Write-Host "Or run the build inside Docker: see the README at build-script/README-cross.md"
    exit 1
}
