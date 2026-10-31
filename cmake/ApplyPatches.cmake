# Applies patches to a fetched source tree (FetchContent's PATCH_COMMAND, run in that tree):
#   cmake -DGIT=<git> -DPATCHES=<patch;...> -P ApplyPatches.cmake
# A patch already in the tree is skipped: the patch step runs again when the fetch details change.
# --ignore-whitespace: Git for Windows checks the tree out with CRLF line ends.
foreach(patch IN LISTS PATCHES)
  execute_process(COMMAND "${GIT}" apply --reverse --check --ignore-whitespace "${patch}"
                  RESULT_VARIABLE not_applied OUTPUT_QUIET ERROR_QUIET)
  if(NOT not_applied)
    continue()
  endif()
  execute_process(COMMAND "${GIT}" apply --ignore-whitespace "${patch}" RESULT_VARIABLE failed)
  if(failed)
    message(FATAL_ERROR "Could not apply ${patch}")
  endif()
  message(STATUS "Applied ${patch}")
endforeach()
