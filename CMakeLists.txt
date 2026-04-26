cmake_minimum_required(VERSION 3.24)

project(nd2-viewer VERSION 0.1.8 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/${PROJECT_NAME}" _nd2viewer_user_install_prefix)
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT
      OR CMAKE_INSTALL_PREFIX STREQUAL "C:/Program Files/${PROJECT_NAME}"
      OR CMAKE_INSTALL_PREFIX STREQUAL "C:/Program Files (x86)/${PROJECT_NAME}")
    set(CMAKE_INSTALL_PREFIX "${_nd2viewer_user_install_prefix}" CACHE PATH
      "Install path prefix, prepended onto install directories." FORCE
    )
  endif()
endif()

if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR "On Windows, nd2-viewer supports only MSVC builds. Use the x64-windows vcpkg triplet.")
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
elseif(WIN32)
  set(_nd2sdk_root_default "C:/Program Files/nd2readsdk-shared")
else()
  set(_nd2sdk_root_default "$ENV{HOME}/Documents/nd2readsdk-shared")
endif()

set(ND2SDK_ROOT "${_nd2sdk_root_default}" CACHE PATH "Path to the ND2 SDK root.")
set(ND2SDK_TIFF_LIBRARY "" CACHE FILEPATH "Path to the TIFF library used with the ND2 SDK on Linux when the shared SDK does not provide libtiff.so.")
set(Qt6_DIR "" CACHE PATH "Path to Qt6Config.cmake (optional; vcpkg toolchain usually supplies Qt).")
set(VTK_DIR "" CACHE PATH "Path to VTKConfig.cmake (optional; vcpkg toolchain usually supplies VTK).")
set(ITK_DIR "" CACHE PATH "Path to ITKConfig.cmake (optional; vcpkg toolchain usually supplies ITK).")
set(onnxruntime_DIR "" CACHE PATH "Path to onnxruntimeConfig.cmake (optional; vcpkg toolchain usually supplies ONNX Runtime).")
set(ND2VIEWER_DEBCR_MODEL_PATH "/home/jack/workspace/DeBCR/checkpoints/LM_2D_CARE/debcr_LM_2D_CARE.onnx" CACHE FILEPATH
  "Path to the DeBCR ONNX model copied into the app/package."
)

