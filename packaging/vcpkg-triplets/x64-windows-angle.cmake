# vcpkg triplet for the ANGLE that Windows packages ship (docs/dev/RENDERING.md "OpenGL ES"):
# libEGL.dll and libGLESv2.dll with the C runtime and zlib linked in, so they need nothing the
# game does not (no Visual C++ redistributable, no z.dll).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
if(PORT STREQUAL "angle")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
