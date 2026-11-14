# Results: what was measured, what is met, what is not

Everything here was measured by the programs in this project (`run_tests.bat full`),
on **one development PC: 13th-generation Intel Core i7-13650HX, Windows 11, 2 CPU
threads per network**. That is **not** the target machine (Lenovo with a
4th-generation Core i5). Columns called *est. i5* are the measurement multiplied by
a guessed factor of 3.5 (`bench --i5-factor`); they are estimates and nothing more.
**Run `build\bin\bench.exe --threads 2 ...` on the target PC to get real numbers.**

The camera picture of the evaluation is **1920x1080** (Full HD, the default camera size): section 5c ("1080p evaluation") evaluates the engines on generated 1920x1080 footage made for the purpose, and section 5d repeats it on the same footage with a **simulated** camera look. **No real camera footage was used anywhere:** the footage recorded by the inspection camera could not be found on this PC, and no camera was switched on. Sections 2 to 5b keep the earlier measurements at 1280x1080 and at other picture sizes, where they say so.

**The SCRFD detector on ncnn (`dev_noncommercial.ini`) was made faster after sections 2 to 5d were measured** (section 5e: the network runs on a 640x384 rectangle for a 1920x1080 picture instead of a 640x640 square). Every SCRFD-on-ncnn figure of sections 2 to 5d (19.5 ms, est. i5 68 ms; 19.3 ms, est. i5 67.6 ms; and so on) is **from the square**, which is what `models\dev_noncommercial_square.ini` still does; the new figures are in section 5e (1920x1080: 11.5 ms, est. i5 40.2 ms). The old figures were left as they were measured and are marked "square" where they appear.

Test faces: 185 public-domain official portraits of 153 people (27 of them with two or
three different photos), from `data/portraits` (see `MODELS.md`). These are straight-on studio
photographs: an **easy** test. Real camera images will give lower scores.

## 1. Requirement checklist

