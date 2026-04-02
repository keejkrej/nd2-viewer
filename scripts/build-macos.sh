#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-build-macos}"
build_type="${CMAKE_BUILD_TYPE:-Debug}"
qt6_dir="${Qt6_DIR:-/opt/homebrew/lib/cmake/Qt6}"
nd2sdk_root="${ND2SDK_ROOT:-$HOME/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8}"

if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
  generator="${CMAKE_GENERATOR}"
elif command -v ninja >/dev/null 2>&1; then
  generator="Ninja"
else
  generator="Unix Makefiles"
fi

parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-$(sysctl -n hw.ncpu)}"

cmake \
  -S "${repo_root}" \
  -B "${repo_root}/${build_dir}" \
  -G "${generator}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DQt6_DIR="${qt6_dir}" \
  -DND2SDK_ROOT="${nd2sdk_root}"

cmake --build "${repo_root}/${build_dir}" --parallel "${parallel}"
