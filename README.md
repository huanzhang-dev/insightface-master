# face_auth - facial authentication with fast C++ engines

Camera image in, "who is this?" out. Written in plain C++14 (no OpenCV), built with
Visual Studio 2019 (four engine solutions plus `_test.sln`, or `build.bat`);
deep-learning models run with ncnn or ONNX Runtime.

**Four projects, four Visual Studio solutions** (`docs/VISUAL_STUDIO.md`):

| Solution | Project |
|---|---|
| `face_detection.sln` | high-speed face detection (detectors, tracker, head pose) |
| `feature_extraction.sln` | high-speed feature extraction (SFace, ArcFace, LBP) |
| `feature_matching.sln` | high-speed feature matching (M:N matcher, card-first matching, managed database in CSV or SQLite); needs no network library |
| `face_authentication.sln` | the face authentication project: pipeline, worker thread, camera, video check |

Each engine has a self-contained library project. `face_authentication.sln` contains only the `face_dashboard` Windows application for registration, authentication and database management; its build target builds and links the authentication library without adding a second solution project. Tests, evaluations and benchmarks live in the separate single-project `_test.sln`; run `_test.exe --list` to list its commands. Use `_test.sln` for F5 debugging of the engine sources.

* **Face detection and tracking** - several faces per frame, a track ID per face, head
  rotation (pitch / yaw / roll) checked against tolerances, face crops stored once per
  second until the person is verified.
* **Feature extraction** - 128 / 256 / 512 numbers per face.
* **Feature matching** - M:N: all M faces detected in a frame against all N registered people at
  once, in one call. M and N are independent (fewer faces than registered people, as many, or more).
  **Card first:** when RF cards have been read, the card information is identified first: the cards are
  looked up in the database, the features stored with them are retrieved, and the faces of the current
  frame are compared only with those features (N can be smaller than, equal to or larger than the number
  M of faces). A card that is not in the database is a failed comparison, never a search among everybody.
* **Worker thread** (optional) - the pose check and feature extraction run on a second thread,
  so every frame takes about as long as the detection alone.
* **Every engine runs on the CPU** (the target PC has no GPU). ncnn is told not to use Vulkan, ONNX Runtime has no
  execution provider but its CPU one, the matcher is AVX2 code and the classical engines are plain C++. This is
  enforced and tested: every network, detector, extractor and the whole pipeline answer `runs_on_cpu()`, and the
  tests check that no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process.
  Media Foundation is told not to use hardware (no DXVA, no hardware transforms), so video is decoded and encoded
  in software. Measured from outside with the Windows GPU counters (`scripts\gpu_probe.ps1`): the complete
  `test_pipeline` and `eval_footage` had no GPU engine instance at all. One nuance: a program that **writes** an MP4
  (`gen_video`, the annotated video of `check_video`) has an idle Direct3D device while it encodes (the `3d` engine at
  0.0% utilization, no video engine; the encoder itself is the Microsoft software encoder). The measurement is coarse
  (one reading about every 3 s) (`docs/TESTING.md`, `docs/RESULTS.md`, "All engines on the CPU").
* **Any camera picture size** - the default is 1920x1080 (Full HD); a camera that does not offer it gets the closest of its own modes, and detection,
  tracking and recognition were tested from 640x480 to 2560x1440 (`docs/RESULTS.md`, section 5b; `camera_check --modes` shows what a camera offers).
  The engines were **evaluated on generated 1920x1080 video** with the exact truth (three videos of 1, 3 and 5 people; `docs/RESULTS.md`, section 5c),
  and again on the **same videos with a simulated camera look** (a cluttered room, uneven light, a soft lens, sensor noise,
  harder compression, and a motion blur that the slow walks do not trigger: `gen_video --camera-look`; section 5d). **No real camera footage is in the project:** the footage recorded by the
  inspection camera could not be found on the development PC, and the built-in camera of the PC offers at most 1280x720, so the
  1920x1080 "camera" footage is a **simulation** made from the same flat portrait photos. Real footage of the inspection camera is
  still needed.
* Each engine exists as a **deep-learning** and a **classical** version.
* Registered people live in a database file that the program manages, in the format the **user chooses**:
  **CSV** (the default: a text file with a header line `id,name,card_id,dim,values`, then one line per
  person: the face feature and, if the person has one, the RF card number) or **SQLite** (one table
  `people`; file names `.db`, `.sqlite`, `.sqlite3`, or asked for explicitly). Register, delete and modify
  are written to the file at once and undone if the write fails; a second program cannot open the same
  file (a lock); a damaged file is refused. Why CSV is the default: the whole database has to be in memory
  anyway for the matcher, a CSV file needs no library and can be read and edited in a text editor or a
  spreadsheet, and it was the original requirement. The price: every CSV change rewrites the whole file,
  about 1.3 s at 10,000 people with 512 numbers. SQLite changes one row, about 3 ms at the same size, and is
  there for the databases where that matters. `build\bin\db_convert.exe` converts a database from one
  format to the other. See `docs/store.md`.

Start with the documents in `docs/`:

