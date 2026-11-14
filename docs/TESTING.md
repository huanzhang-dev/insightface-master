# Building, running and testing

Every solution contains one project: three engine-library solutions, the authentication dashboard solution (which builds its engine library through a target), and `_test.sln`. For IDE testing/debugging, open `_test.sln`; its default command is `test_detect_units`. Select another original test/tool with `_test.exe <program> [arguments]`. See the current layout at the top of `docs/VISUAL_STUDIO.md`. The standalone VS2019 `build.bat` / `run_tests.bat` workflow below is unchanged.

## Requirements

* Windows 10/11 x64, **Visual Studio 2019** (Community is enough) with the C++ desktop workload.
  `build.bat` finds it at `C:\Program Files (x86)\Microsoft Visual Studio\2019\Community`.
  (To work in **Visual Studio 2015** instead, open one of the four solutions listed under "The four
  solutions" below; see `docs/VISUAL_STUDIO.md`. The sources are C++14 so that both work;
  `scripts\check_vs2015_compat.bat` checks that nothing newer crept in. **`scripts\run_tests_vs2015.bat`**
  builds all four solutions with the Visual Studio 2015 IDE and runs every program of every project from that
  build, using nothing of Visual Studio 2019: it is the Visual Studio 2015 counterpart of `run_tests.bat full`,
  see "Running and testing with Visual Studio 2015 only" in `docs/VISUAL_STUDIO.md`.)
* CPU with AVX2 (all Intel Core i5 of the 4th generation and newer have it).
* For the test data scripts only: any Python 3 with `requests` (`pip install requests`). The
  C++ programs and tests need no Python.
* Keep the project folder and every model or data path **plain ASCII** (no accents, no
  non-Latin characters): the picture, model and database code read paths with different
  encodings.

## Build

```
build.bat                 builds the library and every program in tools\ and tests\
build.bat test_match      builds the library and only tests\test_match.cpp
```

Programs appear in `build\bin\`. **Run them from the project root** (the folder
that contains `models\` and `data\`), because model and data paths are relative:

```
build\bin\test_match.exe
```

Compiler settings: `/std:c++14 /O2 /arch:AVX2 /MD /openmp`. Extra settings (for example another
rotation tolerance): `set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f` before `build.bat`.

The message `'vswhere.exe' is not recognized` printed by `build.bat` is harmless.

## Test data

```
python scripts\fetch_portraits.py --max-people 150      downloads public-domain portraits (a few minutes; Wikimedia
                                                        slows downloads down, a large set can take hours)
python scripts\merge_portraits.py                       merges people that were split over two folders
python scripts\fetch_portraits.py --max-people 500 --out data\portraits_new
python scripts\merge_portraits.py --import data\portraits_new    add more people from a staging folder
build\bin\gen_features.exe data\synthetic\f.csv 10000 512   10,000 random registered features (a CSV file, the default
                                                            database format; f.db makes a SQLite database instead)
build\bin\db_convert.exe f.csv f.db                         convert a database of registered people from CSV to SQLite
build\bin\db_convert.exe f.db f.csv --force                 and back (each conversion reads its result back and compares)
build\bin\gen_video.exe data\videos\demo 8 15 3 0 2         an MP4 video (and frames, truth, scenario) of 3 people
                                                            walking toward the camera, 2 of them registered (1920x1080)
build\bin\gen_video.exe data\videos\demo_720p 8 15 3 0 2 --size 1280x720
                                                            the same at another picture size (default 1920x1080;
                                                            both numbers even, at least 64; any camera size works)
build\bin\gen_video.exe data\videos\demo_cam 8 15 3 0 2 --camera-look
                                                            the same with the simulated camera look (a room, uneven
                                                            light, a soft lens, sensor noise, harder compression:
                                                            4000 kbit/s unless --bitrate K); a SIMULATION, no camera
scripts\make_fhd_footage.bat                                the six 1920x1080 videos of the evaluation: 1, 3 and 5
                                                            people, in data\videos\fhd_1person, fhd_3people, fhd_5people,
                                                            and the same three with the simulated camera look in
                                                            fhd_cam_1person, fhd_cam_3people, fhd_cam_5people
build\bin\check_video.exe models\permissive.ini data\videos\demo\video.mp4 [--async]
                                                            plays the video through the whole system, checks it
                                                            against the truth, writes an annotated MP4
build\bin\eval_footage.exe models\permissive.ini data\videos\fhd_3people
                                                            the engines one by one on the footage: detection and
                                                            tracking, feature extraction, M:N matching
```

All of it is generated or downloaded; nothing is recorded with a camera.

**About the "camera" footage.** The footage that the inspection camera recorded could not be found on the development PC (the Videos, Downloads, Desktop and Documents folders, `Z:\AST` and `D:\` were searched), and no camera was switched on to record new footage: the built-in camera of the PC offers 1280x720 at most. So the 1920x1080 footage with the look of a camera (`fhd_cam_1person`, `fhd_cam_3people`, `fhd_cam_5people`, made with `gen_video --camera-look`) is a **simulation**, **not real camera footage**: the same flat portrait photos as the clean footage, in front of a drawn room, with a formula for the uneven light, the soft lens and the sensor noise, and a harder compression. It is a harder test than the clean footage, but it is still flat photos of frontal faces without occlusion, glasses, backlight or camera shake, and the registered photo is the very picture that walks through the video. **Real footage from the inspection camera is still needed** to say how the engines behave in front of the real camera. How the footage is made: `docs/testdata.md`; the results: `docs/RESULTS.md`, section 5d.

### Camera picture sizes: what a camera offers, and capturing at a size

The system works with **any camera picture size**; the default is 1920 x 1080 (`FA_CAMERA_DEFAULT_WIDTH` and
`FA_CAMERA_DEFAULT_HEIGHT` in `src\common\config.h`). The camera does not have to offer that size: the camera mode that
suits best is switched on (`pick_camera_mode`: avoid H264/HEVC, avoid modes slower than 15 frames per second, take the
closest size; see `docs/camera.md`). The automatic tests never open a camera. Two commands show what a real camera
does, from the project root:

```
build\bin\camera_check.exe                           lists the camera names; opens no camera
build\bin\camera_check.exe --modes                   lists the picture formats camera 0 offers (size, frame rate,
                                                     pixel format) and marks the one that would be switched on for 1920x1080;
                                                     the camera is opened, but its picture stream is not started
