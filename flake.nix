{
  description = "Openterface Mini-KVM: Host Applications for Windows and Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    let
      # NixOS module needs to be outside of eachDefaultSystem
      nixosModule = { config, lib, pkgs, ... }: {
        options.services.openterface = {
          enable = lib.mkEnableOption "Openterface Mini-KVM support";
          
          package = lib.mkOption {
            type = lib.types.package;
            default = self.packages.${pkgs.system}.openterface-qt;
            description = "The Openterface QT package to use";
          };
        };

        config = lib.mkIf config.services.openterface.enable {
          # Install the package
          environment.systemPackages = [ config.services.openterface.package ];
          
          # Install udev rules
          services.udev.packages = [ config.services.openterface.package ];
          
          # Add users to dialout group for serial port access
          users.groups.dialout = {};
          
          # Reload udev rules
          services.udev.extraRules = ''
            # Reload rules when Openterface device is connected
            SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", RUN+="${pkgs.systemd}/bin/udevadm trigger"
            SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", RUN+="${pkgs.systemd}/bin/udevadm trigger"
          '';
        };
      };
    in
    {
      # NixOS module available for all systems
      nixosModules.openterface = nixosModule;
    } //
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        openterface-qt = pkgs.stdenv.mkDerivation rec {
          pname = "openterface-qt";
          version = "0.3.19";

          src = pkgs.fetchurl {
            url = "https://github.com/TechxArtisanStudio/Openterface_QT/archive/refs/tags/${version}.tar.gz";
            sha256 = "sha256-98G6DZWYg9y35+wK2mOfmyCktgtgqSY20qtqSH2xS3s=";
          };

          # The archive extracts to Openterface_QT-${version}/
          sourceRoot = "Openterface_QT-${version}";

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            qt6.wrapQtAppsHook
            qt6.qttools
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtmultimedia
            qt6.qtserialport
            qt6.qtsvg
            libusb1
            openssl
            ffmpeg.lib  # Use the lib output for libraries
            ffmpeg.dev  # Include development headers
            xorg.libX11
            xorg.libXrandr
            xorg.libXrender
            expat
            freetype
            fontconfig
            bzip2
            systemd # for libudev
          ];

          # Set UTF-8 locale for Qt and compiler flags to treat warnings as non-fatal
          env.LANG = "C.UTF-8";
          env.LC_ALL = "C.UTF-8";
          
          # Allow deprecated Qt APIs and disable warnings as errors
          env.NIX_CFLAGS_COMPILE = "-Wno-deprecated-declarations -Wno-error";

          # Pre-build phase to generate language files
          preBuild = ''
            # Check if .pro file exists and generate Qt language files if present
            if [ -f "openterfaceQT.pro" ]; then
              echo "Generating Qt language files..."
              ${pkgs.qt6.qttools}/bin/lrelease openterfaceQT.pro
            else
              echo "No .pro file found, skipping language file generation"
              # Look for .ts files in common locations and process them
              find . -name "*.ts" -exec ${pkgs.qt6.qttools}/bin/lrelease {} \; || true
            fi
          '';

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -Wno-error"
            # Let CMake find FFmpeg libraries automatically
            "-DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON"
            # Explicitly set FFmpeg paths
            "-DFFMPEG_FOUND=TRUE"
            "-DFFMPEG_INCLUDE_DIRS=${pkgs.ffmpeg.dev}/include"
            "-DFFMPEG_LIBRARIES=${pkgs.ffmpeg.lib}/lib/libavformat${pkgs.stdenv.hostPlatform.extensions.sharedLibrary};${pkgs.ffmpeg.lib}/lib/libavcodec${pkgs.stdenv.hostPlatform.extensions.sharedLibrary};${pkgs.ffmpeg.lib}/lib/libavutil${pkgs.stdenv.hostPlatform.extensions.sharedLibrary};${pkgs.ffmpeg.lib}/lib/libswresample${pkgs.stdenv.hostPlatform.extensions.sharedLibrary};${pkgs.ffmpeg.lib}/lib/libswscale${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}"
            # Disable Qt6's automatic deployment which causes issues in Nix
            "-DQT_NO_DEPLOY_RUNTIME_DEPENDENCIES=ON"
          ];

          # Patch CMakeLists.txt to fix hardcoded paths and disable Qt deployment
          postPatch = ''
            # Fix any hardcoded FFmpeg paths in CMake files
            find . -name "*.cmake" -o -name "CMakeLists.txt" | xargs sed -i \
              -e 's|/usr/local/lib|${pkgs.ffmpeg.lib}/lib|g' \
              -e 's|/var/empty/local/lib|${pkgs.ffmpeg.lib}/lib|g' \
              -e 's|\.a|${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}|g'
              
            # Also check for any .pro files that might have hardcoded paths
            find . -name "*.pro" | xargs sed -i \
              -e 's|/usr/local/lib|${pkgs.ffmpeg.lib}/lib|g' \
              -e 's|/var/empty/local/lib|${pkgs.ffmpeg.lib}/lib|g' \
              -e 's|\.a|${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}|g' || true
              
            # More targeted approach: just remove deployment lines
            find . -name "CMakeLists.txt" | xargs sed -i \
              -e '/qt6_deploy_runtime_dependencies/d' \
              -e '/qt_deploy_runtime_dependencies/d'
          '';

          # Override the install target to skip Qt deployment
          buildPhase = ''
            runHook preBuild
            make -j$NIX_BUILD_CORES
            runHook postBuild
          '';

          # Don't use make install - it triggers Qt deployment
          dontUseInstall = true;

          # Custom install phase to avoid Qt deployment issues
          installPhase = ''
            runHook preInstall
            
            # Create output directories
            mkdir -p $out/bin
            mkdir -p $out/share/applications
            mkdir -p $out/lib/udev/rules.d
            
            # Install the main executable
            cp openterfaceQT $out/bin/
            
            # Install udev rules for device permissions
            cat > $out/lib/udev/rules.d/51-openterface.rules << EOF
            SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
            SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
            SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
            SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
            EOF
            
            # Create desktop entry
            cat > $out/share/applications/openterface-qt.desktop << EOF
            [Desktop Entry]
            Version=1.0
            Type=Application
            Name=Openterface Mini-KVM
            Comment=KVM control application for Openterface hardware
            Exec=$out/bin/openterfaceQT
            Icon=openterface
            Terminal=false
            Categories=System;Hardware;
            EOF
            
            # Install icon if it exists in the source
            if [ -f "../resources/icon.png" ]; then
              mkdir -p $out/share/pixmaps
              cp ../resources/icon.png $out/share/pixmaps/openterface.png
            fi
            
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "Host application for Openterface Mini-KVM hardware";
            longDescription = ''
              Openterface Mini-KVM QT is a cross-platform application that provides
              KVM (Keyboard, Video, Mouse) control capabilities for the Openterface
              Mini-KVM hardware device. Features include basic KVM operations,
              mouse control in absolute and relative modes, audio playback from
              target devices, text pasting, and OCR text extraction.
            '';
            homepage = "https://github.com/TechxArtisanStudio/Openterface_QT";
            license = licenses.agpl3Only;
            maintainers = [ ];
            platforms = platforms.linux;
            mainProgram = "openterfaceQT";
          };
        };
      in
      {
        packages = {
          default = openterface-qt;
          openterface-qt = openterface-qt;
        };

        # Development shell with all build dependencies
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            cmake
            pkg-config
            qt6.qtbase
            qt6.qtmultimedia
            qt6.qtserialport
            qt6.qtsvg
            qt6.qttools
            qt6.qtcreator
            libusb1
            openssl
            ffmpeg
            xorg.libX11
            xorg.libXrandr
            xorg.libXrender
            expat
            freetype
            fontconfig
            bzip2
            systemd
            gdb
            valgrind
          ];

          shellHook = ''
            echo "Openterface Mini-KVM QT Development Environment"
            echo "=============================================="
            echo ""
            echo "To build the project:"
            echo "  mkdir build && cd build"
            echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
            echo "  make -j\$(nproc)"
            echo ""
            echo "To run Qt Creator:"
            echo "  qtcreator"
            echo ""
            echo "Note: You may need to add your user to the 'dialout' group"
            echo "and install the udev rules for proper device access."
          '';
        };



                # Application for running the program
        apps.default = {
          type = "app";
          program = "${self.packages.${system}.openterface-qt}/bin/openterfaceQT";
        };
      });
}
