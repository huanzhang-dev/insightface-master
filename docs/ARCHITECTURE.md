# Architecture

The project is a set of small C++ engines that can each be built, tested and
timed on their own, plus one pipeline that joins them. All code is plain C++14
(so that Visual Studio 2015 can build it, see `docs/VISUAL_STUDIO.md`) without OpenCV; the
neural networks run with ncnn or ONNX Runtime.

```
  camera / recorded frames
          |
          v
 +------------------+     +-----------------+     +------------------+
 |  FaceDetector    | --> |  FaceTracker    | --> |  pose gate       |
 |  (detect/)       |     |  track IDs      |     |  + crop storing  |
 +------------------+     +-----------------+     +------------------+
   deep: YuNet, SCRFD                                       |
   classical: Haar                                          v
                                               +----------------------+
                                               |  FeatureExtractor    |
                                               |  (feature/)          |
                                               +----------------------+
                                                 deep: SFace, ArcFace
                                                 classical: LBP
                                                            |
                                                            v
                                               +----------------------+
                                               |  FeatureMatcher M:N  | <-- FeatureStore (CSV or SQLite)
                                               |  (match/)            |     register / delete / modify
                                               +----------------------+     RF card first: N = the cards' features
                                                            |
                                                            v
                                                   verified / not verified
                                                   per track ID
```

The matcher compares M faces with N registered features in one call. Normally N is every registered person. When RF cards have been read, the **card comes first**: each card is looked up in the database, the feature stored with it is retrieved, and the faces are compared only with those retrieved features (`FeatureStore::match_cards`); a card that is not in the database is a failed comparison. The store keeps the people in a file of the format the user chooses, CSV (the default) or SQLite (`docs/store.md`).

## The four projects

The three engines and the pipeline that joins them are four projects with four Visual Studio solutions
(`docs/VISUAL_STUDIO.md`). Each can be built, tested and timed without the others' programs:

| Project (solution) | Source folders | Verified by | Speed |
|---|---|---|---|
| high-speed face detection (`face_detection.sln`) | `detect/` (+ `common/`) | `test_detect_units`, `eval_detect` | `bench_detect` |
| high-speed feature extraction (`feature_extraction.sln`) | `feature/` (+ `common/`; `detect/` only to find landmarks for the tools) | `test_feature`, `eval_features` | `bench_extract` |
| high-speed feature matching (`feature_matching.sln`) | `match/`, `store/`, `third_party/sqlite` (+ the small part of `common/` that has no network library) | `test_match` (also `gen_features`, `db_convert`) | `bench_match` |
| face authentication (`face_authentication.sln`) | `pipeline/`, `camera/`, uses all three engines | `test_pipeline`, `check_video`, `eval_footage`, `test_common` | `bench` |

The dependencies run one way only: the pipeline uses the engines, the engines use `common/`, and no engine
uses another (the matching engine does not even use the network wrapper).

## Folders

| Folder | Content |
|---|---|
| `src/common/` | image type and operations, picture files (WIC), the network wrapper (ncnn + ONNX Runtime), model profiles, timers, text drawing for result videos, `fs_compat.h` (file-system library under one name), `config.h` |
| `src/detect/` | the face detection engine: deep and classical detectors, head pose, tracker |
| `src/feature/` | the feature extraction engine: deep and classical extractors |
| `src/match/` | the feature matching engine (M:N, AVX2; N = all registered people, or the features that RF cards stand for: `score_matrix`) |
| `src/store/` | the database of registered people: a managed file in the format the user chooses, CSV (the default) or SQLite (`sqlite_db.h`), locked, every change written at once, and its copy in memory; features and RF card numbers; the card-first comparison (`match_cards`) |
| `src/camera/` | Windows Media Foundation: camera and video-file sources (the same reader and RGB conversion), the choice of the camera mode for any wanted picture size (default 1920x1080), recorded-frame source, MP4 video writer |
| `src/pipeline/` | the complete authentication pipeline |
| `src/testdata/` | generators for test data: synthetic features, scenes with walking people, optionally with a simulated camera look (`CameraLook`: a room, uneven light, a soft lens, sensor noise (and motion blur for fast movers; the generated walks are too slow for it); a **simulation**, no real camera footage is in the project) |
| `tools/` | command-line programs (evaluation, benchmark, generators, calibration, `db_convert`) |
| `tests/` | the automatic tests |
| `models/` | model files and the profile `.ini` files that choose between them |
| `scripts/` | Python helpers that download and clean the test portraits, write the Visual Studio files (`make_vs_projects.py`), check the code against Visual Studio 2015 (`check_vs2015_compat.bat`), run everything with Visual Studio 2015 (`run_tests_vs2015.bat`), make the 1920x1080 test footage (`make_fhd_footage.bat`) and see from outside whether a program used the GPU (`gpu_probe.ps1`, PowerShell: reads the Windows GPU counters while a program runs) |
| `msvc/`, `*.sln` | the four Visual Studio 2015 solutions (`face_detection`, `feature_extraction`, `feature_matching`, `face_authentication`) and their projects: ten libraries (one of them is the SQLite library, `fa_sqlite`) and one project per program (generated; `docs/VISUAL_STUDIO.md`) |
| `docs/` | this documentation |
| `third_party/` | ncnn, ONNX Runtime and SQLite (the amalgamation `sqlite3.c`, public domain, compiled into the project; `docs/MODELS.md`) |

