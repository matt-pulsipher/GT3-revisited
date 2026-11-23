@echo off
rem Saves 12 consecutive race frames (D:\gt3r\win-flicker-<k>.png) behind the moving car, to
rem compare the car reflections frame to frame (started in the desktop session by race-shot.ps1).
set GT3R_SHOT_FRAME=420
set GT3R_SHOT_COUNT=12
set GT3R_RACE=course=trialmt,car=r34_gtr_v_99,auto=1,cam=0,music=0,hud=0
del /q D:\gt3r\win-flicker-*.png 2>nul
"%~1" D:\gt3r\GT3.iso --screen race --shot D:\gt3r\win-flicker.png > D:\gt3r\race-shot.log 2>&1
echo exit %ERRORLEVEL% >> D:\gt3r\race-shot.log
