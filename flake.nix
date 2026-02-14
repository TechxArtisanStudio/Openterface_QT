{
  description = "Openterface Mini-KVM: Host Applications for Windows and Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }: let
    # NixOS module needs to be outside of eachDefaultSystem
    nixosModule = {
      config,
      lib,
      pkgs,
      ...
    }: {
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
        environment.systemPackages = [config.services.openterface.package];

        # Install udev rules
        services.udev.packages = [config.services.openterface.package];

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
      #
      # Usage in your flake.nix:
      #
      #   inputs.openterface.url = "github:TechxArtisanStudio/Openterface_QT";
      #
      #   nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
      #     modules = [
      #       openterface.nixosModules.openterface
      #       { services.openterface.enable = true; }
      #     ];
      #   };
      #
      # This installs the application, sets up udev rules for device
      # access, and adds users to the dialout group for serial ports.
      #
      # To just build or run without NixOS:
      #
      #   nix build github:TechxArtisanStudio/Openterface_QT
      #   nix run github:TechxArtisanStudio/Openterface_QT
      #
      nixosModules.openterface = nixosModule;
    }
    // flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};

      # Merge ffmpeg dev (headers + .so symlinks) and lib (shared libraries)
      # into a single prefix so CMake's FFmpeg detection finds both
      ffmpegMerged = pkgs.symlinkJoin {
        name = "ffmpeg-merged";
        paths = [pkgs.ffmpeg.dev pkgs.ffmpeg.lib];
      };

      openterface-qt = pkgs.stdenv.mkDerivation {
        pname = "openterface-qt";
        version = "0.5.14";

        src = self;

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
          ffmpegMerged
          xorg.libX11
          xorg.libXrandr
          xorg.libXrender
          xorg.libXv
          xorg.libXi
          xorg.libXext
          expat
          freetype
          fontconfig
          bzip2
          systemd # for libudev
          libgudev
          v4l-utils # Provides libv4l2
          libGL
          glib
          gst_all_1.gstreamer
          gst_all_1.gst-plugins-base
          libcap
          orc
        ];

        # Set UTF-8 locale and compiler flags
        env.LANG = "C.UTF-8";
        env.LC_ALL = "C.UTF-8";
        env.NIX_CFLAGS_COMPILE = "-Wno-deprecated-declarations -Wno-error -DHAVE_FFMPEG";

        postPatch = ''
          # Remove hardcoded -include /usr/include/time.h
          # This path doesn't exist on Nix; the underlying FFmpeg/system time.h
          # conflict is not an issue with Nix's isolated include paths
          substituteInPlace CMakeLists.txt \
            --replace-fail '-include /usr/include/time.h' ""

          # Remove FORCE overrides that hardcode Qt6 paths to /opt/Qt6
          # Nix's cmake hooks already set CMAKE_PREFIX_PATH correctly
          substituteInPlace cmake/Configuration.cmake \
            --replace-fail 'set(Qt6_DIR "''${QT_BUILD_PATH}/lib/cmake/Qt6" CACHE PATH "Qt6 installation directory" FORCE)' \
              '# Qt6_DIR: detected automatically via CMAKE_PREFIX_PATH (Nix)' \
            --replace-fail 'set(Qt6Core_DIR "''${QT_BUILD_PATH}/lib/cmake/Qt6Core" CACHE PATH "Qt6Core directory" FORCE)' \
              '# Qt6Core_DIR: detected automatically (Nix)' \
            --replace-fail 'set(Qt6Gui_DIR "''${QT_BUILD_PATH}/lib/cmake/Qt6Gui" CACHE PATH "Qt6Gui directory" FORCE)' \
              '# Qt6Gui_DIR: detected automatically (Nix)' \
            --replace-fail 'set(Qt6Widgets_DIR "''${QT_BUILD_PATH}/lib/cmake/Qt6Widgets" CACHE PATH "Qt6Widgets directory" FORCE)' \
              '# Qt6Widgets_DIR: detected automatically (Nix)'

          # Remove hardcoded include_directories block and implicit include stripping
          # These override Nix's correct Qt6 header paths with /opt/Qt6 paths
          sed -i '/# Prioritize static Qt6 include directories/,/Prioritized static Qt6 include directories/d' \
            cmake/Configuration.cmake
        '';

        cmakeFlags = [
          "-DCMAKE_BUILD_TYPE=Release"
          "-DOPENTERFACE_BUILD_STATIC=OFF"
          "-DUSE_SHARED_FFMPEG=ON"
          "-DFFMPEG_PREFIX=${ffmpegMerged}"
          "-DUSE_GSTREAMER=ON"
          "-DUSE_HWACCEL=OFF"
          "-DENABLE_QT_DEPLOY=OFF"
          "-DQT_NO_DEPLOY_RUNTIME_DEPENDENCIES=ON"
        ];

        postInstall = ''
          # Install udev rules for device permissions
          mkdir -p $out/lib/udev/rules.d
          cat > $out/lib/udev/rules.d/51-openterface.rules << EOF
          SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
          SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
          SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
          SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
          EOF
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
          maintainers = [];
          platforms = platforms.linux;
          mainProgram = "openterfaceQT";
        };
      };
    in {
      packages = {
        default = openterface-qt;
        openterface-qt = openterface-qt;
      };

      # Development shell with all build dependencies
      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs;
          openterface-qt.buildInputs
          ++ openterface-qt.nativeBuildInputs
          ++ [
            qt6.qtcreator
            gdb
            valgrind
          ];

        shellHook = ''
          echo "Openterface Mini-KVM QT Development Environment"
          echo "=============================================="
          echo ""
          echo "To build the project:"
          echo "  mkdir build && cd build"
          echo "  cmake .. -DCMAKE_BUILD_TYPE=Release -DOPENTERFACE_BUILD_STATIC=OFF -DUSE_SHARED_FFMPEG=ON -DUSE_GSTREAMER=OFF"
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