build\bin\camera_check.exe --modes 1 1280x720        the same for camera 1 and a request of 1280x720
build\bin\camera_check.exe --capture 5 0 1280x720    opens camera 0 asking for 1280x720, says which camera mode was
                                                     switched on, reads 5 frames, prints their real size and speed
                                                     and saves the last one as build\camera_check.png
```

The camera number and the size may be given in either order, and both are optional. Example, measured on the development
PC: its "Integrated Camera" offers 16 formats, the largest 1280x720 (NV12 at 30 fps, YUY2 at 10 fps); for the default
request 1920x1080 the 1280x720 NV12 mode at 30 fps is chosen and the frames are 1280x720. Open `build\camera_check.png`
to check by eye that the picture is the right way up and the colours are right.

How the rest of the system behaves at other sizes is measured without a camera: `test_pipeline` video 5 plays the same
walk at five sizes, `gen_video --size` makes video files of any size, `eval_detect --frame WxH` measures the detectors
at a size (the default is 1920x1080; for example `eval_detect.exe models\permissive.ini models\classical.ini --frame 3840x2160`), and `bench_detect`
times the detection at 1920x1080 and 1280x720. A face must be at least 80 camera pixels wide to be stored for
recognition, so a smaller picture needs people closer to the camera; at 3840 x 2160 only YuNet still finds 80 pixel faces
reliably, so a 4K camera should still be asked for 1920 x 1080 (the default). Details and measured numbers:
`docs/camera.md` and `docs/detect.md` ("Detection at different camera picture sizes").

### Checking a video (`check_video`)

`check_video` registers the people marked "registered" in `scenario.txt` plus 20 other
people, plays the video **frame by frame through `AuthPipeline`** exactly as camera frames
would arrive (after the profile line it prints `all engines run on the CPU: yes (no GPU is used)`,
the answer of `AuthPipeline::runs_on_cpu()`), and prints per person: was the face detected in at least 90% of the frames,
did the person keep one track ID, was a registered person recognized as the right person
within 4 s, was a stranger never verified; and overall: at most 2% false detections, and
(with `--async`) no frame of 60 ms or more. It writes `annotated_sync.mp4` or
`annotated_worker.mp4` next to the video: a **green** box with name and score = verified,
**yellow** = not (yet) verified, thin gray box = true position, the head rotation under each
face and the frame time at the top. Open it in any video player to watch what the system
did. `--async` uses the worker thread and plays in real time. Exit code 0 = all checks passed.

## The four solutions

The programs are grouped into four Visual Studio solutions: one for each engine, so that an engine can be
built, tested and measured on its own, and one for the integrated project. Every engine has one test, the
tools that evaluate or calibrate it and one benchmark.

| Solution | Engine | Test | Tools (evaluate, calibrate, generate) | Benchmark |
|---|---|---|---|---|
| `face_detection.sln` | face detection, tracking, head pose | `test_detect_units` | `eval_detect`, `detect_image`, `calibrate_pose`, `roll_probe` | `bench_detect` |
| `feature_extraction.sln` | feature extraction (SFace, ArcFace, LBP) | `test_feature` | `eval_features`, `calibrate_lbp` | `bench_extract` |
| `feature_matching.sln` | feature matching and the database store (CSV or SQLite) | `test_match` | `gen_features`, `db_convert` | `bench_match` |
| `face_authentication.sln` | the integrated project: pipeline, worker thread, camera, video files | `test_pipeline`, `test_common` | `check_video`, `eval_footage`, `gen_video`, `camera_check` | `bench` |

`bench` goes through the complete pipeline and measures the three engines in one run; the three single-engine
benchmarks use the same measuring code (the headers `tools\bench_*.h`) and print the same rows for their
engine. `build.bat` is not affected by the grouping: it builds every program of `tools\` and `tests\` into
`build\bin\`. How to open and build the solutions is in `docs/VISUAL_STUDIO.md`; what each program does is in
`docs/tools.md`.

## Automatic tests

`run_tests.bat` builds everything and runs the tests. `run_tests.bat full` also
runs the evaluations. In this order it runs: `test_common`, `test_detect_units`, `test_match`,
`test_feature` (with `models\permissive.ini`, `models\dev_noncommercial.ini` and `models\classical.ini`),
`test_pipeline` (with `permissive.ini` and `dev_noncommercial.ini`, synchronous; then `permissive.ini` with `--sqlite`, the
registered people in a SQLite database; then each profile with `--async`), then the **1920x1080 footage**:
`scripts\make_fhd_footage.bat` makes six videos (1, 3 and 5 people, with the exact truth, clean, and the same three with
the **simulated** camera look: `fhd_cam_*`) and `check_video` plays them through the whole system (`fhd_3people`
synchronous and with `--async`, `fhd_5people` and `fhd_1person` synchronous; then the same for the simulated look:
`fhd_cam_3people` synchronous and with `--async`, `fhd_cam_5people` synchronous),
then the same at 1280x720 (`gen_video ... --size 1280x720` into `data\videos\demo_720p`, checked synchronous), and last
the database conversion (`gen_features` makes 500 people, `db_convert` converts them CSV to SQLite and back).
`scripts\run_tests_vs2015.bat` runs the same tests and video checks. It stops at the first
failing program. With `full` it then runs `eval_footage` (five profiles on each of the six footages: the engines one
by one on the 1920x1080 video, clean and with the simulated camera look), `eval_detect` (five profiles, frames of
1920x1080), `eval_features` (two profiles) and `bench --threads 2` (five profiles); these only print numbers. The five
profiles are `permissive`, `dev_noncommercial`, `dev_noncommercial_square`, `dev_noncommercial_onnx` and `classical`:
`dev_noncommercial_square` is the SCRFD profile with the 640x640 square, listed next to `dev_noncommercial`
(SCRFD on a fitted rectangle, 640x384 for 1920x1080) so that the two can be compared. The
numbers of one complete run are in `docs/RESULTS.md`, section 5c (clean footage) and 5d (simulated camera look); the
comparison of the fitted rectangle with the square is section 5e.
`scripts\run_tests_vs2015.bat` runs `eval_footage` on all six footages always (five profiles), `eval_detect` and `bench_detect` with the
five profiles, and `bench` with two (permissive and dev_noncommercial). **Both full flows were run on the SCRFD fitted-rectangle
change on 2026-11-12:** `run_tests.bat` with Visual Studio 2019 (`build\run_tests_final5.log`: `ALL DONE`, 11 test runs
`ALL TESTS PASSED`, 8 video checks `VIDEO CHECK PASSED`) and `scripts\run_tests_vs2015.bat` with Visual Studio 2015 only
(`build\run_vs2015_final4.log`: the four solutions rebuilt with 10, 9, 8 and 17 projects and 0 failed, 12 test runs
`ALL TESTS PASSED`, 8 video checks passed, `ALL DONE (Visual Studio 2015, Release)` in one go). **After those two runs five
checks were added to `test_common` and the comparison block of `test_pipeline` was rewritten** (positions that follow `--size`);
these small changes were checked with a full Visual Studio 2019 rebuild (`test_common` 74 checks and `test_pipeline` with the dev
profile, synchronous, 70 checks, all ok) and with incremental Visual Studio 2015 builds of the four solutions (8, 5, 5 and 8 projects,
0 failed; `test_common` 74 ok; `test_pipeline` with the dev profile, synchronous, passed). **The full flows were not repeated after
those changes.**

**Three of the tests also prove that nothing runs on a GPU** (the target PC has none); see "Proving that everything runs on the
CPU" below. `test_common` has 3 checks for it, `test_feature` 2 (for every profile) and `test_pipeline` 2 (in every mode). From outside, `scripts\gpu_probe.ps1` runs any program and reads the Windows GPU counters while it runs (the programs that write an MP4 show idle `3d` instances; the same section).

| Program | What it checks | Needs |
|---|---|---|
| `test_common` | image resize / letterbox / alignment maths, picture files, the recorded-frame source, the video file writer and reader, text drawing, model profile files, both network backends load and run, and **which camera mode suits a requested picture size** (18 checks starting with `camera mode:` for `pick_camera_mode`, run on made-up lists of camera modes: 1920x1080 gives the 30 fps mode and not a 5 fps one, the default size when nothing is asked, a 4:3 camera, H264 avoided, slow modes avoided, ties; no camera is opened), **the simulated camera look of the generated footage** (13 checks starting with `camera look:`: the ground-truth face box is exactly the same with and without the look, the picture is deterministic, another frame index gives other noise and another seed another room, and each effect is really there and measured: the sensor noise, uneven light, vignette, warm cast, lens softness, motion blur, no smear for a person who stands still, the strong edges of the room, and the room is the same in every frame; the measured values are printed), and **that everything runs on the CPU** (3 checks: both networks, ONNX Runtime and ncnn, report `runs_on_cpu`; the ONNX Runtime library offers no GPU provider, only the CPU one and the never-used remote Azure one; and no GPU library such as Vulkan, CUDA, DirectML, OpenCL or Direct3D 12 is loaded in the process after both networks have run), and **SCRFD with scaling upsampling layers** (3 checks: the ncnn model `det_dyn` loads; at 640x640 its nine raw outputs equal the flat outputs of the original model `det`, largest difference 3.7e-9 in the run of 2026-11-12; and it runs on a 640x384 picture and gives 48x80, 24x40 and 12x20 cells, which the original square model cannot do), and **`letterbox_fit`** (2 checks: the smallest multiple-of-32 rectangle for a long side of 640 for seven picture sizes, for example 1920x1080 gives 640x384 and 1280x1080 gives 640x544, a square stays 640x640; and a marker pixel lands where scale 1/3 and 12 rows of padding say, the padding being gray 114) and **the profile key `detector.layout`** (3 checks: `chw` needs nine `detector.outputs`, `chw` is only for SCRFD and a YuNet profile is rejected, a misspelled layout is rejected and `flat` accepted). **74 checks** in all (58 + 13 + 3; counted from the source; it was 66 before the SCRFD fitted rectangle, 69 in the two full flows of 2026-11-12, and 74 after the five checks of the last two groups were added, which was seen in a Visual Studio 2019 and a Visual Studio 2015 build, not in a full flow) | models (including `det_dyn.param` / `det_dyn.bin`) |
| `test_detect_units` | head pose maths (exact round trip), the pose gate, NMS, tracker behaviour, Haar cascade loading, and the section "the pose detector of a profile" (8 checks: the shipped profiles `permissive.ini` and `dev_noncommercial.ini` load; the permissive profile names no pose detector; the SCRFD profile names one; `create_pose_detector` gives YuNet for the SCRFD profile and the profile's own type for the permissive one; the SCRFD profile's own detector is still SCRFD; a named profile file that does not exist is refused; a named profile that names another one itself is refused; it writes small profile files `build\pose_*.ini` for the last two) | models (the SCRFD profile is loaded too, so the ncnn files of `dev_noncommercial` are needed) |
| `test_match` | M:N matching (M query faces against N registered features) equals the reference answers (M separate 1:N searches), also for 72 combinations of M and N that differ in every direction (M = 0, N = 0, M larger than N); speed; stranger acceptance; the store: register / delete / modify, quoting, damaged files; the 10,000-feature CSV round trip with its save and load times; **card first**: the RF card number stored in the record next to the face feature (look-up by card, a card belongs to one person only), the matcher's table of similarities for a subset of the registered features (`score_matrix`: 25 combinations of M faces and N_s features), and the whole card-first comparison `match_cards` for M < N, M = N and M > N, for a card that is not in the database (a failed comparison, never a search among all people), and for the card of a deleted owner, plus the CSV format with a header line and the `card_id` column (older files still read); the **managed database in both formats, CSV and SQLite**, with the same checks for each (opening creates the file, every change is written at once, a change that cannot be written is undone, a second program cannot open the same database (lock), a damaged file is refused); the **choice of the format** (by file name, by file content, refusals, conversion in both directions, damaged and foreign SQLite files); and the **file speed** with 10,000 people of 512 numbers, CSV against SQLite. 196 checks in all (8 + 8 + 7 + 27 + 47 + 38 + 38 + 20 + 3; counted from the output) | nothing |
| `test_feature [profile.ini]` | the **feature extraction engine alone** (default `models\permissive.ini`), on the first 8 portraits of `data/portraits` (the profile's detector is used only to find the face landmarks): the extractor has the length the profile says; every feature has that length, is finite and has length 1; the same crop gives the same feature twice; a crop without pixels and a crop without landmarks are refused; the features of different people are less alike than the profile's match threshold; and, for the deep extractors only (not `classical.ini`), a 15% brighter copy of a face is still alike (similarity at least 0.8) and still identified as that person among the 8. 14 checks (12 for the classical extractor). Run it for each of the four profiles: `permissive.ini`, `dev_noncommercial.ini`, `dev_noncommercial_onnx.ini`, `classical.ini` (`run_tests.bat` runs three of them; all four pass; `dev_noncommercial_square.ini` has the same recognizer as `dev_noncommercial.ini` and is not run through this test; with `dev_noncommercial.ini` the landmarks come from the fitted SCRFD rectangle and the test still passes its 14 checks, `build\test_feature_dyn.log`). **It also checks that the extractor and the detector run on the CPU and, at the end, that no GPU library is loaded in the process** (2 of the checks, for every profile). It does not measure accuracy over many people (that is `eval_features`); a profile with `recognizer.threshold = 0.10` makes it fail the "different people" check, which shows that the check works | models + `data/portraits` |
| `test_pipeline [profile.ini] [--async] [--sqlite] [--size WxH]` | the **complete system** on generated video (1920x1080 by default; `--size` changes it): registered person is detected, tracked with one ID, cropped only inside the rotation tolerance, verified, then only tracked; stranger is never verified; a stranger who takes the place of a verified person does not inherit the verification; two people at once; register from the camera view, modify and delete; **card first** (video 6, 22 checks, nine walks): the RF cards are identified first, each card is looked up in the database and the faces are compared only with the features stored for the cards, for **M < N** (one face, two cards), **M = N** (two faces, two cards) and **M > N** (two faces, one card): the owner with his own card is verified, a registered person with somebody else's card is **not** verified, a card that is not in the database is a failed comparison and the face is **not** searched among all people instead (also when only one of two cards is unknown), after the check ends the normal search works again, and a deleted owner's card is worthless; and **the same walk at five camera picture sizes** (video 5: 640x480, 1280x720, 1920x1080 (the default), 1600x1200 and 2560x1440; per size, 3 checks: the face is detected in at least 80% of the frames, one track ID, verified as the right person within 5 s; their timings are not part of the timing statistic of the default size); and, only for a profile with `detector.layout = chw` (`dev_noncommercial.ini`), **the fitted rectangle against the square** (3 checks, before the registration: the square profile `dev_noncommercial_square.ini` loads; the SCRFD detector on the fitted 640x384 rectangle and the one on the 640x640 square find the same three faces in a picture of the test size (1920x1080 by default; the positions and sizes follow `--size`), with box overlap above 0.95 and landmarks within 3 pixels; measured 0.994 and 0.26 px in the two full flows, 0.23 px after the positions were made to follow the picture size). 67 checks in synchronous mode, 72 with `--async` for a profile without that comparison (`permissive.ini`, `dev_noncommercial_square.ini`, `dev_noncommercial_onnx.ini`), and **70** in synchronous mode and **75** with `--async` for `dev_noncommercial.ini` (all counted from the output of the Visual Studio 2019 full flow, `build\run_tests_final5.log`). The last two checks, made after all the videos (and the worker lifecycle test), are that **every engine of the pipeline runs on the CPU** (`AuthPipeline::runs_on_cpu()`, including the worker's own detector and extractor with `--async`) and that **no GPU library is loaded in the process**; they are the same in every mode. `--sqlite` keeps the registered people in a SQLite database (`build\test_pipeline.db`) instead of the default CSV file; every check is the same, so the whole system is tested with both formats. At the start it prints the database file and its format, and right after `init` it prints `pose check by: <detector name>` (which detector measures the head pose: YuNet for the SCRFD profiles, otherwise the profile's own) and checks that the pipeline has a separate pose detector exactly when the profile names one; all its `refine_face` calls use `pipeline.pose_detector()`. With `--async` the pipeline uses its worker thread and the videos are played in real time; it also checks the frame times (99% under 40 ms, none over 60 ms; see the known problem of the dev profile below) and that the worker can be reset and shut down while it is busy | models + `data/portraits` |

Every test prints one line per check (`ok:` / `FAIL:`) and ends with
`ALL TESTS PASSED` or `TESTS FAILED`; the exit code is 0 only when all passed.

`test_feature` takes the profile as its argument. Start it from the project root, once for each profile:

```
build\bin\test_feature.exe models\permissive.ini
build\bin\test_feature.exe models\dev_noncommercial.ini
build\bin\test_feature.exe models\dev_noncommercial_onnx.ini
build\bin\test_feature.exe models\classical.ini
```

Besides the checks it prints `portraits used: N`, the median time of one `extract()` call, and the most alike
pair of different people (and, for the deep extractors, how alike the brightened copies are), so that a
failing check can be understood. Exit code 0 = all checks passed, 1 = a check failed, 2 = the profile or its
models could not be loaded.

**After changing `pose.detector_profile`** in a profile (or changing which detector a profile
uses), the pose model constants in `src/detect/pose.cpp` may no longer fit, because the pose
is computed from the landmarks of whichever detector does the refinement. Re-run

```
build\bin\calibrate_pose.exe <profile.ini> --refine data\portraits
```

and compare the printed `PoseModel` line and the share of straight-on portraits that pass the
gate with the constants for `<detector type>-refined` in `pose.cpp` (for a profile with a pose
detector, the first output line names it: `detector: <main>  (landmarks of the refinement step,
made by <pose detector>)`). `test_pipeline` prints `pose check by: <name>` so a log shows which
detector measured the pose.

## Evaluation programs

These measure; they do not pass or fail.

| Program | What it measures |
|---|---|
| `eval_detect profile.ini ... [--frame WxH]` | share of faces found, box overlap, false detections and time, for six face sizes, per detector; `--frame` sets the picture size (default 1920x1080, the default camera size), for example `--frame 1280x1080` or `--frame 3840x2160` to see how the smallest face that is found depends on the camera picture size. `models\dev_noncommercial_square.ini` is SCRFD on ncnn with the 640x640 square, `models\dev_noncommercial.ini` the same models on the fitted rectangle: give both to compare them (`docs/RESULTS.md`, section 5e) |
| `eval_footage profile.ini <footage folder> [--fillers N] [--every K] [--fps N]` | the **engines one by one on generated footage** (the six generated 1920x1080 videos with their exact truth, made by `scripts\make_fhd_footage.bat`: three clean ones and the same three with the **simulated** camera look, `fhd_cam_*`; it first prints `all engines run on the CPU: yes (no GPU is used)`): detection and tracking (share of true faces found by face width, false detections, track IDs per person, ID changes, time per frame), feature extraction (pose gate, time of each step) and M:N matching (rank 1 of the registered people, false accepts of strangers, time of the call); the classical profile cannot enrol the people from their portraits, so its matching figures are empty |
| `eval_features profile.ini data\portraits` | same-person and different-person similarity, false accept / false reject per threshold, 1:N identification and false accept among N people (the alignment and pose landmarks come from the profile's pose detector; the output says which when the profile names one) |
| `bench --threads 2 profile.ini ...` | all three engines through the complete pipeline: detection time at 1280x1080, 640x640, 1920x1080 and 1280x720, and for three faces at 1280x1080 and 1920x1080 (the 60 ms limit is applied to the 1920x1080 and 1280x720 rows by analogy: the requirement names only 1280x1080 and 640x640), extraction time (on a 1920x1080 frame), pose refinement time (by the pipeline's pose detector; the row is marked "(other detector)" when that is not the main detector), matching time; PASS/FAIL against the limits and an **estimate** for a 4th-generation i5 |
| `bench_detect --threads 2 profile.ini ...` | the **detection engine alone**: the detection rows (six: 1280x1080, 640x640, 1920x1080, 1280x720, three faces at 1280x1080, three faces at 1920x1080) and the pose refinement row of `bench`, per profile (no extractor, no pipeline); needs `data/portraits` |
| `bench_extract --threads 2 profile.ini ...` | the **extraction engine alone**: the row "extract 1 face (crop+align+net)" of `bench` with its 150 ms limit and the ideal 50 ms, per profile (the profile's detector only finds the landmarks); needs `data/portraits`. With `classical.ini` it prints a message instead of the row, because the Haar detector finds no face with landmarks in the test frame |
| `bench_match` | the **matching engine alone**: one M:N match of M = 1, 4 and 16 faces against N = 10,000 registered features of 128, 256 and 512 numbers; needs no model, no picture and no network library. Only `--i5-factor F` has an effect |
| `detect_image profile.ini --draw out image.jpg` | runs a detector on pictures and saves annotated copies |
| `calibrate_pose`, `calibrate_lbp`, `roll_probe` | produce / check the constants used by the pose estimator and the classical extractor |
| `camera_check` | lists cameras (opens none); `camera_check --modes [index] [WxH]` lists the picture formats a camera offers and marks the one chosen for the wanted size (default 1920x1080; the stream is not started); `camera_check --capture 30 [index] [WxH]` opens the camera (default camera 0, 1920x1080), says which camera mode was switched on and reads 30 frames (see "Camera picture sizes" above) |
| `check_video` | plays a video through the system and checks it (see above); it is also part of `run_tests.bat`; it prints `all engines run on the CPU: yes (no GPU is used)` |

The four benchmark programs read the same options: `--threads N` (CPU threads; replaces the profile's
`runtime.threads`), `--frames K` (timed repetitions, default 40) and `--i5-factor F` (the multiplier of the
estimate, default 3.5); every other word is a profile file. `bench`, `bench_detect` and `bench_extract` print
a usage line and exit with code 2 when no profile is given; a profile that cannot be loaded is skipped.
Every row shows median, p95 and maximum on this PC, and `est. i5` (the median times the factor); a row that
has a limit also shows PASS or FAIL for this PC and for the estimate, but the exit code is 0 either way. The
three single-engine programs use exactly the same measuring code as `bench`, so their rows are the rows of
`bench`. Examples:

```
build\bin\bench_detect.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini models\dev_noncommercial_square.ini
build\bin\bench_extract.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini
build\bin\bench_match.exe
```

### Proving that everything runs on the CPU

The requirement is that **all engines run on the CPU**: the target PC has no GPU. What is true, and how each part is checked:

| Engine | Runs on | How it is made sure, and checked |
|---|---|---|
| Neural networks on ncnn (SCRFD, ArcFace in the `dev_noncommercial` profile) | CPU | `NcnnNet::load` sets `use_vulkan_compute = false` for every network. The ncnn library in `third_party` was **built with** its Vulkan code (`platform.h` has `NCNN_VULKAN 1`, which is why the glslang libraries are linked), but because the option is off no Vulkan instance is ever created. Rebuilding ncnn with `NCNN_VULKAN=OFF` would remove the code path altogether; that was not done. `NcnnNet::runs_on_cpu()` reads the option back |
| Neural networks on ONNX Runtime (YuNet, SFace, and SCRFD / ArcFace in `dev_noncommercial_onnx`) | CPU | The bundled ONNX Runtime 1.17.3 is the CPU build: its providers are `CPUExecutionProvider` and the remote `AzureExecutionProvider` (not a GPU: it sends data to a remote service and only works if a program appends it to a session; nothing here does). `OnnxNet::load` appends no execution provider. `test_common` checks the provider list |
| Haar detector, LBP extractor (classical profile) | CPU | plain C++, no library that could use a GPU |
| Matcher | CPU | AVX2 code |
| Media Foundation, reading (video files and the camera: `CameraSource`) | CPU | the source reader is created with `MF_SOURCE_READER_DISABLE_DXVA = TRUE` and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (besides `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = TRUE`): no decoding through the graphics card, no hardware decoder or video processor; no Direct3D device manager is handed over. `eval_footage` (decodes an MP4) had no GPU engine instance in the measurement below |
| Media Foundation, writing (`VideoWriter`: the MP4 files of `gen_video`, `check_video`, `test_common`) | CPU (the Microsoft software H.264 encoder), **but it opens an idle Direct3D device** | the sink writer is created with `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (no hardware encoder, no GPU video processor). The programs that write an MP4 still show GPU engine instances in the measurement below: the `3d` engine at 0.0% utilization and no video engine |

