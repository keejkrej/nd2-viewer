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
nd2sdk_root="${ND2SDK_ROOT:-$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-macos.sh --configuration <Debug|Release> [options]

Configures and builds with the repo vcpkg manifest by default. Qt, VTK, ITK,
and libCZI are installed or updated as needed before CMake configure.

Options:
  --configuration <type>  Required. Supported: Debug, Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-macos-debug or build-macos-release
  --vcpkg-root <path>     vcpkg clone root (directory containing scripts/buildsystems/vcpkg.cmake). Default: VCPKG_ROOT, ~/vcpkg, or vcpkg on PATH
  --vcpkg-triplet <t>     e.g. arm64-osx or x64-osx. Default: from machine arch
  --qt6-dir <path>        Optional. Path to Qt6Config.cmake; if set, skips vcpkg for Qt/VTK/libczi (advanced)
  --vtk-dir <path>        Optional. Only used with --qt6-dir (non-vcpkg VTK)
  --nd2sdk-root <path>    Path to the Nikon macOS shared SDK. Default: ~/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8
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

  sysctl -n hw.ncpu
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
  build_dir="build-macos-${build_suffix}"
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
if [[ ! -f "${nd2sdk_root}/lib/libnd2readsdk-shared.dylib" ]]; then
  echo "ND2 shared SDK library was not found at '${nd2sdk_root}/lib/libnd2readsdk-shared.dylib'." >&2
  exit 1
fi

cmake_extra=()

if [[ -n "${qt6_dir}" ]]; then
  if [[ -z "${vtk_dir}" ]]; then
    echo "--vtk-dir is required when using --qt6-dir (non-vcpkg build)." >&2
    exit 1
  fi
  if [[ "${configuration}" == "Debug" && ! -f "${vtk_dir}/VTK-targets-debug.cmake" ]]; then
    echo "Debug builds require VTK debug targets at VTK_DIR='${vtk_dir}'." >&2
    exit 1
  fi
  cmake_extra+=(-DQt6_DIR="${qt6_dir}" -DVTK_DIR="${vtk_dir}")
else
  vcpkg_root="$(resolve_vcpkg_root "${vcpkg_root}")"
  if [[ -z "${vcpkg_triplet}" ]]; then
    vcpkg_triplet="$(default_vcpkg_triplet_macos)"
  fi

  toolchain_file="${vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
  vcpkg_installed_dir="${repo_root}/vcpkg_installed"

  echo "vcpkg: installing manifest dependencies (Qt, VTK, ITK, libczi, ...) triplet=${vcpkg_triplet}"
  echo "vcpkg root: ${vcpkg_root}"
  (cd "${repo_root}" && "${vcpkg_root}/vcpkg" install --triplet "${vcpkg_triplet}" --vcpkg-root "${vcpkg_root}")

  cmake_extra+=(
    "-DCMAKE_TOOLCHAIN_FILE=${toolchain_file}"
    "-DVCPKG_TARGET_TRIPLET=${vcpkg_triplet}"
    "-DVCPKG_INSTALLED_DIR=${vcpkg_installed_dir}"
  )
  qt6_dir="${vcpkg_installed_dir}/${vcpkg_triplet}/share/Qt6"
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

app_bundle="${repo_root}/${build_dir}/bin/nd2-viewer.app"
if [[ ! -d "${app_bundle}" ]]; then
  echo "Expected app bundle was not found at '${app_bundle}' after build." >&2
  exit 1
fi

bash "${repo_root}/scripts/macos-macdeployqt.sh" "${app_bundle}" "${qt6_dir}"