| Document | Content |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | how the pieces fit together, folders, how one frame is processed |
| [docs/TESTING.md](docs/TESTING.md) | build, run, tests, evaluation tools, measuring on the target PC |
| [docs/VISUAL_STUDIO.md](docs/VISUAL_STUDIO.md) | the four Visual Studio 2015 solutions (ten libraries, including the SQLite library): contents, build, what was checked, rules for the code |
| [docs/VIDEO_VERIFICATION_2026-11-13.md](docs/VIDEO_VERIFICATION_2026-11-13.md) | fresh video verification: registration, detection, tracking, authentication, evidence and known fixture limitation |
| [docs/RESULTS.md](docs/RESULTS.md) | **measured accuracy and speed, requirement by requirement, and the honest limits** |
| [docs/MODELS.md](docs/MODELS.md) | every model / library / test image: source and licence |
| function references | [common](docs/common.md), [detect](docs/detect.md), [feature](docs/feature.md), [match](docs/match.md), [store](docs/store.md), [camera](docs/camera.md), [pipeline](docs/pipeline.md), [testdata](docs/testdata.md), [tools](docs/tools.md) |

## Authentication dashboard

Open `face_authentication.sln`, set **face_dashboard** as the startup project, and
press F5. Or run `dashboard.bat` (builds with VS2019 if the executable is missing).
The dashboard combines photo/camera registration, recognition, optional card checks,
and a CSV/SQLite people table with editing, deletion and backup export.
See [dashboard usage](docs/DASHBOARD.md).

## Inspect engines with Visual Studio 2019

Install VS2019 Desktop development with C++ (v142 and Windows 10 SDK), then run:

```bat
scripts\build_vs2019.bat Release
inspect_engines.bat
```

The Windows inspection window runs checks for detection, extraction, matching and
complete authentication, with a model selector and a separate results tab for each
engine. See [VS2019 setup and inspection](docs/VISUAL_STUDIO.md).

## Quick start

```
python scripts\fetch_portraits.py --max-people 150     (test images; needs Python + requests)
python scripts\merge_portraits.py
run_tests.bat                                          (builds, then runs all tests)
run_tests.bat full                                     (also the evaluations on the 1920x1080 footage, accuracy and speed)
scripts\run_tests_vs2015.bat                           (the same with Visual Studio 2015 only: builds the five
                                                        solutions in the IDE and runs the original programs through `_test.exe`)

scripts\make_fhd_footage.bat                            (make the 1920x1080 test footage: three MP4 videos of 1, 3 and 5
                                                        people walking toward the camera, with the exact truth, and the
                                                        same three again with the simulated camera look: fhd_cam_*)
build\bin\eval_footage.exe models\permissive.ini data\videos\fhd_3people
                                                       (the engines one by one on that footage: detection and tracking,
                                                        feature extraction, M:N matching)
build\bin\gen_video.exe data\videos\demo 8 15 3 0 2    (make an MP4 of 3 people walking toward the camera; 1920x1080 by default;
                                                        add --camera-look for the simulated camera look)
build\bin\check_video.exe models\permissive.ini data\videos\demo\video.mp4 --async
                                                       (play it through the whole system, check it against the
                                                        truth, write an annotated MP4 you can watch)
build\bin\db_convert.exe people.csv people.db           (convert the database of registered people CSV <-> SQLite)
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\test_pipeline.exe -ProgramArguments 'models\permissive.ini'"
                                                       (run a program and read the Windows GPU counters while it runs:
                                                        did it use the GPU? the arguments go in one quoted list; see docs/TESTING.md)
```

Programs end up in `build\bin\`; run them from this folder.

## Choosing models

`models\*.ini` are profiles. `permissive.ini` (YuNet + SFace, MIT / Apache-2.0) is the
default. `dev_noncommercial.ini` uses InsightFace models that give 512-number features but
are **licensed for non-commercial research only**; its head-pose check is still done by YuNet
(`pose.detector_profile = models/permissive.ini`), so it loads the YuNet model too. Its SCRFD detector (on ncnn)
runs on the smallest rectangle whose sides are multiples of 32 (640x384 for a 1920x1080 picture) instead of a 640x640
square: 40% fewer pixels for a 16:9 camera picture, 36 to 38% less detection time (17.9 to 11.5 ms at 1920x1080 on the
development PC) and the same detections (`docs/RESULTS.md`, section 5e);
`dev_noncommercial_square.ini` keeps the old square for comparison and as a fallback, and `dev_noncommercial_onnx.ini`
is the same SCRFD + ArcFace on ONNX Runtime. `classical.ini` uses no neural network. See [docs/MODELS.md](docs/MODELS.md)
before shipping anything.

## Important to know

* All timings in the documents were measured on a 13th-generation Core i7, **not** on a
  4th-generation Core i5. Run `build\bin\bench.exe` on the target PC for the real numbers. The "est. i5" figures are
  the measurement times 3.5, a guess: for SCRFD on ncnn the estimate is now inside the 60 ms limit at 1920x1080 (40.2 ms) and
  for one face at 1280x1080 (54.4 ms; with three faces it is 59.4 to 60.4 ms, on the limit), but the 25 ms limit at 640x640
  is still over (70.5 ms): a square picture gets no gain from the fitted rectangle (`docs/RESULTS.md`, section 5e).
* The face images used for accuracy tests are public-domain official portraits. They are an
  easy test; accuracy on a real camera will be lower. Re-calibrate the match threshold on
  your own camera images.
* The test video is **generated**, not recorded. Even with `--camera-look` it is a simulation of camera footage:
  flat portrait photos, frontal faces, no occlusion, glasses, backlight or camera shake, and the registered photo is the
  very picture that walks through the video. Footage from the inspection camera has not been tested.
* ONNX Runtime is pinned to 1.17.3 on purpose (newer versions crash with the Visual C++
  runtime that Windows ships; see [docs/MODELS.md](docs/MODELS.md)).
