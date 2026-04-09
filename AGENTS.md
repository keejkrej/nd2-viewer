# AGENTS.md

## Overview

- `nd2-viewer` is a Qt 6 desktop viewer for Nikon ND2 microscopy files.
- The primary workflow on this machine is Windows with Qt `6.11.0` and `msvc2022_64`.
- The repo also supports macOS Apple Silicon builds via Homebrew Qt 6 and the Nikon macOS shared SDK.
- On Windows, Nikon's shared ND2 SDK is expected at `C:\Program Files\nd2readsdk-shared`.
- The Nikon SDK is not vendored in this repo; install it separately and override `ND2SDK_ROOT` if it is not in the default location.
- `libCZI` must exist at `third_party/libczi`; if it is missing, clone it with `git clone https://github.com/ZEISS/libczi.git third_party/libczi`.

## Preferred Build And Run Commands

- Day-to-day debug build: `.\scripts\build-msvc.ps1`
- Run the built app: `.\build-msvc\bin\nd2-viewer.exe` (or your chosen `-BuildDir`)
- Release packaging entrypoint: `.\scripts\package-msvc.ps1`
- Portable package instead of installer: `.\scripts\package-msvc.ps1 -Generator ZIP`
- macOS debug build: `./scripts/build-macos.sh` (skips slow `macdeployqt` by default; `./scripts/package-macos.sh` deploys for the DMG)
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

- Debug build tree: `build-msvc`
- Release build tree: `build-msvc-release`
- macOS debug build tree: `build-macos`
- macOS release build tree: `build-macos-release`
- Built executable: `build-*\bin\nd2-viewer.exe`
- Packaged artifacts: `dist`

## Packaging Notes

- `scripts/package-msvc.ps1` defaults to an `NSIS` installer and writes it into `dist`.
- NSIS must be installed for the default installer flow to work.
- A successful recent package output should now use the `0.1.3` version string, for example `dist\nd2-viewer-0.1.3-win64.exe`.
- CPack packages the release runtime payload from `build-msvc-release\bin`, not the debug tree.
- `scripts/package-macos.sh` builds a release `.app` bundle and writes a DMG into `dist`.

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
- `Live auto` is now percentile-based per channel. Each channel keeps its own min/max percentile defaults and uses them on frame reloads and movie export.
- The channel tune control opens a histogram dialog for the current frame. Numeric percentile edits preview immediately, while dragging the min/max threshold lines is intentionally deferred until mouse release.
- `Tools > 3D View` is now available for files with a usable z-loop and opens a separate 3D viewer window while the main window is disabled.
- The 3D viewer loads the full z-stack in the background, keeps its own channel colors and contrast state, and supports `Balanced`, `Volume`, and `Detail` render modes.
- `Balanced`, `Volume`, and `Detail` now share the same Y-axis orientation instead of rendering upside-down copies of each other.
- `Reset View` restores the default camera angle and refits the stack, while `Fit To Volume` preserves the current angle and only reframes the occupied volume.
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
  - `Tools > 3D View` stays disabled for 2D-only files and enables for ND2/CZI z-stacks
  - the 3D window disables the main window while open, then restores it on close
  - `Balanced`, `Volume`, and `Detail` align on the same structures instead of showing vertically mirrored copies
  - `Fit To Volume`, `Reset View`, orbit, zoom, and render-mode switching behave predictably
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

- Do not edit `build-msvc`, `build-msvc-release`, or `dist` unless the task is specifically about generated outputs.
- Prefer changing source under `src` and using the scripts to validate.
- Keep changes compatible with the Windows/MSVC Qt flow already encoded in `CMakeLists.txt` and the PowerShell scripts.
