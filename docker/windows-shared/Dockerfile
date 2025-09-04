# Windows Shared Build Environment for Openterface QT
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Install Chocolatey
RUN Set-ExecutionPolicy Bypass -Scope Process -Force; \
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; \
    iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))

# Install build tools and dependencies
RUN choco install -y cmake --installargs 'ADD_CMAKE_TO_PATH=System'; \
    choco install -y git; \
    choco install -y 7zip; \
    choco install -y vcredist-all; \
    choco install -y python3; \
    choco install -y ninja

# Install Visual Studio Build Tools 2022
RUN choco install -y visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22000"

# Install Qt 6.5.3 for shared builds using aqt (Another Qt installer)
RUN pip install aqt; \
    aqt install-qt windows desktop 6.5.3 win64_msvc2022_64 -O C:\Qt; \
    aqt install-tool windows desktop tools_qtcreator -O C:\Qt

# Install additional dependencies
RUN choco install -y pkgconfiglite; \
    choco install -y nasm

# Set environment variables for shared builds
ENV CMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2022_64"
ENV PATH="${PATH};C:/Qt/6.5.3/msvc2022_64/bin;C:/Program Files/CMake/bin;C:/Program Files/Git/bin"
ENV BUILD_TYPE="SHARED"
ENV Qt6_DIR="C:/Qt/6.5.3/msvc2022_64/lib/cmake/Qt6"
ENV VCPKG_ROOT="C:/vcpkg"

# Install vcpkg for additional dependencies
RUN git clone https://github.com/Microsoft/vcpkg.git C:/vcpkg; \
    cd C:/vcpkg; \
    ./bootstrap-vcpkg.bat; \
    ./vcpkg integrate install

# Set up Visual Studio environment
ENV VSINSTALLDIR="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
ENV VCINSTALLDIR="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC"

# Create workspace directory
WORKDIR C:/workspace

# Copy build scripts
COPY windows-build-scripts/ C:/build-scripts/

# Set default command
CMD ["powershell"]
