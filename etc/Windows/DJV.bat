@echo off
REM Portable launcher for DJV (no installation required).
cd /d "%~dp0"
set "PATH=%CD%\bin;%PATH%"
start "" "%CD%\bin\djv.exe" %*
