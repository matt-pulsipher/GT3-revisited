# Builds GT3 Revisited natively with MSVC (Visual Studio 2022 Build Tools) and runs the tests.
#   powershell -ExecutionPolicy Bypass -File D:\gt3r\build.ps1 [-Clean]
param([switch]$Clean, [string]$Targets = "gt3r gt3tool gt3r_tests gt3r-relay")
$ErrorActionPreference = "Stop"
$env:Path = "C:\Program Files\Git\cmd;C:\Program Files\CMake\bin;" + $env:Path
$src = "D:\gt3r\src"; $bld = "D:\gt3r\build"
if ($Clean -and (Test-Path $src)) { Remove-Item -Recurse -Force $src }
New-Item -Force -ItemType Directory $src | Out-Null
tar -xzf D:\gt3r\gt3r-src.tar.gz -C $src # the newest source over the old (the build stays incremental)
if ($Clean -and (Test-Path $bld)) { Remove-Item -Recurse -Force $bld }
cmake -S $src -B $bld -G "Visual Studio 17 2022" -A x64 -DGT3R_BUILD_DIFFHARNESS=OFF 2>&1 | Select-Object -Last 3
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build $bld --config Release --parallel --target $($Targets -split ' ') -- /m /v:minimal /nologo 2>&1 |
    Where-Object { $_ -match "error|warning C\d+|->" }
if ($LASTEXITCODE) { throw "build failed" }
"built:"
Get-ChildItem "$bld\Release\*.exe" | ForEach-Object { "  $($_.Name) $([math]::Round($_.Length/1MB,1)) MB" }
