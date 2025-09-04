# Windows Static Build Environment for Openterface QT
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
    choco install -y python3; \
    choco install -y ninja; \
    choco install -y strawberryperl

# Install Visual Studio Build Tools 2022
RUN choco install -y visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools"

# Install Windows SDK
RUN choco install -y windows-sdk-11

# Install additional build dependencies
RUN choco install -y pkgconfiglite; \
    choco install -y nasm

# Install vcpkg for dependencies
RUN git clone https://github.com/Microsoft/vcpkg.git C:/vcpkg; \
    cd C:/vcpkg; \
    ./bootstrap-vcpkg.bat; \
    ./vcpkg integrate install

# Build Qt from source for static linking
RUN git clone --recursive --branch v6.5.3 https://github.com/qt/qt5.git C:/qt-source; \
    cd C:/qt-source; \
    cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" && configure -static -release -opensource -confirm-license -nomake examples -nomake tests -no-opengl -skip qtwebengine -skip qtwebchannel -skip qtwebview -prefix C:/Qt-static'; \
    cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" && cmake --build . --parallel 4'; \
    cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" && cmake --install .'

# Clean up Qt source to save space
RUN Remove-Item -Recurse -Force C:/qt-source

# Set environment variables for static builds
ENV CMAKE_PREFIX_PATH="C:/Qt-static"
ENV PATH="${PATH};C:/Qt-static/bin;C:/Program Files/CMake/bin;C:/Program Files/Git/bin"
ENV BUILD_TYPE="STATIC"
ENV Qt6_DIR="C:/Qt-static/lib/cmake/Qt6"
ENV VCPKG_ROOT="C:/vcpkg"

# Set up Visual Studio environment
ENV VSINSTALLDIR="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
ENV VCINSTALLDIR="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC"

# Create workspace directory
WORKDIR C:/workspace

# Copy build scripts
COPY windows-build-scripts/ C:/build-scripts/

# Set default command
CMD ["powershell"]
