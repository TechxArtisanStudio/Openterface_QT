# Windows cross-build Docker image

This container image is an Ubuntu-based environment prepared to cross-compile Windows (x86_64) artifacts using the mingw-w64 toolchain and `vcpkg`.

What is included
- mingw-w64 cross compilers (x86_64-w64-mingw32-*)
- ninja, cmake, gcc/g++ toolchain
- vcpkg (bootstrap installed at /opt/vcpkg)
- build entrypoint: `/usr/local/bin/entrypoint-windows-build.sh`

How to use locally

1. Build the image from the repository root:

```bash
docker build -f docker/Dockerfile.windows-builder -t openterface/windows-builder:latest .
```

2. Run a build in the container (mount your repo into /src):

```bash
docker run --rm -v $(pwd):/src -w /src openterface/windows-builder:latest
```

The container will attempt a vcpkg install (if `vcpkg.json` is present) and then run a cross-compile `cmake`/`ninja` build. If successful, the built executable will be copied to `build/package/openterfaceQT-portable.exe` in your working copy.

Notes & limitations
- Cross-compiling Qt itself or building the full static Qt toolchain is out of scope for this image (very heavy). This image is intended to make local reproducible cross-compiles easier and to be used as a starting point for automated CI builds.
- Building large third-party dependencies (FFmpeg, Qt) may still be slow — consider using pre-built toolchains or caching in CI.

Manifest note (vcpkg)
- vcpkg manifest files follow a strict schema: in particular, override entries must specify a concrete version field (`"version": "1.2.3"`) instead of inequality operators like `"version>=": "1.2.3"`. If you see errors such as:

  /src/vcpkg.json: error: $.overrides[4] (an override): unexpected field 'version>='

  then update the manifest to use `"version"` (or version-semver / version-string) for overrides. This container will attempt `vcpkg install` when a manifest exists; if install fails, the entrypoint will fallback and configure CMake with `-DVCPKG_MANIFEST_INSTALL=OFF` so the configure can continue for debugging.
