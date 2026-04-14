# AGENTS.md

## Overview

- `nd2-viewer` is a Qt 6 desktop viewer for Nikon ND2 microscopy files.
- The primary workflow on this machine is Windows with Qt `6.11.0` and `msvc2022_64`.
- The repo also supports macOS Apple Silicon builds via Qt `6.11.0`, VTK, and the Nikon macOS shared SDK.
- On Windows, Nikon's shared ND2 SDK is expected at `C:\Program Files\nd2readsdk-shared`.
- The 3D viewer is VTK-backed on both Windows and macOS, and VTK is now required for configure/build on both platforms.
- Build or install VTK before running the app build scripts on either platform.
- The Nikon SDK is not vendored in this repo; install it separately and override `ND2SDK_ROOT` if it is not in the default location.
- On Windows, pass `-VtkDir` to the PowerShell scripts or set `VTK_DIR` in the environment when CMake cannot already resolve `VTKConfig.cmake`.
- On macOS, the current default Qt path is `$HOME/Qt/6.11.0/macos/lib/cmake/Qt6`.
- On macOS, the helper scripts now resolve `VTK_DIR` from configuration-specific paths:
  - Debug: `$HOME/opt/vtk-9.5.2-qt611-debug/lib/cmake/vtk-9.5`, fallback `$HOME/build/vtk-9.5.2-qt611-debug/lib/cmake/vtk-9.5`
  - Release: `$HOME/opt/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5`, fallback `$HOME/build/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5`
- When documenting or recreating the local toolchain, use a Qt-enabled VTK `9.5.2` build with `VTK_BUILD_ALL_MODULES=OFF`, `VTK_BUILD_TESTING=OFF`, and `VTK_ENABLE_WRAPPING=OFF`.
- The VTK bootstrap scripts build only the module set `nd2-viewer` links against, plus required VTK dependencies. They do not try to build every available VTK module.
- Windows VTK bootstrap example:
  - clone `https://github.com/Kitware/VTK.git`, checkout `v9.5.2`, configure with MSVC + Ninja, `Qt6_DIR=C:\Qt\6.11.0\msvc2022_64\lib\cmake\Qt6`, and `CMAKE_INSTALL_PREFIX=%USERPROFILE%\opt\vtk-9.5.2-qt611-release`
  - after install, use `VTK_DIR=%USERPROFILE%\opt\vtk-9.5.2-qt611-release\lib\cmake\vtk-9.5`
- macOS VTK bootstrap example:
  - clone `https://github.com/Kitware/VTK.git`, checkout `v9.5.2`, configure with `Qt6_DIR=$HOME/Qt/6.11.0/macos/lib/cmake/Qt6`, `CMAKE_INSTALL_PREFIX=$HOME/opt/vtk-9.5.2-qt611-release`, `VTK_BUILD_ALL_MODULES=OFF`, `VTK_BUILD_TESTING=OFF`, and `VTK_ENABLE_WRAPPING=OFF`
  - after install, use `VTK_DIR=$HOME/opt/vtk-9.5.2-qt611-release/lib/cmake/vtk-9.5`
- `libCZI` must exist at `third_party/libczi`; if it is missing, clone it with `git clone https://github.com/ZEISS/libczi.git third_party/libczi`.

## Preferred Build And Run Commands

- Bootstrap VTK on Windows: `.\scripts\build-vtk-msvc.ps1 -Configuration Debug` or `.\scripts\build-vtk-msvc.ps1 -Configuration Release`
- Bootstrap VTK on macOS: `./scripts/build-vtk-macos.sh --configuration Debug` or `./scripts/build-vtk-macos.sh --configuration Release`
- Day-to-day debug build: `.\scripts\build-msvc.ps1 -Configuration Debug`
- Run the built app: `.\build-msvc-debug\bin\nd2-viewer.exe` (or your chosen `-BuildDir`)
- Release packaging entrypoint: `.\scripts\package-msvc.ps1`
- Portable package instead of installer: `.\scripts\package-msvc.ps1 -Generator ZIP`
- macOS debug build: `./scripts/build-macos.sh --configuration Debug`
- macOS packaging entrypoint: `./scripts/package-macos.sh`

## Important Toolchain Note

