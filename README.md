# nd2-viewer

nd2-viewer is not a qt6 desktop viewer for nd2 but ND^2 (n-dimensional data).

## Features

- Open `.nd2` files with Nikon's official SDK
- Open supported `.czi` files with libCZI (from vcpkg)
- Navigate experiment loops dynamically, including time, z, scene, rotation, illumination, phase, view, block, XY, and other SDK-exposed loops
- Render 8-bit, 16-bit, and 32-bit float image data
- Toggle channels and adjust per-channel contrast
- Use percentile-based per-channel live auto contrast with a histogram tuning dialog
- Run 2D Richardson-Lucy deconvolution previews from the current raw frame
- Switch between integrated `2D` and `3D` viewer modes for z-stacks from ND2 and CZI files
- Explore z-stacks in 3D with the VTK-backed viewer, including orbit, zoom, `Reset`, and render-mode switching
- Keep 3D channel visibility, colors, and live auto contrast independent from the main 2D view
- Export MP4 movies with explicit `start`, `end`, and `step` controls on the time axis
- Inspect attributes, experiment metadata, frame metadata, and text info
- Zoom, fit-to-window, pan, and inspect pixel values

## Build

### Dependencies (vcpkg)

Qt, VTK, ITK, and libCZI are pulled in via [vcpkg](https://vcpkg.io/) using the repo `vcpkg.json` manifest. Install vcpkg yourself (on Windows, `scoop install vcpkg` is typical). The build scripts find it via `VCPKG_ROOT`, the Scoop install path, or `vcpkg` on `PATH`.

On Windows with Scoop, use the helper scripts or point `VCPKG_ROOT` at the real version directory under `scoop\apps\vcpkg\<version>`. Qt rejects vcpkg build paths that go through Scoop's `current` junction.

- Windows triplet: `x64-windows` (default in `scripts/build-msvc.ps1`)
- macOS triplet: `arm64-osx` or `x64-osx` (default from `uname -m` in `scripts/build-macos.sh`)

Install Nikon's shared ND2 SDK and point `ND2SDK_ROOT` at it:

- Windows: `C:\Program Files\nd2readsdk-shared` (or override)
- macOS: `$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8` (or override)

Install manifest dependencies once (or after changing `vcpkg.json`); the first run can take a long time while VTK and ITK build:

```powershell
.\scripts\install-vcpkg-deps.ps1
```

The easiest path on Windows:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
.\build-msvc-debug\bin\nd2-viewer.exe
```

The build script enters the Visual Studio build environment, configures CMake with the vcpkg toolchain, and builds into `build-msvc-debug` or `build-msvc-release`.

On macOS, warm up vcpkg deps first, then build:

```bash
./scripts/install-vcpkg-deps.sh
./scripts/build-macos.sh --configuration Debug
```

The macOS script also supports explicit options:

```bash
./scripts/build-macos.sh --configuration Release --build-dir build-macos-release
```

Defaults when using vcpkg:

- `ND2SDK_ROOT=$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8`
- `build_dir=build-macos-debug` or `build-macos-release`
- `--configuration` is required
- the build script runs `macdeployqt` after a successful build so the `.app` bundle is directly runnable

Override `ND2SDK_ROOT` when needed. For a non-vcpkg Qt/VTK install on macOS only, pass `--qt6-dir` and `--vtk-dir` to `build-macos.sh`.

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
- Windows release deployments now bundle the ICU runtime set required by `Qt6Core.dll`, and packaging will stop with an error if `icu.dll`, `icuin.dll`, or `icuuc.dll` are missing from the release payload.

### Manual MSVC build

If you want to run CMake yourself, use the vcpkg toolchain (after `.\scripts\install-vcpkg-deps.ps1` or an equivalent `vcpkg install` for this manifest):

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = (Get-Command cmake.exe).Source
$vcpkgRoot = "$env:VCPKG_ROOT"
if (-not $vcpkgRoot) { $vcpkgRoot = "$PWD\vcpkg" }
$toolchain = "$vcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && `"$cmake`" -S . -B build-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=$toolchain -DVCPKG_TARGET_TRIPLET=x64-windows -DND2SDK_ROOT=C:/Program Files/nd2readsdk-shared && `"$cmake`" --build build-msvc-debug --config Debug -j 8"
```

Run `windeployqt` from vcpkg's Qt (`installed\x64-windows\tools\Qt6\bin\windeployqt.exe`) so the build tree is runnable.

## Notes

- `CMakeLists.txt` copies the ND2 SDK runtime DLLs and the libCZI runtime after build.
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
- The app now treats loop coordinates as the primary reader API for both ND2 and CZI. Frame reads and frame-metadata reads go through coordinate-based access, while sequence indices remain backend details used mainly for status text and diagnostics.
- The CZI reader now composes standard, sparse, tiled, mosaic, and pyramid CZI planes through `libCZI`, including shared higher pyramid levels when they are selected for normal viewing.
- CZI phase/view/block loop parsing is not new to `0.1.6`; those dimensions were already exposed through the dynamic loop model before `v0.1.5`.
- The current CZI reader still does not do ROI-aware virtualized loading; it composes the selected plane or pyramid level into a full frame for viewing.
- In 3D, CZI pyramid-backed volumes now keep the correct physical XY spacing for the selected pyramid layer, so downsampled pyramid reads do not exaggerate Z thickness.
- Plane read failures are now reported with loop coordinates when available, for example `Time=51, Z=9, Phase=0`, instead of only a global frame number.
- Movie export now drives the visible viewer in both 2D and 3D. During export, the app temporarily switches readers into a tolerant policy that substitutes black data for failed ND2/CZI reads instead of aborting immediately.
- Export-time read substitutions do not show modal read-error popups. Instead, the export continues, the affected frame or slice is rendered black, and a `.warnings.txt` sidecar report is written next to the MP4 when substitutions occurred.
- Per-channel `Live auto` now uses configurable min/max percentiles instead of raw min/max, which makes it less sensitive to isolated bright artifacts.
- The histogram tuning dialog previews numeric percentile edits immediately, while dragged threshold lines commit the image preview on mouse release.
- The integrated `3D` mode is enabled only for files with a usable z-loop and reuses the current shared viewer state.
- The shared view action now shows `Fit` in 2D and `Reset` in 3D.
- `Fit` in 2D is a one-shot action and no longer acts like a live auto-fit mode during playback or navigation.
- The 3D mode supports `Balanced`, `Volume`, and `Detail` render modes and a `Reset` action that restores the default camera angle and refits the occupied volume.
- The `Balanced`, `Volume`, and `Detail` 3D render modes now share the same vertical orientation, so overlays line up instead of appearing vertically mirrored.
- Movie export now opens a config dialog first, then prompts for the final save path. The suggested filename includes fixed non-time coordinates plus the chosen `start`, `end`, and `step` range.
