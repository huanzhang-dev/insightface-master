@echo off
setlocal
cd /d "%~dp0"
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"
if /i not "%CONFIG%"=="Release" if /i not "%CONFIG%"=="Debug" (echo Usage: dashboard.bat [Release^|Debug]& exit /b 2)
if not exist "build\vs2019\%CONFIG%\face_dashboard.exe" (
  call scripts\build_vs2019.bat %CONFIG%
  if errorlevel 1 exit /b 1
)
start "Face authentication dashboard" "build\vs2019\%CONFIG%\face_dashboard.exe"