- If you run `cmake` or `cl.exe` directly from a plain PowerShell session, the build can fail with missing standard library headers such as `type_traits`.
- The helper scripts already enter the Visual Studio developer environment for you via `VsDevCmd.bat`.
- If you build manually, do it from the VS developer environment or prefix commands with:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && <your command>"
```

## Output Directories

- Debug build tree: `build-msvc-debug`
- Release build tree: `build-msvc-release`
- macOS debug build tree: `build-macos-debug`
- macOS release build tree: `build-macos-release`
- Built executable: `build-*\bin\nd2-viewer.exe`
- Packaged artifacts: `dist`

## Packaging Notes

- `scripts/package-msvc.ps1` defaults to an `NSIS` installer and writes it into `dist`.
- NSIS must be installed for the default installer flow to work.
- A successful recent package output should now use the `0.1.7` version string, for example `dist\nd2-viewer-0.1.7-win64.exe`.
- On Windows, the default install prefix is now per-user under `%LOCALAPPDATA%\Programs\nd2-viewer`, not `Program Files`.
- `scripts/build-msvc.ps1` now runs `windeployqt` after a successful build so the build tree is directly runnable.
- The Windows deploy/package flow now expects the release payload to contain `icu.dll`, `icuin.dll`, and `icuuc.dll` alongside `Qt6Core.dll`; packaging should fail if any of them are missing.
- `scripts/build-macos.sh` now runs `macdeployqt` after a successful build so the `.app` bundle is directly runnable.
- CPack packages the release runtime payload from `build-msvc-release\bin`, not the debug tree.
- `scripts/package-msvc.ps1` is package-only now. Run `.\scripts\build-msvc.ps1 -Configuration Release` first.
- `scripts/package-macos.sh` is package-only now. Run `./scripts/build-macos.sh --configuration Release` first.
- `scripts/package-macos.sh` packages the already-deployed release `.app` bundle into `dist`.

## Repo Layout

- `src/core`
  - ND2 file reading, frame caching, rendering, and controller logic.
- `src/ui`
  - Main window, viewport, and channel controls.
- `scripts`
  - Windows PowerShell helpers and macOS shell helpers for build, run, and packaging.

## Current Behavior Worth Knowing

- Navigator sliders are intentionally deferred now: moving a slider updates the paired spin box immediately, but frame loads commit only when the slider interaction ends.
- Coordinate-driven frame loads now prepare metadata, auto-contrast, and rendered images off the UI thread before applying the result.
- Spin boxes still commit immediately for precise stepping.
- The CZI reader now composes standard, sparse, tiled, mosaic, and pyramid planes, and it may choose a shared higher pyramid level for normal viewing when that keeps large documents responsive.
- CZI `Phase`, `View`, and `Block` dimensions are part of the existing dynamic loop model already present before `v0.1.6`; the `0.1.6` CZI work was about irregular plane composition and pyramid handling, not adding those loops.
- `Live auto` is now percentile-based per channel. Each channel keeps its own min/max percentile defaults and uses them on frame reloads and movie export.
- The channel tune control opens a histogram dialog for the current frame. Numeric percentile edits preview immediately, while dragging the min/max threshold lines is intentionally deferred until mouse release.
- The integrated `3D` mode is available for files with a usable z-loop and reuses the main viewer instead of opening a separate window.
- The 3D viewer loads the full z-stack in the background, keeps its own channel colors and contrast state, supports `Balanced`, `Volume`, and `Detail` render modes, and now always uses the VTK backend.
- CZI pyramid-backed 3D volumes now inherit the selected pyramid layer's XY voxel spacing, so downsampled pyramid reads should not look stretched along Z.
- `Balanced`, `Volume`, and `Detail` now share the same Y-axis orientation instead of rendering upside-down copies of each other.
- The shared view action now shows `Fit` in 2D and `Reset` in 3D.
- 2D `Fit` is a one-shot action and no longer behaves like a persistent auto-fit mode during playback or frame changes.
- 3D `Reset` restores the default camera angle and refits the occupied part of the volume.
- Movie export now uses `start`, `end`, and `step` terminology. The config dialog no longer asks for a path up front; after `Continue`, the save dialog suggests a filename that includes fixed non-time coordinates plus `movie_start..._end..._step...`.

## Validation Expectations

- There is no automated test suite in the repo right now.
- Normal verification is:
  - build Debug
  - build Release
  - run the app manually
  - if relevant, run the package script
- On this machine, the default validation path is still the MSVC scripts unless the task is specifically about macOS packaging.
- For 3D viewer work, manually verify:
  - the `3D` mode toggle stays disabled for 2D-only files and enables for ND2/CZI z-stacks
  - sparse, tiled, mosaic, and pyramid CZI planes still open and render instead of failing at load time
  - downsampled CZI pyramid layers do not make the 3D render look artificially stretched along Z
  - `Balanced`, `Volume`, and `Detail` align on the same structures instead of showing vertically mirrored copies
  - `Fit` in 2D and `Reset` in 3D behave predictably
  - orbit, zoom, and render-mode switching behave predictably
  - 3D channel toggles, colors, and auto-contrast adjustments do not change the main 2D view
- For slider/navigation work, manually verify:
  - drag/click interactions only update once per completed slider interaction
  - first frame navigation feels responsive
  - metadata still updates correctly
- For percentile auto-contrast work, manually verify:
  - high-intensity artifacts no longer dominate `Live auto` with the default percentile range
  - the histogram dialog reflects the current frame and `Cancel` restores the original settings
  - dragging histogram threshold lines updates the markers live but only commits the image preview on release
- For movie export UI work, manually verify:
  - `Start frame` defaults to `0`
  - `Step` defaults to `1`
  - the final save dialog appears only after the config dialog is accepted
  - the suggested filename includes fixed non-time coordinates and the selected `start`, `end`, and `step`

## Editing Guidance

- Do not edit `build-msvc-debug`, `build-msvc-release`, `build-macos-debug`, `build-macos-release`, or `dist` unless the task is specifically about generated outputs.
- Prefer changing source under `src` and using the scripts to validate.
- Keep changes compatible with the Windows/MSVC Qt flow, the macOS Qt/VTK defaults, and the required VTK integration already encoded in `CMakeLists.txt` and the helper scripts.
