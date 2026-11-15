@echo off
call "%~dp0tools\build_cmake.cmd" v140
exit /b %errorlevel%
