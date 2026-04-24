# Changelog

All notable changes to `nd2-viewer` are documented in this file.

## [0.1.8] - 2026-04-24

### Changed
- Bumped the project version to `0.1.8`.
- Migrated the build pipeline to vcpkg manifest dependencies for Qt, VTK, ITK, QtSvg, QtMultimedia, and libCZI.
- Switched CMake from the vendored `third_party/libczi` subdirectory to the vcpkg `libczi::libCZI` package target.
- Updated Windows and macOS build/deploy scripts to install manifest dependencies, configure with the vcpkg toolchain, and deploy from vcpkg's Qt runtime layout.

### Added
- Added vcpkg root-resolution helpers and explicit dependency warm-up scripts for Windows and macOS.
- Added Linux vcpkg build, dependency-install, triplet, and package helper scripts.
- Added a vcpkg ICU overlay used by the manifest build.

### Removed
- Removed the standalone VTK and ITK bootstrap scripts now replaced by vcpkg manifest installation.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe the vcpkg-based dependency flow and the current `0.1.8` packaging output example.

## [0.1.7] - 2026-04-14

### Changed
- Bumped the project version to `0.1.7`.
- Standardized the app-facing ND2/CZI reader flow around coordinate-based access for frame reads and frame metadata reads, while keeping sequence indices as backend details and diagnostics.
- Changed movie export so both 2D and 3D exports now advance the visible viewer through time instead of reading from a separate export-only path.

### Fixed
- Hardened Windows Qt deployment so release builds and packaged installs now bundle the full ICU runtime set (`icu.dll`, `icuin.dll`, `icuuc.dll`) instead of relying on whatever `windeployqt` happens to copy from the local machine.
- Added a package-time validation check that refuses to build a Windows installer or ZIP when the release runtime payload is missing the required ICU DLLs.
- Changed ND2 and CZI plane read failures to report loop coordinates such as `Time=51, Z=9, Phase=0` instead of only a global frame number, while low-level backend readers now propagate raw SDK/libCZI errors upward.
- Made movie export tolerant to ND2/CZI plane read failures by substituting black frame data during export, continuing the MP4 encode, and writing a warning sidecar report next to the exported movie.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe the coordinate-first reader API, the current coordinate-based error reporting behavior, and the live viewer-driven movie export policy.

## [0.1.6] - 2026-04-13

### Changed
- Bumped the project version to `0.1.6`.

### Fixed
- Reworked CZI plane loading so the reader can compose sparse, tiled, mosaic, and pyramidal CZI planes instead of failing on irregular layer-0 layouts.
- Fixed CZI 3D voxel spacing when the reader selects a downsampled pyramid layer, so the rendered volume no longer appears stretched along Z.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe the `0.1.6` CZI reader behavior, including sparse/pyramid support and the current pyramid-spacing fix.

## [0.1.5] - 2026-04-10

### Changed
- Bumped the project version to `0.1.5`.
- Simplified the shared viewport action so 2D mode uses a one-shot `Fit` action while 3D mode uses `Reset`.
- Changed 2D fit behavior to be one-shot instead of a persistent auto-fit mode, so playback and navigation retain the fitted zoom and pan state until the user changes it again.
- Made the Windows and macOS build scripts require an explicit `Debug` or `Release` configuration and use matching `-debug` / `-release` build-tree naming.
- Split VTK bootstrap and resolution into configuration-specific `vtk-9.5.2-qt611-debug` and `vtk-9.5.2-qt611-release` paths on both Windows and macOS.
- Changed the package scripts to package-only entrypoints that assume a completed release build already exists.

### Fixed
- Restored deterministic 3D reset behavior without reintroducing the unstable repeated-reset drift.
- Fixed Windows release packaging so CPack no longer inherits a `Program Files` default install prefix and now uses a per-user install root under `%LOCALAPPDATA%\Programs\nd2-viewer`.
- Fixed Windows build-tree runtime deployment by moving Qt deploy into the build step, making both debug and release build trees directly runnable after a successful build.

### Build And Packaging
- Added release-tree migration for existing VTK installs so older `vtk-9.5.2-qt611` build/install roots are renamed in place to `vtk-9.5.2-qt611-release` instead of being rebuilt.
- Added configure-time validation that rejects debug app builds when the resolved VTK package only exposes release targets.
- Moved `windeployqt` and `macdeployqt` into the normal build scripts so packaging reuses an already-deployed build tree.
- Kept NSIS and DMG packaging as release-only flows, but removed deploy work from the package scripts themselves.

### Documentation
- Updated `README.md` and `AGENTS.md` for the current in-app 2D/3D view workflow, the `Fit` / `Reset` control behavior, the explicit debug/release build commands, and the `0.1.5` packaging output example.

## [0.1.4] - 2026-04-10

### Added
- Added an integrated 3D viewer mode inside the main window, replacing the older separate 3D window workflow.
- Added VTK-backed volume rendering support with shared 3D viewport infrastructure, volume loading helpers, and dedicated VTK bootstrap scripts for Windows and macOS.
- Added 3D frame export and 3D movie export support for z-stack datasets.
- Added time playback controls for time-series datasets, including explicit `start`, `end`, and `step` handling shared with movie export.
- Added macOS app icon assets and updated macOS deployment helpers for bundled builds.