## Independent engines

Every engine can be used and tested without the others.

| Engine | Interface (header) | Independent test / tool |
|---|---|---|
| Face detection and tracking | `detector.h`, `tracker.h`, `pose.h` | `test_detect_units`, `eval_detect`, `detect_image`, `bench_detect` |
| Feature extraction | `extractor.h` | `test_feature`, `eval_features`, `bench_extract` |
| Feature matching | `matcher.h`, `feature_store.h`, `sqlite_db.h` | `test_match`, `bench_match`, `gen_features`, `db_convert` |
| Complete authentication | `auth_pipeline.h` | `test_pipeline` (also `--async`, `--sqlite`), `check_video` (a whole video, checked against the truth), `eval_footage` (the engines one by one on recorded 1920x1080 footage), `bench` (all engines through the pipeline) |

## How a frame is processed (`AuthPipeline::process`)

1. **Detect.** The deep detectors shrink the camera image to what their model needs
   (a 640x640 square keeping the aspect ratio, or 320x320 for small camera images with
   YuNet) and return every face with box, score and 5 landmarks. **SCRFD with
   `detector.layout = chw` (`dev_noncommercial.ini`) uses a rectangle instead of the square:**
   the picture is shrunk to a long side of 640 and padded only up to the next multiple of 32 on
   each side (`letterbox_fit`), so a 1920x1080 picture becomes 640x384 and not 640x640, a
   1280x1080 picture 640x544, a 4:3 picture 640x480; a square picture stays 640x640. The scale is
   the same as with the square, so the faces and the result are the same, with 40% fewer
   pixels (16:9) to compute: 11.5 ms instead of 17.9 ms at 1920x1080 on the development PC
   (`docs/RESULTS.md`, section 5e). The classical Haar
   detector works on a 320-pixel-wide gray image, follows known faces between full
   scans, and gives landmarks only when it finds both eyes.
2. **Track.** The tracker gives every face an ID that stays the same between
   frames (it predicts where the box moves, so fast movers are still followed).
