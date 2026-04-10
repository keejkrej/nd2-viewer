#!/usr/bin/env bash

set -euo pipefail

configuration="Release"
build_dir="build-macos-release"
output_dir="dist"
generator="DragNDrop"

usage() {
  cat <<'EOF'
Usage: ./scripts/package-macos.sh [options]

Options:
  --configuration <type>  CMake build type. Supported: Release. Default: Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-macos-release
  --output-dir <path>     Package output directory relative to the repo root. Default: dist
  --generator <name>      CPack generator. Supported: DragNDrop. Default: DragNDrop
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

case "${configuration}" in
  Release)
    ;;
  *)
    echo "Unsupported configuration '${configuration}'. Use Release." >&2
    exit 1
    ;;
esac

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
app_bundle="${build_path}/bin/nd2-viewer.app"

if [[ ! -d "${build_path}" ]]; then
  echo "Release build directory not found at '${build_path}'." >&2
  echo "Run ./scripts/build-macos.sh --configuration Release first." >&2
  exit 1
fi

if [[ ! -d "${app_bundle}" ]]; then
  echo "Release app bundle not found at '${app_bundle}'." >&2
  echo "Run ./scripts/build-macos.sh --configuration Release first." >&2
  exit 1
fi

if [[ ! -f "${cpack_config}" ]]; then
  echo "CPackConfig.cmake not found at '${cpack_config}'." >&2
  echo "Run ./scripts/build-macos.sh --configuration Release first so CMake generates packaging metadata." >&2
  exit 1
fi

mkdir -p "${output_path}"
cpack --config "${cpack_config}" -G "${generator}" -B "${output_path}"
