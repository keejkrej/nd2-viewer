# nd2-viewer

nd2-viewer is not a qt6 desktop viewer for nd2 but ND^2 (n-dimensional data).

## Features

- Open `.nd2` files with Nikon's official SDK
- Open supported `.czi` files with vendored `libCZI`
- Navigate experiment loops dynamically (time, z, XY, and other SDK-exposed loops)
- Render 8-bit, 16-bit, and 32-bit float image data
- Toggle channels and adjust per-channel contrast
- Use percentile-based per-channel live auto contrast with a histogram tuning dialog
- Open a separate `Tools > 3D View` window for z-stacks from ND2 and CZI files
- Explore z-stacks in 3D with orbit, zoom, `Fit To Volume`, `Reset View`, and render-mode switching
- Keep 3D channel visibility, colors, and live auto contrast independent from the main 2D view
- Export MP4 movies with explicit `start`, `end`, and `step` controls on the time axis
- Inspect attributes, experiment metadata, frame metadata, and text info
- Zoom, fit-to-window, pan, and inspect pixel values

## Build

### Default toolchains

The project supports:

- Windows: `Qt 6 msvc2022_64` plus the Nikon Windows SDK
- macOS Apple Silicon: Homebrew `Qt 6` plus the Nikon macOS shared SDK

The easiest path is:

```powershell
.\scripts\build-msvc.ps1
.\scripts\run-msvc.ps1
```

The helper script enters the Visual Studio build environment for you, configures CMake with the MSVC Qt kit, and builds into `build-msvc`.

On macOS, the easiest path is:

```bash
./scripts/build-macos.sh
```

The macOS build script also supports explicit options:

```bash
./scripts/build-macos.sh --configuration Release --build-dir build-macos-release
```

That script defaults to:

- `Qt6_DIR=/opt/homebrew/lib/cmake/Qt6`
- `ND2SDK_ROOT=$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8`
- `build_dir=build-macos`
- `configuration=Debug`

If your SDK lives elsewhere, override `ND2SDK_ROOT` when invoking the script.

To produce a macOS archive package, run:

```bash
./scripts/package-macos.sh
```

That script builds a release tree in `build-macos-release`, deploys a real `nd2-viewer.app` bundle, and writes a DMG into `dist` by default.

### Build a Windows installer

To produce a Windows installer, build a release tree and package it with CPack:

```powershell
.\scripts\package-msvc.ps1
```

That script builds into `build-msvc-release` by default and writes the package into `dist`.

Installer notes:

- The default generator is `NSIS`, which produces a standard `.exe` installer.
- `makensis.exe` must be on `PATH` for the NSIS package to be created.
- If you want a portable archive instead, run `.\scripts\package-msvc.ps1 -Generator ZIP`.

### Manual MSVC build

If you want to run the steps yourself:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && `"$cmake`" -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt6_DIR=C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6 -DND2SDK_ROOT=C:/Program Files/nd2readsdk-shared && `"$cmake`" --build build-msvc --config Debug -j 8"
```

## Notes

- `CMakeLists.txt` copies the ND2 SDK runtime DLLs and the vendored `libCZI` runtime after build.
- If `windeployqt` is available from the selected Qt kit, it is run automatically after build.
- The 3D viewer uses Qt OpenGL and ships as part of the normal desktop app, there is no Napari runtime dependency.
- CPack installs the built runtime payload from `build-*/bin`, so package from a release build rather than a debug build.
- The installer bundles the Microsoft VC++ runtime when available through the active MSVC toolchain.
- `scripts/build-msvc.ps1` is the intended day-to-day build entrypoint on this machine.
- `scripts/build-macos.sh` is the intended macOS build entrypoint.
- `scripts/package-macos.sh` is the macOS packaging entrypoint.
- `scripts/package-msvc.ps1` is the release packaging entrypoint.
- On Windows, the project supports only the MSVC Qt toolchain.
- The current implementation is read-only and focused on core viewing workflows.
- The first CZI milestone supports standard plane-based files and rejects tiled, mosaic, pyramid, or otherwise irregular CZI layouts with a clear open-time error.
- Per-channel `Live auto` now uses configurable min/max percentiles instead of raw min/max, which makes it less sensitive to isolated bright artifacts.
- The histogram tuning dialog previews numeric percentile edits immediately, while dragged threshold lines commit the image preview on mouse release.
- `Tools > 3D View` is enabled only for files with a usable z-loop and opens a separate 3D window seeded from the current 2D channel state.
- The 3D window now supports `Balanced`, `Volume`, and `Detail` render modes plus an explicit `Fit To Volume` action for reframing the occupied part of the stack.
- The `Balanced`, `Volume`, and `Detail` 3D render modes now share the same vertical orientation, so overlays line up instead of appearing vertically mirrored.
- Movie export now opens a config dialog first, then prompts for the final save path. The suggested filename includes fixed non-time coordinates plus the chosen `start`, `end`, and `step` range.
