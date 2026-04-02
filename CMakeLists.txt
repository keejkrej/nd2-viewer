cmake_minimum_required(VERSION 3.24)

project(nd2-viewer VERSION 0.1.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR "On Windows, nd2-viewer now supports only the MSVC Qt toolchain. Use Qt 6 msvc2022_64.")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(ND2VIEWER_USE_SHARED_SDK "Link against the Nikon shared SDK." ON)
if(APPLE)
  set(_nd2sdk_root_default "$ENV{HOME}/Documents/nd2readsdk-shared-1.7.6.0-Macos-armv8")
  set(_qt6_dir_default "/opt/homebrew/lib/cmake/Qt6")
elseif(WIN32)
  set(_nd2sdk_root_default "C:/Program Files/nd2readsdk-shared")
  set(_qt6_dir_default "")
else()
  set(_nd2sdk_root_default "")
  set(_qt6_dir_default "")
endif()

set(ND2SDK_ROOT "${_nd2sdk_root_default}" CACHE PATH "Path to the ND2 SDK root.")
set(Qt6_DIR "${_qt6_dir_default}" CACHE PATH "Path to Qt6Config.cmake, e.g. C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6")

if(Qt6_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Multimedia)

set(_nd2viewer_target_type)
if(APPLE)
  set(_nd2viewer_target_type MACOSX_BUNDLE)
endif()

add_executable(nd2-viewer ${_nd2viewer_target_type}
  src/main.cpp
  src/core/documentcontroller.cpp
  src/core/documentcontroller.h
  src/core/framecache.h
  src/core/framerenderer.cpp
  src/core/framerenderer.h
  src/core/movieexporter.cpp
  src/core/movieexporter.h
  src/core/nd2reader.cpp
  src/core/nd2reader.h
  src/core/nd2types.h
  src/ui/channelcontrolswidget.cpp
  src/ui/channelcontrolswidget.h
  src/ui/imageviewport.cpp
  src/ui/imageviewport.h
  src/ui/mainwindow.cpp
  src/ui/mainwindow.h
)

if(APPLE)
  set_target_properties(nd2-viewer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "nd2-viewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nd2viewer.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

target_include_directories(nd2-viewer
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ND2SDK_ROOT}/include"
)