3. **Gate and store.** For every track that is **not yet verified**, whose face is at
   least `FA_MIN_CROP_FACE_PIXELS` (80) wide and whose crop is due (at most one stored crop
   per second, `FA_CROP_STORE_INTERVAL_MS`): the pose is checked at most every
   `FA_POSE_CHECK_INTERVAL_MS` (100 ms) until it is acceptable. The face is looked at a
   second time, enlarged (`refine_face`), which gives
   exact landmarks and so exact **pitch, yaw and roll**. The detector that measures the pose
   is the profile's **pose detector** (`AuthPipeline::pose_detector()`): the main detector
   itself, unless the profile names another one with `pose.detector_profile` (the SCRFD
   profiles let YuNet do it, because SCRFD's landmarks give a noisier pitch). The main
   detector still finds the faces in the whole frame (step 1) and in registration photos.
   Only if all three are
   inside the tolerances (`FA_POSE_*_TOL_DEG`, defaults 5 / 5 / 8 degrees) is the
   face cut out and stored as the track's crop, replacing the older one.
4. **Extract.** The features of all new crops are computed.
5. **Match.** All M new features (of the currently detected faces) are compared with all N
   registered features in one **M:N** call (`FeatureMatcher::match`). When RF cards have been read
   (`AuthPipeline::begin_card_check`), the **card information is identified first**: every card is
   looked up in the database, the feature stored with it is retrieved, and the M faces are compared only
   with those N retrieved features (`FeatureStore::match_cards`, which uses `FeatureMatcher::score_matrix`).
   M and N are independent of each other: fewer faces than cards (M < N), as many (M = N) or more
   (M > N). A face is verified as the owner of the card whose feature it is most like, when the score
   reaches the threshold, so a registered person who shows somebody else's card is not verified. A card
   that is **not in the database** is a failed comparison: it is reported as not found
   (`CardStatus::found = false`), nothing is compared for it, and nothing falls back to a search among
   all people. A frame makes one matcher call either way. A score at or above the profile's threshold
   marks the track *verified*. From then on that track is only tracked: no new crop, no feature,
   no matching. One exception: a verified track that was not found for
   `FA_REVERIFY_AFTER_MISSED_FRAMES` (3) frames in a row loses its verification when it is
   found again, because another person may have stepped into the same spot and received the
   same track ID; it then goes through steps 3 to 5 again.

## The database of registered people

The registered people are kept in a file that `FeatureStore` (`src/store/`) manages, in one of **two formats that the user chooses**: **CSV** (the default: a header line `id,name,card_id,dim,values`, then one line per person with the face feature and the person's RF card number, empty when there is none) or **SQLite** (one table `people`); see `docs/store.md`. `AuthPipeline::init(profile, path, async_worker, db_format)` opens it with `FeatureStore::open`. With `DatabaseFormat::Auto` (the default of the parameter) an existing file is recognised by its **content** (a SQLite file starts with `SQLite format 3`), a new file by its **extension** (`.db`, `.sqlite`, `.sqlite3` = SQLite, anything else CSV); `Csv` or `Sqlite` is an explicit choice, and an explicit choice that does not fit an existing file is refused. A missing file is created at once. From then on:

* every registration, deletion and change is **written to the file before the call returns**, and **undone** (the call returns `false`) if the file cannot be written, so the memory and the file never disagree;
* a crash cannot damage the file: a CSV file is written completely under another name and then swapped in; SQLite changes one row in one transaction through its own journal;
* a lock file `<path>.lock` keeps a second program from opening the same database (Windows removes it when the program ends, even when it crashes);
* a damaged file is refused when it is opened (CSV: with the line number; SQLite: not a SQLite database, a foreign or newer table, a bad row).

`tools/db_convert <input> <output>` converts a database from one format to the other (and reads the result back to compare it). `tools/gen_features` and `test_pipeline --sqlite` use the format that the name or the option asks for.

**Why CSV is the default, and why SQLite is offered.** The whole database has to be in memory anyway: the matcher compares a camera face with every registered feature, so all of them sit in one flat table in RAM (`docs/match.md`). The file is only a safe place to keep that table between runs. CSV needs no library, can be read and edited in a text editor or a spreadsheet, and was the original requirement. SQLite's advantage is changing one row without rewriting the file. The **price of CSV** is that every change rewrites the whole file; measured by `test_match` (`test_database_speed`, 10,000 people with 512 numbers each): a 66.5 MB file that takes 1306 ms to write and 1023 ms to open, and about 1.3 s for one registration, deletion or modification. **SQLite** with the same people: a 39.4 MB file, 97 ms to write, 33 ms to open, and 3.0 to 3.5 ms for one change. The SQLite file is not smaller than the numbers alone because a feature of 512 numbers is a 2 KB blob that almost fills a 4 KB page. The CSV time for fewer people is an extrapolation by proportion (about 26 ms for 200 people), not a measurement.

**Card first: N is a subset.** Each record holds the face feature **and** the card number. The card information is always identified first: the card is looked up in the database (`FeatureStore::find_by_card`, `FeatureStore::index_of_card`), the feature stored with it is retrieved, and only then are the faces of the current frame compared with the retrieved features (`FeatureStore::match_cards`, `AuthPipeline::begin_card_check`). A card that is not in the database is a failed comparison.

## Two modes: synchronous or with a worker thread

Steps 3 and 4 (the pose check with its second detector run by the pose detector, the crop, the feature
extraction) are the expensive ones, and they are only needed about once a second per
unverified face. They are packaged as a **job** (a copy of the picture region around
the face, plus the face found by the detector) and run in one of two ways:

* **Synchronous** (default): inside `process()`. Every result is there when `process()`
  returns, which makes the tests deterministic. A frame that has such a job takes
  noticeably longer (on the development PC up to 85 ms, against 7 ms for a frame without).
* **Worker thread** (`AuthPipeline::init(profile, path, true)`): the job goes to a second
  thread that has its **own** pose detector and extractor (the main thread's objects are not
  thread safe; the worker never detects faces in whole frames, so with a SCRFD profile it
  holds only a YuNet network, not SCRFD). `process()` then only detects, tracks and matches, and picks up finished
  results at the start of the next frames. The price: recognition arrives a few frames
  later (a tenth of a second or less in the measurements), and the CPU runs one more
  thread. A counter that is raised by `reset_tracking()` makes the pipeline throw away
  results of tracks that no longer exist; `flush()` waits for the worker.

The target PC has 2 cores and no GPU, so the project is CPU-only (ncnn with Vulkan
switched off, the CPU build of ONNX Runtime) and the defaults give the detection 2 threads
and the worker 1 (`FA_WORKER_NETWORK_THREADS`); `RESULTS.md` shows the measurement that led
to those numbers.

## CPU only

**Every engine runs on the CPU** (the target PC has no GPU), and this is enforced and tested, not only intended. The neural networks go through the `Net` interface (`src/common/inference.h`): the ncnn backend sets `use_vulkan_compute = false` for every network, and the ONNX Runtime backend (the bundled 1.17.3 is the CPU build) appends no execution provider. The matcher is AVX2 code and the classical engines (Haar, LBP) are plain C++. Each layer can be asked: `Net::runs_on_cpu()`, `FaceDetector::runs_on_cpu()`, `FeatureExtractor::runs_on_cpu()` and `AuthPipeline::runs_on_cpu()` (which asks the detector, the pose detector, the extractor and the worker's own two). The tests assert it and also check from outside that no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded into the process (`tests/cpu_only_check.h`), and `eval_footage` and `check_video` print `all engines run on the CPU: yes (no GPU is used)`. From outside, `scripts\gpu_probe.ps1` read the Windows GPU counters while programs ran: the complete `test_pipeline` and `eval_footage` had no GPU engine instance at all. Two facts to know. First, the bundled ncnn library was built *with* its Vulkan code, which is unused because it is switched off in code (`docs/MODELS.md`). Second, Media Foundation is told not to use hardware (no DXVA, no hardware transforms: `docs/camera.md`), so video is decoded and encoded in software, but a program that **writes** an MP4 (`gen_video`, the annotated video of `check_video`) still has an idle Direct3D device while it encodes: GPU engine instances of the `3d` type at 0.0% utilization, never a video, compute or copy engine. Every engine runs on the CPU; the video writer opens that idle device. The measurement is coarse (one reading about every 3 s). How it is proven and measured: `docs/TESTING.md`, `docs/RESULTS.md`.

## Video files

`open_video_file_source()` plays an .mp4 through the same Media Foundation reader and RGB
conversion that the camera uses, and `VideoWriter` writes H.264 .mp4 files (both with hardware decoding and encoding switched off, so that it is done in software on the CPU). Together with
`gen_video` (makes a video of people walking toward the camera, with the true face
positions) and `check_video` (plays a video through the pipeline, compares with the truth,
writes an annotated video) this lets the whole system be checked without a camera. `gen_video` makes 1920x1080 video by default, the default camera size, and `scripts\make_fhd_footage.bat` makes the six videos of the 1920x1080 evaluation (1, 3 and 5 people, clean, and the same three with `gen_video --camera-look`, a simulated camera in a room); `eval_footage` plays them through the engines one by one (detection and tracking, feature extraction, matching), where `check_video` plays them through the whole system. The footage recorded by the inspection camera could not be found, so the "camera" footage is a **simulation made from the same flat portrait photos, not real camera footage**; real footage is still needed (`docs/RESULTS.md`, section 5d).

## Model profiles

`models/*.ini` select the models; no code changes are needed to switch.

| Profile | Detector | Recognizer | Licence |
|---|---|---|---|
| `permissive.ini` | YuNet (ONNX Runtime) | SFace 128 (ONNX Runtime) | MIT / Apache-2.0 |
| `dev_noncommercial.ini` | SCRFD (ncnn, `det_dyn.param`, fitted rectangle: `detector.layout = chw`) | ArcFace 512 (ncnn) | non-commercial |
| `dev_noncommercial_square.ini` | SCRFD (ncnn, `det.param`, 640x640 square: `flat` layout) | ArcFace 512 (ncnn) | non-commercial |
| `dev_noncommercial_onnx.ini` | SCRFD (ONNX Runtime) | ArcFace 512 (ONNX Runtime) | non-commercial |
| `classical.ini` | Haar cascade | LBP histograms 256 | BSD-style |

`dev_noncommercial_square.ini` is the SCRFD profile as it was before the fitted rectangle: the same models, the original
conversion and a 640x640 square. It is kept to compare the two (`run_tests.bat full` and `scripts\run_tests_vs2015.bat` run `eval_detect`, `bench` or `bench_detect`, and `eval_footage` with both) and as a
fallback; for a 16:9 camera picture it takes about 1.5 times as long (17.9 against 11.5 ms at 1920x1080) and gives the same detections.

The three `dev_noncommercial` profiles also contain `pose.detector_profile = models/permissive.ini`:
SCRFD finds the faces, but the head pose (the pose gate of step 3) is measured by YuNet, so
these profiles load the YuNet model (MIT) as well. `permissive.ini` and `classical.ini` name no
pose detector; their own detector measures the pose.

## Settings that can be changed

All numeric settings are macros in `src/common/config.h` (and the Haar speed
settings at the top of `detector_haar.cpp`). Change the file, or pass
`/DNAME=value` to the compiler: `set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f` before
running `build.bat`.

## Build and run

See `TESTING.md`.