if(Qt6_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
endif()
if(VTK_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${VTK_DIR}")
endif()
if(ITK_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${ITK_DIR}")
endif()
if(onnxruntime_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${onnxruntime_DIR}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Multimedia OpenGL OpenGLWidgets Svg)
if(UNIX AND NOT APPLE)
  get_target_property(ND2VIEWER_QT_GUI_DISABLED_FEATURES Qt6::Gui QT_DISABLED_PUBLIC_FEATURES)
  if("xcb" IN_LIST ND2VIEWER_QT_GUI_DISABLED_FEATURES)
    message(FATAL_ERROR
      "Linux builds require Qt's xcb platform plugin. Re-run "
      "./scripts/install-vcpkg-deps-linux.sh so vcpkg rebuilds qtbase with the Linux xcb features from vcpkg.json."
    )
  endif()
endif()
find_package(ITK REQUIRED COMPONENTS ITKCommon ITKDeconvolution)
include(${ITK_USE_FILE})
find_package(onnxruntime CONFIG REQUIRED)
find_package(VTK CONFIG REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  InteractionStyle
  GUISupportQt
  RenderingCore
  RenderingOpenGL2
  RenderingVolume
  RenderingVolumeOpenGL2
)
if((WIN32 OR APPLE) AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(VTK_DIR AND NOT EXISTS "${VTK_DIR}/VTK-targets-debug.cmake")
    message(FATAL_ERROR
      "Debug builds require a VTK package with debug targets. Resolved VTK_DIR='${VTK_DIR}', "
      "but '${VTK_DIR}/VTK-targets-debug.cmake' was not found. "
      "Install dependencies with the vcpkg triplet that includes debug libraries (e.g. x64-windows), or override VTK_DIR."
    )
  endif()
endif()
set(ND2VIEWER_VTK_LIBS
  VTK::CommonCore
  VTK::CommonDataModel
  VTK::InteractionStyle
  VTK::GUISupportQt
  VTK::RenderingCore
  VTK::RenderingOpenGL2
  VTK::RenderingVolume
  VTK::RenderingVolumeOpenGL2
)

find_package(libCZI CONFIG REQUIRED)
if(TARGET libczi::libCZI)
  set(ND2VIEWER_LIBCZI_TARGET libczi::libCZI)
elseif(TARGET libczi::libCZIStatic)
  set(ND2VIEWER_LIBCZI_TARGET libczi::libCZIStatic)
else()
  message(FATAL_ERROR "libCZI was found, but neither libczi::libCZI nor libczi::libCZIStatic is available.")
endif()

set(_nd2viewer_target_type)
if(WIN32)
  set(_nd2viewer_target_type WIN32)
elseif(APPLE)
  set(_nd2viewer_target_type MACOSX_BUNDLE)
endif()

add_executable(nd2-viewer ${_nd2viewer_target_type}
  src/main.cpp
  src/resources/resources.qrc
  src/core/documentcontroller.cpp
  src/core/documentcontroller.h
  src/core/documentreader.h
  src/core/documentreaderfactory.cpp
  src/core/documentreaderfactory.h
  src/core/documenttypes.h
  src/core/policydocumentreader.cpp
  src/core/policydocumentreader.h
  src/core/czireader.cpp
  src/core/czireader.h
  src/core/deconvolution.cpp
  src/core/deconvolution.h
  src/core/framerenderer.cpp
  src/core/framerenderer.h
  src/core/movieexporter.cpp
  src/core/movieexporter.h
  src/core/nd2reader.cpp
  src/core/nd2reader.h
  src/core/readfailurepolicy.cpp
  src/core/readfailurepolicy.h
  src/core/volumeloader.cpp
  src/core/volumeloader.h
  src/core/volumeutils.cpp
  src/core/volumeutils.h
  src/ui/autocontrasttuningdialog.cpp
  src/ui/autocontrasttuningdialog.h
  src/ui/channelcontrolswidget.cpp
  src/ui/channelcontrolswidget.h
  src/ui/imageviewport.cpp
  src/ui/imageviewport.h
  src/ui/mainwindow.cpp
  src/ui/mainwindow.h
  src/ui/volumeviewport3d_backend.h
  src/ui/volumeviewport3d.cpp
  src/ui/volumeviewport3d.h
  src/ui/volumeviewport3d_vtk.cpp
  src/ui/volumeviewport3d_vtk.h
)

if(WIN32)
  target_sources(nd2-viewer PRIVATE
    src/resources/app-icon.rc
  )
endif()

if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE app-icon.icns)
  set_source_files_properties(src/resources/app-icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  target_sources(nd2-viewer PRIVATE src/resources/app-icon.icns)
  set_target_properties(nd2-viewer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "nd2-viewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nd2viewer.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE ${MACOSX_BUNDLE_ICON_FILE}
  )
endif()

target_include_directories(nd2-viewer
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ND2SDK_ROOT}/include"
)

get_target_property(ND2VIEWER_LIBCZI_INCLUDE_DIRS "${ND2VIEWER_LIBCZI_TARGET}" INTERFACE_INCLUDE_DIRECTORIES)
foreach(libczi_include_dir IN LISTS ND2VIEWER_LIBCZI_INCLUDE_DIRS)
  if(EXISTS "${libczi_include_dir}/libCZI/libCZI.h")
    target_include_directories(nd2-viewer PRIVATE "${libczi_include_dir}/libCZI")
  endif()
endforeach()

find_path(ND2VIEWER_ONNXRUNTIME_CXX_API_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  PATH_SUFFIXES onnxruntime onnxruntime/core/session
  REQUIRED
)
target_include_directories(nd2-viewer PRIVATE "${ND2VIEWER_ONNXRUNTIME_CXX_API_INCLUDE_DIR}")

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
target_compile_definitions(nd2-viewer PRIVATE ND2VIEWER_HAS_VTK_3D=1)

if(ND2VIEWER_USE_SHARED_SDK)
  add_library(ND2ReadSdk::Reader SHARED IMPORTED GLOBAL)
  if(UNIX AND NOT APPLE)
    add_library(ND2ReadSdk::Tiff INTERFACE IMPORTED GLOBAL)
  else()
    add_library(ND2ReadSdk::Tiff SHARED IMPORTED GLOBAL)
  endif()
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
    target_link_options(nd2-viewer PRIVATE LINKER:--allow-shlib-undefined)
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
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Widgets
    "${ND2VIEWER_LIBCZI_TARGET}"
    ND2ReadSdk::Reader
    ND2ReadSdk::Tiff
    onnxruntime::onnxruntime
    ${ITK_LIBRARIES}
    ${ND2VIEWER_VTK_LIBS}
)

vtk_module_autoinit(TARGETS nd2-viewer MODULES ${ND2VIEWER_VTK_LIBS})

if(ND2VIEWER_DEBCR_MODEL_PATH AND EXISTS "${ND2VIEWER_DEBCR_MODEL_PATH}")
  if(APPLE)
    set(ND2VIEWER_MODEL_BUILD_DIR "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Resources/models")
  else()
    set(ND2VIEWER_MODEL_BUILD_DIR "$<TARGET_FILE_DIR:nd2-viewer>/models")
  endif()
  add_custom_command(TARGET nd2-viewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ND2VIEWER_MODEL_BUILD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${ND2VIEWER_DEBCR_MODEL_PATH}"
      "${ND2VIEWER_MODEL_BUILD_DIR}/debcr_LM_2D_CARE.onnx"
    COMMENT "Copying bundled DeBCR ONNX model"
  )
else()
  message(WARNING "ND2VIEWER_DEBCR_MODEL_PATH does not exist; DeBCR inference will require a packaged model at runtime.")
endif()

if(WIN32)
  add_custom_command(TARGET nd2-viewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      $<TARGET_RUNTIME_DLLS:nd2-viewer>
      "$<TARGET_FILE_DIR:nd2-viewer>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying runtime DLL dependencies"
  )
endif()

if(TARGET onnxruntime::onnxruntime)
  get_target_property(ND2VIEWER_ONNXRUNTIME_TARGET_TYPE onnxruntime::onnxruntime TYPE)
  if(ND2VIEWER_ONNXRUNTIME_TARGET_TYPE STREQUAL "SHARED_LIBRARY"
      OR ND2VIEWER_ONNXRUNTIME_TARGET_TYPE STREQUAL "MODULE_LIBRARY"
      OR ND2VIEWER_ONNXRUNTIME_TARGET_TYPE STREQUAL "UNKNOWN_LIBRARY")
    set(ND2VIEWER_COPY_ONNXRUNTIME_RUNTIME ON)
  endif()
endif()
if(ND2VIEWER_COPY_ONNXRUNTIME_RUNTIME)
  if(APPLE)
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Frameworks"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:onnxruntime::onnxruntime>"
        "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Frameworks"
      COMMENT "Copying ONNX Runtime library"
    )
  else()
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:onnxruntime::onnxruntime>"
        "$<TARGET_FILE_DIR:nd2-viewer>"
      COMMENT "Copying ONNX Runtime library"
    )
    install(FILES "$<TARGET_FILE:onnxruntime::onnxruntime>"
      DESTINATION "."
      COMPONENT Runtime
    )
  endif()
