#!/usr/bin/env bash

set -euo pipefail

configuration="Release"
build_dir="build-macos-release"
output_dir="dist"
generator="DragNDrop"
qt6_dir="${Qt6_DIR:-/opt/homebrew/lib/cmake/Qt6}"
nd2sdk_root="${ND2SDK_ROOT:-$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8}"

usage() {
  cat <<'EOF'
Usage: ./scripts/package-macos.sh [options]

Options:
  --configuration <type>  CMake build type. Default: Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-macos-release
  --output-dir <path>     Package output directory relative to the repo root. Default: dist
  --generator <name>      CPack generator. Supported: DragNDrop. Default: DragNDrop
  --qt6-dir <path>        Path to Qt6Config.cmake. Default: /opt/homebrew/lib/cmake/Qt6
  --nd2sdk-root <path>    Path to the Nikon macOS shared SDK. Default: ~/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8
  -h, --help              Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      configuration="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --output-dir)
      output_dir="$2"
      shift 2
      ;;
    --generator)
      generator="$2"
      shift 2
      ;;
    --qt6-dir)
      qt6_dir="$2"
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

case "${generator}" in
  DragNDrop)
    ;;
  *)
    echo "Unsupported generator '${generator}'. Use DragNDrop." >&2
    exit 1
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_path="${repo_root}/${build_dir}"
output_path="${repo_root}/${output_dir}"
cpack_config="${build_path}/CPackConfig.cmake"

CMAKE_BUILD_TYPE="${configuration}" \
BUILD_DIR="${build_dir}" \
Qt6_DIR="${qt6_dir}" \
ND2SDK_ROOT="${nd2sdk_root}" \
"${repo_root}/scripts/build-macos.sh"

if [[ ! -f "${cpack_config}" ]]; then
  echo "CPackConfig.cmake not found at '${cpack_config}'." >&2
  exit 1
fi

mkdir -p "${output_path}"

cpack --config "${cpack_config}" -G "${generator}" -B "${output_path}"
