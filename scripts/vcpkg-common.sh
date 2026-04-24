#!/usr/bin/env bash
# Shared vcpkg root resolution for POSIX scripts (source from scripts/*.sh).

require_vcpkg_root() {
  local root="$1"
  if [[ ! -f "${root}/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "Invalid vcpkg root '${root}' (missing scripts/buildsystems/vcpkg.cmake)." >&2
    exit 1
  fi
  if [[ ! -x "${root}/vcpkg" ]]; then
    echo "vcpkg executable not found or not executable at '${root}/vcpkg'." >&2
    exit 1
  fi
}

physical_vcpkg_root() {
  local root="$1"
  (cd -P "${root}" && pwd)
}

resolve_vcpkg_root() {
  local explicit="${1:-}"
  if [[ -n "${explicit}" ]]; then
    require_vcpkg_root "${explicit}"
    physical_vcpkg_root "${explicit}"
    return 0
  fi
  if [[ -n "${VCPKG_ROOT:-}" ]] && [[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
    physical_vcpkg_root "${VCPKG_ROOT}"
    return 0
  fi
  if [[ -n "${HOME:-}" ]] && [[ -f "${HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake" ]]; then
    physical_vcpkg_root "${HOME}/vcpkg"
    return 0
  fi
  if command -v vcpkg >/dev/null 2>&1; then
    local _vp _dir
    _vp="$(command -v vcpkg)"
    _dir="$(cd -P "$(dirname "${_vp}")" && pwd)"
    if [[ -f "${_dir}/scripts/buildsystems/vcpkg.cmake" ]]; then
      printf '%s' "${_dir}"
      return 0
    fi
  fi
  echo "Could not find vcpkg. Clone vcpkg to ~/vcpkg, set VCPKG_ROOT, or put a real vcpkg install on PATH (not a shim without the scripts tree)." >&2
  exit 1
}

default_vcpkg_triplet_macos() {
  local m
  m="$(uname -m)"
  case "${m}" in
    arm64) printf '%s' "arm64-osx" ;;
    x86_64) printf '%s' "x64-osx" ;;
    *)
      echo "Unsupported machine type '${m}' for default vcpkg triplet." >&2
      exit 1
      ;;
  esac
}