endif()

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

endif()

if(TARGET libczi::libCZI)
  if(WIN32)
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:libczi::libCZI>"
        "$<TARGET_FILE_DIR:nd2-viewer>"
      COMMENT "Copying libCZI runtime"
    )
    install(FILES "$<TARGET_FILE:libczi::libCZI>"
      DESTINATION "."
      COMPONENT Runtime
    )
  elseif(APPLE)
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Frameworks"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:libczi::libCZI>"
        "$<TARGET_BUNDLE_DIR:nd2-viewer>/Contents/Frameworks"
      COMMENT "Copying libCZI runtime"
    )
  endif()
endif()

if(APPLE)
  set_target_properties(nd2-viewer PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
  )
elseif(UNIX AND NOT APPLE)
  set_target_properties(nd2-viewer PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()

if(APPLE AND ND2VIEWER_USE_SHARED_SDK)
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
endif()

if(UNIX AND NOT APPLE AND ND2VIEWER_USE_SHARED_SDK)
  if(EXISTS "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.so")
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.so"
        "$<TARGET_FILE_DIR:nd2-viewer>"
      COMMENT "Copying ND2 SDK runtime library"
    )
  endif()
  if(EXISTS "${ND2SDK_ROOT}/lib/liblimfile-shared.so")
    add_custom_command(TARGET nd2-viewer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ND2SDK_ROOT}/lib/liblimfile-shared.so"
        "$<TARGET_FILE_DIR:nd2-viewer>"
      COMMENT "Copying limlimfile-shared runtime library"
    )
  endif()
