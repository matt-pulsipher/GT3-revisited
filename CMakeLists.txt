cmake_minimum_required(VERSION 3.21)
project(GT3Revisited VERSION 0.3.0 LANGUAGES C CXX)

# GT3 Revisited: an open reimplementation of the Gran Turismo 3 engine.
# No game data is part of this project; everything is read at runtime from
# the user's own disc image.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GT3R_BUILD_TOOLS "Build the gt3tool command line utility" ON)
option(GT3R_BUILD_TESTS "Build unit tests" ON)
option(GT3R_PROGRAM_TOOLS "Developer-only gt3tool commands" OFF)

include(cmake/Dependencies.cmake)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  # fopen/getenv/localtime/sscanf are used portably; silence MSVC's "_s" suggestions.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Engine library: everything that is not the executable entry point.
# ---------------------------------------------------------------------------
# Engine sources are picked up per directory so parallel work never has to
# edit this file: put new code under src/<area>/ (anything except src/app).
file(GLOB_RECURSE GT3R_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER GT3R_ENGINE_SOURCES EXCLUDE REGEX "/src/app/")
add_library(gt3r_engine STATIC ${GT3R_ENGINE_SOURCES})
target_include_directories(gt3r_engine PUBLIC src)
target_include_directories(gt3r_engine PRIVATE third_party/minimp3)
target_link_libraries(gt3r_engine PUBLIC SDL3::SDL3 ZLIB::ZLIB)
if(APPLE)
  target_link_libraries(gt3r_engine PUBLIC objc) # platform/mac_layer.cpp
endif()
# VR headsets (src/xr, docs/dev/VR.md). The code compiles to nothing without GT3R_XR.
if(GT3R_XR)
  target_compile_definitions(gt3r_engine PUBLIC GT3R_XR=1)
  target_include_directories(gt3r_engine SYSTEM PUBLIC ${GT3R_OPENXR_INCLUDE_DIR})
endif()
# Online play (src/net): Winsock on Windows.
if(WIN32)
  target_link_libraries(gt3r_engine PUBLIC ws2_32)
endif()

# The game's version: project(... VERSION) above is the only source. gt3r_version.h
# (generated) feeds the window / app metadata, the main menu, the online version
# checks (docs/dev/ONLINE.md "Versions and updates") and the macOS bundle; Android's
# build.gradle reads the same line. A local build may add a suffix
# (-DGT3R_VERSION_SUFFIX=-dev): it still counts as x.y.z for online play, but shows the
# update notice once a release of x.y.z or later exists.
set(GT3R_VERSION_SUFFIX "" CACHE STRING "Suffix for local builds, e.g. -dev (empty for releases)")
if(NOT GT3R_VERSION_SUFFIX STREQUAL "" AND NOT GT3R_VERSION_SUFFIX MATCHES "^-[A-Za-z0-9.]+$")
  message(FATAL_ERROR "GT3R_VERSION_SUFFIX must look like -dev (got '${GT3R_VERSION_SUFFIX}')")
endif()
set(GT3R_VERSION "${PROJECT_VERSION}${GT3R_VERSION_SUFFIX}")
configure_file(${CMAKE_SOURCE_DIR}/cmake/gt3r_version.h.in ${CMAKE_BINARY_DIR}/generated/gt3r_version.h @ONLY)
target_include_directories(gt3r_engine PUBLIC ${CMAKE_BINARY_DIR}/generated)

