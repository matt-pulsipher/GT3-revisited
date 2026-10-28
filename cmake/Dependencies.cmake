# Third-party dependencies. A system/package-manager install is preferred;
# otherwise the dependency is fetched and built from source so that Windows,
# Linux, macOS and Android builds all work without manual setup.

include(FetchContent)

# --- SDL3: windowing, input (gamepads, wheels, sensors), audio, GPU ---------
# Release packages build SDL from source (GT3R_FETCH_SDL) so they don't depend
# on a system or Homebrew SDL; Windows packages use SDL3.dll (GT3R_SDL_SHARED).
option(GT3R_FETCH_SDL "Always build SDL3 from source instead of using an installed one" OFF)
option(GT3R_SDL_SHARED "Build the fetched SDL3 as a shared library" OFF)
# VR headsets (docs/dev/VR.md): SDL_GPU's OpenXR support arrives with SDL 3.6, so these
# builds take SDL from its development branch at a pinned commit (the Quest APK's
# android/fetch-sdl.sh --xr fetches the same one); every other build stays on 3.4.
option(GT3R_XR "OpenXR mode for VR headsets (Meta Quest); needs SDL 3.6 with SDL_GPU OpenXR" OFF)
set(GT3R_SDL_TAG release-3.4.12)
set(GT3R_SDL_SHALLOW TRUE)
if(GT3R_XR)
  set(GT3R_SDL_TAG c17963b8ff64ebd51cbc0bac7389dd48a57c1786) # SDL main, 2026-10-18
  set(GT3R_SDL_SHALLOW FALSE) # a shallow clone has only the branch tip; a pinned commit needs history
  set(GT3R_FETCH_SDL ON)
  set(SDL_GPU_OPENXR ON CACHE BOOL "" FORCE)
endif()
# iOS: an installed SDL3 (Homebrew's) is built for macOS, so the device build always makes its own.
if(IOS)
  set(GT3R_FETCH_SDL ON)
endif()
if(NOT GT3R_FETCH_SDL)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT SDL3_FOUND)
  message(STATUS "Building SDL3 from source")
  set(SDL_SHARED ${GT3R_SDL_SHARED} CACHE BOOL "" FORCE)
  if(GT3R_SDL_SHARED)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${GT3R_SDL_TAG}
    GIT_SHALLOW ${GT3R_SDL_SHALLOW})
  FetchContent_MakeAvailable(SDL3)
  if(GT3R_LIBTOOL_QUIET AND TARGET SDL3-static)
    set_target_properties(SDL3-static PROPERTIES STATIC_LIBRARY_OPTIONS ${GT3R_LIBTOOL_QUIET})
  endif()
endif()
if(GT3R_XR)
  # The OpenXR headers SDL builds against (it vendors them; they are not installed).
  if(NOT EXISTS ${sdl3_SOURCE_DIR}/include/SDL3/SDL_openxr.h OR
     NOT EXISTS ${sdl3_SOURCE_DIR}/src/video/khronos/openxr/openxr.h)
    message(FATAL_ERROR "GT3R_XR needs SDL 3.6 with OpenXR (got ${sdl3_SOURCE_DIR}); "
                        "on Android run android/fetch-sdl.sh --xr")
  endif()
  set(GT3R_OPENXR_INCLUDE_DIR ${sdl3_SOURCE_DIR}/src/video/khronos)
endif()

# --- zlib: GT3 stores compressed files as gzip / raw deflate ---------------
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
  message(STATUS "zlib not found on system; fetching")
  set(SKIP_INSTALL_ALL ON) # a static zlib is linked in; don't install its files into packages
  # zlib's example programs register CTest tests ("example", "minigzip") that our build never
  # builds, so ctest reported them as failed ("Not Run") on Windows CI.
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(zlib)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
  target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()
