#!/bin/sh
# Fetches the SDL3 source used for both the Java glue and the native build.
#   android/fetch-sdl.sh        SDL 3.4 for the phone/tablet APK (third_party/SDL)
#   android/fetch-sdl.sh --xr   SDL's development branch for the Meta Quest APK
#                               (third_party/SDL-xr; SDL_GPU's OpenXR support, docs/dev/VR.md)
set -e
cd "$(dirname "$0")"
TAG=release-3.4.12
# Keep in step with GT3R_SDL_TAG for GT3R_XR in cmake/Dependencies.cmake.
XR_COMMIT=c17963b8ff64ebd51cbc0bac7389dd48a57c1786
if [ "$1" = "--xr" ]; then
  if [ ! -d third_party/SDL-xr ]; then
    git init -q third_party/SDL-xr
    git -C third_party/SDL-xr fetch -q --depth 1 https://github.com/libsdl-org/SDL.git "$XR_COMMIT"
    git -C third_party/SDL-xr checkout -q FETCH_HEAD
  fi
  exit 0
fi
if [ ! -d third_party/SDL ]; then
  git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git third_party/SDL
fi
# SDL's Vulkan device with a pipeline cache file (sdl-pipeline-cache.patch, GpuOptions::pipeline_cache).
if ! git -C third_party/SDL apply --reverse --check ../../sdl-pipeline-cache.patch 2>/dev/null; then
  git -C third_party/SDL apply ../../sdl-pipeline-cache.patch
fi
