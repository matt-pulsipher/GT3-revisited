@echo off
rem Runs a short race and saves a screenshot (started in the desktop session by race-shot.ps1).
set GT3R_SHOT_FRAME=300
set GT3R_RACE=course=trialmt,car=r34_gtr_v_99,auto=1,warp=8,cam=0,music=0
del /q D:\gt3r\win-race.png 2>nul
"%~1" D:\gt3r\GT3.iso --screen race --shot D:\gt3r\win-race.png > D:\gt3r\race-shot.log 2>&1
echo exit %ERRORLEVEL% >> D:\gt3r\race-shot.log
