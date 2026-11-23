@echo off
rem Screenshots the controller screen (lists connected input devices).
set GT3R_SHOT_FRAME=180
del /q D:\gt3r\win-input-%2.png 2>nul
"%~1" D:\gt3r\GT3.iso --screen input --shot D:\gt3r\win-input-%2.png > D:\gt3r\input.log 2>&1