endif()

if(ND2VIEWER_DEBCR_MODEL_PATH AND EXISTS "${ND2VIEWER_DEBCR_MODEL_PATH}")
  if(APPLE)
    install(FILES "${ND2VIEWER_DEBCR_MODEL_PATH}"
      DESTINATION "nd2-viewer.app/Contents/Resources/models"
      RENAME "debcr_LM_2D_CARE.onnx"
      COMPONENT Runtime
    )
  else()
    install(FILES "${ND2VIEWER_DEBCR_MODEL_PATH}"
      DESTINATION "models"
      RENAME "debcr_LM_2D_CARE.onnx"
      COMPONENT Runtime
    )
  endif()
endif()

if(WIN32)
  install(
    DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    DESTINATION "."
    COMPONENT Runtime
    PATTERN "nd2-viewer.exe" EXCLUDE
    PATTERN "cpack-nsis-wrapper.exe" EXCLUDE
    PATTERN "*.ilk" EXCLUDE
    PATTERN "*.pdb" EXCLUDE
  )
elseif(UNIX AND NOT APPLE)
  if(EXISTS "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.so")
    install(FILES "${ND2SDK_ROOT}/lib/libnd2readsdk-shared.so"
      DESTINATION "."
      COMPONENT Runtime
    )
  endif()
  if(EXISTS "${ND2SDK_ROOT}/lib/liblimfile-shared.so")
    install(FILES "${ND2SDK_ROOT}/lib/liblimfile-shared.so"
      DESTINATION "."
      COMPONENT Runtime
    )
  endif()
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
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt 6 desktop viewer for Nikon ND2 and Zeiss CZI microscopy files")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nd2-viewer")
set(CPACK_PACKAGE_EXECUTABLES "nd2-viewer" "nd2-viewer")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(WIN32)
  add_executable(cpack-nsis-wrapper
    scripts/cpack-nsis-wrapper.cpp
  )
  set_target_properties(cpack-nsis-wrapper PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
  )

  file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/bin/cpack-nsis-wrapper.exe" _cpack_nsis_executable)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_INSTALL_ROOT "$LOCALAPPDATA\\\\Programs")
  set(CPACK_NSIS_DISPLAY_NAME "nd2-viewer")
  set(CPACK_NSIS_PACKAGE_NAME "nd2-viewer")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "nd2-viewer.exe")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app-icon.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app-icon.ico")
  # CPack's stock NSIS template hard-codes admin/UAC behavior; nsis-cpack.ps1 patches
  # the generated script then runs makensis. CPack also probes /VERSION directly,
  # so this must be a real executable rather than powershell.exe with pre-arguments.
  set(CPACK_NSIS_EXECUTABLE "${_cpack_nsis_executable}")
  set(CPACK_NSIS_EXECUTABLE_PRE_ARGUMENTS
    "--powershell-script"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/nsis-cpack.ps1"
  )
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
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
