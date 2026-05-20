@echo off
REM Full super-build + portable ZIP package for Windows 10+.
REM Run from: x64 Native Tools Command Prompt for VS 2022
setlocal
set SOURCE_DIR=%~dp0
set SOURCE_DIR=%SOURCE_DIR:~0,-1%

set JOBS=8
set TLRENDER_NET=OFF
set TLRENDER_OCIO=ON
set TLRENDER_JPEG=ON
set TLRENDER_TIFF=ON
set TLRENDER_EXR=ON
set TLRENDER_AOM=OFF
set TLRENDER_SVTAV1=OFF
set TLRENDER_FFMPEG=ON
set TLRENDER_FFMPEG_MINIMAL=ON
set TLRENDER_FFMPEG_PLUGIN=ON
set TLRENDER_FFMPEG_CMD=ON
set TLRENDER_NASM=ON
set TLRENDER_OIIO=ON
set TLRENDER_USD=OFF
set FTK_API=GL_4_1
set BUILD_SHARED_LIBS=OFF

echo === DJV Windows super-build (first run may take hours) ===
call "%SOURCE_DIR%\sbuild-win.bat" "%SOURCE_DIR%"
if errorlevel 1 exit /b 1

echo === DJV portable package (ZIP) ===
call "%SOURCE_DIR%\etc\Windows\windows-portable-package.bat" "%SOURCE_DIR%" Release
if errorlevel 1 exit /b 1

echo.
echo Done. Unzip the ZIP on Windows 10 and run DJV.bat
endlocal
