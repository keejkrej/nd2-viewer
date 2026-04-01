# nd2-viewer

A Qt 6 desktop viewer for Nikon ND2 microscopy files using Nikon's official ND2 SDK.

## Features

- Open `.nd2` files with the Nikon SDK
- Navigate experiment loops dynamically (time, z, XY, and other SDK-exposed loops)
- Render 8-bit, 16-bit, and 32-bit float image data
- Toggle channels and adjust per-channel contrast
- Inspect attributes, experiment metadata, frame metadata, and text info
- Zoom, fit-to-window, pan, and inspect pixel values

## Build

### Planned production toolchain

The project is set up for `Qt 6 msvc2022_64` plus the Nikon Windows SDK.

```powershell
$env:PATH = "C:\Qt\Tools\Ninja;" + $env:PATH
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -S . `
  -B build-msvc `
  -G Ninja `
  -DQt6_DIR="C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6" `
  -DND2SDK_ROOT="C:/Program Files/nd2readsdk-shared"

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-msvc -j 8
```

### Local validation build used during implementation

This repository was syntax/build validated locally with the installed MinGW Qt kit:

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;" + $env:PATH
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -S . `
  -B build-mingw `
  -G Ninja `
  -DQt6_DIR="C:/Qt/6.11.0/mingw_64/lib/cmake/Qt6" `
  -DND2SDK_ROOT="C:/Program Files/nd2readsdk-shared"

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-mingw -j 8
```

## Notes

- `CMakeLists.txt` copies the ND2 SDK runtime DLLs after build.
- If `windeployqt` is available from the selected Qt kit, it is run automatically after build.
- The current implementation is read-only and focused on core viewing workflows.
