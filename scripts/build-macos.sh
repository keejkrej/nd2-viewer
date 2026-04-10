#!/usr/bin/env bash

set -euo pipefail

build_dir="${BUILD_DIR:-build-macos}"
build_type="${CMAKE_BUILD_TYPE:-Debug}"
qt6_dir="${Qt6_DIR:-/opt/homebrew/lib/cmake/Qt6}"
vtk_dir="${VTK_DIR:-}"
nd2sdk_root="${ND2SDK_ROOT:-$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-macos.sh [options]

Options:
  --configuration <type>  CMake build type. Default: Debug
  --build-dir <path>      Build directory relative to the repo root. Default: build-macos
  --qt6-dir <path>        Path to Qt6Config.cmake. Default: /opt/homebrew/lib/cmake/Qt6
  --vtk-dir <path>        Path to VTKConfig.cmake. Default: auto-detect in CMake
  --nd2sdk-root <path>    Path to the Nikon macOS shared SDK. Default: ~/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8
  -h, --help              Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      build_type="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --qt6-dir)
      qt6_dir="$2"
      shift 2
      ;;
    --vtk-dir)
      vtk_dir="$2"
      shift 2
      ;;
    --nd2sdk-root)
      nd2sdk_root="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
  generator="${CMAKE_GENERATOR}"
elif command -v ninja >/dev/null 2>&1; then
  generator="Ninja"
else
  generator="Unix Makefiles"
fi

parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-$(sysctl -n hw.ncpu)}"

cmake_args=(
  -S "${repo_root}"
  -B "${repo_root}/${build_dir}"
  -G "${generator}"
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DQt6_DIR="${qt6_dir}"
  -DND2SDK_ROOT="${nd2sdk_root}"
)

if [[ -n "${vtk_dir}" ]]; then
  cmake_args+=(-DVTK_DIR="${vtk_dir}")
fi

cmake "${cmake_args[@]}"

cmake --build "${repo_root}/${build_dir}" --parallel "${parallel}"
