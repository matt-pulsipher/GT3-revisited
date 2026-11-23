@echo off
rem GT3R_CAPTURE3D: 30 consecutive frames of the race's 3D view (D:\gt3r\win-cap-<k>.png),
rem copied on the GPU without stalling it (--shot waits for the GPU every frame, which hides
rem frame-overlap bugs). The car stands still at the start (no input). %2 = car label (default
rem the Viper GTS-R), %3 = first frame. Started in the desktop session by race-shot.ps1.
set CAR=%~2
if "%CAR%"=="" set CAR=viper_gtsr_00
set FIRST=%~3
set EXTRA=%~4
if "%FIRST%"=="" set FIRST=240
set GT3R_CAPTURE3D=%FIRST%,30,D:\gt3r\win-cap.png
set GT3R_RACE=course=trialmt,car=%CAR%,auto=0,cam=0,music=0,hud=0,quit=12%EXTRA%
del /q D:\gt3r\win-cap-*.png 2>nul
"%~1" D:\gt3r\GT3.iso --screen race > D:\gt3r\race-shot.log 2>&1
echo exit %ERRORLEVEL% >> D:\gt3r\race-shot.log
