# nd2-viewer

nd2-viewer is not a qt6 desktop viewer for nd2 but ND^2 (n-dimensional data).

## Features

- Open `.nd2` files with Nikon's official SDK
- Open supported `.czi` files with vendored `libCZI`
- Navigate experiment loops dynamically, including time, z, scene, rotation, illumination, phase, view, block, XY, and other SDK-exposed loops
- Render 8-bit, 16-bit, and 32-bit float image data
- Toggle channels and adjust per-channel contrast
- Use percentile-based per-channel live auto contrast with a histogram tuning dialog
- Switch between integrated `2D` and `3D` viewer modes for z-stacks from ND2 and CZI files
- Explore z-stacks in 3D with the VTK-backed viewer, including orbit, zoom, `Reset`, and render-mode switching
- Keep 3D channel visibility, colors, and live auto contrast independent from the main 2D view
- Export MP4 movies with explicit `start`, `end`, and `step` controls on the time axis
- Inspect attributes, experiment metadata, frame metadata, and text info
- Zoom, fit-to-window, pan, and inspect pixel values

## Build

### Default toolchains

The project supports:

- Windows: `Qt 6.11.0 msvc2022_64` plus the Nikon Windows SDK and VTK
- macOS Apple Silicon: `Qt 6.11.0` plus the Nikon macOS shared SDK and VTK

Recommended VTK bootstrap commands:

- Windows: `.\scripts\build-vtk-msvc.ps1 -Configuration Debug` or `.\scripts\build-vtk-msvc.ps1 -Configuration Release`
- macOS Apple Silicon: `./scripts/build-vtk-macos.sh --configuration Debug` or `./scripts/build-vtk-macos.sh --configuration Release`

### Dependency setup

Before building, make sure the external reader dependencies are present:

- Install Nikon's shared ND2 SDK and point `ND2SDK_ROOT` at it.
- Build or install VTK before building `nd2-viewer`, then make `VTK_DIR` discoverable to CMake. The 3D viewer now always uses the VTK backend; there is no legacy OpenGL fallback on either Windows or macOS.
- Clone `libCZI` into `third_party/libczi`.

Typical setup commands:

```powershell
git clone https://github.com/ZEISS/libczi.git third_party/libczi
```

Expected SDK locations on the main supported platforms:

- Windows: install the Nikon shared SDK so it is available at `C:\Program Files\nd2readsdk-shared`, or override `ND2SDK_ROOT`.
- macOS Apple Silicon: unpack/install the Nikon shared SDK so it is available at `$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8`, or override `ND2SDK_ROOT`.

Current Qt and VTK defaults:

- Windows: the helper scripts default to `QtRoot=C:\Qt\6.11.0\msvc2022_64` and resolve `VTK_DIR` by configuration:
  - Debug: `%USERPROFILE%\opt\vtk-9.5.2-qt611-debug\lib\cmake\vtk-9.5`, fallback `%USERPROFILE%\build\vtk-9.5.2-qt611-debug\lib\cmake\vtk-9.5`
  - Release: `%USERPROFILE%\opt\vtk-9.5.2-qt611-release\lib\cmake\vtk-9.5`, fallback `%USERPROFILE%\build\vtk-9.5.2-qt611-release\lib\cmake\vtk-9.5`
- macOS Apple Silicon: `Qt6_DIR=$HOME/Qt/6.11.0/macos/lib/cmake/Qt6`
- macOS Apple Silicon: build or install VTK first. `VTK_DIR` defaults by configuration:
  - Debug: `$HOME/opt/vtk-9.5.2-qt611-debug/lib/cmake/vtk-9.5`, fallback `$HOME/build/vtk-9.5.2-qt611-debug/lib/cmake/vtk-9.5`
  - Release: `$HOME/opt/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5`, fallback `$HOME/build/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5`

The build uses the vendored `libCZI` checkout at `third_party/libczi`, so if that directory is missing or empty the configure step will fail until it is cloned there.

### Build VTK First

The app expects a Qt-enabled VTK 9.5 build. Build VTK against the same Qt 6.11 installation that you will use for `nd2-viewer`.

The intended path is to use the bootstrap scripts:

```powershell
.\scripts\build-vtk-msvc.ps1 -Configuration Debug
.\scripts\build-vtk-msvc.ps1 -Configuration Release
```

