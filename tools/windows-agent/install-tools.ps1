# Build tools for GT3 Revisited on a Windows build agent (run as administrator).
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"
$d = "D:\gt3r\installers"
New-Item -Force -ItemType Directory $d | Out-Null

if (-not (Test-Path "C:\Program Files\Git\cmd\git.exe")) {
    Invoke-WebRequest https://github.com/git-for-windows/git/releases/download/v2.49.0.windows.1/Git-2.49.0-64-bit.exe -OutFile "$d\git.exe"
    Start-Process "$d\git.exe" -ArgumentList "/VERYSILENT","/NORESTART","/SUPPRESSMSGBOXES" -Wait
}
"git:   $(Test-Path 'C:\Program Files\Git\cmd\git.exe')"

if (-not (Test-Path "C:\Program Files\CMake\bin\cmake.exe")) {
    Invoke-WebRequest https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.msi -OutFile "$d\cmake.msi"
    Start-Process msiexec.exe -ArgumentList "/i","$d\cmake.msi","/quiet","/norestart","ADD_CMAKE_TO_PATH=System" -Wait
}
"cmake: $(Test-Path 'C:\Program Files\CMake\bin\cmake.exe')"

$cl = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $cl) {
    Invoke-WebRequest https://aka.ms/vs/17/release/vs_BuildTools.exe -OutFile "$d\vs_BuildTools.exe"
    $p = Start-Process "$d\vs_BuildTools.exe" -ArgumentList "--quiet","--wait","--norestart","--nocache","--add","Microsoft.VisualStudio.Workload.VCTools","--includeRecommended" -Wait -PassThru
    "vs installer exit: $($p.ExitCode)"
    $cl = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
}
"cl:    $($cl.FullName)"