Three levels of proof, from the code to the outside:

1. **The engines are asked.** `Net::runs_on_cpu()`, `FaceDetector::runs_on_cpu()`, `FeatureExtractor::runs_on_cpu()` and `AuthPipeline::runs_on_cpu()` (which asks the detector, the pose detector, the extractor and the worker's own two) must be true. This says what the code asked for.
2. **The process is looked at** (`tests\cpu_only_check.h`). A network that had used a GPU would have loaded a GPU library, so after the engines have run the process must not have loaded any of: `vulkan-1`, `d3d12`, `nvcuda`, `cudart`, `cublas`, `cudnn`, `nvinfer`, `opencl`, `directml`, `onnxruntime_providers_cuda`, `_tensorrt`, `_dml`, `_openvino`. Direct3D 11 and DXGI are only reported: in `test_common`, which writes and reads an MP4, `d3d11.dll` and `dxgi.dll` are loaded (Windows components that come with the MP4 path of Media Foundation, see the measurement below; informational); in `test_pipeline`, which writes and reads no video file, none of them is.
3. **The GPU is looked at from outside the program** (a measurement, not a test): `scripts\gpu_probe.ps1` runs any program of the project and reads the Windows GPU counters while it runs; see below.

How to run it (from the project root; every program prints `all engines run on the CPU: yes (no GPU is used)` or passes the checks):

```
build\bin\test_common.exe                                  3 checks (both networks, the ONNX Runtime providers, no GPU library loaded)
build\bin\test_feature.exe models\permissive.ini           2 checks: the extractor and the detector on the CPU; no GPU library loaded
                                                           (also dev_noncommercial.ini, dev_noncommercial_onnx.ini, classical.ini)
build\bin\test_pipeline.exe models\permissive.ini          2 checks at the very end: pipeline.runs_on_cpu(); no GPU library loaded
                                                           (also dev_noncommercial.ini, and each with --async or --sqlite)
build\bin\check_video.exe models\permissive.ini data\videos\fhd_cam_3people\video.mp4     prints the line after the profile line
build\bin\eval_footage.exe models\permissive.ini data\videos\fhd_cam_3people              prints the line after the profile line
```

The CPU checks of all of them passed on the development PC (the logs `build\test_common_cpu.log`, `build\test_feature_cpu_*.log` for the four profiles, `build\gpu_probe_pipeline_models_*_ini.log` and `build\gpu_probe_permissive.log`, `build\gpu_probe_dev_noncommercial.log`; the two failing checks in the dev `--async` log are the worker-thread timing checks described below, not CPU checks). The first two levels say what the code asked for and which libraries were loaded; they cannot see a GPU being used in some other way, which is what the measurement is for.

**The independent measurement: `scripts\gpu_probe.ps1`.** Windows keeps a performance counter per process and GPU engine, `\GPU Engine(*)\Utilization Percentage`. A process that uses a GPU appears there with instances whose names contain its process id (`pid_<id>_...`) and the engine type (`3d`, `copy`, `videodecode`, `videoencode`, ...); a process that uses none has no instance at all. The script starts a program, reads the counters back to back until the program ends (one reading takes about 3 s: 237 s with 80 samples is one reading per 3.0 s), and says how many GPU engine instances the program had. Start it from the project root (it changes to the root itself, so the paths are relative to the root). **The arguments of the program go into one quoted list after `-ProgramArguments`**, because PowerShell would take a bare word that starts with `-` or `--` (for example `--async`) for one of its own parameters:

```
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\test_pipeline.exe -ProgramArguments 'models\permissive.ini'"
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\eval_footage.exe -ProgramArguments 'models\permissive.ini','data\videos\fhd_cam_5people'"
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\check_video.exe -ProgramArguments 'models\permissive.ini','data\videos\fhd_cam_3people\video.mp4','--async','--out','build\gpu_probe_video.mp4'"
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\gen_video.exe -ProgramArguments 'build\gpu_probe_gen','8','15','1','0','1'"
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\test_common.exe"
```

The program's own output goes to `build\gpu_probe_<program>.log` (the next run of the same program overwrites it); the summary line is printed and appended to `build\gpu_probe_summary.log`. The line looks like this (a run of `gen_video`, 2026-11-12):

```
2026-11-12 09:09  build\bin\gen_video.exe build\gpu_probe_gen 8 15 1 0 1: ran 7 s (exit code 0), 2 counter samples; GPU engine instances of this program: 21, highest utilization 0.0%; other processes with GPU instances: 27 -> THE PROGRAM HAD GPU ENGINE INSTANCES; engine types: 3d 0.0%
```

How to read it:

| Part of the line | Meaning |
|---|---|
| `ran 7 s (exit code 0)` | how long the program ran and what it returned (a non-zero exit code means that a check of the probed program failed; its own log, `build\gpu_probe_<program>.log`, says which). The probe itself says nothing about the program's checks |
| `2 counter samples` | how many times the counters were read. One reading takes about 3 s, so a program that runs 7 to 12 s is seen only 2 to 4 times; a GPU burst between two readings is not seen |
| `GPU engine instances of this program: 21` | the number of GPU engine instances whose name holds the program's process id. **0 = the program never touched a GPU in any sample** |
| `highest utilization 0.0%` | the highest utilization of any of its instances in any sample |
| `engine types: 3d 0.0%` | the engine types of the instances and the highest utilization of each. `3d` is the graphics engine; `videoencode`, `videodecode`, `videoprocessing`, `compute` and `copy` would show real video or compute work |
| `other processes with GPU instances: 27` | the **control**: how many other processes had GPU instances in the same samples (27 or 28 on the development PC). **If this is 0 the counters themselves may not work and a result of 0 instances means nothing**: the script then prints `no GPU instance seen, but NO other process was visible either: the counters may not work, so this proves nothing` |
| `NO GPU USE (the counters work: other processes were visible)` | the verdict for 0 instances with a working control. With instances the verdict is `THE PROGRAM HAD GPU ENGINE INSTANCES`, followed by the engine types |

The script ends with exit code 2 when the program does not exist or cannot be started, and 3 when the Windows counter set `GPU Engine` is not available on the PC (it needs Windows 10 or later and a WDDM 2.0 graphics driver); in that case nothing can be measured.

**What it found (development PC, 2026-11-12; the table is in `docs/RESULTS.md`, section 2).** The programs that run the engines, `test_pipeline` with the permissive profile (the whole system: 237 s, 80 samples, 28 other processes visible) and `eval_footage` with the dev profile on ncnn (12 s, 4 samples, 27 other processes visible), had **0 GPU engine instances**, so the counters do work (all exit codes 0). Earlier probes, archived in `build\gpu_probe_summary_earlier.log`, and runs with a throw-away sampler before them gave the same: the complete `test_pipeline` with the permissive profile (231 s, 83 samples; 230 s, 83 samples) and with the dev profile and the worker thread (235 s, 85 samples), `eval_footage` with the permissive profile and two runs of 6 s. **The programs that write an MP4 file show idle `3d` instances:** `gen_video` (7 s, 2 samples) and `check_video` (the annotated video: permissive with `--async`, 12 s, 4 samples; dev profile synchronous, 8 s, 2 samples) had 21 GPU engine instances each, all of the `3d` type, at 0.0% utilization, and never a `videoencode`, `videoprocessing`, `videodecode`, `compute` or `copy` instance. The video writer (`VideoWriter`) opens a Direct3D device while the Microsoft software H.264 encoder runs; nothing was computed on a GPU as far as the samples show. So a verdict `THE PROGRAM HAD GPU ENGINE INSTANCES` is to be expected for `gen_video` and `check_video` (and possibly `test_common`, which writes a small MP4 but showed none in an earlier probe of 3 s with a single sample), and what matters is the engine type and the utilization; the same verdict for an engine program (`test_pipeline`, `eval_footage`, `test_feature`, `test_match`) or any `videoencode`, `videoprocessing`, `videodecode`, `compute` or `copy` instance, or a utilization above 0.0%, would need an explanation. The writer is created with `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` and the readers also with `MF_SOURCE_READER_DISABLE_DXVA = TRUE` (`docs/camera.md`); for the writer, the instances were the same before and after that attribute was set. The measurement is coarse (one reading about every 3 s, 2 to 4 samples for a short program; 0.0% is what the samples saw, and the `3d` instances only show that a device exists, not that nothing was submitted to it); the longer account, the isolation experiments and what is not proven are in `docs/RESULTS.md`, "All engines on the CPU".

**A caveat about this measurement.** The sampler runs next to the program and uses CPU itself, so **timing checks can fail while a program is probed**: use the script for the GPU question only, and never at the same time as a timing measurement or the worker-thread limits of `test_pipeline --async`. For example, in an early run of `test_pipeline` with the dev profile (SCRFD on ncnn, `--async`) under a throw-away sampler the timing checks of the worker thread failed (`worker mode: 99% of the frames take less than 40 ms` and `worker mode: no frame takes 60 ms or more`; frame time median 40.3 ms, 99th percentile 59.1 ms, slowest frame 135.9 ms, in `build\gpu_probe_pipeline_models_dev_noncommercial_ini.log`) because of that load. This is **not a regression** of the project and says nothing about the GPU: without the sampler the same check passes when the PC is quiet (median 18.8 and 18.5 ms, 99th percentile 23.2 and 23.6 ms, slowest frame 40.0 and 36.3 ms in the two reruns `build\dev_async_recheck_1.log` and `_2.log`; it also passed in the earlier Visual Studio 2015 run, of the version before this work), and it failed earlier on the commit before this work too, during a noisy period (see "Known problem on the development PC" below). All the other checks of that run passed (70 of 72 `ok:`). Do not run a sampler while measuring time.

What is **not** shown: the camera is not opened by any test, so whether a real camera driver or Media Foundation uses the GPU for a live stream is not tested (the camera's source reader is created with DXVA and hardware transforms switched off, like the one for video files, and the target has no GPU anyway). `test_match` was not probed, and `test_common` was probed only in an earlier run of 3 s with 1 sample. The programs that write an MP4 open an idle Direct3D device while they encode (above); removing even that was not done, because it would mean converting the pictures to NV12 on the CPU before the encoder, which changes the colour handling of the footage.

### Imitating the 2-core target on this PC

The target (Lenovo, 4th-generation Core i5) has 2 cores and no GPU; every engine of the project runs on the CPU (and the project proves it, see above).
To see how the pipeline behaves with only two cores, pin a run to two physical cores (the
cores themselves stay much faster than a 4th-generation i5's):

```
cmd /c "start /b /wait /affinity 5 build\bin\check_video.exe models\permissive.ini data\videos\demo\video.mp4 --async"
```

(`5` = logical processors 0 and 2; on a CPU with hyper-threading these are different physical
cores. Adjust the mask on other CPUs.)

### Repeatable timings on a CPU with fast and slow cores

The development PC (Core i7-13650HX) has 6 fast "P" cores (logical processors 0-11) and 8 slow "E"
cores (12-19). Windows decides where a console program runs, and depending on what else is going on
(a user working in other windows, for example) it sometimes puts it on the E cores: the very same test
then takes 2 to 3 times as long, in some runs only during a part of the run. Measured on 2026-11-11 with
`test_pipeline models\permissive.ini` (frame median over all videos): **7.0 ms pinned to the P cores,
22.5 ms pinned to the E cores, 18.8 ms unpinned in a busy moment**. The timing limits of the worker-thread
tests (99% of frames under 40 ms, none over 60 ms) can fail for that reason alone, without any change in
the code. To get repeatable numbers, pin the run to the P cores:

```
cmd /c "start /b /wait /affinity FFF run_tests.bat"
```

(`FFF` = logical processors 0-11. On a PC without E cores, such as the target, this does not matter.)

**Known problem on the development PC, 2026-11-12: `test_pipeline models\dev_noncommercial.ini --async` fails its timing limit.**
The check `worker mode: no frame takes 60 ms or more` fails (the slowest frame took 59 to 139 ms in several runs), and sometimes
`worker mode: 99% of the frames take less than 40 ms` as well (99th percentile 38 to 48 ms). Everything else passes: the dev
profile with the worker thread passes all functional checks, and the permissive profile passes the timing checks too
(99th percentile 15.0 ms, slowest frame 17.2 ms). An interleaved comparison on this PC showed that the last commit before the
card-first, SQLite and 1920x1080 changes (built in a separate folder) fails the **same** check with the same numbers (99th
percentile 37.8 and 48.3 ms, slowest frame 76.8 and 83.6 ms; with the new code 38.4 and 42.7 ms, 115.6 and 95.8 ms), and the
same check at 1280x1080 fails with the new code as well. So it is **not caused by the 1920x1080 size or by those changes**: the
tail latency of the dev profile's worker thread (SCRFD on ncnn, with two threads, plus the worker) is currently too noisy on
this PC, which has fast and slow cores; earlier the same day the test passed (slowest frame 34.8 ms). The thresholds were
not loosened. Pinning the run to the fast cores (above) is the first thing to try; on the target PC (2 cores) the dev profile
is not expected to fit the 60 ms limit anyway (`docs/RESULTS.md`, section 2). (That sentence and the timings of this paragraph are from before the SCRFD detector was changed to the fitted rectangle, which cuts its detection from about 18 to about 11 ms at 1920x1080: `docs/RESULTS.md`, section 5e. The numbers of this paragraph were not remeasured. With the fitted rectangle the dev worker-thread timing check **passed in both full flows of 2026-11-12 with much more headroom**: Visual Studio 2019 `process()` median 11.7 ms, p95 17.8, p99 21.3, max 24.4 (`build\run_tests_final5.log`; the median was about 18.5 ms before the change), Visual Studio 2015 median 11.7 ms, p99 21.4, max 41.9 (`build\run_vs2015_final4.log`); the limits are 40 ms for the 99th percentile and 60 ms for the slowest frame. Two passing runs do not show that the intermittency of this PC is gone: the check is still known to fail in noisy periods.)

**The same two checks failed again in a run with a GPU sampler next to the test, and that is not a regression either.** For the measurement in "Proving that everything runs on the CPU" a sampler of the Windows GPU counters ran next to the complete `test_pipeline models\dev_noncommercial.ini --async`; its own CPU use slowed the frames (median 40.3 ms instead of about 18.5 ms), so `worker mode: 99% of the frames take less than 40 ms` and `worker mode: no frame takes 60 ms or more` failed. Without the sampler the check passes when the PC is quiet: it passed in the two Visual Studio 2019 reruns made afterwards (99th percentile 23.2 and 23.6 ms, slowest frame 40.0 and 36.3 ms) and in the earlier Visual Studio 2015 run (of the version before the camera look and the CPU checks); and it failed on the last commit before this work too, during a noisy period. The thresholds were not loosened.

## Measuring on the target PC

All timings in `RESULTS.md` come from the development PC (13th-generation Core
i7), **not** from a 4th-generation Core i5. On the target machine run

```
build\bin\bench.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini models\classical.ini
build\bin\bench.exe --threads 2 models\dev_noncommercial.ini models\dev_noncommercial_square.ini     (SCRFD: fitted rectangle against square)
```

and read the PASS / FAIL columns (the `this PC` column is the real one there). To measure one engine only,
use `bench_detect.exe`, `bench_extract.exe` (same arguments) or `bench_match.exe` (no arguments, no model and
no pictures needed).
Copy `build\bin\`, `models\`, `data\portraits` and the two ONNX Runtime DLLs next
to the programs; build is not needed on the target.

Engine boundary regressions: run `_test.exe test_engine_boundaries` from the repository root. This checks strict numeric profile parsing, malformed detector outputs, zero model features, and authentication reinitialization against an empty database.