```bash
./scripts/build-vtk-macos.sh --configuration Debug
./scripts/build-vtk-macos.sh --configuration Release
```

Those scripts do not build every VTK target. They request only the VTK modules `nd2-viewer` links against, plus the dependency closure VTK needs to satisfy them.

Windows MSVC example:

```powershell
$source = Join-Path $HOME "src\VTK"
$build = Join-Path $HOME "build\vtk-9.5.2-qt611-release"
$install = Join-Path $HOME "opt\vtk-9.5.2-qt611-release"

git clone https://github.com/Kitware/VTK.git $source
cd $source
git checkout v9.5.2

$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$qt6 = "C:\Qt\6.11.0\msvc2022_64\lib\cmake\Qt6"

cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && `"$cmake`" -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$install -DQt6_DIR=$qt6 -DVTK_BUILD_ALL_MODULES=OFF -DVTK_BUILD_EXAMPLES=OFF -DVTK_BUILD_TESTING=OFF -DVTK_ENABLE_WRAPPING=OFF -DVTK_MODULE_ENABLE_VTK_CommonCore=YES -DVTK_MODULE_ENABLE_VTK_CommonDataModel=YES -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES -DVTK_MODULE_ENABLE_VTK_RenderingCore=YES -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES -DVTK_MODULE_ENABLE_VTK_RenderingVolume=YES -DVTK_MODULE_ENABLE_VTK_RenderingVolumeOpenGL2=YES && `"$cmake`" --build $build --config Release -j 8 && `"$cmake`" --install $build --config Release"
```

After that, point `nd2-viewer` at:

```powershell
$env:VTK_DIR = Join-Path $HOME "opt\vtk-9.5.2-qt611-release\lib\cmake\vtk-9.5"
.\scripts\build-msvc.ps1 -Configuration Release
```

macOS Apple Silicon example:

```bash
git clone https://github.com/Kitware/VTK.git "$HOME/src/VTK"
cd "$HOME/src/VTK"
git checkout v9.5.2

cmake -S "$HOME/src/VTK" -B "$HOME/build/vtk-9.5.2-qt611-release" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/opt/vtk-9.5.2-qt611-release" \
  -DQt6_DIR="$HOME/Qt/6.11.0/macos/lib/cmake/Qt6" \
  -DVTK_BUILD_ALL_MODULES=OFF \
  -DVTK_BUILD_EXAMPLES=OFF \
  -DVTK_BUILD_TESTING=OFF \
  -DVTK_ENABLE_WRAPPING=OFF \
  -DVTK_MODULE_ENABLE_VTK_CommonCore=YES \
  -DVTK_MODULE_ENABLE_VTK_CommonDataModel=YES \
  -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES \
  -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingCore=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingVolume=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingVolumeOpenGL2=YES

cmake --build "$HOME/build/vtk-9.5.2-qt611-release" --parallel
cmake --install "$HOME/build/vtk-9.5.2-qt611-release"
```

After that, `./scripts/build-macos.sh` should pick up:

```bash
export VTK_DIR="$HOME/opt/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5"
./scripts/build-macos.sh --configuration Release
```

