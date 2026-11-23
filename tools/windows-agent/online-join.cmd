@echo off
rem Joins an online battle hosted at %2 and screenshots the race (started in the desktop session).
set GT3R_SHOT_FRAME=%3
set GT3R_RACE=auto=1,music=0,cam=1
set GT3R_ONLINE=join=%2,car=supra_rz_97,name=WindowsPC,accept=1
del /q D:\gt3r\win-online.png 2>nul
"%~1" D:\gt3r\GT3.iso --screen online --shot D:\gt3r\win-online.png > D:\gt3r\online.log 2>&1
echo exit %ERRORLEVEL% >> D:\gt3r\online.log
