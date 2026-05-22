@echo off
REM Portable launcher for DJV (no installation required).
set "DJV_ROOT=%~dp0"
cd /d "%DJV_ROOT%"
set "PATH=%DJV_ROOT%bin;%PATH%"
start "" "%DJV_ROOT%bin\djv.exe" -settingsFile "%DJV_ROOT%djv.portable.json" %*