# ---------------------------------------------------------------------------
# Game executable.
# ---------------------------------------------------------------------------
# App sources (screens self-register via GT3R_REGISTER_SCREEN, so they must be
# compiled into the executable rather than the static library).
file(GLOB GT3R_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/app/*.cpp)

if(ANDROID)
  # SDL's Android Java glue loads libmain.so.
  add_library(main SHARED ${GT3R_APP_SOURCES})
  set(GT3R_APP_TARGET main)
else()
  add_executable(gt3r ${GT3R_APP_SOURCES})
  set(GT3R_APP_TARGET gt3r)
  if(APPLE)
    set_target_properties(gt3r PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER org.gt3revisited.gt3r
      MACOSX_BUNDLE_BUNDLE_NAME "GT3 Revisited"
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  elseif(WIN32)
    set_target_properties(gt3r PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
endif()
target_link_libraries(${GT3R_APP_TARGET} PRIVATE gt3r_engine)

# UI font (Barlow, SIL OFL 1.1, third_party/fonts/barlow) compiled into the
# executable, rasterised at runtime with stb_truetype (public domain).
set(GT3R_UI_FONTS
  ${CMAKE_SOURCE_DIR}/third_party/fonts/barlow/BarlowSemiCondensed-Medium.ttf
  ${CMAKE_SOURCE_DIR}/third_party/fonts/barlow/BarlowCondensed-SemiBold.ttf)
set(GT3R_UI_FONT_SRC ${CMAKE_BINARY_DIR}/generated/ui_font_data.cpp)
list(JOIN GT3R_UI_FONTS "|" _ui_font_files)
add_custom_command(OUTPUT ${GT3R_UI_FONT_SRC}
  COMMAND ${CMAKE_COMMAND} -DOUT=${GT3R_UI_FONT_SRC} -DNAMESPACE=gt3r::font_data
          -DNAMES=kBody|kTitle -DFILES=${_ui_font_files}
          -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${GT3R_UI_FONTS} ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Embedding UI fonts"
  VERBATIM)
target_sources(${GT3R_APP_TARGET} PRIVATE ${GT3R_UI_FONT_SRC})
target_include_directories(${GT3R_APP_TARGET} SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

if(GT3R_BUILD_TOOLS AND NOT ANDROID)
  # The online relay server (also builds on its own: tools/gt3r-relay/CMakeLists.txt).
  add_executable(gt3r-relay tools/gt3r-relay/main.cpp src/net/socket.cpp src/net/protocol.cpp
                 src/net/rendezvous.cpp src/net/relay_server.cpp)
  target_include_directories(gt3r-relay PRIVATE src)
  if(WIN32)
    target_link_libraries(gt3r-relay PRIVATE ws2_32)
  endif()
  file(GLOB GT3TOOL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/gt3tool/*.cpp)
  add_executable(gt3tool ${GT3TOOL_SOURCES})
  target_link_libraries(gt3tool PRIVATE gt3r_engine)
  if(GT3R_PROGRAM_TOOLS)
    target_compile_definitions(gt3tool PRIVATE GT3R_PROGRAM_TOOLS)
  endif()
endif()

# Optional developer tools that are not part of every checkout: any
# tools/<name>/devtool.cmake is included (none of them is needed to build or
# test the game).
if(NOT ANDROID)
  file(GLOB GT3R_DEVTOOL_SCRIPTS ${CMAKE_SOURCE_DIR}/tools/*/devtool.cmake)
  foreach(_devtool ${GT3R_DEVTOOL_SCRIPTS})
    include(${_devtool})
  endforeach()
endif()

if(GT3R_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  file(GLOB GT3R_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
  add_executable(gt3r_tests ${GT3R_TEST_SOURCES})
  target_link_libraries(gt3r_tests PRIVATE gt3r_engine)
  # Disc tests that load files from the source tree (examples/mods/...).
  target_compile_definitions(gt3r_tests PRIVATE GT3R_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME unit COMMAND gt3r_tests)
endif()

# Fuzz targets for untrusted input (downloaded mod cars, network data): tests/fuzz/fuzz.cpp.
# Configure a separate tree with -fsanitize=address,undefined (see the file's header).
option(GT3R_FUZZ "Build the gt3r_fuzz targets" OFF)
option(GT3R_LIBFUZZER "Link gt3r_fuzz with clang's libFuzzer (coverage guided)" OFF)
if(GT3R_FUZZ AND NOT ANDROID)
  add_executable(gt3r_fuzz tests/fuzz/fuzz.cpp)
  target_link_libraries(gt3r_fuzz PRIVATE gt3r_engine)
  if(GT3R_LIBFUZZER)
    target_compile_definitions(gt3r_fuzz PRIVATE GT3R_LIBFUZZER)
    target_compile_options(gt3r_fuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(gt3r_fuzz PRIVATE -fsanitize=fuzzer)
  endif()
endif()

# Icons, Info.plist and install rules for release packages.
include(cmake/Packaging.cmake)
