@echo off
setlocal
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "TOOLSET=%~1"
if "%TOOLSET%"=="" set "TOOLSET=v142"
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set "CMAKEBASE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "CMAKE=%CMAKEBASE%\CMake\bin\cmake.exe"
set "CTEST=%CMAKEBASE%\CMake\bin\ctest.exe"
set "NINJA=%CMAKEBASE%\Ninja\ninja.exe"
if not exist "%CMAKE%" (echo Install CMake tools for VS2019 or edit CMAKEBASE. & exit /b 1)
if "%TOOLSET%"=="v140" (
  call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86_amd64 >nul
) else if "%TOOLSET%"=="v142" (
  call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64 >nul
) else (echo Use v140 or v142. & exit /b 1)
if errorlevel 1 exit /b 1
set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86;%PATH%"
set "DATA=%ROOT%\..\sleep-data"
if not "%SLEEP_DATA_ROOT%"=="" set "DATA=%SLEEP_DATA_ROOT%"
set "BUILD=%ROOT%-build\%TOOLSET%-x64"
if not "%~2"=="" set "BUILD=%~f2"
"%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" "-DCMAKE_TOOLCHAIN_FILE=%ROOT%\cmake\windows-x64.cmake" -DCMAKE_BUILD_TYPE=Release "-DSLEEP_DATA_ROOT=%DATA%" "-DSLEEP_ORT_ROOT=%DATA%\third_party\onnxruntime"
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%BUILD%" --parallel 2
if errorlevel 1 exit /b 1
"%CTEST%" --test-dir "%BUILD%" --output-on-failure
exit /b %errorlevel%
