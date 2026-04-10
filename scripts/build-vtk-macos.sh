#!/usr/bin/env bash

set -euo pipefail

vtk_ref="${VTK_REF:-v9.5.2}"
source_dir="${VTK_SOURCE_DIR:-$HOME/src/VTK}"
build_dir="${VTK_BUILD_DIR:-$HOME/build/vtk-9.5.2-qt611}"
install_dir="${VTK_INSTALL_DIR:-$HOME/opt/vtk-9.5.2-qt611}"
qt6_dir="${Qt6_DIR:-$HOME/Qt/6.11.0/macos/lib/cmake/Qt6}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-vtk-macos.sh [options]

Options:
  --vtk-ref <ref>         VTK git ref to build. Default: v9.5.2
  --source-dir <path>     VTK source checkout. Default: ~/src/VTK
  --build-dir <path>      VTK build directory. Default: ~/build/vtk-9.5.2-qt611
  --install-dir <path>    VTK install prefix. Default: ~/opt/vtk-9.5.2-qt611
  --qt6-dir <path>        Path to Qt6Config.cmake. Default: ~/Qt/6.11.0/macos/lib/cmake/Qt6
  -h, --help              Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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

if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
  generator="${CMAKE_GENERATOR}"
elif command -v ninja >/dev/null 2>&1; then
  generator="Ninja"
else
  generator="Unix Makefiles"
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
  -DCMAKE_BUILD_TYPE=Release
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
echo "Source: ${source_dir}"
echo "Build: ${build_dir}"
echo "Install: ${install_dir}"
echo "VTK_DIR=${install_dir}/lib/cmake/vtk-9.5"
echo "This bootstrap only requests the VTK modules nd2-viewer links against, plus their dependency closure."
