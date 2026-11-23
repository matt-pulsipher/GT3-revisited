# Runs the unit tests of the MSVC build (with the disc when D:\gt3r\GT3.iso exists).
param([string]$Filter = "")
$env:GT3R_DISC = "D:\gt3r\GT3.iso"
$out = & D:\gt3r\build\Release\gt3r_tests.exe $Filter 2>&1
$out | Where-Object { $_ -match "FAIL|failure" }
