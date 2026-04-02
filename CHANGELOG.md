# Changelog

All notable changes to `nd2-viewer` are documented in this file.

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