### Changed
- Bumped the project version to `0.1.4`.
- Simplified the main viewer layout into a top navigation area with a `Viewer | Channels` content split and moved overview and metadata into a `File Info` dialog.
- Replaced the old `Tools > 3D View` detached-window flow with an in-place `2D` / `3D` switch in the main viewer.
- Reworked channel controls so `Auto Contrast` and `Live Auto` are global controls, while per-channel rows now focus on visibility, color, thresholds, and 2D-only percentile tuning.
- Moved `Fit To Volume` and `Reset View` beside the `2D` / `3D` switch and removed inline 3D status labels from the viewer.
- Changed movie export to use the current shared frame-range model and current viewer state more consistently across 2D and 3D export flows.
- Removed the frame cache from document loading and kept frame preparation focused on the current navigation state.
- Refactored Windows and macOS build and packaging scripts around the current VTK-required toolchain and CPack packaging flow.

### Fixed
- Preserved the loaded 3D volume when switching between 2D and 3D so returning to 3D does not trigger an unnecessary reload.
- Made time playback in 3D wait for each volume load and render step to finish before advancing.
- Changed 3D auto contrast to aggregate per-z 2D slice results instead of using one whole-volume histogram.
- Removed 3D live-auto percentile tuning from the dialog flow and restricted percentile tuning back to 2D, avoiding expensive full-stack recomputation during drag/edit interactions.
- Flipped the VTK 3D volume Y orientation so the 3D render aligns with the 2D top-left image origin.
- Cleaned up no-file empty states across the navigation, canvas, and channel controls, and hid controls that are not useful before a file is loaded.

### Build And Packaging
- Made VTK a required dependency on both Windows and macOS builds.
- Added `build-vtk-msvc.ps1` and `build-vtk-macos.sh` helper scripts for bootstrapping the supported VTK toolchain.
- Updated the packaging scripts to better support release bundling on both platforms, including the current NSIS and DMG flows.

### Documentation
- Updated `README.md` and `AGENTS.md` for the VTK-required build flow, current 3D workflow, validation expectations, and the `0.1.4` packaging output example.

## [0.1.3] - 2026-04-02

### Changed
- Bumped the project version to `0.1.3`.

### Fixed
- Fixed the 3D smooth volume path sampling Y in the opposite direction from the point-cloud path, which made `Volume` and `Balanced` appear as upside-down copies of `Detail`.
- Aligned the `Balanced`, `Volume`, and `Detail` render modes so they now overlay the same structures with a consistent vertical orientation.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe the aligned 3D render-mode orientation and the current `0.1.3` packaging output.

## [0.1.2] - 2026-04-02

### Added
- Added a separate `Tools > 3D View` window for ND2 and CZI files that expose a usable z-stack.
- Added 3D channel controls with independent visibility, color, and percentile-based live auto contrast state.
- Added `Fit To Volume`, `Reset View`, and `Balanced` / `Volume` / `Detail` render modes in the 3D viewer.

### Changed
- Changed 3D z-stack loading to assemble the full volume in the background from a fresh reader instance before rendering.
- Changed the 3D renderer to fit the occupied part of the stack into view and blend smoother volume-style slices with the fitted point cloud fallback.
- Bumped the project version to `0.1.2`.

### Fixed
- Fixed blank 3D renders caused by missing z calibration, overly conservative fit bounds, and fragile first-pass volume visibility defaults.
- Fixed thin z-stacks collapsing into a tiny center dot by fitting the occupied voxel bounds instead of a clamped minimum extent.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe the new 3D viewer workflow, render modes, validation checks, and the current `0.1.2` packaging output.

## [0.1.1] - 2026-04-02

### Added
- Added live rendered MP4 export playback for time-series ND2 files.
- Added per-channel percentile-based `Live auto` contrast with histogram tuning controls.

### Changed
- Changed movie export to use explicit `start`, `end`, and `step` controls.
- Changed movie export flow to collect export settings first, then prompt for the final save path.
- Changed suggested movie filenames to include fixed non-time coordinates and the selected export range.
- Changed per-channel tuning so dragged histogram thresholds update lazily and commit preview changes on release.
- Bumped the project version to `0.1.1`.

### Fixed
- Reduced `Live auto` sensitivity to isolated bright artifacts by replacing raw min/max auto-contrast with percentile thresholds.
- Preserved tuned per-channel auto-contrast settings through rendering and movie export.

### Documentation
- Updated `README.md` and `AGENTS.md` to describe percentile auto-contrast, movie export behavior, and the current `0.1.1` packaging output.
- Updated `scripts/build-macos.sh` to support explicit CLI options and usage help.

## [0.1.0] - 2026-04-02

### Added
- Added the initial Qt 6 ND2 desktop viewer with metadata inspection, pixel readout, zooming, panning, and dynamic loop navigation.
- Added Windows MSVC build helpers and switched the project workflow to the MSVC Qt toolchain on this machine.
- Added PNG preview export, per-channel TIFF export, ROI drawing, and ROI export support.
- Added Windows NSIS installer packaging for release builds.
- Added macOS app bundling and DMG packaging support.

### Changed
- Simplified the main viewer layout and improved ROI persistence across frame changes.
- Deferred navigator slider commits so frame loads happen after slider interaction ends instead of on every drag step.

### Fixed
- Fixed packed ND2 channel color decoding.
- Improved frame navigation and packaging stability.

### Documentation
- Added repository agent instructions in `AGENTS.md`.
