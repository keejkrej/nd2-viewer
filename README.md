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

### Default toolchain

The project now targets `Qt 6 msvc2022_64` plus the Nikon Windows SDK by default.

The easiest path is:

```powershell
.\scripts\build-msvc.ps1
.\scripts\run-msvc.ps1
```

The helper script enters the Visual Studio build environment for you, configures CMake with the MSVC Qt kit, and builds into `build-msvc`.

### Manual MSVC build

If you want to run the steps yourself:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
cmd.exe /c "`"$vs`" -arch=x64 -host_arch=x64 && `"$cmake`" -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt6_DIR=C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6 -DND2SDK_ROOT=C:/Program Files/nd2readsdk-shared && `"$cmake`" --build build-msvc --config Debug -j 8"
```

## Notes

- `CMakeLists.txt` copies the ND2 SDK runtime DLLs after build.
- If `windeployqt` is available from the selected Qt kit, it is run automatically after build.
- `scripts/build-msvc.ps1` is the intended day-to-day build entrypoint on this machine.
- On Windows, the project now supports only the MSVC Qt toolchain.
- The current implementation is read-only and focused on core viewing workflows.
