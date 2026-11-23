# Builds ANGLE (OpenGL ES on Direct3D 11, docs/dev/RENDERING.md "OpenGL ES") from source with
# Microsoft's vcpkg (its angle port: ANGLE chromium/7258, from WebKit's copy with WebKit's CMake
# build) and puts the files the game ships in D:\gt3r\angle: libEGL.dll, libGLESv2.dll and
# ANGLE's license. Configure the game with -DGT3R_ANGLE_DIR=D:\gt3r\angle to copy them next to
# gt3r.exe. CI's Windows package job does the same (packaging/angle.ps1, cached in CI).
#   powershell -ExecutionPolicy Bypass -File D:\gt3r\build-angle.ps1 [-Vcpkg <tag or commit>]
# About 3 minutes on an i7-12700K after the vcpkg checkout; later runs reuse vcpkg's binary cache.
# The triplet (packaging/vcpkg-triplets/x64-windows-angle.cmake, from the source tree at -Src)
# links the C runtime and zlib into the DLLs.
param([string]$Vcpkg = "9a5dfef5d1a0438a7d24856a85bd60f309d355e9", [string]$Root = "D:\gt3r",
      [string]$Src = "D:\gt3r\src")
$ErrorActionPreference = "Stop"
$env:Path = "C:\Program Files\Git\cmd;C:\Program Files\CMake\bin;" + $env:Path
$vc = "$Root\vcpkg"; $out = "$Root\angle"
if (-not (Test-Path "$vc\.git")) { git clone --quiet https://github.com/microsoft/vcpkg $vc }
Push-Location $vc
git fetch --quiet --tags origin
if ($Vcpkg) { git checkout --quiet $Vcpkg } else { git checkout --quiet origin/master }
"vcpkg at $(git rev-parse HEAD) ($(git describe --tags --always))"
if (-not (Test-Path "$vc\vcpkg.exe")) { & "$vc\bootstrap-vcpkg.bat" -disableMetrics | Out-Null }
& "$vc\vcpkg.exe" install angle:x64-windows-angle --overlay-triplets="$Src\packaging\vcpkg-triplets" --clean-after-build --disable-metrics
if ($LASTEXITCODE) { Pop-Location; throw "vcpkg install angle failed" }
Pop-Location
$inst = "$vc\installed\x64-windows-angle"
New-Item -Force -ItemType Directory $out | Out-Null
Copy-Item -Force "$inst\bin\libEGL.dll", "$inst\bin\libGLESv2.dll" $out
Copy-Item -Force "$inst\share\angle\copyright" "$out\LICENSE-ANGLE.txt"
"ANGLE $(Get-Content "$inst\share\angle\vcpkg.spdx.json" -ErrorAction SilentlyContinue | Select-String '"versionInfo"' | Select-Object -First 1)"
Get-ChildItem $out | ForEach-Object { "  $($_.Name) $([math]::Round($_.Length/1MB,1)) MB" }
