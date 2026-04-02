# Changelog

All notable changes to `nd2-viewer` are documented in this file.

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
