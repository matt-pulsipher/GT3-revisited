# ANGLE (OpenGL ES on Direct3D 11) for the Windows package (docs/dev/RENDERING.md "OpenGL ES"):
# vcpkg's angle port at a pinned vcpkg commit, with the C runtime and zlib linked in
# (packaging/vcpkg-triplets). Puts libEGL.dll, libGLESv2.dll and LICENSE-ANGLE.txt in .\angle,
# which -DGT3R_ANGLE_DIR copies next to gt3r.exe. About 18 minutes on a CI runner, so CI keeps
# that folder in its cache under this file's and the triplets' hash (.github/workflows/build.yml):
# a change to either builds it again.
#   pwsh packaging/angle.ps1
$ErrorActionPreference = "Stop"
$vc = Join-Path ($env:RUNNER_TEMP ?? $env:TEMP) vcpkg
git clone --quiet https://github.com/microsoft/vcpkg $vc
git -C $vc checkout --quiet 9a5dfef5d1a0438a7d24856a85bd60f309d355e9
if ($LASTEXITCODE) { exit 1 }
& "$vc\bootstrap-vcpkg.bat" -disableMetrics
& "$vc\vcpkg.exe" install angle:x64-windows-angle --overlay-triplets=packaging/vcpkg-triplets --disable-metrics
if ($LASTEXITCODE) { exit 1 }
$inst = "$vc\installed\x64-windows-angle"
New-Item -Force -ItemType Directory angle | Out-Null
Copy-Item -Force "$inst\bin\libEGL.dll", "$inst\bin\libGLESv2.dll" angle
Copy-Item -Force "$inst\share\angle\copyright" angle\LICENSE-ANGLE.txt
