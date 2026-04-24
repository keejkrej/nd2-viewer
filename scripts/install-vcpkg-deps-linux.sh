#!/usr/bin/env bash
# Warm up vcpkg manifest deps (Qt, VTK, ITK, libczi, ...) before build-linux.sh.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
# shellcheck source=/dev/null
source "${script_dir}/vcpkg-common.sh"

vcpkg_root="${VCPKG_ROOT:-}"
vcpkg_triplet=""

usage() {
  cat <<'EOF'
Usage: ./scripts/install-vcpkg-deps-linux.sh [options]

Runs vcpkg install for the repo manifest (vcpkg.json). Do this once (or after
manifest changes) before ./scripts/build-linux.sh.

Options:
  --vcpkg-root <path>   vcpkg clone root. Default: VCPKG_ROOT, ~/vcpkg, or vcpkg on PATH
  --vcpkg-triplet <t>   e.g. x64-linux or arm64-linux. Default: from machine arch
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vcpkg-root)
      vcpkg_root="$2"
      shift 2
      ;;
    --vcpkg-triplet)
      vcpkg_triplet="$2"
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

if [[ -z "${vcpkg_root}" ]]; then
  vcpkg_root="$(resolve_vcpkg_root "")"
else
  require_vcpkg_root "${vcpkg_root}"
fi

if [[ -z "${vcpkg_triplet}" ]]; then
  vcpkg_triplet="$(default_vcpkg_triplet_linux)"
fi

echo "vcpkg: installing manifest dependencies (Qt, VTK, ITK, libczi, ...) triplet=${vcpkg_triplet}"
echo "vcpkg root: ${vcpkg_root}"

(cd "${repo_root}" && "${vcpkg_root}/vcpkg" install --triplet "${vcpkg_triplet}" --vcpkg-root "${vcpkg_root}")

echo "vcpkg install finished."
