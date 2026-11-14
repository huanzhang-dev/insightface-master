@echo off
rem run_tests.bat - build everything and run every test and the evaluation tools.
rem
rem   run_tests.bat          all unit/integration tests (about 4 minutes)
rem   run_tests.bat full     the tests, then the accuracy / timing evaluations (about 15 minutes more)
rem
rem Every test prints "ALL TESTS PASSED" or "TESTS FAILED"; the script stops at the first failing test
rem (the evaluations of "full" only print numbers and cannot fail).
rem The footage of the video checks and of the evaluation is 1920x1080 (Full HD), made by scripts\make_fhd_footage.bat.
setlocal EnableExtensions
cd /d "%~dp0"

call "%~dp0build.bat"
if errorlevel 1 exit /b 1

echo.
echo ==== test_common (image operations, file I/O, both network backends) ====
build\bin\test_common.exe || goto :failed
echo.
echo ==== test_engine_boundaries (configuration, model outputs, authentication reinitialization) ====
build\bin\test_engine_boundaries.exe || goto :failed
echo.
echo ==== test_detect_units (pose, gate, NMS, tracker, Haar loading) ====
build\bin\test_detect_units.exe || goto :failed
echo.
echo ==== test_match (M:N matching against 10,000 features, card first, CSV and SQLite database) ====
build\bin\test_match.exe || goto :failed
echo.
echo ==== test_feature (feature extraction engine alone: permissive, dev and classical profiles) ====
build\bin\test_feature.exe models\permissive.ini || goto :failed
build\bin\test_feature.exe models\dev_noncommercial.ini || goto :failed
build\bin\test_feature.exe models\classical.ini || goto :failed
echo.
echo ==== test_pipeline, permissive profile (YuNet + SFace) ====
build\bin\test_pipeline.exe models\permissive.ini || goto :failed
echo.
echo ==== test_pipeline, dev profile (SCRFD + ArcFace, ncnn) ====
build\bin\test_pipeline.exe models\dev_noncommercial.ini || goto :failed
echo.
echo ==== test_pipeline with the registered people in a SQLite database (instead of the default CSV file) ====
build\bin\test_pipeline.exe models\permissive.ini --sqlite || goto :failed
echo.
echo ==== test_pipeline WORKER THREAD, permissive profile (videos played in real time, about 1 minute) ====
build\bin\test_pipeline.exe models\permissive.ini --async || goto :failed
echo.
echo ==== test_pipeline WORKER THREAD, dev profile ====
build\bin\test_pipeline.exe models\dev_noncommercial.ini --async || goto :failed
echo.
echo ==== 1920x1080 footage: three generated MP4 videos with the exact truth ====
call scripts\make_fhd_footage.bat || goto :failed
echo.
echo ==== the footage checked end to end by the complete system (synchronous, then worker thread in real time) ====
build\bin\check_video.exe models\permissive.ini data\videos\fhd_3people\video.mp4 --out data\videos\fhd_3people\annotated_sync.mp4 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\fhd_3people\video.mp4 --async --out data\videos\fhd_3people\annotated_worker.mp4 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\fhd_5people\video.mp4 --out data\videos\fhd_5people\annotated_sync.mp4 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\fhd_1person\video.mp4 --out data\videos\fhd_1person\annotated_sync.mp4 || goto :failed
echo ==== the same with the look of a real camera in a real room (fhd_cam_*: simulated room, light, lens, noise, harder compression) ====
build\bin\check_video.exe models\permissive.ini data\videos\fhd_cam_3people\video.mp4 --out data\videos\fhd_cam_3people\annotated_sync.mp4 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\fhd_cam_3people\video.mp4 --async --out data\videos\fhd_cam_3people\annotated_worker.mp4 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\fhd_cam_5people\video.mp4 --out data\videos\fhd_cam_5people\annotated_sync.mp4 || goto :failed
echo ==== the same system at 1280x720, a size cameras also deliver ====
build\bin\gen_video.exe data\videos\demo_720p 8 15 3 0 2 --size 1280x720 || goto :failed
build\bin\check_video.exe models\permissive.ini data\videos\demo_720p\video.mp4 --out data\videos\demo_720p\annotated_sync.mp4 || goto :failed
echo.
echo ==== db_convert: the database of registered people converted CSV -^> SQLite -^> CSV ====
build\bin\gen_features.exe build\convert_demo.csv 500 128 7 || goto :failed
build\bin\db_convert.exe build\convert_demo.csv build\convert_demo.db --force || goto :failed
build\bin\db_convert.exe build\convert_demo.db build\convert_demo_back.csv --force || goto :failed

if /i "%~1"=="full" (
  echo.
  echo ==== engine evaluation on the 1920x1080 footage (detection + tracking, feature extraction, matching) ====
  for %%p in (permissive dev_noncommercial dev_noncommercial_square dev_noncommercial_onnx classical) do (
    for %%f in (fhd_1person fhd_3people fhd_5people fhd_cam_1person fhd_cam_3people fhd_cam_5people) do (
      echo ---- %%p on %%f ----
      build\bin\eval_footage.exe models\%%p.ini data\videos\%%f
    )
  )
  echo.
  echo ==== detection evaluation (generated frames of several face sizes at 1920x1080) ====
  build\bin\eval_detect.exe models\permissive.ini models\dev_noncommercial.ini models\dev_noncommercial_square.ini models\dev_noncommercial_onnx.ini models\classical.ini --frame 1920x1080
  echo.
  echo ==== feature evaluation ====
  build\bin\eval_features.exe models\permissive.ini data\portraits
  build\bin\eval_features.exe models\dev_noncommercial.ini data\portraits
  echo.
  echo ==== speed ====
  build\bin\bench.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini models\dev_noncommercial_square.ini models\dev_noncommercial_onnx.ini models\classical.ini
)
echo.
echo ALL DONE
exit /b 0

:failed
echo.
echo *** A TEST FAILED ***
exit /b 1
