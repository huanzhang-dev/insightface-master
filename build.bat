@echo off
rem build.bat - builds the whole project with Visual Studio 2019 (x64, Release).
rem (The four solutions face_detection / feature_extraction / feature_matching / face_authentication build the same sources with the Visual Studio 2015 toolset; see docs/VISUAL_STUDIO.md.)
rem
rem   build.bat              build the library and every program in tools\ and tests\
rem   build.bat test_match   build the library and only the program tests\test_match.cpp
rem
rem Output: build\face_auth.lib and one .exe per program in build\bin\
rem Extra compiler settings can be passed with:  set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo Cannot find Visual Studio 2019 & exit /b 1 )

set "NCNN=third_party\ncnn"
set "ORT=third_party\onnxruntime"
rem /std:c++14 on purpose: the code must also compile with Visual Studio 2015 (the four .sln files, docs/VISUAL_STUDIO.md),
rem which has no C++17. Building as C++14 here keeps it that way.
set "CFLAGS=/nologo /std:c++14 /O2 /arch:AVX2 /MD /EHsc /W3 /openmp /MP /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN"
set "INCLUDES=/I src\common /I src\detect /I src\feature /I src\match /I src\store /I src\camera /I src\pipeline /I src\testdata /I third_party\sqlite /I %NCNN%\include\ncnn /I %ORT%\include"
set "LIBS=%NCNN%\lib\ncnn.lib %NCNN%\lib\glslang.lib %NCNN%\lib\SPIRV.lib %NCNN%\lib\MachineIndependent.lib %NCNN%\lib\OSDependent.lib %NCNN%\lib\GenericCodeGen.lib %NCNN%\lib\glslang-default-resource-limits.lib %ORT%\lib\onnxruntime.lib mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib windowscodecs.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib gdi32.lib user32.lib"

if not exist build\obj mkdir build\obj
if not exist build\objprog mkdir build\objprog
if not exist build\bin mkdir build\bin
rem Objects of the programs live in build\objprog. Delete leftovers of older builds from build\obj:
rem everything in build\obj is packed into the library, and a program's main() must not end up there.
del /q build\obj\prog_*.obj 2>nul

rem ---- 1. compile every engine source file into build\obj ----
set "SOURCES="
for %%d in (common detect feature match store camera pipeline testdata) do (
  for %%f in (src\%%d\*.cpp) do set "SOURCES=!SOURCES! %%f"
)
cl %CFLAGS% %FA_EXTRA% %INCLUDES% /c /Fobuild\obj\ !SOURCES! >build\compile.log
if errorlevel 1 ( type build\compile.log & echo BUILD FAILED in engine sources & exit /b 1 )

rem ---- 1b. SQLite (third_party\sqlite, public domain; the optional database format) as plain C, once ----
rem (build\obj\sqlite3.obj is kept between builds: delete it to compile SQLite again)
if not exist build\obj\sqlite3.obj (
  cl /nologo /c /O2 /MD /TC /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_THREADSAFE=1 /DSQLITE_DEFAULT_MEMSTATUS=0 /DSQLITE_OMIT_SHARED_CACHE /Fobuild\obj\sqlite3.obj third_party\sqlite\sqlite3.c >build\compile_sqlite.log
  if errorlevel 1 ( type build\compile_sqlite.log & echo BUILD FAILED in SQLite & exit /b 1 )
)

rem ---- 2. put the objects into one library ----
lib /nologo /OUT:build\face_auth.lib build\obj\*.obj >nul
if errorlevel 1 ( echo lib failed & exit /b 1 )

rem ---- 3. build the programs ----
set "PROGRAMS="
if "%~1"=="" (
  for %%f in (tools\*.cpp tests\*.cpp) do set "PROGRAMS=!PROGRAMS! %%f"
) else (
  for %%n in (%*) do (
    if exist tests\%%n.cpp ( set "PROGRAMS=!PROGRAMS! tests\%%n.cpp" ) else if exist tools\%%n.cpp ( set "PROGRAMS=!PROGRAMS! tools\%%n.cpp" ) else ( echo no such program: %%n & exit /b 1 )
  )
)
for %%f in (!PROGRAMS!) do (
  cl %CFLAGS% %FA_EXTRA% %INCLUDES% %%f /Fobuild\objprog\%%~nf.obj /Fe:build\bin\%%~nf.exe /link /nologo build\face_auth.lib %LIBS% >build\link_%%~nf.log
  if errorlevel 1 ( type build\link_%%~nf.log & echo BUILD FAILED: %%f & exit /b 1 )
  echo built build\bin\%%~nf.exe
)
rem The ONNX Runtime DLLs must sit next to the programs.
copy /y %ORT%\lib\onnxruntime.dll build\bin\ >nul
copy /y %ORT%\lib\onnxruntime_providers_shared.dll build\bin\ >nul
echo BUILD OK
exit /b 0

