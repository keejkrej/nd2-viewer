include("${VCPKG_ROOT_DIR}/triplets/x64-linux.cmake")

if(PORT STREQUAL "liblzma")
  list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DXZ_ARM64_CRC32=OFF)
endif()
