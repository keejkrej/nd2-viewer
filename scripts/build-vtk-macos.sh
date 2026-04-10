#!/usr/bin/env bash

set -euo pipefail

configuration=""
vtk_ref="${VTK_REF:-v9.5.2}"
source_dir="${VTK_SOURCE_DIR:-$HOME/src/VTK}"
build_dir="${VTK_BUILD_DIR:-}"
install_dir="${VTK_INSTALL_DIR:-}"
qt6_dir="${Qt6_DIR:-$HOME/Qt/6.11.0/macos/lib/cmake/Qt6}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-vtk-macos.sh --configuration <Debug|Release> [options]

Options:
  --configuration <type>  Required. Supported: Debug, Release
  --vtk-ref <ref>         VTK git ref to build. Default: v9.5.2
  --source-dir <path>     VTK source checkout. Default: ~/src/VTK
  --build-dir <path>      VTK build directory. Default: ~/build/vtk-9.5.2-qt611-debug or ~/build/vtk-9.5.2-qt611-release
  --install-dir <path>    VTK install prefix. Default: ~/opt/vtk-9.5.2-qt611-debug or ~/opt/vtk-9.5.2-qt611-release
  --qt6-dir <path>        Path to Qt6Config.cmake. Default: ~/Qt/6.11.0/macos/lib/cmake/Qt6
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

migrate_release_path() {
  local legacy_path="$1"
  local target_path="$2"
  if [[ -e "${legacy_path}" && ! -e "${target_path}" ]]; then
    echo "Migrating existing release VTK path:"
    echo "  ${legacy_path}"
    echo "  -> ${target_path}"
    mv "${legacy_path}" "${target_path}"
  elif [[ -e "${legacy_path}" && -e "${target_path}" ]]; then
    echo "Both legacy and new release VTK paths exist:" >&2
    echo "  ${legacy_path}" >&2
    echo "  ${target_path}" >&2
    echo "Resolve the duplicate manually before re-running this script." >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      configuration="$2"
      shift 2
      ;;
    --vtk-ref)
      vtk_ref="$2"
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
    --qt6-dir)
      qt6_dir="$2"
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
  build_dir="$HOME/build/vtk-9.5.2-qt611-${build_suffix}"
fi

if [[ -z "${install_dir}" ]]; then
  install_dir="$HOME/opt/vtk-9.5.2-qt611-${build_suffix}"
fi

if [[ "${configuration}" == "Release" ]]; then
  migrate_release_path "$HOME/build/vtk-9.5.2-qt611" "$HOME/build/vtk-9.5.2-qt611-release"
  migrate_release_path "$HOME/opt/vtk-9.5.2-qt611" "$HOME/opt/vtk-9.5.2-qt611-release"
fi

if ! command -v git >/dev/null 2>&1; then
  echo "git was not found on PATH." >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake was not found on PATH." >&2
  exit 1
fi

if [[ ! -f "${qt6_dir}/Qt6Config.cmake" ]]; then
  echo "Qt6Config.cmake not found at '${qt6_dir}'." >&2
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
  git -C "${source_dir}" checkout "${vtk_ref}"
elif [[ -d "${source_dir}" && -f "${source_dir}/CMakeLists.txt" ]]; then
  echo "Reusing existing non-git VTK source tree at '${source_dir}'."
  echo "Skipping git fetch/checkout because the source directory is not a git clone."
elif [[ ! -e "${source_dir}" ]]; then
  git clone https://github.com/Kitware/VTK.git "${source_dir}"
  git -C "${source_dir}" fetch --tags --force
  git -C "${source_dir}" checkout "${vtk_ref}"
else
  echo "Source directory '${source_dir}' already exists but does not look like a VTK source tree." >&2
  echo "Pass --source-dir to a valid VTK checkout or remove the conflicting directory." >&2
  exit 1
fi

cmake_args=(
  -S "${source_dir}"
  -B "${build_dir}"
  -G "${generator}"
  -DCMAKE_BUILD_TYPE="${configuration}"
  -DCMAKE_INSTALL_PREFIX="${install_dir}"
  -DQt6_DIR="${qt6_dir}"
  -DVTK_BUILD_ALL_MODULES=OFF
  -DVTK_BUILD_EXAMPLES=OFF
  -DVTK_BUILD_TESTING=OFF
  -DVTK_ENABLE_WRAPPING=OFF
  -DVTK_MODULE_ENABLE_VTK_CommonCore=YES
  -DVTK_MODULE_ENABLE_VTK_CommonDataModel=YES
  -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES
  -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES
  -DVTK_MODULE_ENABLE_VTK_RenderingCore=YES
  -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES
  -DVTK_MODULE_ENABLE_VTK_RenderingVolume=YES
  -DVTK_MODULE_ENABLE_VTK_RenderingVolumeOpenGL2=YES
)

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "${parallel}"
cmake --install "${build_dir}"

echo
echo "VTK bootstrap complete."
echo "Configuration: ${configuration}"
echo "Source: ${source_dir}"
echo "Build: ${build_dir}"
echo "Install: ${install_dir}"
echo "VTK_DIR=${install_dir}/lib/cmake/vtk-9.5"
echo "This bootstrap only requests the VTK modules nd2-viewer links against, plus their dependency closure."
