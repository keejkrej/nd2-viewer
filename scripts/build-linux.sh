#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${script_dir}/vcpkg-common.sh"

configuration=""
build_dir=""
vcpkg_root="${VCPKG_ROOT:-}"
vcpkg_triplet=""
qt6_dir=""
vtk_dir="${VTK_DIR:-}"
nd2sdk_root="${ND2SDK_ROOT:-${HOME}/Documents/nd2readsdk-shared}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-linux.sh --configuration <Debug|Release> [options]

Run ./scripts/install-vcpkg-deps-linux.sh first (or after vcpkg.json changes)
to build Qt/VTK/ITK/libczi.

Options:
  --configuration <type>  Required. Supported: Debug, Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-linux-debug or build-linux-release
  --vcpkg-root <path>     vcpkg clone root (directory containing scripts/buildsystems/vcpkg.cmake). Default: VCPKG_ROOT, ~/vcpkg, or vcpkg on PATH
  --vcpkg-triplet <t>     e.g. x64-linux or arm64-linux. Default: from machine arch
  --qt6-dir <path>        Optional. Path to Qt6Config.cmake; if set, skips vcpkg for Qt/VTK/libczi (advanced)
  --vtk-dir <path>        Optional. Only used with --qt6-dir (non-vcpkg VTK)
  --nd2sdk-root <path>    Path to the Nikon Linux SDK root. Default: ND2SDK_ROOT or ~/Documents/nd2readsdk-shared
  -h, --help              Show this help text
EOF
}

resolve_generator() {
  if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
    printf '%s' "${CMAKE_GENERATOR}"
  elif command -v ninja >/dev/null 2>&1; then
    printf '%s' "Ninja"
  else
    printf '%s' "Unix Makefiles"
  fi
}

read_cached_generator() {
  local candidate_build_dir="$1"
  local cache_path="${candidate_build_dir}/CMakeCache.txt"
  local cache_generator=""

  if [[ ! -f "${cache_path}" ]]; then
    return 0
  fi

  cache_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "${cache_path}" | head -n 1)"
  if [[ -n "${cache_generator}" ]]; then
    printf '%s' "${cache_generator}"
  fi
}

reset_build_dir_for_generator_switch() {
  local candidate_build_dir="$1"

  rm -rf "${candidate_build_dir}"
}

detect_parallelism() {
  if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
    printf '%s' "${CMAKE_BUILD_PARALLEL_LEVEL}"
    return 0
  fi

  if command -v nproc >/dev/null 2>&1; then
    nproc
    return 0
  fi

  if command -v getconf >/dev/null 2>&1; then
    getconf _NPROCESSORS_ONLN
    return 0
  fi

  printf '%s' "8"
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
    --vcpkg-root)
      vcpkg_root="$2"
      shift 2
      ;;
    --vcpkg-triplet)
      vcpkg_triplet="$2"
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

case "${configuration}" in
  Debug)
    build_suffix="debug"
    ;;
  Release)
    build_suffix="release"
    ;;
  "")
    echo "--configuration is required." >&2
    usage >&2
    exit 1
    ;;
  *)
    echo "Unsupported configuration '${configuration}'. Use Debug or Release." >&2
    exit 1
    ;;
esac

if [[ -z "${build_dir}" ]]; then
  build_dir="build-linux-${build_suffix}"
fi

repo_root="$(cd "${script_dir}/.." && pwd)"

if [[ ! -d "${nd2sdk_root}" ]]; then
  echo "ND2 SDK root was not found at '${nd2sdk_root}'." >&2
  exit 1
fi
if [[ ! -f "${nd2sdk_root}/include/Nd2ReadSdk.h" ]]; then
  echo "ND2 SDK header was not found at '${nd2sdk_root}/include/Nd2ReadSdk.h'." >&2
  exit 1
fi
if [[ ! -f "${nd2sdk_root}/lib/libnd2readsdk-shared.so" ]]; then
  echo "ND2 shared SDK library was not found at '${nd2sdk_root}/lib/libnd2readsdk-shared.so'." >&2
  exit 1
fi

if [[ -z "${vcpkg_root}" ]]; then
  vcpkg_root="$(resolve_vcpkg_root "")"
else
  require_vcpkg_root "${vcpkg_root}"
fi

if [[ -z "${vcpkg_triplet}" ]]; then
  vcpkg_triplet="$(default_vcpkg_triplet_linux)"
fi

toolchain_file="${vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
cmake_extra=()

if [[ -n "${qt6_dir}" ]]; then
  if [[ -z "${vtk_dir}" ]]; then
    echo "--vtk-dir is required when using --qt6-dir (non-vcpkg build)." >&2
    exit 1
  fi
  cmake_extra+=(-DQt6_DIR="${qt6_dir}" -DVTK_DIR="${vtk_dir}")
else
  cmake_extra+=(
    "-DCMAKE_TOOLCHAIN_FILE=${toolchain_file}"
    "-DVCPKG_TARGET_TRIPLET=${vcpkg_triplet}"
  )
fi

generator="$(resolve_generator)"
cached_generator="$(read_cached_generator "${repo_root}/${build_dir}")"

if [[ -n "${cached_generator}" && "${cached_generator}" != "${generator}" ]]; then
  if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
    echo "Build directory '${repo_root}/${build_dir}' was configured with '${cached_generator}', but CMAKE_GENERATOR requests '${generator}'." >&2
    echo "Removing the existing build directory so the requested generator can be used." >&2
    reset_build_dir_for_generator_switch "${repo_root}/${build_dir}"
  else
    echo "Switching build directory '${repo_root}/${build_dir}' from cached generator '${cached_generator}' to default generator '${generator}'." >&2
    echo "Removing the existing build directory so the default generator can be used." >&2
    reset_build_dir_for_generator_switch "${repo_root}/${build_dir}"
  fi
fi

parallel="$(detect_parallelism)"

cmake_args=(
  -S "${repo_root}"
  -B "${repo_root}/${build_dir}"
  -G "${generator}"
  -DCMAKE_BUILD_TYPE="${configuration}"
  -DND2SDK_ROOT="${nd2sdk_root}"
  "${cmake_extra[@]}"
)

cmake "${cmake_args[@]}"
cmake --build "${repo_root}/${build_dir}" --parallel "${parallel}"

exe_path="${repo_root}/${build_dir}/bin/nd2-viewer"
if [[ ! -x "${exe_path}" ]]; then
  echo "Expected executable was not found at '${exe_path}' after build." >&2
  exit 1
fi
