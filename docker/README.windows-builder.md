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

Qt requirement and options
------------------------
This project requires a Qt development installation (Qt5 or Qt6) available at CMake configure time. For cross-compiling to Windows (mingw-w64), CMake needs a Qt CMake package prefix containing files like `Qt6Config.cmake` or `qt6-config.cmake` for the target environment.

You have several options to supply Qt to the container:

- Mount a prebuilt cross-compiled Qt installation into the container at `/opt/Qt6`. For example (from repo root):

```bash
docker run --rm -v /path/to/your/qt-mingw-install:/opt/Qt6 -v "$(pwd)":/src -w /src openterface/windows-builder:latest
```

- Set an explicit CMake prefix path via environment variable `QT_CMAKE_PATH` (inside container) pointing to the dir that contains `lib/cmake/Qt6` or a qmake install. Example:

```bash
docker run --rm -e QT_CMAKE_PATH=/some/qt/prefix -v "$(pwd)":/src -w /src openterface/windows-builder:latest
```

- Use the repo's Qt builder container files to produce a `/opt/Qt6` inside a builder image first (see `docker/Dockerfile.qt-base-qtml`, `Dockerfile.qt-complete`) then mount those artifacts into this windows-builder container.

- Finally, you can use `vcpkg` to build Qt for the target triplet, but that is heavy and can take a very long time — only use it if you have the time and some caching strategy for CI.

When `qt` is not present, the entrypoint prints helpful instructions and will exit so you can supply the Qt path.

Notes & limitations
- Cross-compiling Qt itself or building the full static Qt toolchain is out of scope for this image (very heavy). This image is intended to make local reproducible cross-compiles easier and to be used as a starting point for automated CI builds.
- Building large third-party dependencies (FFmpeg, Qt) may still be slow — consider using pre-built toolchains or caching in CI.

Manifest note (vcpkg)
- vcpkg manifest files follow a strict schema: in particular, override entries must specify a concrete version field (`"version": "1.2.3"`) instead of inequality operators like `"version>=": "1.2.3"`. If you see errors such as:

  /src/vcpkg.json: error: $.overrides[4] (an override): unexpected field 'version>='

  then update the manifest to use `"version"` (or version-semver / version-string) for overrides. This container will attempt `vcpkg install` when a manifest exists; if install fails, the entrypoint will fallback and configure CMake with `-DVCPKG_MANIFEST_INSTALL=OFF` so the configure can continue for debugging.

Shallow clone / 'failed to unpack tree' troubleshooting
----------------------------------------------------
If you see errors like:

  error: /usr/bin/git --git-dir /opt/vcpkg/.git ... failed with exit code 128
  fatal: failed to unpack tree object ...
  /opt/vcpkg/.git: note: vcpkg was cloned as a shallow repository. Try again with a full vcpkg clone.

Those occur when the container's vcpkg installation was cloned shallow (common when Dockerfile used `--depth 1` during clone). The `entrypoint` script now attempts a best-effort recovery: if a `vcpkg install` fails with a shallow-repo message the container will try to unshallow the vcpkg git repo (fetch full history + tags) and retry the install once.

If you still hit this in CI, two reliable fixes are:

- Change the vcpkg clone to be a full clone (no `--depth`), which is what the provided `Dockerfile.windows-builder` does now.
- Use a pre-provisioned vcpkg cache / volume in CI so port checkouts don't depend on shallow clones or transient network problems.

Publishing / Pulling the image from GitHub Packages (GHCR)
-----------------------------------------------------
This repository's GitHub Actions workflow can publish the built image to GitHub Container Registry (GHCR) under `ghcr.io/${{ github.repository_owner }}/openterface/windows-builder`.

To pull the published image locally (example for owner `OWNER`):

```bash
# Authenticate with GHCR using your GitHub personal access token (or GITHUB_TOKEN in CI)
echo "${GHCR_PAT}" | docker login ghcr.io -u YOUR_GITHUB_USERNAME --password-stdin

docker pull ghcr.io/OWNER/openterface/windows-builder:latest
docker run --rm -v $(pwd):/src -w /src ghcr.io/OWNER/openterface/windows-builder:latest
```

Note: by default GitHub Packages image visibility may be private — make sure repository/package settings allow the intended access or use a GitHub PAT with read permissions to pull images in CI or locally.