| # | Requirement | Status | Evidence / remark |
|---|---|---|---|
| 1 | Detect and track faces, store face crops | **Met** | `test_pipeline`: one track ID for a whole walk, crops stored |
| 1.1 | Resize camera image to the size the face engine needs | **Met** | letterbox to 640x640 (or 320x320 for small YuNet frames), 2 ms for 1280x1080 (`test_common`); SCRFD on ncnn (`dev_noncommercial.ini`) uses `letterbox_fit`, the smallest multiple-of-32 rectangle, 640x384 for 1920x1080 (section 5e); works for any picture size (section 5b) |
| 1.2 | Rotation angles in real time; store the crop only inside the tolerances (pitch 5, yaw 5, roll 8 degrees, as macros) | **Met, with limits** | macros `FA_POSE_*_TOL_DEG`; roll accurate to about 1 degree, yaw/pitch only to about 5-6 degrees person to person (section 4) |
| 1.3 | A verified track is never stored again; an unverified one is stored once per second, overwriting the old crop | **Met** | `test_pipeline`: no crop after verification; crops at least 1 s apart; stranger gets about 1 crop per second |
| 1.4 | At most 60 ms (1280x1080) and 25 ms (640x640) on a 4th-generation i5 | **Measured on this PC: met. On the i5: unverified. Estimate: 60 ms met for YuNet, Haar, SCRFD on ONNX Runtime and, since the fitted rectangle, SCRFD on ncnn (with one exception to know about, below); 25 ms at 640x640 not met for SCRFD on either runtime** | section 2 and section 5e. The requirement names 1280x1080 and 640x640; the 60 ms limit is applied to 1920x1080 and 1280x720 by analogy. **SCRFD on ncnn, which used to be over 60 ms in the estimate** (square: 19.3 ms, est. i5 67.6 ms at 1920x1080, section 5c), **is now inside it at 1920x1080 (11.5 ms, est. i5 40.2 ms), at 1280x720 (est. 41.4 ms) and at 1280x1080 with one face (15.5 ms, est. 54.4 ms)**; at 1280x1080 with **three** faces the estimate is **59.4 ms in one run and 60.4 ms in another: on the limit, a pass or a fail by the run-to-run noise**. At **640x640** (limit 25 ms) the estimate is **70.5 ms, over**, because a square picture gets nothing from the fitted rectangle (the same for SCRFD on ONNX Runtime, est. 44 ms); YuNet (est. 7.5 ms with its 320 network) and Haar pass. All "est. i5" figures are the measurement times 3.5, not a measurement on the i5 (section 5e, "What is not done") |
| 1.5 | Detection and tracking with both a classical and a deep method, both within those times | **Met on this PC; classical has real limits** | YuNet, SCRFD (deep); Haar (classical). Haar cannot see faces below 96 px at 1280 wide |
| 1.6 | Several faces per frame | **Met** | 2 faces tracked with separate IDs (`test_pipeline`), 3 faces detected (`bench`) |
| 1.7 | Camera pictures of various sizes, default 1920x1080 | **Met on generated pictures and video files; no camera that offers 1920x1080 was tried** | the camera mode closest to the wish is switched on; detection, tracking and recognition tested at 640x480, 1280x720, 1920x1080, 1600x1200 and 2560x1440 (section 5b). **The engines were evaluated at 1920x1080 on three generated 1920x1080 videos (1, 3 and 5 people) with the exact truth (section 5c), and on the same three with a simulated camera look (section 5d; a simulation, not real camera footage)**: the deep detectors find 100% of the true faces with 0 false detections, every person keeps one track ID, in both sets |
| 2 | Extract features from the stored crops | **Met** | SFace 128, ArcFace 512, LBP 256 |
| 2.1 | 256-512 numbers, error about 1 in 10,000 | **Partly, see section 3** | ArcFace 512: 0 false accepts in 16,982 stranger pairs at threshold 0.40 (**non-commercial model**). SFace 128 is below the 256-512 range. LBP cannot reach it |
| 2.2 | Classical and deep extraction, at most 150 ms (ideally 50) | **Met on this PC; i5 estimated** | 7-12 ms deep, 1 ms classical on this PC; est. i5 24-44 ms deep |
| 2.3 | Registered features in a CSV file; database management in which the user can choose between SQLite and the default format (CSV) | **Met** | `feature_store.h`, `store.md`, `sqlite_db.h`. A **managed** database in either format: opened with a lock (a second program cannot open it), every register / delete / modify is written at once and undone if the write fails, a damaged file is refused. **CSV is the default** (header line `id,name,card_id,dim,values`, then one line per person; older files without header and card column are still read). **SQLite** (one table `people`; SQLite 3.54.1, public domain, compiled into the project) is chosen by the file name (`.db`, `.sqlite`, `.sqlite3`) or explicitly (`DatabaseFormat` in `FeatureStore::open`, `AuthPipeline::init`'s `db_format`, `test_pipeline --sqlite`); an existing file is recognised by its content, and an explicit choice that does not fit the file is refused; `tools/db_convert` converts a database between the two. 10,000 people x 512 numbers (`test_match`, run of 2026-11-12): CSV 66.5 MB, write 1306 ms, open 1023 ms, one change 1.27 to 1.29 s (the whole file is rewritten); SQLite 39.4 MB, write 97 ms, open 33 ms, one change 3.0 to 3.5 ms (one row). CSV stays the default (section 6) |
| 2.4 | Register, delete, modify | **Met** | `test_match` (store and managed database: write-through, undo, lock; the same checks for CSV and for SQLite), `test_pipeline` (also with `--sqlite`) |
| 3.1 | Fast comparison | **Met** | 10,000 registered, 512 numbers: 0.7 ms for one face, 4.1 ms for 16 faces (`bench`, 2026-11-12; est. i5 2.5 / 14.5 ms; `test_match` measured 0.71 and 3.59 ms) |
| 3.2 | M:N, not M separate searches | **Met** | all M faces of a frame against all N registered people in one call (M and N independent: 72 combinations tested, 30 of them with M larger than N, and M or N = 0); 3 to 10 times faster than M separate 1:N searches, identical results (`test_match`, run of 2026-11-12, 512 numbers, N = 10,000: 1 face 0.71 against 2.39 ms, 4 faces 1.47 against 9.42 ms, 16 faces 3.59 against 34.58 ms) |
| 3.3 | Card first: the card information is always identified first, the features retrieved for the cards are compared with the faces of the current frame; a card that is not in the database is a failed comparison; M:N with M < N, M = N and M > N | **Met** | `FeatureStore::match_cards` looks the cards up FIRST, retrieves their records and compares the M faces with exactly those N features in one call (`FeatureMatcher::score_matrix`); an unknown card is `found = false`, nothing is compared for it, the call returns `false`, and there is never a fallback to the whole database. `AuthPipeline::begin_card_check` / `end_card_check` do the same for the pipeline (`FrameResult::cards`). `test_match` (`test_card_first`, 47 checks: 25 combinations of M and N_s, M < N, M = N, M > N, an unknown card, a deleted owner's card) and `test_pipeline` video 6 (22 checks, nine walks: M = 1 / N = 1 the owner is verified; M = 1 / N = 1 a registered person with another person's card is **not** verified (best similarity to the owner 0.18, 0.01 with the dev profile); M = 1 / N = 1 an unknown card: failure, and the registered person is **not** verified; M = 2 / N = 1; M = 1 / N = 2; M = 2 / N = 2; M = 2 / N = 2 with one unknown card; back to the normal search after `end_card_check`; a deleted owner's card: failure). The owner with his own card was verified after 1.07 s (permissive, synchronous). The RF card reader itself is not part of the program: the card numbers are handed to `begin_card_check` |
| 4 | Each engine verifiable on its own, and the integration | **Met** | four separate projects (`face_detection.sln`, `feature_extraction.sln`, `feature_matching.sln`, `face_authentication.sln`), each with its own test, evaluation and benchmark: `test_detect_units` / `eval_detect` / `bench_detect`; `test_feature` / `eval_features` / `bench_extract`; `test_match` / `bench_match`; `test_pipeline` / `check_video` / `bench`. Built and tested one by one from an empty folder with the real Visual Studio 2015 (`docs/VISUAL_STUDIO.md`; that first run is of the version before the card-first, SQLite and 1920x1080 changes; later full runs with Visual Studio 2015 only, the latest on the SCRFD fitted-rectangle change (`build\run_vs2015_final4.log`, section 5e), are described there) |
| 4.1 | The authentication project itself verifiable | **Met** | `test_pipeline` on generated video, for the permissive and the dev profile (and with SQLite); `check_video` on generated MP4 files, including the three 1920x1080 videos and the three with the simulated camera look (section 5d) |
| 4.2 | Verification data generated internally: approaching-person video, 10,000 synthetic features | **Met, with a caveat** | `gen_features` (CSV or SQLite), `gen_video` (1920x1080 by default; `--camera-look` simulates a camera in a room); the six evaluation videos (three clean, three with the simulated camera look) are made by `scripts\make_fhd_footage.bat`. The video is built from downloaded public-domain portraits; flat photos can only be tilted in the image plane, not turned in 3-D. **The footage of the inspection camera could not be found, so the "camera" footage is a simulation, not real camera footage; real footage is still missing** (section 5d) |
| 5 | All engines in simple C/C++ | **Met** | C++14, no OpenCV; Python only downloads test images; the one outside library besides the networks is the public-domain SQLite C source for the optional database format. Four Visual Studio 2015 solutions exist (`face_detection.sln`, `feature_extraction.sln`, `feature_matching.sln`, `face_authentication.sln`); built and tested with the real Visual Studio Community 2015 Update 3 (compiler 19.00.24210): 8 clean builds (4 solutions, Debug and Release) without error or warning, and the tests pass from those programs (`docs/VISUAL_STUDIO.md`; the 8 clean builds are of the earlier version; the solutions now have ten libraries including `fa_sqlite` and were rebuilt in the later full runs, the latest on the SCRFD fitted-rectangle change: 10, 9, 8 and 17 projects, 0 failed, section 5e) |
| 6 | Markdown documentation of every function | **Met** | `docs/common.md`, `detect.md`, `feature.md`, `match.md`, `store.md`, `camera.md`, `pipeline.md`, `testdata.md`, `tools.md` |
| CPU | All engines run on the CPU (the target PC has no GPU). (Asked for separately: no earlier row of this list names it, so it has no number.) | **Met** | Every engine runs on the CPU, and this is enforced and tested: ncnn is told not to use Vulkan (`use_vulkan_compute = false`), ONNX Runtime 1.17.3 is the CPU build and gets no execution provider, the matcher is AVX2 code, Haar and LBP are plain C++. `Net`, `FaceDetector`, `FeatureExtractor` and `AuthPipeline` answer `runs_on_cpu()`; `test_common` (3 checks), `test_feature` (2) and `test_pipeline` (2) assert it and check that no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded into the process; measured from outside with the Windows GPU counters (`scripts\gpu_probe.ps1`), the complete `test_pipeline` process and `eval_footage` (which decodes an MP4) had no GPU engine instance at all, with 27 or 28 other processes visible as a control (section 2, "All engines on the CPU"). **One nuance:** the programs that write an MP4 file (`gen_video`, `check_video`'s annotated video) show GPU engine instances while the Microsoft software H.264 encoder runs: the `3d` engine at 0.0% utilization, no video engine; the video writer opens an idle Direct3D device, no engine does. **Limits:** the bundled ncnn library was built with its Vulkan code (unused because it is switched off in code); rebuilding it with `NCNN_VULKAN=OFF` would remove it, and was not done. The probes take one sample every 3 s, so a short GPU burst between samples could be missed |

## 2. Speed (this PC; "est. i5" = x3.5 guess)

**Timing noise.** This PC runs other programs (browser, editors) and uses the Balanced
power plan, so timings change between runs. The tables below are from a quiet run. A
second complete run from a clean rebuild, with about 36% background CPU load, gave the
same accuracy numbers (they are deterministic) but slower and less steady speed numbers,
for example SCRFD on ncnn 24 ms instead of 19.5 ms, Haar median 8 ms instead of 3.5 ms,
and individual `eval_detect` rows up to 2 times slower; YuNet at 1280x1080 stayed at
7.2 ms. Treat every millisecond figure as good to about a factor of 2, look at the p95
and max columns that `bench` prints, and measure on the target PC with nothing else running.
**A bigger effect on this PC: it has fast (P) and slow (E) cores, and Windows sometimes runs a test
on the slow ones** (a long test took a median of 18.8 ms per frame instead of 7.0 ms, and the
worker-thread limits of `test_pipeline --async` failed for that reason alone; the same test pinned to the
P cores gave 7.0 ms again). Run `start /affinity FFF` for repeatable numbers (`docs/TESTING.md`). The
target i5 has no such cores.

Detection, 1 face in the frame, median of 40-60 runs:

| Detector | 1280x1080 | est. i5 | 640x640 | est. i5 | Limits 60 / 25 ms |
|---|---|---|---|---|---|
| YuNet, ONNX Runtime (permissive) | 7.2 ms | 25 ms | 2.2 ms | 7.5 ms | met / met (estimated) |
| SCRFD, ONNX Runtime (dev) | 12.6 ms | 44 ms | 12.5 ms | 44 ms | met / **not met** (estimated) |
| SCRFD, ncnn, 640x640 square (dev; `dev_noncommercial_square.ini`; the earlier measurement) | 19.5 ms | 68 ms | 19.8 ms | 69 ms | **not met / not met** (estimated) |
| SCRFD, ncnn, fitted rectangle (dev; `dev_noncommercial.ini`; section 5e, a later run) | 15.5 ms (640x544) | 54 ms | 20.1 ms (square, no gain) | 70.5 ms | met for one face, on the limit for three faces / **not met** (estimated) |
| Haar cascade (classical) | 3.5 ms median, 9.6 ms worst | 12 / 34 ms | 2.8 / 10.4 ms | 10 / 36 ms | met / met (estimated) |

* The 640x640 YuNet number uses the 320x320 version of the network (`yunet_320.onnx`,
  used for camera images up to 640 pixels on the long side). On 640x640 test frames it
  found 100% of faces from 50 px up, the same as the full-size network.
* The Haar detector scans the whole image only every 8th frame and follows known
  faces in between, so its **median** is much lower than its **worst case** (a full
  scan, 8-10 ms here; the benchmark shows both). Faces smaller than 96 px (at 1280
  wide) are not seen at all.
* SCRFD on ONNX Runtime is 1.5 times faster than the same network on ncnn with the square (the earlier measurement). With the fitted rectangle the gap at 1920x1080 is small: 11.5 ms on ncnn (section 5e) against 10.0 ms on ONNX Runtime (section 5c, an earlier run, which was not repeated for the comparison).
* One thread instead of two: YuNet 10.9 ms, SCRFD/ONNX 19 ms, Haar unchanged
  (`bench --threads 1`).

Extraction (crop, align and network) per face, and matching:

| Extractor | this PC | est. i5 | Limit 150 ms (ideal 50) |
|---|---|---|---|
| SFace 128, ONNX Runtime | 12.4 ms | 44 ms | met (also ideal, estimated) |
| ArcFace 512, ncnn | 10.4 ms | 36 ms | met (also ideal, estimated) |
| ArcFace 512, ONNX Runtime | 6.8 ms | 24 ms | met (also ideal, estimated) |
| LBP 256 (classical) | 1.0 ms | 3.5 ms | met |

Matching against 10,000 registered features (median; `bench`, run of 2026-11-12, est. i5 for 512 numbers 2.5 / 3.6 / 14.5 ms):

| Feature length | 1 face | 4 faces | 16 faces |
|---|---|---|---|
| 128 | 0.1 ms | 0.2 ms | 0.9 ms |
| 256 | 0.1 ms | 0.5 ms | 1.6 ms |
| 512 | 0.7 ms | 1.0 ms | 4.1 ms |

### What the pipeline does per frame: synchronous mode and worker thread

The pose check (a second detection, twice when the head is tilted) and the extraction are
needed about once a second per unverified face. In the default **synchronous** mode they run
inside `process()`, in the frame where a face's crop is due: median per frame 7.3 ms
(permissive), but 62-90 ms in those frames. With the **worker thread**
(`AuthPipeline::init(..., true)`) they run on a second thread with its own networks and
`process()` only detects, tracks and matches.

Measured over all 557 frames of the four `test_pipeline` videos (permissive profile, 1280x1080, an earlier run;
the same at 1920x1080 is in section 5c; this PC, 20 logical processors free):

| | median | p95 | p99 | max |
|---|---|---|---|---|
| synchronous | 7.3 ms | 32.7 | 51.3 | 84.8 |
| **worker thread** | 7.4 ms | 10.5 | 12.6 | **17.7** |

With the worker thread, recognition arrives one to two frames later than synchronous (for
example 1.80 s instead of 1.73 s after the person is first seen); the worker spends 35-38 ms
per result off the main thread (1 thread). `test_pipeline --async` checks that all the same things
still happen with the worker, plus that no frame takes 60 ms or more and that the worker can be
reset and shut down while it is busy.

**The target PC has 2 cores and no GPU.** The project is CPU-only (ncnn with Vulkan switched
off in code; ONNX Runtime without any GPU provider; enforced and tested, see "All engines on the
CPU" at the end of this section). To see what two cores do, I pinned
`check_video` to two physical cores of this PC (the cores themselves are far faster than a
4th-generation i5's, so the absolute numbers are optimistic; this table is from the earlier 1280x1080 footage):

| generated MP4, 3 people, 120 frames | median | p95 | p99 | max | recognition |
|---|---|---|---|---|---|
| YuNet + SFace, synchronous, 2 cores | 9.0 ms | 57.8 | 75.2 | 94.9 | 1.73 s / 0.80 s |
| YuNet + SFace, **worker**, 2 cores | 7.4 ms | 25.3 | 36.8 | **41.9** | 1.80 s / 0.93 s |
| same, 1 thread per network instead of 2 | 12.2 ms | 32.6 | 44.4 | 44.8 | 1.80 s / 0.93 s |
| SCRFD + ArcFace (ncnn), worker, 2 cores, SCRFD also measured the pose (before the change of section 4) | 37.5 ms | 88.6 | 132.4 | 135.9 | one person **never** verified |
| SCRFD + ArcFace (ncnn), worker, 2 cores, **YuNet measures the pose** (now) | 19.9 ms | 51.1 | 82.9 | **90.6** | 1.53 s / 1.13 s, both recognized |

So on two cores the worker thread still more than halves the worst frame (95 ms to 42 ms), 2
threads for the detection is better than 1, and **the SCRFD / ncnn profile still does not fit two
cores**: with YuNet doing the pose check (section 4) its median halves (37.5 to 19.9 ms) and both
registered people are recognized, but the worst frame is 90.6 ms, over the 60 ms limit (the
SCRFD detection alone takes about 20 ms per frame here, three times YuNet's 7 ms, and the two networks
fight for the two cores). The permissive YuNet profile is the realistic choice for the target.
A pose check now costs 8 ms instead of 20 ms in the SCRFD profile (`bench`, 2 threads).
**These two-core rows were measured with the 640x640 square.** Since then the SCRFD detection takes about 11 ms per frame at 1920x1080 instead of about 18 ms (section 5e), which can only help, but the two-core `check_video` run was **not repeated** with the fitted rectangle, so whether the SCRFD / ncnn profile now fits two cores is **not known**; the sentence above is the old result.

### A real video file, checked end to end

`gen_video` writes a 120-frame, 1920x1080 (the default), 8 s MP4 (H.264) of three people walking toward the
camera (two registered, one stranger) and `check_video` plays it through the pipeline with the
Media Foundation file reader and compares with the true face positions (the run of 2026-11-12, `fhd_3people`):

* Orientation: the first frame read back from the MP4 differs from the original by 2.4 (0-255
  scale) and from the original turned upside down by 27; so frames arrive the right way up.
  This is the same reader and RGB conversion the camera uses, so it also supports the camera
  path; a live capture is still to be confirmed (`camera_check --capture 5`).
* Result (both synchronous and worker mode, YuNet + SFace): all three people detected in 100% of
  the frames, one track ID each, both registered people recognized as the right person (after
  1.60 s and 1.33 s in synchronous mode, 1.67 s and 1.40 s with the worker thread), the stranger never
  verified, 0 false detections in 333, and in worker mode no frame above 17.4 ms. The other two
  videos and the details are in section 5c.
* The annotated MP4 (`annotated_sync.mp4`, `annotated_worker.mp4`) shows green boxes with name
  and score for verified people, yellow for the others, the head rotation and the frame time;
  I looked at a frame read back from the file and the picture is right way up and complete.
* A person whose natural head pose is near the +-5 degree limit is recognized late or never:
  the first demo video contained such a person (recognized 6.9 s after appearing, which the check
  reported as a failure), which is the pose-gate strictness of section 4 and why `gen_video` now
  picks people with a comfortable pose margin by default (`--any-pose` includes everybody).

### All engines on the CPU

**The requirement:** all engines must run on the CPU (the target PC has no GPU). **Status: met for every engine, enforced in the code and proven by tests and by a measurement.** One incidental finding about the programs that write an MP4 file is stated below (the video writer opens an idle Direct3D device while it encodes), with the limits of the measurement. Runs of 2026-11-12 on the development PC, which does have GPUs (an NVIDIA GeForce RTX 4060 Laptop GPU and an Intel UHD Graphics), so a GPU use would have been possible and visible.

**What is true of each engine:**

| Engine | Runs on | Remark |
|---|---|---|
| ncnn networks (SCRFD and ArcFace of `dev_noncommercial.ini`) | CPU | `use_vulkan_compute = false` for every network. **The bundled ncnn library was built with its Vulkan code** (`platform.h` has `NCNN_VULKAN 1`, which is why the glslang libraries are linked), but because the option is off no Vulkan instance is created and no GPU is opened. Rebuilding ncnn with `NCNN_VULKAN=OFF` would remove the code path altogether; **not done** |
| ONNX Runtime networks (YuNet, SFace, and SCRFD / ArcFace of `dev_noncommercial_onnx.ini`) | CPU | the bundled 1.17.3 is the CPU build; providers offered: `CPUExecutionProvider` and `AzureExecutionProvider`. The Azure provider is no GPU: it sends data to a remote service and works only if a program appends it to a session; nothing here does. No execution provider is appended |
| Haar detector, LBP extractor | CPU | plain C++ |
| Matcher | CPU | AVX2 code |
| Media Foundation, reading (`CameraSource`: video files and the camera) | CPU | the source reader is created with `MF_SOURCE_READER_DISABLE_DXVA = TRUE` (no decoding through the graphics card) and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (no hardware decoder or video processor), besides `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = TRUE`; no Direct3D device manager is handed over. Measured: `eval_footage`, which decodes an MP4 with this reader, had no GPU engine instance (table below). The camera path is the same code, but no test opens a camera |
| Media Foundation, writing (`VideoWriter`: the MP4 files of `gen_video`, `check_video` and `test_common`) | CPU (the Microsoft software H.264 encoder); **but an idle Direct3D device is open while it encodes** | the sink writer is created with `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE`, so no hardware (GPU) encoder and no GPU video processor is used; the module list of a running `gen_video` shows `mfh264enc.dll`, the software encoder. The programs that write an MP4 still show GPU engine instances in the Windows counters: the `3d` engine at 0.0% utilization, and no video engine (table and discussion below) |

**How it is proven:**

1. The engines answer `runs_on_cpu()` (`Net`, `FaceDetector`, `FeatureExtractor`, `AuthPipeline`; the last asks the detector, the pose detector, the extractor and the worker's own two). `eval_footage` and `check_video` print `all engines run on the CPU: yes (no GPU is used)`.
2. The tests look at the process: after the engines have run, **no GPU library** (`vulkan-1`, `d3d12`, `nvcuda`, `cudart`, `cublas`, `cudnn`, `nvinfer`, `opencl`, `directml`, `onnxruntime_providers_cuda` / `_tensorrt` / `_dml` / `_openvino`) is loaded (`tests\cpu_only_check.h`). `test_common` also checks that the ONNX Runtime library offers no provider other than the CPU one and the Azure one. Passed (this list is of the run before the fitted SCRFD rectangle; `test_common` has 74 checks and the dev `test_pipeline` 70 (synchronous) or 75 (`--async`) since, with the new groups of `docs/tools.md`; the CPU checks are unchanged): `test_common` (66 checks, 3 of them for this; `build\test_common_cpu.log`), `test_feature` for the four profiles (14, 14, 14 and 12 checks, 2 of them for this; `build\test_feature_cpu_*.log`), `test_pipeline` permissive, synchronous (67 checks, 0 failed, 2 of them for this; GPU libraries loaded in the process: none) and `test_pipeline` dev with the worker thread (70 of 72 checks passed; the two that failed are the worker-thread **timing** checks, see the caveat below; both CPU checks passed). In `test_common`, which writes and reads an MP4, `d3d11.dll` and `dxgi.dll` are loaded (Windows components that come with the MP4 path of Media Foundation, see item 3; they are not on the forbidden list and are only reported); in `test_pipeline`, which writes and reads no video file, none of them is.
3. **Independent measurement** with `scripts\gpu_probe.ps1` (how to run it and how to read its output: `docs/TESTING.md`, "Proving that everything runs on the CPU"; the script itself: `docs/tools.md`). Windows keeps a performance counter per process and GPU engine, `\GPU Engine(*)\Utilization Percentage`. A process that uses a GPU has instances there whose names hold its process id and an engine type (`3d`, `copy`, `videodecode`, `videoencode`, ...); a process that uses none has no instance at all. The script starts a program, reads the counters back to back until the program ends (one reading takes about 3 s: 237 s with 80 samples is one reading per 3.0 s, and the short programs below ran 7 to 12 s and got 2 to 4 samples), and reports how many GPU engine instances the program had, the highest utilization seen, the engine types seen, and, as a **control**, how many other processes had GPU instances in the same samples (27 or 28 on this PC in every run, so the counters work). Results of 2026-11-12 (`build\gpu_probe_summary.log`):

| Program | Ran | Exit code | Samples | GPU engine instances of the program | Highest utilization | Engine types seen | Control: other processes with GPU instances |
|---|---|---|---|---|---|---|---|
| `test_pipeline models\permissive.ini` (the whole system, synchronous; writes no video file) | 237 s | 0 | 80 | **0** | 0.0% | none | 28 |
| `eval_footage models\dev_noncommercial.ini data\videos\fhd_cam_5people` (SCRFD and ArcFace on ncnn; decodes an MP4 with the source reader) | 12 s | 0 | 4 | **0** | 0.0% | none | 27 |
| `gen_video build\gpu_probe_gen 8 15 1 0 1` (writes an MP4) | 7 s | 0 | 2 | **21** | 0.0% | `3d` only | 27 |
| `check_video models\permissive.ini` with `--async` (writes the annotated MP4) | 12 s | 0 | 4 | **21** | 0.0% | `3d` only | 27 |
| `check_video models\dev_noncommercial.ini`, synchronous (writes the annotated MP4) | 8 s | 0 | 2 | **21** | 0.0% | `3d` only | 27 |

All five runs ended with exit code 0 (no failed check of the probed program). `test_match` was not measured. **Earlier archive.** `build\gpu_probe_summary_earlier.log` holds an earlier set of probes, made with an earlier version of the script, and runs with a throw-away sampler came before that; they gave the same result: the complete `test_pipeline` (permissive profile on ONNX Runtime, synchronous: 231 s with 83 samples in the archive, 230 s with 83 samples before; dev profile, SCRFD on ncnn, with the worker thread: 235 s, 85 samples) had no GPU engine instance, with 27 other processes visible in the same samples; `eval_footage` with the permissive profile (12 s, 4 samples) and two runs of 6 seconds (permissive and dev profile) had none either; `gen_video` and `check_video` (permissive and dev profile) had 21 instances each; and `test_common`, which writes a small MP4, had none in a run of 3 s with a single sample, which is too short to prove much (writing its 20-frame MP4 may have fallen between two samples). The programs' own output is in `build\gpu_probe_*.log`; the samples themselves are not kept.

**The programs that write an MP4 show an idle Direct3D device.** `gen_video` and `check_video` (the annotated video) had 21 GPU engine instances, all of the `3d` engine type, at 0.0% utilization in every sample. No `videoencode`, `videoprocessing`, `videodecode`, `compute` or `copy` instance was ever seen, so nothing was computed on a GPU as far as the samples show. This was the same before and after the sink writer was given `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE`. While `gen_video` ran, its process had `d3d11.dll`, `dxgi.dll`, `dxcore.dll` and `d3d10warp.dll` (the Windows software rasterizer) loaded, besides `mfh264enc.dll` (the Microsoft **software** H.264 encoder) and `mfcore.dll`; no vendor driver library (name patterns `igd`, `nvwgf`, `nvd3d`, `amdxx`, `atidxx`), no Vulkan, CUDA or OpenCL library was in the module list. So the encoder itself is the software encoder, and the instances are most likely those of a `3d` device that Media Foundation opens for its video path (an inference from the module list and from the elimination below, not an observation of the call that opens it): **every ENGINE runs on the CPU (proven); the video WRITER opens an idle Direct3D device (3D engine, 0.0% utilization, no video engine used) while it encodes.** This is incidental to writing a video file (`gen_video`, the annotated video of `check_video`, the MP4 test of `test_common`); it was not seen in the detection, extraction or matching engines (the complete `test_pipeline`, which runs all three, had no instance; `test_match` alone was not probed), nor in a program that only reads video (`eval_footage`).

**How the cause was narrowed down.** A throw-away program (`gpu_iso`, not kept in the project) ran each piece in its own process, waited 5 seconds after the work and was sampled in that time (3 samples each; the lines are in `build\gpu_probe_summary_earlier.log`). Saving a 1920x1080 JPEG once and 120 times (WIC), saving a PNG, drawing text with GDI (`draw_text`), the ONNX Runtime detector (YuNet), the ncnn detector (SCRFD), decoding an MP4 alone, and encoding an MP4 and then decoding it in one process: **all had 0 instances.** Because these were sampled after the work had finished, they could not see a device that exists only during the encoding, so they show that none of these pieces leaves a GPU instance behind; that the instances belong to the running MP4 encoder follows from the elimination (every piece but the encoding is clean, and `gen_video` and `check_video`, which encode, show them while `eval_footage`, which only decodes, does not), not from a direct sample taken during encoding in isolation.

**Not done:** removing even this idle device. It would mean converting the pictures to NV12 on the CPU before they reach the encoder (not tried); it was not done because it changes the colour handling of the footage.

**What is not proven.**
* The probes are coarse. The script reads the counters back to back and one reading takes about 3 s (237 s with 80 samples is one reading per 3.0 s), so the short programs ran 7 to 12 s and got only 2 to 4 samples (`gen_video` 2, `check_video` 2 and 4, `eval_footage` 4). A short burst on a GPU between two samples could be missed; 0.0% is what the samples saw.
* The `3d` instances at 0.0% show that a Direct3D device exists in the process, not that nothing was ever submitted to it.
* `test_match` and the camera were not measured (no test opens a camera). The measurements are from the development PC, which has two GPUs; they say nothing about the target PC, which has none.

**Caveat of the measurement.** The sampler runs next to the test and uses CPU itself. In the early dev run (235 s, 85 samples, made with the throw-away sampler) the worker-thread timing checks failed (`99% of the frames take less than 40 ms`, `no frame takes 60 ms or more`; median 40.3 ms, 99th percentile 59.1 ms, slowest frame 135.9 ms) because of that load. This is **not a regression**: without the sampler the check passes when the PC is quiet (two later reruns: 99th percentile 23.2 and 23.6 ms, slowest frame 40.0 and 36.3 ms; and it passed in the earlier Visual Studio 2015 run), and it failed on the last commit before this work too during a noisy period (`docs/TESTING.md`). **What is not shown:** no test opens a camera, so whether the driver of a real camera uses a GPU for a live stream is not tested; the source reader of the camera is created with DXVA and hardware transforms switched off, like the one for video files, and the target PC has no GPU. Do not run the probe while measuring time.

## 3. Feature accuracy (`eval_features`, 153 people, 185 photos)

"Genuine" = two different photos of the same person (38 pairs). "Impostor" = photos of
different people (16,982 pairs). The profile thresholds were chosen on this same data,
so these are optimistic numbers.

| Extractor | genuine score (min / mean) | impostor score (mean / p99.9 / max) | threshold | false accepts / 16,982 pairs | false rejects / 38 |
|---|---|---|---|---|---|
| ArcFace 512 (non-commercial; crops aligned with YuNet landmarks, see section 4) | 0.511 / 0.809 | 0.016 / 0.243 / 0.327 | 0.40 | 0 | 0 |
| SFace 128 (permissive) | 0.654 / 0.844 | 0.116 / 0.379 / 0.498 | 0.55 | 0 | 0 |
| SFace 128 at the authors' threshold | | | 0.363 | 34 (0.20%) | 0 |
| LBP 256 (classical; Haar found a face in only 145 of the 185 photos: 29 genuine, 10,411 impostor pairs) | -0.11 / 0.26 | 0.008 / 0.299 / 0.431 | 0.30 | 10 (0.10%) | 18 of 29 (62%) |

**Why the SFace threshold is 0.55.** On the earlier set of 116 people the worst stranger pair
scored 0.439 and 0.50 looked safe. With 153 people the worst stranger pair (`mike_levin` and
`salud_carbajal`, two different people) scores 0.498, only 0.002 below 0.50, so the threshold
moved to 0.55: 0.05 above the worst stranger and 0.10 below the weakest same-person pair
(0.654). This is what more data does: the more people, the higher the worst stranger pair
gets. Real camera images will lower the same-person scores, so 0.55 can reject real people;
calibrate on your own camera.

Searching **among registered people** is harder than comparing two photos: with 153
people registered and each stranger searched against 152 others,

* SFace at 0.363 accepts **26.8%** of strangers (41 of 153), at 0.45 2.6% (4), at 0.50 or
  above none.
* ArcFace accepts none at 0.363 and above.
* LBP: rank-1 identification right for only 13 of 25 probes, and at its threshold of 0.50
  only 3 of 25 probes are right and above the threshold; a threshold that accepts no
  stranger (0.431) rejects 76% of genuine pairs.

What this does and does not show:

* **ArcFace 512 still has the larger gap** between same-person scores (at least 0.51) and
  stranger scores (at most 0.33), 0.18, than SFace 128 (0.65 versus 0.50), 0.16, but the lead is
  small now. It was 0.30 (0.60 versus 0.30) while SCRFD's own landmarks aligned the ArcFace
  crops: letting YuNet measure the pose (section 4) also moved the alignment to YuNet's
  landmarks, which ArcFace likes slightly less. Random features show
  the trend of length: the spread of stranger similarity is 1 / sqrt(length), so 512 numbers
  need a lower threshold for the same false accept rate (`test_match`).
* **A false accept rate of 1 in 10,000 cannot be proven with 16,982 pairs.** Zero
  accepts in 16,982 pairs only says the rate is probably below about 2 in 10,000 (95%
  confidence, treating the pairs as independent, which they are not quite). It would take
  about 100,000 pairs, which means at least 450 different people.
* **The data is easy.** Official portraits are evenly lit and frontal. On a real camera
  (distance, blur, lighting, expression) the same-person scores fall and a fixed threshold
  will reject more genuine people. Calibrate the threshold on your own camera images.
* The classical LBP extractor is **not** usable for authentication at this accuracy level.
  The 1-in-10,000 requirement can only be met by a deep model. In the integrated test
  with the classical profile (Haar + LBP) the pose gate and the crop rule worked, but the
  registered person was never verified, even though the actor is the very photo that was
  registered: LBP features do not survive the change of scale, tilt and blending in the
  generated video. This is why `run_tests.bat` runs `test_pipeline` for the permissive and
  the dev profile only.
* The 512-number result comes from a **non-commercial** model (`MODELS.md`). The
  permissive profile has 128 numbers, outside the required 256-512.

## 4. Head pose (pitch, yaw, roll)

* **Exactness of the maths** (`test_detect_units`): given landmarks of a face with known
  rotation, the three angles are recovered to within 0.0 degrees one axis at a time and
  0.6 degrees with all three combined.
* **Roll** from detected faces: with the "second look" (`refine_face`: enlarge the face,
  turn it upright by the measured tilt, detect again) the measured roll followed the true
  tilt of a generated face within about 1 degree for tilts of -30..+30 degrees and faces of
  120-400 px (`roll_probe`). Without it the network under-reported the tilt (a true 30
  degrees came out as 3.7 degrees for a 120 px face), which let tilted heads through the
  gate. The pipeline test now stores its first crop exactly at the 8 degree limit.
* **Yaw and pitch** from 5 landmarks are less exact. The angle depends on how a person's
  nose and mouth sit relative to the eyes, which differs between people. On the 185
  frontal portraits (refined landmarks, YuNet) the sideways nose offset has a standard
  deviation of 0.048 eye distances, which is about 6 degrees of yaw, and the nose height
  0.024 (about 3.6 degrees of pitch). Part of that is real head turn in the photos, but the
  `+-5 degree` yaw gate can only be enforced to about +-5-6 degrees person to person. 106
  of the 185 straight-on portraits (57%) pass the gate with YuNet. A dedicated head-pose
  network would be needed to tighten this.
* **More calibration data did not change the constants.** The `PoseModel` constants were
  first measured on 135 portraits and measured again on 185 (153 people). Every mean moved by
  0.004 eye distances or less (YuNet refined: nose 0.566, mouth 0.983, sideways -0.013 against
  0.567, 0.986, -0.017 in `pose.cpp`; SCRFD refined: 0.607, 0.976, -0.032 against 0.607,
  0.976, -0.036), so they were kept. The mean was never the problem; **the spread is**:

  | landmarks used by the gate | pitch spread (std) | yaw spread (std) | straight-on faces accepted |
  |---|---|---|---|
  | YuNet, refined | 3.6 deg | 6.1 deg | 106 of 185 (57%) |
  | SCRFD, refined | 8.9 deg | 6.8 deg | 51 of 185 (28%) |

  SCRFD's pitch estimate is far noisier (nose height std 0.065 eye distances against 0.024
  for YuNet), so only about a quarter of the straight-on portraits pass. Collecting still
  more photos cannot fix that.
* **The SCRFD profiles now let YuNet measure the pose** (`pose.detector_profile =
  models/permissive.ini` in `dev_noncommercial.ini`, `dev_noncommercial_square.ini` and `dev_noncommercial_onnx.ini`; SCRFD
  still finds the faces; the calibration below was made before the fitted rectangle; the pose is still measured by YuNet, and on the four footages of section 5e the number of faces through the pose gate differs by 0 to 3 between the fitted rectangle and the square; `calibrate_pose` was not run again). Measured with `calibrate_pose <profile> --refine` on the same 185
  portraits:

  | profile | pose measured by | pitch spread | yaw spread | straight-on faces accepted |
  |---|---|---|---|---|
  | dev, ncnn | SCRFD (before) | 8.9 deg | 6.8 deg | 51 of 185 (28%) |
  | dev, ncnn | **YuNet** (now) | 3.5 deg | 6.2 deg | **103 of 185 (56%)** |
  | dev, ONNX Runtime | **YuNet** (now) | | | **106 of 185 (57%)** |

  The acceptance doubled, equal to the permissive profile's. What it cost:
  * **ArcFace accuracy dropped a little**, because the crop is aligned with the landmarks of the
    pose check, which are now YuNet's: the lowest same-person score fell from 0.596 to 0.511 and the
    highest stranger score rose from 0.300 to 0.327 (section 3). The threshold 0.40 still separates
    them with no error on these 185 photos, but the margin on the same-person side shrank from 0.20 to
    0.11. (Aligning with SCRFD's landmarks would need a second refinement by SCRFD, 20 ms more per
    pose check in the worker; not done.)
  * The pose check is faster (8 ms against 20 ms in the SCRFD/ncnn profile), the worker thread loads one
    YuNet instance instead of an SCRFD instance, and the dev profiles now need the YuNet model as well.
  * **In the video check** (`check_video`, two pinned cores) the SCRFD profile now recognizes both registered people
    (1.53 s and 1.13 s after they appear) where it failed to recognize one before (section 2).
  * The pose constants for this combination are the `yunet-refined` ones (`pose.cpp`); the printed
    means for the dev profile are nose 0.568, mouth 0.987, sideways -0.013 against 0.567, 0.986, -0.017.
  * To go back to SCRFD for the pose check, delete the `pose.detector_profile` line of the profile.
* **Noise sensitivity**: 0.5 px of landmark error gives 3.2 degrees rms yaw error for a
  face with 30 px eye distance, 1.5 degrees at 60 px and 0.8 degrees at 120 px. Small faces
  have unreliable pose; the pipeline therefore ignores faces narrower than
  `FA_MIN_CROP_FACE_PIXELS` (80).
* **Classical (Haar) path**: roll is exact (from the eye line); yaw and pitch are rough
  estimates from where the eyes sit in the face box, and about 70% of the portraits were
  outside the gate (60 of 84 in the latest run). They are documented in `detector_haar.cpp`.
* **In-plane roll is the only rotation the generated video can contain.** Yaw/pitch gating
  is verified with synthetic landmarks (`test_detect_units`) and the portraits' natural
  pose, not with turned heads.

## 5. Detection accuracy (`eval_detect --frame 1280x1080`, generated 1280x1080 frames, 20-24 portraits)

Found = box overlap (IoU) at least 0.5 with the true face. Face width is the face box
width in the 1280-pixel-wide frame. (The default frame of `eval_detect` is now 1920x1080: its table is in section 5c.)

| Face width | YuNet | SCRFD (ncnn / ONNX) | Haar (classical) |
|---|---|---|---|
| 50 px | 100% (IoU 0.90) | 100% (0.94) | **0%** |
| 80 px | 100% | 100% | 95% (IoU 0.72) |
| 110 px | 100% | 100% | 95% |
| 160-400 px | 100% (0.93) | 100% (0.93) | 100% (0.68-0.74) |

False detections per frame: 0.00 for the deep detectors; up to 0.08 for Haar at large
sizes. The Haar boxes are drawn differently from the true face box (IoU about 0.7).

## 5b. Camera picture sizes (the default is 1920x1080)

The default size that `open_camera_source` asks a camera for is **1920x1080** (`FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT`).
A camera does not have to offer it: the code asks the camera for its modes, switches on the one that is closest to the wish
(`pick_camera_mode`: no H264, no mode below 15 frames per second if there is a faster one, then the smallest relative
difference in width and height) and the whole system works with the size that arrives.

* **The camera of the development PC** ("Integrated Camera") offers 16 formats; the largest is 1280x720 (NV12 at 30 fps; as YUY2 only
  10 fps). For the default request the chosen mode is 1280x720, NV12, 30 fps, and the frames are 1280x720 (checked with
  `camera_check --modes` and `--capture 5`; the picture was deleted unseen, so its orientation is still unchecked).
  **No camera that offers 1920x1080 was available**, so that path is tested with generated pictures and an MP4 file of 1920x1080
  read through the same Media Foundation reader and conversion that a camera uses.
* **Detection accuracy at 1920x1080** (`eval_detect --frame 1920x1080`, found at face width in the 1920-pixel-wide frame):

  | Face width | YuNet | SCRFD (ncnn, square) | Haar before the change | Haar now |
  |---|---|---|---|---|
  | 50 px | 100% | 90% | 0% | 0% |
  | 80 px | 100% | 100% | **0%** | **90%** |
  | 110 px | 100% | 100% | 85% | 90% |
  | 160 px | 100% | 100% | 90% | 100% |
  | 250-400 px | 100% | 100% | 100% | 100% |

  The classical detector shrank every picture to 320 px wide, so its smallest visible face grew with the picture (144 px at 1920 wide
  instead of 96 px). Now the working width grows with the picture (480 px at 1920 wide, never more: `FA_HAAR_MAX_WORK_WIDTH`), which
  brings 1920x1080 back to the 1280x1080 level; at 1280x1080 nothing changed.
* **Detection time** (`bench_detect`, Visual Studio 2015 build, median, this PC; "est. i5" is the usual x3.5 guess; the 60 ms limit of the
  requirement names 1280x1080 and is applied to these two sizes by analogy):

  | Detector | 1920x1080 | est. i5 | 1280x720 | est. i5 |
  |---|---|---|---|---|
  | YuNet, ONNX Runtime | 7.6 ms | 26.5 ms | 7.1 ms | 24.9 ms |
  | SCRFD, ncnn, 640x640 square (as measured then; with the fitted rectangle see section 5e: 11.5 ms, est. 40.2, and 11.8 ms, est. 41.4) | 18.7 ms | 65.6 ms (**over 60**) | 18.3 ms | 64.2 ms (**over 60**) |
  | SCRFD, ONNX Runtime | 11.8 ms | 41.4 ms | 11.2 ms | 39.3 ms |
  | Haar (classical) | 8.5 ms | 29.9 ms | 2.9 ms | 10.3 ms |

  The picture size hardly matters for the deep detectors, because every picture is shrunk to the same 640 px network input; the extra
  cost is a larger resize (and, for Haar, the wider working image). (With the fitted rectangle of section 5e this is no longer true for
  SCRFD on ncnn: the shape of the picture decides the size of the network input, 640x384 for 16:9, 640x544 for 1280x1080, 640x640 for a square.)
* **Whole system, generated walk** (`test_pipeline` video 5, the same registered person at five sizes, both deep profiles): at 640x480,
  1280x720, 1920x1080, 1600x1200 and 2560x1440 the face was detected in **90 of 90 frames**, kept **one track ID** and was verified as the
  right person after **1.3 to 1.8 s**. A generated MP4 of 1920x1080 and one of 1280x720 played through `check_video`: both registered people
  recognized (1920x1080: 1.73 s and 0.93 s synchronous, 1.80 s and 1.00 s with the worker thread; 1280x720: 1.93 s and 0.93 s),
  the stranger never verified, no false detection, the MP4 read back at the right size and the right way up (an earlier run with other people; section 5c has the three 1920x1080 videos of the evaluation).

What the picture size changes, and what it does not:

* **Small faces in big pictures.** Every picture is shrunk to the 640-pixel network input, so a face of width w in a picture whose longer side
  is L is w x 640 / L pixels wide there. Measured with `eval_detect`: at 1920x1080 YuNet finds 100% of faces from 50 px (17 px in the network
  input) and SCRFD from 80 px (90% at 50 px). At **3840x2160** YuNet finds 100% from **80 px** (13 px in the network input; 15% at 50 px), SCRFD only
  from **110 px** (40% at 80 px) and the classical Haar detector from **160 px** (90%; 0% at 110 px). So the 80 px that a stored crop needs are still found
  by YuNet at 4K, but not by SCRFD or Haar. Detection time at 4K is about the same (YuNet 6.3 ms, SCRFD 19 ms, Haar 8 ms median; detection only).
  A 4K camera should still be asked for 1920x1080, which is the default. (The 4K figures for SCRFD are from the 640x640 square and were not
  repeated with the fitted rectangle of section 5e, which has the same scale, so the same face sizes in the network input; the SCRFD time at 4K
  would be about the 1920x1080 time of section 5e, which is a calculation, not a measurement.)
* **The 80-pixel minimum for a stored crop is in camera pixels** (`FA_MIN_CROP_FACE_PIXELS`), so a smaller picture means closer people: at
  640 wide a person must come twice as close as at 1280 wide.
* **The classical detector** sees faces from about 96 px up to 1920 wide; above that the working width is capped to bound the time, so its smallest
  face grows (calculated: 128 px at 2560 wide; measured at 3840 wide: 160 px found 90%, 110 px 0%).
* Small camera pictures are not enlarged (it would add no information).

## 5c. 1080p evaluation (the engines at a camera picture of 1920x1080)

**What was asked.** Evaluate the engines assuming a camera image resolution of 1920x1080, using video footage of 1920x1080 for the evaluation, and create the footage used for the testing. **What was done.** The footage was generated (no camera, nobody filmed; the pictures are made from the public-domain portraits), and every engine was run on it, one by one (`eval_footage`) and together (`check_video`). All numbers in this section are from runs of the programs of `run_tests.bat full` on the development PC on 2026-11-12 (Windows 11, Core i7-13650HX, 2 CPU threads per network; the classical `eval_footage` rows are from separate runs of the final tool, and the database speed is from a run of `test_match` alone); the "est. i5" columns are **extrapolations** from this i7 (the measurement times 3.5, `tools/bench_common.h`), not measurements.

### The footage

`scripts\make_fhd_footage.bat` makes six videos of people walking toward the camera (8 s, 15 frames per second, 120 frames each, 1920x1080, `video.mp4` as H.264 plus the same pictures as JPEG, `truth.csv` with the exact face box of every person in every frame, `scenario.txt` with who is registered). **This section uses the first three, the clean footage** (`fhd_1person`, `fhd_3people`, `fhd_5people`); the other three, the same people and walks with a simulated camera look (`fhd_cam_*`), are the subject of section 5d. The folder `data\videos\` is ignored by git; the footage is recreated by the script.

| Footage | People | Registered | Face width at the end | True face boxes in all frames | MP4 read back: difference to the original / turned upside down |
|---|---|---|---|---|---|
| `fhd_1person` | 1 | 1 | 340 px | 120 | 2.35 / 27.21 |
| `fhd_3people` | 3 | 2 | 328 px | 333 | 2.39 / 27.34 |
| `fhd_5people` | 5 | 3 | 197 px | 510 | 2.36 / 27.17 |

The last column is the check that `gen_video` makes: the first frame read back through the Media Foundation reader (the path a camera uses) is 1920x1080, close to the original (mean difference 2.4 on the 0-255 scale) and far from the original turned upside down (27), so the frames arrive the right way up; all three passed.

**A flaw of the first footage, found and fixed.** In the first version of the 5-people footage the pictures of neighbours covered faces, and one registered person was detected in only 64 of his 111 visible frames. That was a flaw of the footage, not of the engines: `check_video` found it, and the layout in `gen_video` was fixed. It now limits the face width to what the room allows (a person's picture is 2.8 face widths wide, so the width at the end is at most the picture width divided by the number of people, divided by 1.95): 340 px for 1 or 2 people, 328 px for 3, 197 px for 5, and the start positions leave the same room, so no face is covered (`docs/testdata.md`, `docs/tools.md`). All the results below are from the fixed footage.

### The engines one by one (`eval_footage`)

Detection and tracking, every frame (a true face counts as found when a detected box overlaps it with IoU at least 0.5). **The rows "SCRFD, ncnn (dev, square)" were measured with the 640x640 square, before the fitted rectangle of section 5e; the 3-people and 5-people footage with the fitted rectangle (`dev_noncommercial.ini` now) finds the same faces, with detection times of 11.4 and 11.0 ms instead of 19.5 and 20.1 ms (section 5e has both layouts side by side; the 1-person footage was not run again).**

| Profile | Footage | True faces found | Mean IoU | False detections | Track IDs per person / ID changes | Detection time per frame: median / p95 / max |
|---|---|---|---|---|---|---|
| YuNet (permissive) | 1 person | 120 / 120 = **100%** | 0.864 | 0 | 1 / 0 | 6.7 / 8.4 / 10.2 ms |
| | 3 people | 333 / 333 = **100%** | 0.884 | 0 | 1 / 0 | 6.6 / 7.9 / 10.5 ms |
| | 5 people | 510 / 510 = **100%** | 0.887 | 0 | 1 / 0 | 6.9 / 10.7 / 12.9 ms |
| SCRFD, ncnn (dev, square) | 1 person | 120 / 120 = **100%** | 0.890 | 0 | 1 / 0 | 19.5 / 22.6 / 47.0 ms |
| | 3 people | 333 / 333 = **100%** | 0.875 | 0 | 1 / 0 | 19.5 / 23.2 / 31.1 ms |
| | 5 people | 510 / 510 = **100%** | 0.910 | 0 | 1 / 0 | 20.1 / 27.0 / 28.9 ms |
| SCRFD, ONNX Runtime (dev) | 1 person | 120 / 120 = **100%** | 0.890 | 0 | 1 / 0 | 11.6 / 14.1 / 20.7 ms |
| | 3 people | 333 / 333 = **100%** | 0.875 | 0 | 1 / 0 | 11.2 / 14.0 / 21.9 ms |
| | 5 people | 510 / 510 = **100%** | 0.910 | 0 | 1 / 0 | 11.4 / 14.4 / 21.7 ms |
| Haar (classical) | 1 person | 110 / 120 = 91.7% | 0.767 | 0 | 1 / 0 | 5.8 / 12.8 / 15.2 ms |
| | 3 people | 318 / 333 = 95.5% | 0.733 | 0 | 1 / 0 | 11.2 / 23.0 / 29.1 ms |
| | 5 people | 474 / 510 = 92.9% | 0.727 | 0 | 1 / 0 | 10.0 / 22.8 / 34.5 ms |

* **The three deep detectors find 100% of the true faces at 1920x1080 on all three footages, with 0 false detections, and every person keeps one track ID with no ID change.** The tracker takes 0.001 to 0.003 ms per frame (at most 0.019 ms).
* **The classical (Haar) detector misses faces under 200 px**: 81.1% of the faces of 100-199 px in the 1-person footage (43 of 53), 89.4% in the 3-people footage (126 of 141), 92.9% in the 5-people footage (474 of 510, all of them in that size class); every face of 200 px and more was found (100%). It has no false detections and one track ID per person, as the others.
* Detection plus tracking stays far below the 60 ms limit (applied to 1920x1080 by analogy) on this PC for every profile: the slowest frame of any footage took 47.0 ms (SCRFD on ncnn, first footage), the next slowest 34.5 ms (Haar).

Feature extraction (every 3rd frame, faces of at least 80 px; the pipeline's own steps: second look with exact landmarks and the pose gate by the profile's pose detector, crop and align, network) and matching (the faces of one frame against N = 20 filler people plus the registered people of the footage in one M:N call; threshold 0.55 for SFace, 0.40 for ArcFace):

| Profile | Footage | Faces that passed the pose gate | Time per face: pose check / crop+align / extract | Registered people: similarity to himself mean / min; rank 1 | Strangers: highest similarity to anybody; accepted | M:N call, median |
|---|---|---|---|---|---|---|
| SFace 128 (permissive) | 1 person | 26 of 40 (65%) | 20.9 / 0.11 / 12.6 ms | 0.960 / 0.941; 26 of 26 | no stranger | 0.003 ms |
| | 3 people | 85 of 111 (77%) | 10.0 / 0.10 / 12.8 ms | 0.932 / 0.893; 60 of 60 | 0.297; 0 of 25 | 0.004 ms |
| | 5 people | 141 of 170 (83%) | 10.6 / 0.04 / 13.7 ms | 0.942 / 0.922; 83 of 83 | 0.302; 0 of 58 | 0.005 ms |
| ArcFace 512, ncnn (dev) | 1 person | 27 of 40 (68%) | 22.1 / 0.10 / 10.4 ms | 0.969 / 0.959; 27 of 27 | no stranger | 0.004 ms |
| | 3 people | 80 of 111 (72%) | 10.6 / 0.11 / 11.0 ms | 0.941 / 0.926; 59 of 59 | 0.224; 0 of 21 | 0.007 ms |
| | 5 people | 132 of 170 (78%) | 11.3 / 0.05 / 11.0 ms | 0.938 / 0.911; 73 of 73 | 0.199; 0 of 59 | 0.008 ms |
| ArcFace 512, ONNX Runtime (dev) | 1 person | 26 of 40 (65%) | 21.3 / 0.13 / 6.5 ms | 0.968 / 0.957; 26 of 26 | no stranger | 0.005 ms |
| | 3 people | 77 of 111 (69%) | 11.1 / 0.11 / 6.8 ms | 0.942 / 0.923; 58 of 58 | 0.224; 0 of 19 | 0.007 ms |
| | 5 people | 133 of 170 (78%) | 10.8 / 0.05 / 6.8 ms | 0.936 / 0.912; 74 of 74 | 0.202; 0 of 59 | 0.007 ms |
| LBP 256 (classical) | 1 person | 29 of 36 (81%) | 20.8 / 0.20 / 0.6 ms | none registrable | no stranger | 0.003 ms |
| | 3 people | 37 of 105 (35%) | 18.7 / 0.17 / 0.6 ms | none registrable | 0.255; 0 of 5 | 0.003 ms |
| | 5 people | 132 of 153 (86%) | 18.0 / 0.07 / 0.6 ms | none registrable | 0.251; 0 of 55 | 0.007 ms |

* **Every registered person was found first (rank 1) and accepted at the threshold, in every face that reached the matcher** (26 of 26, 60 of 60, 83 of 83, and the same for the two ArcFace profiles), and **no stranger was accepted** (the highest similarity of a stranger to anybody was 0.30 against the threshold 0.55 for SFace, 0.22 against 0.40 for ArcFace). The extraction takes 6.5 to 13.7 ms per face for the deep extractors (limit 150 ms) and 0.6 ms for LBP; the pose check (the second look) takes 10 to 11 ms per face, and 21 to 22 ms in the 1-person footage, the first of the three runs of each profile (probably the slower first calls of the network; not investigated).
* The pose gate (pitch 5, yaw 5, roll 8 degrees) lets 65% to 83% of the faces of a frame pass (deep profiles), as in the earlier tests (section 4).
* **The classical profile cannot register the people of the footage.** The Haar detector finds the landmarks of a portrait only when it finds both eyes, and it did not for any of the six registered people of the three footages, so `eval_footage` leaves their faces out of the matching figures (it prints `cannot register ...`). The classical row therefore has no rank-1 figure, only strangers (none accepted). This agrees with section 3: LBP is not usable for authentication.
* **How to read the similarity to himself (0.93 to 0.97):** the actor in the footage is a flat photo and the registered photo is the very same picture, so this number measures how well the features survive H.264 compression, scaling and tilt, **not** accuracy on different photos. That accuracy is measured by `eval_features` on separate photos (below). The strangers' numbers are real: they are different people.

### Detection by face size at 1920x1080 (`eval_detect`, the default frame size now)

Generated frames of 1920x1080, one face in the middle, 20 portraits, found = IoU of at least 0.5 (mean IoU in brackets); false detections per frame are 0.00 for all four detectors at all sizes:

| Face width | YuNet | SCRFD (ncnn, square) | SCRFD (ONNX Runtime) | Haar (classical) |
|---|---|---|---|---|
| 50 px | 100% (0.88) | 90% (0.90) | 90% (0.90) | **0%** |
| 80 px | 100% (0.89) | 100% (0.94) | 100% (0.94) | 90% (0.73) |
| 110 px | 100% (0.91) | 100% (0.94) | 100% (0.94) | 90% (0.73) |
| 160 px | 100% (0.92) | 100% (0.92) | 100% (0.92) | 100% (0.68) |
| 250 px | 100% (0.92) | 100% (0.93) | 100% (0.93) | 100% (0.74) |
| 400 px | 100% (0.93) | 100% (0.93) | 100% (0.93) | 100% (0.74) |

Median detection time per frame in these runs: YuNet 6.5 to 7.2 ms, SCRFD on ncnn (square) 18.2 to 24.1 ms, SCRFD on ONNX Runtime 10.8 to 11.6 ms, Haar 3.0 to 9.3 ms (p95 up to 20.1 ms). The faces of the footage are 100 px and wider, so they are in the range where all deep detectors find 100%. With the fitted rectangle of section 5e the SCRFD (ncnn) column is the same from 80 px up (at 50 px: 85% instead of 90%, one portrait of 20) and the time is 9.1 to 9.6 ms.

### Feature accuracy on separate photos (`eval_features`, unchanged)

The accuracy of the features does not depend on the camera picture size (every face is cropped and aligned to the network's input), and the numbers of the run are the same as in section 3: SFace 128, 153 people, 185 photos: same-person similarity 0.844 on average, 0.654 at the lowest, strangers 0.498 at the highest, no false accept among 16,982 stranger pairs at the threshold 0.55, no false reject among 38 same-person pairs, correct first in 32 of 32 searches among 153 registered people; ArcFace 512 (non-commercial): 0.809 and 0.511 against a highest stranger score of 0.327, threshold 0.40, the same 0 and 0 and 32 of 32. The limits stated in section 3 still apply (easy photos; 1 in 10,000 is not proven).

### Speed at 1920x1080 (`bench`, 2 threads, run of 2026-11-12)

Median of 40 runs on this PC; "est. i5" is the measurement times 3.5 (an extrapolation, not a measurement); the limit of 60 ms is named for 1280x1080 and applied to 1920x1080 and 1280x720 by analogy, the extraction is timed on a 1920x1080 frame:

The SCRFD + ArcFace (ncnn) column of this table is from the **640x640 square** (the profile that is now `dev_noncommercial_square.ini`); the same rows with the fitted rectangle are in section 5e (1920x1080, 1 face: 11.5 ms, est. i5 40.2).

| Row | YuNet + SFace | SCRFD + ArcFace (ncnn, square) | SCRFD + ArcFace (ONNX Runtime) | Haar + LBP |
|---|---|---|---|---|
| detect 1920x1080, 1 face (limit 60 ms) | 6.6 ms (est. i5 23.3) | 19.3 ms (est. i5 **67.6**, over 60) | 10.0 ms (est. i5 34.9) | 10.9 ms (est. i5 38.0) |
| detect 1920x1080, 3 faces (limit 60 ms) | 6.0 ms (est. i5 21.2); 3 found | 18.1 ms (est. i5 **63.4**, over 60); 3 found | 11.3 ms (est. i5 39.4); 3 found | 13.1 ms (est. i5 45.9); 3 found |
| detect 1280x1080, 3 faces (limit 60 ms) | 6.7 ms (est. i5 23.5) | 18.3 ms (est. i5 **63.9**, over 60) | 11.1 ms (est. i5 38.9) | 8.9 ms (est. i5 31.1) |
| detect 1280x1080, 1 face / 640x640 / 1280x720 | 6.8 / 2.0 / 6.5 ms | 18.5 / 19.6 / 18.2 ms | 10.8 / 11.0 / 10.8 ms | 5.1 / 5.8 / 2.7 ms |
| extract 1 face (crop+align+net, limit 150 ms, ideal 50) | 13.4 ms (est. i5 46.8) | 11.0 ms (est. i5 38.5) | 6.3 ms (est. i5 22.2) | no row: Haar finds no face with landmarks in the test frame |
| pose refinement of 1 face | 8.0 ms (est. i5 28.1) | 7.3 ms (by YuNet; est. i5 25.7) | 7.0 ms (by YuNet; est. i5 24.6) | 6.9 ms (est. i5 24.3) |

* **The limits hold on this PC for every profile** (every row passes for "this PC"). **The estimate for the i5 was over the 60 ms limit for the SCRFD profile on ncnn at 1920x1080 (67.6 ms), as it was at 1280x1080 (64.8 ms), with the 640x640 square used when this was measured; section 5e brought it to 40.2 ms at 1920x1080**, but the estimate at 640x640 against the 25 ms limit is still over for both SCRFD profiles; YuNet, SCRFD on ONNX Runtime and Haar stay inside 60 ms at 1920x1080 in the estimate. The picture size hardly matters for the deep detectors with a square network input (every picture is shrunk to the same 640 px network input; for SCRFD on ncnn with the fitted rectangle it does, section 5e); Haar, which scans a working image that grows with the picture, is the one that gets slower (5.1 ms at 1280x1080, 10.9 ms at 1920x1080).
* Matching 10,000 registered features of 512 numbers: 0.7 ms for one face, 1.0 ms for four, 4.1 ms for sixteen (est. i5 2.5 / 3.6 / 14.5 ms); 128 numbers: 0.1 / 0.2 / 0.9 ms; 256 numbers: 0.1 / 0.5 / 1.6 ms.
* These are the numbers of the Visual Studio 2019 build (`build.bat`). The detection limits are named for 1280x1080 in the requirement; their application to 1920x1080 is by analogy.

### The complete system on the footage (`check_video`, YuNet + SFace)

All five runs ended `VIDEO CHECK PASSED (0 failed checks)`: every person detected in at least 90% of the frames (all in 100%), one track ID per person, the registered people recognized as the right person within 4 s and never as somebody else, the strangers never verified, at most 2% false detections (all 0), and with the worker thread no frame of 60 ms or more.

| Footage and mode | Frame time in `process()`: median / p95 / p99 / max | Recognition after the person is first seen | False detections |
|---|---|---|---|
| 3 people, synchronous | 6.9 / 31.9 / 55.0 / 60.3 ms | 1.60 s and 1.33 s | 0 of 333 |
| 3 people, worker thread, real time | 7.0 / 14.3 / 16.0 / **17.4** ms | 1.67 s, 1.40 s | 0 of 333 |
| 5 people, synchronous | 7.2 / 43.2 / 54.1 / 61.7 ms | 1.60 s, 0.80 s, 0.67 s | 0 of 510 |
| 1 person, synchronous | 6.8 / 30.6 / 33.2 / 43.9 ms | 1.87 s | 0 of 120 |
| 3 people at 1280x720 (made with `--size 1280x720`), synchronous | 7.4 / 40.7 / 84.3 / 103.0 ms | 2.00 s, 0.87 s | 0 of 333 |

In synchronous mode the frames in which a face is looked at a second time are the slow ones (the slowest frame took 44 to 62 ms at 1920x1080 and 103 ms in the 1280x720 run); with the worker thread the slowest frame is 17.4 ms. Recognition arrives about 0.07 s later with the worker thread, as before (section 2).

### Card first and the database on this PC

* `test_match`: **196 checks, all passed** (the card-first group `test_card_first` has 47 of them: `score_matrix` for 25 combinations of M faces and N_s cards, M < N, M = N and M > N, an unknown card, a deleted owner's card). `test_pipeline` video 6 (22 checks) passed for the permissive profile (synchronous, with SQLite, and with the worker thread) and for the dev profile (synchronous and with the worker thread): the owner with his own card is verified after 1.07 s (permissive, synchronous; 1.13 s with the worker; 1.33 s dev) with a similarity of 0.942 (permissive) or 0.928 (dev) to himself; a registered person with another person's card is not verified (his best similarity to the owner is 0.18 permissive, 0.01 dev); with an unknown card the registered person is not verified either, and with M = 2 faces and N = 2 cards both faces are verified, each as the owner of its own card (after 1.07 s and 0.80 s).
* **Database file, CSV against SQLite** (`test_match`, `test_database_speed`, 10,000 people x 512 numbers):

  | | file | write the whole file | open | one change (register / rename / give a card / delete) |
  |---|---|---|---|---|
  | CSV | 66.5 MB | 1306 ms | 1023 ms | 1271.6 / 1284.8 / 1290.6 / 1269.2 ms |
  | SQLite | 39.4 MB | 97 ms | 33 ms | 3.3 / 3.0 / 3.0 / 3.5 ms |

  The SQLite file is 39.4 MB and not about 20 million bytes (10,000 x 512 x 4 bytes) because a feature of 512 numbers is a 2 KB blob and fills a 4 KB page almost alone; the page size was not changed. CSV rewrites the whole file at every change, SQLite one row, so a change is about 360 to 430 times faster with SQLite at this size.
* `db_convert` converted a database of 500 people CSV to SQLite and back, and each result read back identical.

### Test status on this PC

All of these passed (0 failures): `test_common`, `test_detect_units`, `test_match`, `test_feature` (permissive, dev, classical), `test_pipeline` with the permissive profile (synchronous, with `--sqlite` and with `--async`) and with the dev profile (synchronous), and the five `check_video` runs above. (This is the status of the run before the SCRFD fitted rectangle. For that change both full flows were run and passed, with Visual Studio 2019 and with Visual Studio 2015 only; section 5e, "The full test flows".)

**One test fails on this PC: `test_pipeline models\dev_noncommercial.ini --async`, its timing check `worker mode: no frame takes 60 ms or more`** (the slowest frame took 59 to 139 ms in several runs; sometimes also `99% of the frames take less than 40 ms`, with a 99th percentile of 38 to 48 ms). All the functional checks of that run pass. It is **not caused by the 1920x1080 size or by the work of this version**: an interleaved comparison on the same PC with the last commit before the card-first, SQLite and 1920x1080 changes (built in a separate folder) fails the same check with the same kind of numbers (99th percentile 37.8 and 48.3 ms, slowest frame 76.8 and 83.6 ms there; 38.4 and 42.7 ms, 115.6 and 95.8 ms with the new code), and the same check at 1280x1080 fails with the new code too. The tail latency of the dev profile's worker thread (SCRFD on ncnn) is currently too noisy on this PC, which mixes fast and slow cores (`docs/TESTING.md`); earlier the same day the same test passed with a slowest frame of 34.8 ms. The thresholds were not loosened. The permissive profile with the worker thread passes (median 7.3 ms, 99th percentile 15.0 ms, slowest frame 17.2 ms over 1367 frames).

### Limits of this evaluation

* The footage is generated: the person is a flat photo that is moved, scaled and tilted in the picture plane, not turned in 3-D, and the registered photo is the same picture as the actor. So the figures for the registered people (similarity 0.93 to 0.97, rank 1 in every face) test the system and its robustness against compression, scaling and tilt, not recognition accuracy on different photos; accuracy is `eval_features` (section 3), on easy frontal portraits.
* The faces of the footage are 100 px and wider; the smaller faces are covered by the `eval_detect` table above and by section 5b.
* "est. i5" is an extrapolation from this i7 (factor 3.5), not a measurement: the dev profile on ncnn was estimated at 67.6 ms for detection at 1920x1080 (the 640x640 square of the time), over the 60 ms limit, and 40.2 ms after the fitted rectangle of section 5e; neither estimate is verified until `bench` is run on the target PC. The detection limits are named for 1280x1080 and are applied to 1920x1080 by analogy.
* The classical profile detects 92 to 96% of the faces of the footage and cannot register any of its people; none of the classical figures says anything about recognition.
* The Visual Studio 2015 run on this version is not part of this section (`docs/VISUAL_STUDIO.md`).
* This section's footage is clean (no room, no noise, no soft lens); section 5d has the same people and walks with a simulated camera look, which is a harder test, and says what is still missing: real footage of the inspection camera.

## 5d. Camera-look footage (simulated)

**What was asked.** Use the video footage recorded by the actual camera during the inspection; if it is not on this PC, create it. **What was found and done.** The inspection footage could not be found on this PC (the Videos, Downloads, Desktop and Documents folders, `Z:\AST` and `D:\` were searched). No camera was switched on, and the built-in camera of this PC offers 1280x720 at most. It was decided to **simulate camera footage at 1920x1080**. **The result is a simulation, not real camera footage:** the same flat portrait photos as in section 5c, moved over a drawn room, with formulas for the light, the lens and the sensor noise, and a harder compression. All numbers in this section are from runs of 2026-11-12 on the development PC (Windows 11, Core i7-13650HX, 2 CPU threads per network; `eval_footage` for the four profiles on the three `fhd_cam_*` videos, then five `check_video` runs; log `build\eval_cam.log`). The "est. i5" extrapolation is not repeated here. **The SCRFD on ncnn figures of this section are from the 640x640 square, before the fitted rectangle of section 5e** (the same footage with the fitted rectangle: `fhd_cam_3people` 11.3 ms and `fhd_cam_5people` 12.1 ms median detection against 20.0 and 19.7 ms with the square in the later comparison, the same faces found, section 5e).

### The footage

`gen_video --camera-look` (`scripts\make_fhd_footage.bat` makes the three videos) gives `fhd_cam_1person`, `fhd_cam_3people` and `fhd_cam_5people`: the **same people, walks and ground truth** as `fhd_1person`, `fhd_3people` and `fhd_5people` (8 s, 15 frames per second, 120 frames of 1920x1080, 120 / 333 / 510 true face boxes), with:

* a static room behind the people: wall panels, a floor, a window, a door, furniture and a notice board with text-like lines (high-contrast shapes a detector has to ignore); a different room for each video;
* uneven light (the left edge 20% darker than the right edge), darker corners (25% in the corner), slow brightness changes (plus and minus 6%) and a warm colour cast (6%);
* a slightly soft lens (a Gaussian blur of 0.9 pixels) and sensor noise (4 levels at mid gray, partly different in each colour channel);
* H.264 at **4000 kbit/s instead of 8000**: the files are 4.15 to 4.24 million bytes instead of 7.03 to 7.60 million; the first frame read back from the MP4 differs from the original by 3.57 to 3.59 (clean: 2.35 to 2.39; the upside-down comparison 35.3 to 38.4 against 27.2 to 27.3), so the frames still arrive the right way up (`gen_video`'s check passed for all three).

**Motion blur is part of the look but does not occur in this footage.** It smears a person over 0.4 times the pixels he moves between frames, and only from 2 pixels up; the people of `gen_video` move 1.5 pixels per frame at most (the middle of a face travels 180 pixels in 120 frames), so the blur length is 0.6 pixels and nothing is smeared (calculated from the code; the frames were not measured). The effect works and is tested with a fast mover (`test_common`: sharpness 637255 without, 527240 with the lens softness, 286953 with motion blur), but this footage does not exercise it. Other values printed by that test: sensor noise of 6 levels gives a picture-to-picture difference of 10.5 levels (the plain scene: 2.8), and the room has 1980 strong edges against none in the plain scene.

### Clean footage against camera look (`eval_footage`)

Each cell gives the **clean value and the camera-look value, in that order, joined by "and"** (so `6.7 and 6.5 ms` is 6.7 ms on the clean footage and 6.5 ms on the camera-look footage; the clean numbers are those of section 5c: `build\rest_of_tests_1080p.log` for the three deep profiles and `build\eval_classical_fhd_*.log` for Haar; the camera look numbers are from `build\eval_cam.log`). Detection and tracking, every frame (a true face counts as found when a detected box overlaps it with IoU of at least 0.5):

| Profile | Footage | True faces found | False detections | Track IDs per person / ID changes | Detection time per frame, median |
|---|---|---|---|---|---|
| YuNet (permissive) | 1 person | 120 / 120 and 120 / 120 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 6.7 and 6.5 ms |
| | 3 people | 333 / 333 and 333 / 333 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 6.6 and 6.4 ms |
| | 5 people | 510 / 510 and 510 / 510 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 6.9 and 6.7 ms |
| SCRFD, ncnn (dev, square) | 1 person | 120 / 120 and 120 / 120 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 19.5 and 18.5 ms |
| | 3 people | 333 / 333 and 333 / 333 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 19.5 and 18.0 ms |
| | 5 people | 510 / 510 and 510 / 510 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 20.1 and 18.6 ms |
| SCRFD, ONNX Runtime (dev) | 1 person | 120 / 120 and 120 / 120 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 11.6 and 11.2 ms |
| | 3 people | 333 / 333 and 333 / 333 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 11.2 and 11.1 ms |
| | 5 people | 510 / 510 and 510 / 510 (100%) | 0 and 0 | 1 / 0 and 1 / 0 | 11.4 and 11.0 ms |
| Haar (classical) | 1 person | 110 / 120 (91.7%) and 109 / 120 (90.8%) | 0 and 0 | 1 / 0 and 1 / 0 | 5.8 and 7.4 ms |
| | 3 people | 318 / 333 (95.5%) and 319 / 333 (95.8%) | 0 and 0 | 1 / 0 and 1 / 0 | 11.2 and 12.4 ms |
| | 5 people | 474 / 510 (92.9%) and 485 / 510 (95.1%) | **0 and 2** (in 120 frames) | **1 / 0 and 2 / 1** | 10.0 and 11.2 ms |

Matching (every 3rd frame, faces of at least 80 px that passed the pose gate; the faces of one frame against N = 20 filler people plus the registered people of the footage in one M:N call; threshold 0.55 for SFace, 0.40 for ArcFace). "Rank 1" is the share of the faces of registered people for which the matcher put the right person first and accepted him at the threshold; for strangers the number accepted is given:

| Profile | Footage | Similarity of a registered person to himself, mean | Rank 1 (and accepted) | Strangers accepted |
|---|---|---|---|---|
| SFace 128 (permissive) | 1 person | 0.960 and 0.948 | 100% (26 faces) and 100% (28 faces) | no stranger |
| | 3 people | 0.932 and 0.922 | 100% (60) and 100% (54) | 0 of 25 and 0 of 27 |
| | 5 people | 0.942 and 0.920 | 100% (83) and 100% (66) | 0 of 58 and 0 of 57 |
| ArcFace 512, ncnn (dev) | 1 person | 0.969 and 0.957 | 100% (27) and 100% (30) | no stranger |
| | 3 people | 0.941 and 0.936 | 100% (59) and 100% (54) | 0 of 21 and 0 of 23 |
| | 5 people | 0.938 and 0.916 | 100% (73) and 100% (54) | 0 of 59 and 0 of 59 |
| ArcFace 512, ONNX Runtime (dev) | 1 person | 0.968 and 0.957 | 100% (26) and 100% (30) | no stranger |
| | 3 people | 0.942 and 0.936 | 100% (58) and 100% (53) | 0 of 19 and 0 of 24 |
| | 5 people | 0.936 and 0.914 | 100% (74) and 100% (53) | 0 of 59 and 0 of 59 |
| LBP 256 (classical) | 1 person | none registrable | none registrable | no stranger |
| | 3 people | none registrable | none registrable | 0 of 5 and 0 of 9 |
| | 5 people | none registrable | none registrable | 0 of 55 and 0 of 49 |

**Findings.**

* **The three deep profiles find 100% of the true faces with 0 false detections, and every person keeps one track ID with no ID change, in the camera-look footage as in the clean footage.** Every registered person is still put first (rank 1) and accepted in every face that reached the matcher, and no stranger is accepted (the highest similarity of a stranger to anybody: 0.306 and 0.301 for SFace against the threshold 0.55, as before: 0.297 and 0.302; 0.222 and 0.186 for ArcFace on ncnn against 0.40, before 0.224 and 0.199).
* **The similarity of a registered person to himself is slightly lower with the camera look**: SFace on 3 people 0.932 to 0.922, on 5 people 0.942 to 0.920 (the largest drop, 0.022; the lowest single face 0.877 against 0.922 before), ArcFace 0.941 to 0.936 and 0.938 to 0.916 on ncnn (lowest face 0.869 against 0.911 before). The margin to the thresholds stays large (0.88 against 0.55, 0.87 against 0.40), but the noise, the soft lens and the harder compression do cost a little, by a similar amount in all the deep profiles (0.005 to 0.022 in the mean). As in section 5c this number measures robustness against the picture changes, not accuracy on different photos.
* **Haar finds 91 to 96% of the true faces** (90.8, 95.8 and 95.1%; before 91.7, 95.5 and 92.9%): it still misses faces under 200 px, as in 5c. **In the 5-people footage it had 2 detections that match no true face (in 120 frames) and one track ID change** (one person got 2 IDs): **the only imperfection that the camera look produced in any profile.** It still cannot register any of the people (no landmarks in their portraits), so it has no rank-1 figure, and it accepted no stranger.
* **Detection time is not affected by the camera look for the deep detectors** (the median is 0.1 to 1.5 ms lower in this run, which is within the run-to-run noise of this PC: YuNet 6.4 to 6.7 ms, SCRFD on ncnn 18.0 to 18.6 ms, on ONNX Runtime 11.0 to 11.2 ms). The Haar detector's median was 1.2 to 1.6 ms higher (5.8 to 7.4, 11.2 to 12.4, 10.0 to 11.2 ms; not investigated), still far below the 60 ms limit. The slowest detection-plus-tracking frame on the camera-look footage was 28.0 ms for Haar (3 people), 25.6 ms for SCRFD on ncnn, 17.7 ms for SCRFD on ONNX Runtime and 10.2 ms for YuNet.

### The complete system on the camera-look footage (`check_video`)

All five runs ended `VIDEO CHECK PASSED (0 failed checks)`: every person detected in at least 90% of the frames (all in 100%), one track ID per person, the registered people recognized as the right person within 4 s and never as somebody else, the strangers never verified, at most 2% false detections (all 0), and with the worker thread no frame of 60 ms or more. (These runs were made before `check_video` printed the line `all engines run on the CPU: yes (no GPU is used)`; it prints it now.) The permissive profile (YuNet + SFace) unless noted:

| Footage and mode | Frame time in `process()`: median / p95 / p99 / max | Recognition after the person is first seen | False detections |
|---|---|---|---|
| 3 people, synchronous | 7.4 / 42.1 / 50.9 / 71.9 ms | 1.60 s and 1.47 s | 0 of 333 |
| 3 people, worker thread, real time | 6.7 / 10.3 / 12.0 / **13.1** ms | 1.67 s, 1.53 s | 0 of 333 |
| 5 people, synchronous | 7.7 / 42.9 / 60.3 / 66.1 ms | 1.47 s, 0.93 s, 0.93 s | 0 of 510 |
| 1 person, synchronous | 7.5 / 32.4 / 35.1 / 44.5 ms | 2.13 s | 0 of 120 |
| 3 people, synchronous, **dev profile** (SCRFD + ArcFace, ncnn; the 640x640 square, before section 5e) | 21.2 / 64.6 / 73.6 / 73.9 ms | 2.00 s and 1.33 s | 0 of 333 |

Against the clean footage (section 5c) recognition came from 0.13 s earlier to 0.27 s later (one frame is 0.067 s): for example 3 people synchronous 1.60 and 1.33 s clean against 1.60 and 1.47 s, 1 person 1.87 s against 2.13 s. The frame times are about the same as before (permissive, synchronous: median 7.4 to 7.7 ms against 6.8 to 7.2 ms; with the worker thread 6.7 ms against 7.0 ms, and the slowest frame 13.1 ms against 17.4 ms); in synchronous mode the frames in which a face is looked at a second time are the slow ones, as before. `run_tests.bat` runs the first three rows (`fhd_cam_3people` synchronous and with `--async`, `fhd_cam_5people`); the last two were run by hand.

### Limits of this evaluation

* **It is a simulation, not real camera footage.** The picture is a drawn room with pasted flat portrait photos, and the effects are simple formulas. It does not contain what a real camera in a real hall would add: glasses, hats and masks, a person partly hidden by another, backlight (a bright window behind a person), camera shake, auto-focus and white-balance changes, rolling shutter, lens distortion, a real sensor's colour processing, people who really walk (head movement, turning, looking away, faces that are not frontal).
* The faces are frontal flat photos, tilted only in the picture plane, and **the registered photo is the very picture that appears in the video**, so the similarity to himself (0.91 to 0.97) tests robustness against noise, softness, light and compression, not recognition accuracy on different photos (section 3, `eval_features`).
* The motion blur of the camera look does not occur in the footage: the walks are too slow for it (less than the 2 pixels per frame that it needs; it is only tested with a fast mover, `docs/testdata.md`). So the `fhd_cam_*` videos have the room, the uneven light, the soft lens, the noise and the harder compression, but no motion blur.
* One walk and one room per video, three videos; the numbers are for those, not a statistical sample.
* **Real footage of the inspection camera is still needed**: run `eval_footage` and `check_video` on it, and re-calibrate the match threshold on its pictures. Until then this section says how the engines cope with a simulated, harder picture, and nothing more.
* The Visual Studio 2015 run of this final version is described in `docs/VISUAL_STUDIO.md`: everything built (10, 9, 8 and 17 projects), 11 test runs passed, the dev-profile worker-thread timing check was intermittent (it stopped the script once; the rest was run from a copy and passed: 8 video checks, 24 engine evaluations, `bench`).
* Test status of what was added with the camera look: `test_common` passed (66 checks then, 13 of them the camera look; 74 now, section 5e), all four `eval_footage` profiles and the five `check_video` runs above passed or ran to the end, and `scripts\make_fhd_footage.bat` made all six videos.

## 5e. SCRFD on ncnn made faster: the fitted rectangle (detection 17.9 to 11.5 ms at 1920x1080)

**What was asked, and the decision.** The SCRFD detector on ncnn (`dev_noncommercial.ini`) was the one profile whose **estimated** detection time on the 4th-generation i5 at 1920x1080 was over the 60 ms limit (67.6 ms in section 5c, 68 ms at 1280x1080 in section 2; the `bench` of this section gives 62.7 ms for the same square). The choice was to **make it faster while staying on ncnn** (not to switch that profile to ONNX Runtime, which is `dev_noncommercial_onnx.ini`). All numbers are from the development PC (Core i7-13650HX, 2 CPU threads per network), runs of 2026-11-12; "est. i5" is the measurement times 3.5, a guess (`bench --i5-factor`), not a measurement on the i5.

### What was found (throw-away experiments, not part of the project)

The experiments were made with small programs that are not in the project (the compiled `ncnn_opts.exe` and `layout_check.exe` are still in `build\`; their sources were not kept in the project). What they showed:

* **It was not a missing AVX2 build.** The bundled ncnn is 1.0.20261020 and is built **with** its AVX2 and FMA kernels and chooses them at run time (`NCNN_AVX2 1`, `NCNN_FMA 1`, `NCNN_RUNTIME_CPU 1` in `platform.h`; no AVX512), so the reason that ONNX Runtime ran the same network about 1.7 times faster was not a build without AVX2.
* **The converted model is already fused.** `det.param` has `convrelu` and `convdwrelu` layers (activation inside the convolution): 40 Convolution, 20 ConvolutionDepthWise, 18 Reshape, 9 Permute, 9 Split, 4 BinaryOp, 3 Sigmoid and 2 Interp layers (the counts can be read off `det.param`; 41 of the 60 convolutions are named `convrelu_*` or `convdwrelu_*`, and there is no separate ReLU layer in the file).
* **ncnn's run-time options do not help.** With ncnn's own defaults (fp16 packed, storage and arithmetic **on**; bf16 storage off; packing, Winograd and sgemm on; light mode on) the network alone takes about 18 ms at 640x640 with 2 threads. fp16 off: 17.6 ms (within the noise); bf16 storage off: the same; **bf16 storage on: 28 to 30 ms (slower; never the default)**; 1 thread: 23 to 29 ms; 3 threads: 15.6 to 17.5 ms (the target PC has 2 cores, so this is no option). Skipping the nine Permute / Reshape / Sigmoid layers of the head by reading the raw outputs of the convolutions saves only 0.4 to 2.4 ms (about 1 ms).
* **A pitfall, so that nobody repeats it:** an early experiment seemed to show a 41% gain from switching fp16 off. That was **wrong**: the test had forced bf16 storage on, which is not ncnn's default. Measured against ncnn's real defaults, switching fp16 off changes nothing (17.6 ms against about 18 ms, within the noise).
* **The real waste was the square.** The detector always ran the network on a 640x640 square, but a 1920x1080 picture shrunk to a long side of 640 is only 640x360, so **44% of the work was spent on gray padding**.

### The change

* `letterbox_fit(src, long_side, multiple, dst, scale, pad_x, pad_y)` (`src\common\image_ops.h/.cpp`, `docs/common.md`): the letterbox into the **smallest rectangle whose sides are multiples of 32**: 640x384 for 1920x1080 (40% fewer pixels than the square), 640x544 for 1280x1080 (15% fewer), 640x480 for 4:3 (25% fewer), and still 640x640 for a square picture (no gain). The scale is the same as with the square, so a face has the same size in the network input.
* A new model file, `models\dev_noncommercial\det_dyn.param` (`det_dyn.bin` is a copy of `det.bin`; `docs/MODELS.md`): `det.param` with its **two fixed-size upsampling layers** (`Interp` with `3=40 4=40` and `3=80 4=80`) changed to scale by 2 (`1=2.0 2=2.0`), so the network accepts any size that is a multiple of 32. Nothing else in the file differs.
* A new profile key, `detector.layout` (`docs/common.md`): `flat` (the default: the nine reshaped outputs `out0` to `out8`, a 640x640 square) or `chw` (the nine **raw** outputs of the last convolutions, blobs `65,85,105,66,86,106,67,87,107`, any multiple-of-32 input). `load_profile` refuses `chw` for anything but SCRFD with nine outputs.
* `DeepDetector::detect` uses `letterbox_fit` for `chw`, and the new `DeepDetector::decode_scrfd_chw` decodes the raw outputs (`docs/detect.md`): the score is compared as a logit with `log(t / (1 - t))` before the sigmoid is computed, and the faces come out in the same order as with `flat`.
* `models\dev_noncommercial.ini` now uses `det_dyn.param` and `chw`. **`models\dev_noncommercial_square.ini` keeps the old settings** (`det.param`, `out0` to `out8`, `flat`, square) as a fallback and for comparison; `dev_noncommercial_onnx.ini` (SCRFD on ONNX Runtime) is unchanged. `run_tests.bat` and `scripts\run_tests_vs2015.bat` list the new profile in `eval_detect`, `bench` / `bench_detect` and the `eval_footage` loop (five profiles now).
* **Checked before it was used:** the raw layout against the flat outputs, number by number: `flat[(cell * 2 + a) * per + k] = raw[(a * per + k) * cells + cell]`, and the flat score is the sigmoid of the raw one; the largest difference was 0 for boxes and landmarks and 1e-9 for the scores at all three strides. The same comparison is now a test (`test_common`, largest difference 3.7e-9).

### Speed (`bench`, `build\bench_dev_fitted.log`: the fitted rectangle and the square in one run)

Median of 40 runs; detection only; "est. i5" is the measurement times 3.5.

| Row | Fitted rectangle (`dev_noncommercial.ini`) | Square (`dev_noncommercial_square.ini`) | Limit |
|---|---|---|---|
| detect 1920x1080, 1 face | **11.5 ms** (est. i5 **40.2**) | 17.9 ms (est. i5 62.7, over) | 60 ms |
| detect 1920x1080, 3 faces | 11.7 ms (est. i5 41.0) | 19.0 ms (est. i5 66.3, over) | 60 ms |
| detect 1280x1080, 1 face | 15.5 ms (est. i5 54.4) | 18.6 ms (est. i5 65.3, over) | 60 ms |
| detect 1280x1080, 3 faces | 17.0 ms (est. i5 **59.4**) | 18.9 ms (est. i5 66.0, over) | 60 ms |
| detect 1280x720, 1 face | 11.8 ms (est. i5 41.4) | 18.6 ms (est. i5 65.0, over) | 60 ms |
| detect 640x640, 1 face | 20.1 ms (est. i5 **70.5, over**) | 19.2 ms (est. i5 67.1, over) | 25 ms |
| extract 1 face (crop+align+net) | 10.0 ms | 10.2 ms | 150 ms |
| pose refinement of 1 face (by YuNet) | 8.3 ms | 7.6 ms | none |

What this says, and does not say:

* **At 1920x1080 and 1280x720, which are 16:9 pictures, the detection takes 36 to 38% less time** (17.9 to 11.5 ms and 18.6 to 11.8 ms for one face; 19.0 to 11.7 ms for three faces at 1920x1080) and the **estimate for the i5 goes from over 60 ms to 40 to 41 ms**. The network input has 40% fewer pixels, and the time fell almost as much.
* **At 1280x1080 the gain is smaller**, as the arithmetic says: 640x544 instead of 640x640 is 15% fewer pixels, and the time fell from 18.6 to 15.5 ms (17%) for one face and from 18.9 to 17.0 ms (10%) for three. **The estimate for three faces at 1280x1080, 59.4 ms, is on the limit:** an earlier run (`bench_detect`, `build\bench_detect_dev_noncommercial.log`, the same morning) gave 17.3 ms and **est. i5 60.4 ms, FAIL by 0.4 ms**, for the same row (and 15.6 ms, est. 54.5, for one face at 1280x1080; 12.2 ms, est. 42.6, at 1920x1080; 11.2 ms, est. 39.3, at 1280x720). The requirement names 1280x1080 and this is its most demanding row; it is a **pass or a fail by the run-to-run noise** (the two runs differ by 0.3 ms, which is 1.0 ms after the factor of 3.5).
* **At 640x640 nothing is gained** (a square picture needs the whole square: 20.1 against 19.2 ms, which is noise; 21.5 and 19.4 ms in the second run), and the estimate, 70.5 ms, is **still over the 25 ms limit**. SCRFD on ONNX Runtime is over it too (est. 44 ms, section 2); section 2 lists YuNet (with its 320x320 network for small pictures) and Haar as inside it.
* The extraction and the pose check are unchanged (10.0 and 8.3 ms against 10.2 and 7.6 ms; the pose check is done by YuNet).
* The measurements of one run were made one after the other on a PC that runs other programs: the square gave 17.9 ms in this run and 18.7 to 20.5 ms in others, so treat every figure as good to about a millisecond or two.

### Accuracy: the same faces

* **`eval_detect` at 1920x1080, 20 portraits** (`build\eval_detect_fit_vs_square.log`; found = IoU of at least 0.5; false detections 0.00 per frame for both at all sizes):

  | Face width | Fitted: found / mean IoU / median ms | Square: found / mean IoU / median ms |
  |---|---|---|
  | 50 px | 85% / 0.90 / 9.4 | 90% / 0.90 / 17.6 |
  | 80 px | 100% / 0.94 / 9.5 | 100% / 0.94 / 17.3 |
  | 110 px | 100% / 0.94 / 9.3 | 100% / 0.94 / 17.1 |
  | 160 px | 100% / 0.92 / 9.1 | 100% / 0.92 / 17.3 |
  | 250 px | 100% / 0.93 / 9.6 | 100% / 0.93 / 17.9 |
  | 400 px | 100% / 0.93 / 9.2 | 100% / 0.93 / 18.4 |

  Found and IoU are identical from 80 px up. **At 50 px the rectangle found 85% and the square 90%: one portrait of 20**, below the 80 px minimum crop width (`FA_MIN_CROP_FACE_PIXELS`) that the system needs, so it costs nothing for authentication, but it is a difference and is stated here. With 20 portraits and one size it is not known whether it is more than chance; it was not investigated further.
* **`eval_footage`, four of the six footages, both profiles** (`build\evf_dev_noncommercial_<footage>.log` and `build\evf_dev_noncommercial_square_<footage>.log`; 2 threads; 333 / 333 / 510 / 510 true faces):

  | Footage | Faces found, mean IoU, false detections, track IDs (fitted and square) | Detection time per frame, median / p95: fitted | square | Faces through the pose gate: fitted / square | Registered people: rank 1, mean similarity to himself (fitted / square) | Strangers: highest similarity (fitted / square), accepted |
  |---|---|---|---|---|---|---|
  | `fhd_3people` | 333 / 333 = 100% both; 0.875 both; 0; 1 ID each | 11.4 / 13.6 ms | 18.1 / 20.7 ms | 80 of 111 / 80 of 111 | 100% both; 0.943 / 0.941 | 0.211 / 0.224; 0 |
  | `fhd_5people` | 510 / 510 = 100% both; 0.911 / 0.910; 0; 1 ID each | 11.0 / 13.4 ms | 19.4 / 28.3 ms | 134 of 170 / 132 of 170 | 100% both; 0.937 / 0.938 | 0.201 / 0.199; 0 |
  | `fhd_cam_3people` | 333 / 333 = 100% both; 0.879 both; 0; 1 ID each | 11.3 / 13.1 ms | 20.0 / 24.8 ms | 77 of 111 / 74 of 111 | 100% both; 0.939 / 0.937 | 0.212 / 0.214; 0 |
  | `fhd_cam_5people` | 510 / 510 = 100% both; 0.911 both; 0; 1 ID each | 12.1 / 14.6 ms | 19.7 / 23.1 ms | 116 of 170 / 118 of 170 | 100% both; 0.914 / 0.915 | 0.212 / 0.207; 0 |

  **Detection is identical** (every true face found, mean IoU within 0.001, no false detection, one track ID per person, no ID change) and **recognition is equal** (rank 1 for every registered face that reached the matcher, no stranger accepted); the detection time per frame is **11.0 to 12.1 ms against 18.1 to 20.0 ms**. The number of faces that passed the pose gate differs by 0 to 3 of 111 or 170 between the two profiles (the registered faces that reached the matcher: 58 against 59, 75 against 73, 50 against 49, 57 against 59); the boxes of the two layouts differ by fractions of a pixel and the pose check starts from them, which is probably why, but it was not investigated. The footage is the generated footage of sections 5c and 5d (flat photos, the registered photo identical to the actor), so these figures test that the change does not alter the result, not recognition accuracy; the simulated camera look is a simulation, not real camera footage.
* **The whole system, `test_pipeline models\dev_noncommercial.ini`, synchronous** (`build\test_pipeline_dyn.log`; 70 checks, all passed; `test_common` (69 checks at the time of these logs; 74 since, see "The full test flows" below) and `test_feature models\dev_noncommercial.ini` (14 checks) also passed, `build\test_common_dyn.log` and `build\test_feature_dyn.log`). The three new checks compare the fitted rectangle with the square on a 1920x1080 picture with three faces: both find the same three faces, the worst box overlap is 0.994 and the largest landmark difference 0.26 px (0.23 px after the test's positions were made to follow the picture size). The generated walk at the five picture sizes: the face was detected in 90 of 90 frames at every size, one track ID, verified after 1.3 to 2.2 s (640x480 2.2 s, 1280x720 1.47 s, 1920x1080 1.6 s, 1600x1200 1.33 s, 2560x1440 1.6 s; the earlier run with the square gave 1.47 to 1.73 s; one walk per size, the differences were not investigated). The detection time per frame in that test, with the square (`build\run_tests_final.log`, an earlier run) and with the fitted rectangle:

  | Picture | 640x480 | 1280x720 | 1920x1080 | 1600x1200 | 2560x1440 |
  |---|---|---|---|---|---|
  | Square (earlier run) | 18.1 ms | 17.1 ms | 18.2 ms | 18.3 ms | 18.4 ms |
  | Fitted rectangle | 14.3 ms | 11.0 ms | 10.6 ms | 13.8 ms | 11.0 ms |

  The 16:9 sizes gain the most; the 4:3 sizes (640x480, 1600x1200: a 640x480 network input, 25% fewer pixels) 21 to 25%.

### The full test flows on this change (2026-11-12)

* **Visual Studio 2019, `run_tests.bat` in full** (`build\run_tests_final5.log`): `ALL DONE`; 11 test runs printed `ALL TESTS PASSED` (`test_common` 69 checks at that time, `test_detect_units` 26, `test_match` 196, `test_feature` 40 over the three profiles that `run_tests.bat` runs (14 + 14 + 12), `test_pipeline` permissive 67, dev 70, SQLite 67, worker thread permissive 72, worker thread dev 75); all 8 video checks printed `VIDEO CHECK PASSED`. **The dev worker-thread timing check passed with much more headroom than before:** `process()` median 11.7 ms, p95 17.8, p99 21.3, max 24.4 (it was a median of about 18.5 ms before this change; the limits are 40 ms for the 99th percentile and 60 ms for the slowest frame).
* **Visual Studio 2015 only, `scripts\run_tests_vs2015.bat`** (`build\run_vs2015_final4.log`): the four solutions were rebuilt (10, 9, 8 and 17 projects, 0 failed); 12 test runs printed `ALL TESTS PASSED`; 8 video checks passed; the script ended `ALL DONE (Visual Studio 2015, Release)` **in one go, nothing stopped it**; the dev worker-thread timing check passed (median 11.7 ms, p99 21.4, max 41.9). The five-profile lists, with the square variant, ran in `eval_detect`, `bench_detect` and the `eval_footage` loop. (The earlier Visual Studio 2015 run, `docs/VISUAL_STUDIO.md`, had been stopped once by this timing check on the old square.)
* **After these two flows** the small changes listed in the next section were made and checked with builds and single programs, **not by repeating the full flows**.

### What is not done, and the limits

* **The estimates for the i5 are extrapolations** (the measurement times 3.5). The change was measured on the i7 only; whether the i5 gets 40 ms at 1920x1080 and 54 ms at 1280x1080 is not known until `bench` is run on the target.
* **The 25 ms requirement at 640x640 is still not met by SCRFD** (est. 70.5 ms on ncnn, 44 ms on ONNX Runtime): a square picture cannot be made smaller by this method. **An option that was not done:** a smaller network input for small pictures, like YuNet's 320x320 network, which would bring SCRFD inside 25 ms at 640x640 at the price of missing small faces (YuNet's smaller network finds faces below about 40 px less reliably). It would have to be measured on SCRFD; no SCRFD input smaller than 640 was tried.
* **How much a picture gains depends on its shape:** a 16:9 camera picture gets the whole 40% fewer pixels, a 4:3 picture 25%, 1280x1080 only 15% (17% of the time for one face, 10% for three), and a square picture nothing. The target requirement names 1280x1080, where the estimate is 54.4 ms for one face and 59.4 to 60.4 ms for three (above).
* The 4K figures (section 5b) were not repeated with the fitted rectangle; the scale is the same, but no measurement says what 3840x2160 does now. The two-core `check_video` run of section 2 (SCRFD profile, worker thread, pinned to two cores) was **not repeated**, so it is not known whether the SCRFD profile now fits two cores.
* The nine flat outputs `out0` to `out8` are still in `det_dyn.param`, but their reshape layers have fixed sizes, so they are right for 640x640 only; the profile reads the raw outputs instead (`docs/MODELS.md`).
* **The dev profile's worker-thread timing test (`test_pipeline models\dev_noncommercial.ini --async`) is intermittent on this PC** (section 5c, "Test status"). It passed in both full flows with the new detector and with more headroom than before (below), but two passing runs do not show that the intermittency is gone.
* The comparison block of `test_pipeline` for a `chw` profile was run with the picture size of the test only (1920x1080); its positions follow `--size`, but no other size was tried.
* **After the two full flows (above) small changes were made to the tests and to comments, and the full flows were not repeated after them.** What was added: five checks in `test_common` (two for `letterbox_fit`, three for `detector.layout` in `load_profile`; 74 checks now, 69 in the full flows), the positions and sizes of the `test_pipeline` comparison block now follow `kWidth` / `kHeight`, the comment of `src\common\profile.h` lists the five profiles, and the top comment of `models\dev_noncommercial.ini` no longer says "input fixed to 640x640". Those changes were checked with a full Visual Studio 2019 rebuild (`test_common` 74 ok, `test_pipeline` dev synchronous 70 ok) and with incremental Visual Studio 2015 builds of the four solutions (8, 5, 5 and 8 projects, 0 failed; `test_common` 74 ok; `test_pipeline` dev synchronous passed).

## 6. Other findings

* **ONNX Runtime must stay at 1.17.3.** Version 1.31 crashed at start on this PC because
  it needs a newer Visual C++ runtime than Windows ships here. The same would happen on the
  target PC.
* **Data cleaning mattered, twice.** The portraits had 12 people filed under two names. The
  feature evaluation exposed them (different-name pairs scoring 0.73-0.96); until they were
  merged a "stranger" in the integration test was in fact a registered person's twin. When
  the set grew from 116 to 153 people the same thing happened again (folders that rules cannot
  join are listed by hand in the alias list of `scripts/merge_portraits.py`), and the same Commons file downloaded
  under two names is now dropped on import (a photo compared with itself would score 1.0 as a
  fake "same person").
* **Estimates versus measurements.** Only the "this PC" columns are measured. Whether the
  limits are met on the 4th-generation i5 is not known until `bench` is run there.
* **The database is a managed file, CSV by default, SQLite if the user chooses.** The whole database has to be in memory
  anyway as the matcher's flat table, so the file is only a safe place to keep it between runs. A CSV file needs no library
  (Visual Studio 2015, no extra dependency), can be read and edited in a text editor or a spreadsheet, and was the original
  requirement; that is why it is the **default**. SQLite's advantage is changing one row without rewriting the file, and it is
  offered for the databases where that matters. The price of CSV: every change rewrites the whole file. `test_match`
  (`test_database_speed`, 10,000 people x 512 numbers) prints it for both formats: CSV, a 66.5 MB file, write 1306 ms, open
  1023 ms, one change about 1.27 to 1.29 s; SQLite, a 39.4 MB file, write 97 ms, open 33 ms, one change 3.0 to 3.5 ms (the file
  is bigger than the numbers alone (20,480,000 bytes), because a 2 KB feature almost fills a 4 KB page). For 200 people CSV costs about 26
  ms per change (an extrapolation by proportion, not a measurement); SQLite was not measured at that size. Both formats are
  written crash-safely (CSV through a temporary file swapped in with one operation, SQLite through its journal), both refuse a
  damaged file, and a lock file keeps a second program out. The SQLite source (3.54.1, public domain) is part of the project
  (`third_party/sqlite`); `tools/db_convert` converts a database between the formats.
* **Card first is not a search.** With RF cards the faces are compared only with the features retrieved for the cards; a
  card that is not in the database fails the comparison and nothing is searched among all registered people. The consequence
  is deliberate: a registered person who shows a wrong or unknown card is not verified.

## 6b. Found by independent review and fixed

A second reader (a separate code review of `src/`) found these real problems, all fixed and
covered by tests:

* **A verified track kept its verification when another person stepped into the same spot.**
  The tracker gives a face found in the same place after a short gap the same track ID, and
  the pipeline never looked at a verified track again, so the newcomer was reported as the
  person who had left. Now a verified track that was lost for `FA_REVERIFY_AFTER_MISSED_FRAMES`
  (3) frames in a row has to be verified again when it is found. `test_pipeline` video 4
  covers it; with the fix switched off the stranger is reported as verified (checked).
* Renaming or replacing the feature of a person now updates the faces that are currently
  being tracked as that person.
* The Haar detector's eye search was offset by up to one pixel, which biased its yaw and
  pitch; the Haar detector now also forgets old faces on `reset_tracking`.
* `refine_face` used a zoom that had not been applied for faces 317-320 px wide.
* YuNet/SCRFD output decoding now checks every output tensor, so a wrong output order in a
  profile gives an error message instead of reading past a buffer.
* Profiles with impossible numbers (small_size 0, a non-numeric thread count, a misspelled
  backend) are rejected; the CSV store refuses features that are not finite and of length 1
  (such a record could match every query; the SQLite format is checked by the same rules when a database is opened).
* Smaller: a time stamp that goes backwards no longer stalls crop storing; the Haar results
  no longer depend on thread timing; a missing LBP calibration file is reported.

Still open from that review, deliberately not changed:

* All paths must be plain ASCII. Picture files, ONNX models and the database files (CSV and SQLite) use different text
  encodings internally, so a folder or file name with non-ASCII characters can make one of
  them fail to find the file (this project lives in an ASCII path).
* **Camera image orientation is only checked for video files.** An MP4 read through the same
  Media Foundation reader arrives right way up (section 2), but I have not captured from the
  live camera. If a camera driver does not report a stride the frame could still arrive upside
  down and no face would be found: check once with `camera_check --capture 5` and look at
  `build\camera_check.png`.
* The random projection of the classical extractor uses `std::normal_distribution`, whose
  exact numbers are not fixed by the C++ standard; features stored with a build from another
  compiler may not match.
* The public `store()` accessor lets a caller remove a person without un-verifying the faces
  currently tracked as that person (`AuthPipeline::remove_person` does it correctly). (A failed
  write of the database file no longer leaves the change in memory: the store undoes it and the
  call returns `false`.)

## 7. Known limits, summarized

1. Timings on the target i5 are estimates (the measurement times 3.5). The one that used to be over the limit, SCRFD on ncnn at 1920x1080, is now estimated at 40.2 ms (section 5e); the estimate at 1280x1080 with three faces, 59.4 to 60.4 ms, is on the 60 ms limit, and at 640x640 SCRFD is still over the 25 ms limit (est. 70.5 ms on ncnn, 44 ms on ONNX Runtime).
2. In the default synchronous mode, frames that do a pose check and an extraction can exceed 60 ms; the worker-thread mode removes that (max 17.7 ms here, 41.9 ms on two cores), at the price of recognition arriving a frame or two later.
3. Yaw/pitch gating is only accurate to about 5-6 degrees; the 512-number models are
   non-commercial; the permissive recognizer has 128 numbers.
4. Accuracy was measured on easy, frontal photos of 153 people; 1 in 10,000 is not
   statistically proven.
5. The classical detector misses faces under 96 px (at 1280 and 1920 wide; more above that); the
   classical extractor cannot authenticate.
6. `test_pipeline models\dev_noncommercial.ini --async` fails its worker-thread timing check on the development PC (slowest frame 59 to 139 ms against the 60 ms limit); the last commit before the card-first, SQLite and 1920x1080 changes fails it the same way, so it is the noise of the dev (SCRFD on ncnn) worker thread on this PC (mixed fast and slow cores) and not an effect of these changes (section 5c, `docs/TESTING.md`). The thresholds were not loosened. (With the fitted SCRFD rectangle of section 5e the check passed in both full flows of 2026-11-12, median 11.7 ms, p99 21.3 and 21.4 ms, slowest frame 24.4 and 41.9 ms; the intermittency of this PC is not shown to be gone.)
7. The 1920x1080 evaluation uses generated footage of flat photos, with the registered photo identical to the actor: it tests the system, not recognition accuracy on different photos (section 5c). The "est. i5" columns, including the estimate of 67.6 ms for the dev profile on ncnn at 1920x1080 (the square; 40.2 ms with the fitted rectangle, section 5e), are extrapolations.
8. The Visual Studio 2015 build and run of this final version (card-first matching, SQLite, the 1920x1080 evaluation, the camera look, the CPU-only checks and the Media Foundation settings) are described in `docs/VISUAL_STUDIO.md`: everything built and passed, with one exception to know about: the dev-profile worker-thread timing check (`test_pipeline --async`) is intermittent on this PC (it stopped the script once; of two reruns one passed, one failed on a single slow frame); the Debug configuration was not run again. The full Visual Studio 2015 flow on the SCRFD fitted-rectangle change (section 5e) went through in one go (12 test runs passed, 8 video checks passed, `ALL DONE`).
9. The camera code was run live only with `camera_check` on the built-in camera of the development PC (its modes, and a
   capture of 5 frames: 1280x720; the picture was deleted unseen, so its orientation is unchecked). No automated test opens a camera, by
   design, and no camera that offers 1920x1080 was tried. Use `camera_check --modes` and `--capture` on the target.
10. **No real camera footage was used.** The footage of the inspection camera could not be found on the development PC, and no camera was switched on; the 1920x1080 "camera" footage of section 5d is a **simulation** (flat portrait photos in front of a drawn room, with simulated light, soft lens, sensor noise and compression; the simulated motion blur does not occur in it). It is a harder test than the clean footage and the engines passed it (the deep profiles unchanged in detection and tracking, a slightly lower similarity to himself; Haar with one imperfection), but it does not show how they behave in front of the real camera: frontal faces, no occlusion, glasses, backlight or camera shake, and the registered photo is the picture in the video. **Real footage from the inspection camera is still missing.**
11. All engines run on the CPU, enforced and tested (section 2, "All engines on the CPU"). The bundled ncnn library still contains its Vulkan code (switched off in code, never used); rebuilding it with `NCNN_VULKAN=OFF` would remove it, and was not done. The programs that write an MP4 show an idle Direct3D device (`3d` engine instances at 0.0% utilization, no video engine) while the software encoder runs; removing even that was not done (it would mean converting the pictures to NV12 on the CPU, which changes the colour handling of the footage). The GPU probes are coarse (one reading about every 3 s). Whether a real camera's driver uses a GPU for a live stream is not tested (no test opens a camera; the source reader is created with DXVA and hardware transforms switched off).
12. **SCRFD on ncnn, the fitted rectangle (section 5e).** The speed-up (36 to 38% less time for 16:9 pictures, 17% for one face at 1280x1080, none for a square picture) was measured on the development PC only. What is not done: a smaller network input for small pictures (the 25 ms requirement at 640x640 stays over for SCRFD); the two-core `check_video` run and the 4K run were not repeated with the new detector; the 50 px detection rate is 85% against 90% with the square (one portrait of 20); at 1280x1080 with three faces the estimate is on the 60 ms limit (59.4 ms in one run, 60.4 ms in another). **Verified:** the full Visual Studio 2019 flow (`run_tests.bat`, `build\run_tests_final5.log`) and the full Visual Studio 2015 flow (`scripts\run_tests_vs2015.bat`, `build\run_vs2015_final4.log`) both ran to `ALL DONE` on this change, with all tests and video checks passed, and the dev worker-thread timing passed (median 11.7 ms, p99 21.3 and 21.4 ms). **Not repeated:** small changes to the tests made after those two flows (five more `test_common` checks, 74 now; positions of the `test_pipeline` comparison scaled with the picture size) were checked with builds and single programs only (Visual Studio 2019 full rebuild; Visual Studio 2015 incremental builds), not by repeating the full flows.
