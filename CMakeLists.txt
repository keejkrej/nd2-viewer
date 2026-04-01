cmake_minimum_required(VERSION 3.24)

project(nd2-viewer VERSION 0.1.0 LANGUAGES CXX)

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
set(ND2SDK_ROOT "C:/Program Files/nd2readsdk-shared" CACHE PATH "Path to the ND2 SDK root.")
set(Qt6_DIR "" CACHE PATH "Path to Qt6Config.cmake, e.g. C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6")

if(Qt6_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(nd2-viewer
  src/main.cpp
  src/core/documentcontroller.cpp
  src/core/documentcontroller.h
  src/core/framecache.h
  src/core/framerenderer.cpp
  src/core/framerenderer.h
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

target_compile_definitions(nd2-viewer
  PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(ND2VIEWER_USE_SHARED_SDK)
  add_library(ND2ReadSdk::Reader SHARED IMPORTED GLOBAL)
  set_target_properties(ND2ReadSdk::Reader PROPERTIES
    IMPORTED_IMPLIB "${ND2SDK_ROOT}/lib/nd2readsdk-shared.lib"
    IMPORTED_LOCATION "${ND2SDK_ROOT}/bin/nd2readsdk-shared.dll"
  )
else()
  add_library(ND2ReadSdk::Reader STATIC IMPORTED GLOBAL)
  set_target_properties(ND2ReadSdk::Reader PROPERTIES
    IMPORTED_LOCATION "${ND2SDK_ROOT}/lib/nd2readsdk-static.lib"
  )
endif()

target_link_libraries(nd2-viewer
  PRIVATE
    Qt6::Concurrent
    Qt6::Widgets
    ND2ReadSdk::Reader
)

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
