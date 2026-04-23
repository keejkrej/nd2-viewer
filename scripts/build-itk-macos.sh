#!/usr/bin/env bash

set -euo pipefail

configuration=""
itk_ref="${ITK_REF:-v5.4.4}"
source_dir="${ITK_SOURCE_DIR:-$HOME/src/ITK}"
build_dir="${ITK_BUILD_DIR:-}"
install_dir="${ITK_INSTALL_DIR:-}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-itk-macos.sh --configuration <Debug|Release> [options]

Options:
  --configuration <type>  Required. Supported: Debug, Release
  --itk-ref <ref>         ITK git ref to build. Default: v5.4.4
  --source-dir <path>     ITK source checkout. Default: ~/src/ITK
  --build-dir <path>      ITK build directory. Default: ~/build/itk-5.4.4-debug or ~/build/itk-5.4.4-release
  --install-dir <path>    ITK install prefix. Default: ~/opt/itk-5.4.4-debug or ~/opt/itk-5.4.4-release
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

find_itk_config_dir() {
  local root="$1"
  local exact="${root}/lib/cmake/ITK-5.4"
  local candidate=""

  if [[ -f "${exact}/ITKConfig.cmake" || -f "${exact}/itk-config.cmake" ]]; then
    printf '%s' "${exact}"
    return 0
  fi

  candidate="$(find "${root}/lib/cmake" -maxdepth 1 -type d -name 'ITK-*' 2>/dev/null | sort | head -n 1 || true)"
  if [[ -n "${candidate}" && ( -f "${candidate}/ITKConfig.cmake" || -f "${candidate}/itk-config.cmake" ) ]]; then
    printf '%s' "${candidate}"
    return 0
  fi

  printf '%s' "${exact}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      configuration="$2"
      shift 2
      ;;
    --itk-ref)
      itk_ref="$2"
      shift 2
      ;;
    --source-dir)
      source_dir="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --install-dir)
      install_dir="$2"
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
  build_dir="$HOME/build/itk-5.4.4-${build_suffix}"
fi

if [[ -z "${install_dir}" ]]; then
  install_dir="$HOME/opt/itk-5.4.4-${build_suffix}"
fi

if ! command -v git >/dev/null 2>&1; then
  echo "git was not found on PATH." >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake was not found on PATH." >&2
  exit 1
fi

generator="$(resolve_generator)"
cached_generator="$(read_cached_generator "${build_dir}")"

if [[ -n "${cached_generator}" && "${cached_generator}" != "${generator}" ]]; then
  if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
    echo "Build directory '${build_dir}' was configured with '${cached_generator}', but CMAKE_GENERATOR requests '${generator}'." >&2
    echo "Removing the existing build directory so the requested generator can be used." >&2
    reset_build_dir_for_generator_switch "${build_dir}"
  else
    echo "Switching build directory '${build_dir}' from cached generator '${cached_generator}' to default generator '${generator}'." >&2
    echo "Removing the existing build directory so the default generator can be used." >&2
    reset_build_dir_for_generator_switch "${build_dir}"
  fi
fi

parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-$(sysctl -n hw.ncpu)}"

mkdir -p "$(dirname "${source_dir}")" "$(dirname "${build_dir}")" "$(dirname "${install_dir}")"

if [[ -d "${source_dir}/.git" ]]; then
  git -C "${source_dir}" remote update --prune
  git -C "${source_dir}" fetch --tags --force
  git -C "${source_dir}" checkout "${itk_ref}"
elif [[ -d "${source_dir}" && -f "${source_dir}/CMakeLists.txt" ]]; then
  echo "Reusing existing non-git ITK source tree at '${source_dir}'."
  echo "Skipping git fetch/checkout because the source directory is not a git clone."
elif [[ ! -e "${source_dir}" ]]; then
  git clone https://github.com/InsightSoftwareConsortium/ITK.git "${source_dir}"
  git -C "${source_dir}" fetch --tags --force
  git -C "${source_dir}" checkout "${itk_ref}"
else
  echo "Source directory '${source_dir}' already exists but does not look like an ITK source tree." >&2
  echo "Pass --source-dir to a valid ITK checkout or remove the conflicting directory." >&2
  exit 1
fi

cmake_args=(
  -S "${source_dir}"
  -B "${build_dir}"
  -G "${generator}"
  -DCMAKE_BUILD_TYPE="${configuration}"
  -DCMAKE_INSTALL_PREFIX="${install_dir}"
  -DBUILD_TESTING=OFF
  -DBUILD_EXAMPLES=OFF
  -DITK_BUILD_DOCUMENTATION=OFF
  -DITK_BUILD_DOXYGEN=OFF
  -DITK_WRAP_PYTHON=OFF
  -DITK_WRAP_DEFAULT=OFF
  -DITK_BUILD_DEFAULT_MODULES=OFF
  -DModule_ITKCommon=ON
  -DModule_ITKDeconvolution=ON
)

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "${parallel}"
cmake --install "${build_dir}"

itk_dir="$(find_itk_config_dir "${install_dir}")"

echo
echo "ITK bootstrap complete."
echo "Configuration: ${configuration}"
echo "Source: ${source_dir}"
echo "Build: ${build_dir}"
echo "Install: ${install_dir}"
echo "ITK_DIR=${itk_dir}"
echo "This bootstrap only requests the ITK modules nd2-viewer links against, plus their dependency closure."
