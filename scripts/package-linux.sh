#!/usr/bin/env bash

set -euo pipefail

configuration="Release"
build_dir="build-linux-release"
output_dir="dist"
generator="TGZ"

usage() {
  cat <<'EOF'
Usage: ./scripts/package-linux.sh [options]

Options:
  --configuration <type>  CMake build type. Supported: Release. Default: Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-linux-release
  --output-dir <path>     Package output directory relative to the repo root. Default: dist
  --generator <name>      CPack generator. Supported: TGZ, TXZ, ZIP. Default: TGZ
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
  TGZ|TXZ|ZIP)
    ;;
  *)
    echo "Unsupported generator '${generator}'. Use TGZ, TXZ, or ZIP." >&2
    exit 1
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_path="${repo_root}/${build_dir}"
output_path="${repo_root}/${output_dir}"
cpack_config="${build_path}/CPackConfig.cmake"
exe_path="${build_path}/bin/nd2-viewer"

if [[ ! -d "${build_path}" ]]; then
  echo "Release build directory not found at '${build_path}'." >&2
  echo "Run ./scripts/build-linux.sh --configuration Release first." >&2
  exit 1
fi

if [[ ! -x "${exe_path}" ]]; then
  echo "Release executable not found at '${exe_path}'." >&2
  echo "Run ./scripts/build-linux.sh --configuration Release first." >&2
  exit 1
fi

if [[ ! -f "${cpack_config}" ]]; then
  echo "CPackConfig.cmake not found at '${cpack_config}'." >&2
  echo "Run ./scripts/build-linux.sh --configuration Release first so CMake generates packaging metadata." >&2
  exit 1
fi

mkdir -p "${output_path}"
cpack --config "${cpack_config}" -G "${generator}" -B "${output_path}"
