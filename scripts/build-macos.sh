#!/usr/bin/env bash

set -euo pipefail

configuration=""
build_dir=""
qt6_dir="${Qt6_DIR:-$HOME/Qt/6.11.0/macos/lib/cmake/Qt6}"
vtk_dir="${VTK_DIR:-}"
nd2sdk_root="${ND2SDK_ROOT:-$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8}"

usage() {
  cat <<'EOF'
Usage: ./scripts/build-macos.sh --configuration <Debug|Release> [options]

Options:
  --configuration <type>  Required. Supported: Debug, Release
  --build-dir <path>      Build directory relative to the repo root. Default: build-macos-debug or build-macos-release
  --qt6-dir <path>        Path to Qt6Config.cmake. Default: ~/Qt/6.11.0/macos/lib/cmake/Qt6
  --vtk-dir <path>        Path to VTKConfig.cmake. Default: ~/opt/vtk-9.5.2-qt611-<config>/lib/cmake/vtk-9.5, fallback ~/build/vtk-9.5.2-qt611-<config>/lib/cmake/vtk-9.5
  --nd2sdk-root <path>    Path to the Nikon macOS shared SDK. Default: ~/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8
  -h, --help              Show this help text
EOF
}

migrate_legacy_release_vtk_tree() {
  local root_name="$1"
  local legacy_path="$HOME/${root_name}/vtk-9.5.2-qt611"
  local release_path="$HOME/${root_name}/vtk-9.5.2-qt611-release"
  if [[ -e "${legacy_path}" && ! -e "${release_path}" ]]; then
    echo "Migrating existing release VTK path '${legacy_path}' -> '${release_path}'"
    mv "${legacy_path}" "${release_path}"
  elif [[ -e "${legacy_path}" && -e "${release_path}" ]]; then
    echo "Both legacy and new release VTK paths exist:" >&2
    echo "  ${legacy_path}" >&2
    echo "  ${release_path}" >&2
    echo "Resolve the duplicate manually before continuing." >&2
    exit 1
  fi
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

if [[ "${configuration}" == "Release" ]]; then
  migrate_legacy_release_vtk_tree "opt"
  migrate_legacy_release_vtk_tree "build"
fi

if [[ -z "${vtk_dir}" ]]; then
  vtk_install_dir_default="$HOME/opt/vtk-9.5.2-qt611-${build_suffix}/lib/cmake/vtk-9.5"
  vtk_build_dir_default="$HOME/build/vtk-9.5.2-qt611-${build_suffix}/lib/cmake/vtk-9.5"
  if [[ -f "${vtk_install_dir_default}/vtk-config.cmake" || -f "${vtk_install_dir_default}/VTKConfig.cmake" ]]; then
    vtk_dir="${vtk_install_dir_default}"
  elif [[ -f "${vtk_build_dir_default}/vtk-config.cmake" || -f "${vtk_build_dir_default}/VTKConfig.cmake" ]]; then
    vtk_dir="${vtk_build_dir_default}"
  else
    echo "VTKConfig.cmake was not found for ${configuration}." >&2
    echo "Expected one of:" >&2
    echo "  ${vtk_install_dir_default}" >&2
    echo "  ${vtk_build_dir_default}" >&2
    echo "Run ./scripts/build-vtk-macos.sh --configuration ${configuration} first, or pass --vtk-dir explicitly." >&2
    exit 1
  fi
fi

if [[ "${configuration}" == "Debug" && ! -f "${vtk_dir}/VTK-targets-debug.cmake" ]]; then
  echo "Debug builds require a VTK package with debug targets." >&2
  echo "Resolved VTK_DIR='${vtk_dir}', but '${vtk_dir}/VTK-targets-debug.cmake' was not found." >&2
  echo "Build debug VTK with ./scripts/build-vtk-macos.sh --configuration Debug, or pass a debug-capable --vtk-dir." >&2
  exit 1
fi

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
  -DCMAKE_BUILD_TYPE="${configuration}"
  -DQt6_DIR="${qt6_dir}"
  -DVTK_DIR="${vtk_dir}"
  -DND2SDK_ROOT="${nd2sdk_root}"
)

cmake "${cmake_args[@]}"
cmake --build "${repo_root}/${build_dir}" --parallel "${parallel}"

app_bundle="${repo_root}/${build_dir}/bin/nd2-viewer.app"
if [[ ! -d "${app_bundle}" ]]; then
  echo "Expected app bundle was not found at '${app_bundle}' after build." >&2
  exit 1
fi

bash "${repo_root}/scripts/macos-macdeployqt.sh" "${app_bundle}" "${qt6_dir}"
