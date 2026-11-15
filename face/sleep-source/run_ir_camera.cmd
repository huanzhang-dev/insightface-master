@echo off
setlocal
cd /d "%~dp0"
set "CAMERA=0"
set "FPS=30"
set "EYEWEAR=auto"
set /p "CAMERA=IR camera index [0]: "
set /p "FPS=Preview/camera target FPS [30]: "
set /p "EYEWEAR=Eyewear auto or opaque [auto]: "
set "EXE=%~dp0..\sleep-build\v142-x64\bin\Sleep.exe"
if not exist "%EXE%" set "EXE=%~dp0..\sleep-build\v140-x64\bin\Sleep.exe"
if not exist "%EXE%" (echo Build the project first. & pause & exit /b 1)
"%EXE%" --source "%CAMERA%" --fps "%FPS%" --eyewear "%EYEWEAR%" --infrared --cpu --sound --output "%~dp0..\sleep-data\output\camera_%RANDOM%_%RANDOM%"
pause