The easiest path is:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
.\build-msvc-debug\bin\nd2-viewer.exe
```

The build script enters the Visual Studio build environment for you, configures CMake with the MSVC Qt kit, and builds into `build-msvc-debug` or `build-msvc-release`.

Build or install VTK first. The Windows helper script requires `-Configuration Debug` or `-Configuration Release` and auto-detects the matching config-specific VTK path, or you can still set `VTK_DIR` or pass `-VtkDir`.

On macOS, the easiest path is:

```bash
./scripts/build-macos.sh --configuration Debug
```

The macOS build script also supports explicit options:

```bash
./scripts/build-macos.sh --configuration Release --build-dir build-macos-release
```

That script defaults to:

- `Qt6_DIR=$HOME/Qt/6.11.0/macos/lib/cmake/Qt6`
- `VTK_DIR=$HOME/opt/vtk-9.5.2-qt611-<config>/lib/cmake/vtk-9.5`, fallback `$HOME/build/vtk-9.5.2-qt611-<config>/lib/cmake/vtk-9.5`
- `ND2SDK_ROOT=$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8`
- `build_dir=build-macos-debug` or `build-macos-release`
- no implicit configuration; `--configuration` is required
- the build script runs `macdeployqt` after a successful build so the `.app` bundle is directly runnable

If your SDK, Qt, or VTK lives elsewhere, override `ND2SDK_ROOT`, `Qt6_DIR`, or `VTK_DIR` when invoking the script.

To produce a macOS archive package, run:

```bash
./scripts/package-macos.sh
```

That script assumes a completed release build in `build-macos-release` and writes a DMG into `dist` from the already-deployed `.app` bundle.

### Build a Windows installer

To produce a Windows installer, build a release tree and package it with CPack:

```powershell
.\scripts\build-msvc.ps1 -Configuration Release
.\scripts\package-msvc.ps1
```

That script assumes a completed release build in `build-msvc-release` and writes the package into `dist`.

Installer notes:

- The default generator is `NSIS`, which produces a standard `.exe` installer.
- On Windows, the default install location is per-user under `%LOCALAPPDATA%\Programs\nd2-viewer`, not `Program Files`.
- `makensis.exe` must be on `PATH` for the NSIS package to be created.
- If you want a portable archive instead, run `.\scripts\package-msvc.ps1 -Generator ZIP`.

### Manual MSVC build

If you want to run the steps yourself:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && `"$cmake`" -S . -B build-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt6_DIR=C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6 -DVTK_DIR=$env:USERPROFILE/opt/vtk-9.5.2-qt611-debug/lib/cmake/vtk-9.5 -DND2SDK_ROOT=C:/Program Files/nd2readsdk-shared && `"$cmake`" --build build-msvc-debug --config Debug -j 8"
```

If CMake does not already know where VTK is installed on Windows, build/install VTK first and add `-DVTK_DIR=<path-to-VTKConfig.cmake-directory>` to the configure command above.

## Notes

- `CMakeLists.txt` copies the ND2 SDK runtime DLLs and the vendored `libCZI` runtime after build.
- On Windows, `scripts/build-msvc.ps1` runs `scripts/msvc-windeployqt.ps1` after a successful build so the build tree is directly runnable and packaging reuses that deployed tree.
- The 3D viewer now uses the VTK/QVTK integration on both Windows and macOS, and the VTK backend is the only supported 3D path.
- CPack installs the built runtime payload from `build-*/bin`, so package from a release build rather than a debug build.
- The installer bundles the Microsoft VC++ runtime when available through the active MSVC toolchain.
- `scripts/build-msvc.ps1` is the intended day-to-day build entrypoint on this machine.
- `scripts/build-macos.sh` is the intended macOS build entrypoint.
- `scripts/package-macos.sh` is the macOS packaging entrypoint.
- `scripts/package-msvc.ps1` is the release packaging entrypoint.
- On Windows, the project supports only the MSVC Qt toolchain.
- The current implementation is read-only and focused on core viewing workflows.
- The CZI reader now composes standard, sparse, tiled, mosaic, and pyramid CZI planes through `libCZI`, including shared higher pyramid levels when they are selected for normal viewing.
- CZI phase/view/block loop parsing is not new to `0.1.6`; those dimensions were already exposed through the dynamic loop model before `v0.1.5`.
- The current CZI reader still does not do ROI-aware virtualized loading; it composes the selected plane or pyramid level into a full frame for viewing.
- In 3D, CZI pyramid-backed volumes now keep the correct physical XY spacing for the selected pyramid layer, so downsampled pyramid reads do not exaggerate Z thickness.
- Per-channel `Live auto` now uses configurable min/max percentiles instead of raw min/max, which makes it less sensitive to isolated bright artifacts.
- The histogram tuning dialog previews numeric percentile edits immediately, while dragged threshold lines commit the image preview on mouse release.
- The integrated `3D` mode is enabled only for files with a usable z-loop and reuses the current shared viewer state.
- The shared view action now shows `Fit` in 2D and `Reset` in 3D.
- `Fit` in 2D is a one-shot action and no longer acts like a live auto-fit mode during playback or navigation.
- The 3D mode supports `Balanced`, `Volume`, and `Detail` render modes and a `Reset` action that restores the default camera angle and refits the occupied volume.
- The `Balanced`, `Volume`, and `Detail` 3D render modes now share the same vertical orientation, so overlays line up instead of appearing vertically mirrored.
- Movie export now opens a config dialog first, then prompts for the final save path. The suggested filename includes fixed non-time coordinates plus the chosen `start`, `end`, and `step` range.
