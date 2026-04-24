#!/usr/bin/env bash
# Bundle Qt into a .app with macdeployqt, then ad-hoc sign the bundle.
# Args: <path-to-nd2-viewer.app> <Qt6_DIR (dir with Qt6Config.cmake or path to that file)>
# Used by CMake POST_BUILD and by package-macos.sh.
set -euo pipefail

app_bundle="$1"
qt6_dir="$2"

if [[ ! -d "${app_bundle}" ]]; then
  echo "macos-macdeployqt: app bundle not found: ${app_bundle}" >&2
  exit 1
fi

if [[ -f "${qt6_dir}" ]]; then
  qt6_dir="$(cd "$(dirname "${qt6_dir}")" && pwd)"
elif [[ -d "${qt6_dir}" ]]; then
  qt6_dir="$(cd "${qt6_dir}" && pwd)"
else
  echo "macos-macdeployqt: Qt6_DIR does not exist: ${qt6_dir}" >&2
  exit 1
fi

if [[ "$(basename "$(dirname "${qt6_dir}")")" == "share" ]]; then
  qt_prefix="$(cd "$(dirname "$(dirname "${qt6_dir}")")" && pwd)"
  qt_lib_dir="${qt_prefix}/lib"
else
  qt_lib_dir="$(cd "$(dirname "$(dirname "${qt6_dir}")")" && pwd)"
  qt_prefix="$(cd "$(dirname "$(dirname "$(dirname "${qt6_dir}")")")" && pwd)"
fi
macdeployqt="${qt_prefix}/bin/macdeployqt"
if [[ ! -x "${macdeployqt}" ]]; then
  macdeployqt="${qt_prefix}/tools/Qt6/bin/macdeployqt"
fi
if [[ ! -x "${macdeployqt}" ]]; then
  echo "macos-macdeployqt: macdeployqt not found at ${qt_prefix}/bin or ${qt_prefix}/tools/Qt6/bin" >&2
  exit 1
fi

chmod -R u+w "${app_bundle}" || true
xattr -cr "${app_bundle}" 2>/dev/null || true

declare -a _libpath_seen=()
macdeployqt_args=()
append_libpath() {
  local d="$1"
  [[ -z "${d}" || ! -d "${d}" ]] && return
  local abs
  abs="$(cd "${d}" && pwd)"
  # Avoid "${arr[@]}" on an empty array with set -u (unbound variable on some bash builds).
  local i
  for (( i = 0; i < ${#_libpath_seen[@]}; i++ )); do
    [[ "${_libpath_seen[i]}" == "${abs}" ]] && return
  done
  _libpath_seen+=("${abs}")
  macdeployqt_args+=("-libpath=${abs}")
}

macdeployqt_args=(
  "${app_bundle}"
  -verbose=2
  -always-overwrite
  -no-strip
  -no-codesign
)
append_libpath "${qt_lib_dir}"
if command -v brew >/dev/null 2>&1; then
  _hb="$(brew --prefix 2>/dev/null)"
  [[ -n "${_hb}" ]] && append_libpath "${_hb}/lib"
  for _pkg in qtbase qtsvg qtpdf qtmultimedia qtwebengine; do
    _bp="$(brew --prefix "${_pkg}" 2>/dev/null)" || continue
    append_libpath "${_bp}/lib"
  done
fi

echo "macos-macdeployqt: running ${macdeployqt} ... this may take several minutes on Homebrew Qt."
"${macdeployqt}" "${macdeployqt_args[@]}"

# -no-codesign avoids install_name_tool issues during deploy; re-sign the bundle so dyld accepts
# mapped pages on recent macOS (avoids CODESIGNING / Invalid Page when loading plugins + ICU).
if command -v codesign >/dev/null 2>&1; then
  echo "macos-macdeployqt: ad-hoc signing ${app_bundle} ..."
  codesign --force --deep -s - "${app_bundle}"
fi
