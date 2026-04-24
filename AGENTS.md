# AGENTS.md

## Overview

- `nd2-viewer` is a Qt 6 desktop viewer for Nikon ND2 microscopy files.
- The primary workflow on this machine is Windows with MSVC; **Qt, VTK, ITK, and libCZI are supplied by [vcpkg](https://vcpkg.io/)** via the repo `vcpkg.json` manifest (not a standalone `C:\Qt\...` install).
- The repo also supports macOS Apple Silicon (and Intel) builds via the same vcpkg manifest plus the Nikon macOS shared SDK.
- On Windows, Nikon's shared ND2 SDK is expected at `C:\Program Files\nd2readsdk-shared`.
- The 3D viewer is VTK-backed on both Windows and macOS; VTK is required for configure/build on both platforms.
- The 2D deconvolution tool uses ITK `ITKCommon` and `ITKDeconvolution`; ITK is required for configure/build on both platforms.
- The Nikon SDK is not vendored in this repo; install it separately and override `ND2SDK_ROOT` if it is not in the default location.
- Install vcpkg separately (e.g. Windows: `scoop install vcpkg`). The build scripts locate it via `VCPKG_ROOT`, `%USERPROFILE%\scoop\apps\vcpkg\current`, or `vcpkg` on `PATH` (must resolve to the real tree that contains `scripts/buildsystems/vcpkg.cmake`, not only a shim). They normalize Scoop's `current` junction to the real version directory because Qt rejects build paths containing symlinks. They run `vcpkg install` for the manifest, then configure with `CMAKE_TOOLCHAIN_FILE` and `-DVCPKG_TARGET_TRIPLET` (`x64-windows` on Windows; `arm64-osx` / `x64-osx` on macOS by default).
- Advanced: pass `--qt6-dir` and `--vtk-dir` to `build-macos.sh` only if you want to build against a non-vcpkg Qt/VTK install.

## Preferred Build And Run Commands

- Warm up vcpkg dependencies (Qt, VTK, ITK, libczi): `.\scripts\install-vcpkg-deps.ps1` (re-run after `vcpkg.json` changes; first run can take a long time while VTK and ITK build)
- macOS warm-up: `./scripts/install-vcpkg-deps.sh`
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
- The app-facing reader API is now coordinate-first for both ND2 and CZI: normal frame reads and frame-metadata reads use loop coordinates, while sequence indices are retained mainly for diagnostics and status text.
- Spin boxes still commit immediately for precise stepping.
- The CZI reader now composes standard, sparse, tiled, mosaic, and pyramid planes, and it may choose a shared higher pyramid level for normal viewing when that keeps large documents responsive.
- CZI `Phase`, `View`, and `Block` dimensions are part of the existing dynamic loop model already present before `v0.1.6`; the `0.1.6` CZI work was about irregular plane composition and pyramid handling, not adding those loops.
- ND2 and CZI read errors should now be surfaced from the higher-level coordinate read path, so user-visible messages name loop coordinates like `Time=51, Z=9, Phase=0` instead of only a global frame number.
- Movie export now advances the visible viewer in both 2D and 3D. Export temporarily switches the higher-level read path into a tolerant substitution policy, so failed reads become black frame/slice data for export instead of aborting immediately.
- Export-time substitutions should be summarized in the warning sidecar written beside the MP4 instead of surfaced as modal read-error popups during the export loop.
- `Live auto` is now percentile-based per channel. Each channel keeps its own min/max percentile defaults and uses them on frame reloads and movie export.
- The channel tune control opens a histogram dialog for the current frame. Numeric percentile edits preview immediately, while dragging the min/max threshold lines is intentionally deferred until mouse release.
- The integrated `3D` mode is available for files with a usable z-loop and reuses the main viewer instead of opening a separate window.
- `Tools > Deconvolution...` is a 2D-only Richardson-Lucy preview tool. It snapshots the current raw frame and channel settings, runs ITK in the background, and opens an independent result window without mutating the main viewer state.
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
  - 2D and 3D exports visibly advance through the selected timepoints in the main viewer
  - read failures during export substitute black data and still produce an MP4
  - a `.warnings.txt` sidecar is written when substitutions occur
  - the viewer returns to the original timepoint after export completes or fails

## Editing Guidance

- Do not edit `build-msvc-debug`, `build-msvc-release`, `build-macos-debug`, `build-macos-release`, or `dist` unless the task is specifically about generated outputs.
- Prefer changing source under `src` and using the scripts to validate.
- Keep changes compatible with the Windows/MSVC Qt flow, the macOS Qt/VTK defaults, and the required VTK integration already encoded in `CMakeLists.txt` and the helper scripts.