if(MSVC)
  target_compile_options(nd2-viewer PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(nd2-viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_compile_definitions(nd2-viewer
    PRIVATE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
  )
endif()

if(ND2VIEWER_USE_SHARED_SDK)
  add_library(ND2ReadSdk::Reader SHARED IMPORTED GLOBAL)
  add_library(ND2ReadSdk::Tiff SHARED IMPORTED GLOBAL)
  if(WIN32)
    set_target_properties(ND2ReadSdk::Reader PROPERTIES
      IMPORTED_IMPLIB "${ND2SDK_ROOT}/lib/nd2readsdk-shared.lib"
      IMPORTED_LOCATION "${ND2SDK_ROOT}/bin/nd2readsdk-shared.dll"
    )
    set_target_properties(ND2ReadSdk::Tiff PROPERTIES
      IMPORTED_IMPLIB "${ND2SDK_ROOT}/lib/tiff.lib"
      IMPORTED_LOCATION "${ND2SDK_ROOT}/bin/tiff.dll"
    )
  elseif(APPLE)
    set_target_properties(ND2ReadSdk::Reader PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.dylib"
    )
    set_target_properties(ND2ReadSdk::Tiff PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/bin/libtiff.dylib"
    )
  else()
    set_target_properties(ND2ReadSdk::Reader PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.so"
    )
    set_target_properties(ND2ReadSdk::Tiff PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/libtiff.so"
    )
  endif()
else()
  if(APPLE)
    message(FATAL_ERROR "Static SDK builds are not wired on macOS yet. Use the shared SDK bundle from ~/Documents.")
  endif()

  add_library(ND2ReadSdk::Reader STATIC IMPORTED GLOBAL)
  add_library(ND2ReadSdk::Tiff STATIC IMPORTED GLOBAL)
  if(WIN32)
    set_target_properties(ND2ReadSdk::Reader PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/nd2readsdk-static.lib"
    )
    set_target_properties(ND2ReadSdk::Tiff PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/tiff.lib"
    )
  else()
    set_target_properties(ND2ReadSdk::Reader PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/libnd2readsdk-static.a"
    )
    set_target_properties(ND2ReadSdk::Tiff PROPERTIES
      IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/libtiff.a"
    )
  endif()
endif()

target_link_libraries(nd2-viewer
  PRIVATE
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::Widgets
    ND2ReadSdk::Reader
    ND2ReadSdk::Tiff
)

if(APPLE)
  install(TARGETS nd2-viewer
    BUNDLE DESTINATION "."
    COMPONENT Runtime
  )
else()
  install(TARGETS nd2-viewer
    RUNTIME DESTINATION "."
    COMPONENT Runtime
  )
endif()

if(WIN32 AND ND2VIEWER_USE_SHARED_SDK)
  file(GLOB ND2_SDK_DLLS "${ND2SDK_ROOT}/bin/*.dll")
  foreach(nd2_sdk_dll IN LISTS ND2_SDK_DLLS)
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${nd2_sdk_dll}"
        "$<TARGET_FILE_DIR:nd2-viewer>"
    )
  endforeach()

  if(TARGET Qt6::qmake)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    if(QT_QMAKE_EXECUTABLE)
      get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
      find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
      if(WINDEPLOYQT_EXECUTABLE)
        set(WINDEPLOYQT_ARGS --no-translations)
        if(MSVC)
          list(APPEND WINDEPLOYQT_ARGS --no-compiler-runtime)
        endif()

        add_custom_command(TARGET nd2-viewer POST_BUILD
          COMMAND "${WINDEPLOYQT_EXECUTABLE}" ${WINDEPLOYQT_ARGS} "$<TARGET_FILE:nd2-viewer>"
          COMMENT "Deploying Qt runtime dependencies"
        )
      endif()
    endif()
  endif()

endif()

if(APPLE AND ND2VIEWER_USE_SHARED_SDK)
  set_target_properties(nd2-viewer PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
  )

  file(GLOB ND2_SDK_DYLIBS
    "${ND2SDK_ROOT}/bin/*.dylib"
    "${ND2SDK_ROOT}/lib/*.dylib"
  )
  set(ND2VIEWER_APP_FRAMEWORKS_DIR "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Frameworks")
  foreach(nd2_sdk_dylib IN LISTS ND2_SDK_DYLIBS)
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ND2VIEWER_APP_FRAMEWORKS_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${nd2_sdk_dylib}"
        "${ND2VIEWER_APP_FRAMEWORKS_DIR}"
    )
  endforeach()

  if(TARGET Qt6::qmake)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    if(QT_QMAKE_EXECUTABLE)
      get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
      find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")
      if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET nd2-viewer POST_BUILD
          COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:nd2-viewer>" -always-overwrite
          COMMENT "Deploying macOS app bundle dependencies"
        )
      endif()
    endif()
  endif()
endif()

if(NOT APPLE)
  install(
    DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    DESTINATION "."
    COMPONENT Runtime
    PATTERN "nd2-viewer.exe" EXCLUDE
    PATTERN "*.ilk" EXCLUDE
    PATTERN "*.pdb" EXCLUDE
  )
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "."
  COMPONENT Runtime
)

if(WIN32 AND MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
  include(InstallRequiredSystemLibraries)
  if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
      DESTINATION "."
      COMPONENT Runtime
    )
  endif()
endif()

set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_PACKAGE_VENDOR "nd2-viewer contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt 6 desktop viewer for Nikon ND2 microscopy files")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nd2-viewer")
set(CPACK_PACKAGE_EXECUTABLES "nd2-viewer" "nd2-viewer")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "nd2-viewer")
  set(CPACK_NSIS_PACKAGE_NAME "nd2-viewer")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "nd2-viewer.exe")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "nd2-viewer.exe")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut \\\"$DESKTOP\\\\nd2-viewer.lnk\\\" \\\"$INSTDIR\\\\nd2-viewer.exe\\\""
  )
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete \\\"$DESKTOP\\\\nd2-viewer.lnk\\\""
  )
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "nd2-viewer")
endif()

include(CPack)
