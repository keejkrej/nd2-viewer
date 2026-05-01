# Invoked after linking nd2-viewer on Linux. Nikon's liblimfile-shared.so
# depends on libtiff.so.5, but many distros only ship libtiff.so.6 and the
# ND2 SDK lib/ tree often contains a dangling libtiff.so -> libtiff.so.5 link.
#
# The Nikon Linux limfile shared library uses RUNPATH $ORIGIN/../lib, so when
# the SDK .so files are copied next to the executable under .../bin/, the
# loader resolves libtiff from .../lib/ (sibling of bin/), not from bin/.
if(NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "linux-ensure-libtiff5.cmake requires -DOUT_DIR=")
endif()

get_filename_component(_bindir "${OUT_DIR}" ABSOLUTE)
# Older builds mistakenly placed the symlink in bin/; limfile uses $ORIGIN/../lib.
file(REMOVE "${_bindir}/libtiff.so.5")

set(_libdir "${_bindir}/../lib")
set(_dest "${_libdir}/libtiff.so.5")

if(EXISTS "${_dest}")
  return()
endif()

set(_candidates
  "/usr/lib/x86_64-linux-gnu/libtiff.so.6"
  "/usr/lib/aarch64-linux-gnu/libtiff.so.6"
  "/usr/lib64/libtiff.so.6"
  "/usr/lib/libtiff.so.6"
)
set(_t6 "")
foreach(_c IN LISTS _candidates)
  if(EXISTS "${_c}")
    get_filename_component(_t6 "${_c}" REALPATH)
    break()
  endif()
endforeach()

if(_t6 STREQUAL "")
  message(WARNING
    "nd2-viewer (Linux): liblimfile needs libtiff.so.5; no system libtiff.so.6 "
    "found under /usr/lib*. Install libtiff or place libtiff.so.5 on LD_LIBRARY_PATH."
  )
  return()
endif()

file(MAKE_DIRECTORY "${_libdir}")
file(REMOVE "${_dest}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_t6}" "${_dest}"
  RESULT_VARIABLE _rc
)
if(_rc)
  message(WARNING
    "nd2-viewer (Linux): could not create ${_dest} -> ${_t6} (cmake exit ${_rc})"
  )
endif()
