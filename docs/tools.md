# Tools, tests and scripts (`tools/`, `tests/`, `scripts/`, `build.bat`, `run_tests.bat`)

**What this is for.** Every command-line program of the project lives in `tools/` (programs that measure, generate data, calibrate constants, check a whole video, `check_video`, evaluate the engines one by one on recorded footage, `eval_footage`, or convert the database between its two file formats, `db_convert`) or in `tests/` (programs that check the engines and say `ALL TESTS PASSED` or `TESTS FAILED`). This document explains, for each program, how to start it, what it does, what it prints and how to read what it prints. The two batch files `build.bat` and `run_tests.bat` build and run everything, the two Python scripts in `scripts/` download and tidy the portrait photos that the tests need, and the PowerShell script `scripts/gpu_probe.ps1` runs any program of the project and reads the Windows GPU counters while it runs (the outside proof that no GPU is used). All programs are built by `build.bat` into `build\bin\`. They must be started **from the project root** (the folder that contains `models\` and `data\`), because model, data and output paths are relative; the profile files given on the command line are read with the root `"."`. A profile that is missing, has an unknown key, a non-numeric or out-of-range number (`runtime.threads` must be 1 to 64, `recognizer.dim` 1 to `FA_MAX_FEATURE_DIM`, `detector.small_size` a multiple of 32 when `detector.model_small` is set) or a backend that is not `onnx` or `ncnn` is refused by `load_profile` with a message, and the programs below then stop with exit code 2 (`eval_detect`, `bench`, `bench_detect` and `bench_extract` skip that profile instead). Numbers measured with these programs are collected in `docs/RESULTS.md`; this document does not quote measurements. `docs/TESTING.md` has the short summary of the same programs and the table that says which program belongs to which engine: the programs are grouped into **four Visual Studio solutions**, one for each engine (`face_detection.sln`, `feature_extraction.sln`, `feature_matching.sln`) and one for the integrated project (`face_authentication.sln`); how to open and build them is in `docs/VISUAL_STUDIO.md`. `build.bat` builds every program of `tools\` and `tests\` whatever the solution. The entry of each program below says which engine it belongs to. Besides the programs, `tools/` holds five small headers (`bench_common.h`, `bench_frames.h`, `bench_detect_part.h`, `bench_extract_part.h`, `bench_match_part.h`): the measuring code that the four benchmark programs `bench`, `bench_detect`, `bench_extract` and `bench_match` share. They are not programs and `build.bat` does not compile them by themselves. `tests/` holds one such header too, `cpu_only_check.h`, the helper of the checks that no GPU is used (included by `test_common`, `test_feature` and `test_pipeline`).

Terms used below.

| Term | Meaning |
|---|---|
| Profile | A small text file `models\*.ini` that says which detector and which feature extractor to use, with their settings (for example `models\permissive.ini`). Every program that needs models takes a profile as its first argument. |
| IoU | "Intersection over union": how much two boxes overlap. 0 means no overlap, 1 means identical boxes. |
| Median, p95, max | Summary of many timing samples. The median is the middle value (a few slow runs do not disturb it); p95 is the value that 95 percent of the runs do not exceed; max is the slowest run. They come from `TimingStats` in `timer.h`. |
| Warm-up | The first run of a network is slower than the following ones (memory and caches are prepared), so programs run once before timing and do not count that run. |
| est. i5 | The median time on this PC multiplied by a factor (default 3.5, changed with `--i5-factor`): a guess of the time on the target PC, a 4th-generation Intel Core i5. It is an estimate, not a measurement (see `g_factor`). |
| Genuine / impostor pair | Two photos of the **same** person (genuine) or of **two different** people (impostor). |
| FAR / FRR | False accept rate: share of impostor pairs wrongly accepted. False reject rate: share of genuine pairs wrongly rejected. Both depend on the threshold. |
| Portrait folder | `data\portraits\<person>\` with `117th.jpg`, `116th.jpg`, `alt.jpg` and so on (see `fetch_portraits.py`). |

## tools/calibrate_lbp.cpp

| Function | Purpose |
|---|---|
| `main` | Compute the calibration file of the classical (LBP) feature extractor from a set of portraits. |

### `main` (calibrate_lbp)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line:

`calibrate_lbp <profile.ini> <portrait folder> <letters a-m | singles> <output file>`

For every portrait it computes the 2891 raw LBP histogram numbers of the aligned 112 x 112 face (`lbp_raw_histogram` in `extractor.h`) and then writes, for each of the 2891 numbers, its **mean** and **1 / spread** over all faces to the output file. The classical extractor subtracts the mean and multiplies by 1 / spread, because the LBP histogram of every face looks much like that of every other face and all similarities would otherwise be high. This is not model training; it only needs the average and spread. The third argument selects the people used: a letter range such as `a-m` uses only people whose folder name starts with a letter in that range, so that the people used for evaluation (the other letters, `eval_features --letters n-z`) never influence the calibration; the word `singles` uses all letters but only people who have exactly one `.jpg`. It prints one line at the end: `calibration from N faces (<range or "people with a single photo">) written to <output file>`, or `too few faces (N)` and exit code 1 when fewer than 10 faces were usable. Exit codes: 0 success, 1 too few faces or an error writing the file or creating the detector, 2 wrong usage or unreadable profile. Only the *detector* settings of the profile are used.

To take effect, the file must be saved as `models\lbp_calibration.bin`: `extractor_lbp.cpp` loads exactly that path when it creates the classical extractor, and quietly works without calibration if the file does not exist.

**How it works.**
1. Load the profile, create the detector from it, and read the range or `singles` from argument 3 (`argv[3][0]` and `argv[3][2]` are the first and last letter).
2. Loop over the sub-folders of the portrait folder and skip those whose first letter is outside the range (or, for `singles`, that do not have exactly one `.jpg`).
3. For each `.jpg`: load, detect, take the largest face, run `refine_face` (fall back to the plain detection if refinement fails), require landmarks, `make_face_crop`, `align_face` to 112 x 112, `lbp_raw_histogram`. Add every value to a running sum and a running sum of squares.
4. mean = sum / count; variance = sum of squares / count - mean squared (not below 0); `inverse_spread = 1 / max(sqrt(variance), 0.01)`. The floor of 0.01 stops values that hardly vary from getting a huge weight.
5. `lbp_save_calibration` writes "the means, then the 1 / spread values".

The header comment and the usage message both name the `singles` form (`<a-m | singles>`).

**Engine.** Feature extraction (`feature_extraction.sln`).

**Used by.** Run by hand. No script or batch file calls it. Output is read by the LBP extractor (`src/feature/extractor_lbp.cpp`).

## tools/calibrate_pose.cpp

| Function | Purpose |
|---|---|
| `stats` | Print mean, median and standard deviation of a list of numbers (anonymous namespace). |
| `main` | Measure the proportions of a frontal face as a landmark detector reports them, and how many such faces the current pose gate accepts. |

### `stats`

```cpp
void stats(const char* name, std::vector<float>& v);
```

**What it does.** Prints one line `  <name>  mean M  median D  std S  (n=N)` for the numbers in `v` (signs shown, four decimals). It prints nothing when `v` is empty. The list `v` is sorted by the function. The standard deviation divides by `n` (population form). For a list with an even number of values the "median" is the upper of the two middle values (element `n / 2` of the sorted list).

**How it works.** Sum for the mean, sum of squared differences for the variance, `std::sort`, then `printf`.

**Used by.** `main` of this file.

### `main` (calibrate_pose)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line:

`calibrate_pose <profile.ini> [--type T] [--refine] <folder with portraits>`

`--type T` overrides the profile's detector type (`yunet`, `scrfd` or `haar`) and **clears `pose.detector_profile`**, so that the detector `T` alone is measured. `--refine` measures the landmarks that the *refinement* step (second, zoomed-in look) gives instead of the plain detection; the refinement is run by the profile's **pose detector** (`create_pose_detector`: the detector named by `pose.detector_profile`, for example YuNet for `models\dev_noncommercial.ini`, or the profile's own detector when the profile names none), while the picture's face is still found by the main detector. The folder is searched recursively for `.jpg` files; the largest face of each picture is used. The portraits are official straight-on photos, so on average their pitch and yaw are close to 0. The program prints one line `detector: <main detector name>` (after `--refine` followed by `  (landmarks of the refinement step, made by <pose detector name>)`, for example `detector: scrfd/ncnn  (landmarks of the refinement step, made by yunet/onnxruntime)` for the SCRFD profile) and then, in this order: six statistics lines `nose_x` (sideways position of the nose tip), `nose_y` (nose tip below the line through the eyes), `mouth_y` (mouth below that line), `eye_px` (the distance between the eyes in pixels, so you can see how big the faces were), `pitch deg` and `yaw deg` (the pitch and yaw of the measured faces in **degrees**; the first three lines are in **eye distances**); a **gate line** `with the constants now in pose.cpp the gate (pitch 5, yaw 5, roll 8 degrees) accepts A of B faces (P%): pitch ok X, yaw ok Y, roll ok Z` (the numbers in the brackets are the current tolerances `FA_POSE_*_TOL_DEG`); and a **ready-to-paste line** `mean values for PoseModel{nose_y, mouth_y, nose_z, mouth_z, nose_x0}:  {a, b, 0.45f, 0.0f, c}`.

How to read it: the **means** of `nose_y` and `mouth_y` are the numbers for `PoseModel::nose_y` and `PoseModel::mouth_y`, and the mean of `nose_x` is the number for `PoseModel::nose_x0`, in `pose_model_for` in `src/detect/pose.cpp`. `nose_x` should be close to 0. The `std` is the person-to-person spread of the face proportions, which limits how exact a pose estimated from five landmarks can be. The **gate line** answers the question: "if these (straight-on) portraits are the reference, how many of them would the pose gate accept with the constants that are in `pose.cpp` now?" Because the portraits are frontal, a perfect pose estimate would accept (nearly) all of them; `A of B` counts the faces whose pose is valid and inside all three tolerances (`pose_within_tolerance`), and `pitch ok`, `yaw ok` and `roll ok` count how many pass each angle alone, so you can see which angle rejects the faces. `pitch deg` and `yaw deg` show the mean and spread of the measured angles (they should be near 0 for straight-on photos). With `--refine` the pose of the refined face is used (the pose model `<type>-refined`, where `<type>` is the type of the detector that did the refinement, so `yunet-refined` for the SCRFD profiles, and the ready-to-paste line belongs to that entry of `pose_model_for`), otherwise the pose of the plain detection. **After changing `pose.detector_profile`, run `calibrate_pose <profile.ini> --refine data\portraits` again and compare the line with the constants in `pose.cpp`.** The **ready-to-paste line** is a `PoseModel` initializer: `a` is the mean of `nose_y`, `b` the mean of `mouth_y`, `c` the mean of `nose_x` (the numbers to copy into `pose_model_for`); the two middle numbers `0.45f` and `0.0f` are the fixed anatomical values `nose_z` and `mouth_z` and are always printed as they are (they are not measured). The gate line is printed only if at least one face had a valid pose, and the paste line only if at least one face was measured. Exit code 2 for wrong usage or unreadable profile, 1 if the detector cannot be created (with `--refine` also if the pose detector cannot be created, for example because the profile named by `pose.detector_profile` cannot be read), otherwise 0 (also when no face was found, in which case no statistics lines appear). The usage message and the header comment at the top of the source file both describe `--refine`, the pose detector, `eye_px`, the pitch and yaw statistics, the gate line and the paste line.

**How it works.** For every `.jpg`: load, detect (main detector), pick the largest face; with `--refine` call `refine_face` **with the pose detector** and use its result (skipping the picture if refinement fails); `landmark_geometry` (from `pose.h`) turns the five landmarks into `nose_x`, `nose_y`, `mouth_y`; the distance between landmark 0 and landmark 1 gives `eye_px`. If the face has a valid pose (`pose.valid`), its pitch and yaw are stored and the counters for the gate line are updated (each angle against its tolerance, and `pose_within_tolerance` for all three). Then `stats` is called six times (`nose_x`, `nose_y`, `mouth_y`, `eye_px`, `pitch deg`, `yaw deg`), followed by the gate line and the paste line (the means are computed by a small lambda in `main`).

**Engine.** Face detection (`face_detection.sln`).

**Used by.** Run by hand; the constants it produces were copied into `pose.cpp` (comments there refer to it).

## tools/camera_check.cpp

| Function | Purpose |
|---|---|
| `parse_size` | Read a size such as `1920x1080` from a command-line word (anonymous namespace). |
| `read_options` | Read the optional camera number and size after `--modes` or `--capture N` (anonymous namespace). |
| `main` | List the cameras that Windows reports, show the picture formats of one camera and, only on request, capture a few frames. |

### `parse_size`

```cpp
bool parse_size(const char* text, int& width, int& height);
```

**What it does.** Reads a size written as `WxH`, for example `1920x1080`, from `text` into `width` and `height`. Returns `true` only when both numbers were read and both are greater than 0; otherwise it returns `false` (and the output values may have been overwritten by a partly read text, so `read_options` reads into local variables first).

**How it works.** `std::sscanf(text, "%dx%d", ...) == 2` and both numbers above 0. Text after the second number is ignored (`1280x720abc` is accepted as 1280 x 720). A plain camera number such as `0` gives only one number, so it is not a size.

**Used by.** `read_options`.

### `read_options`

```cpp
void read_options(int argc, char** argv, int first, int& index, int& width, int& height);
```

**What it does.** Reads the optional arguments that may follow `--modes` or `--capture N`: a camera number and/or a size `WxH`, **in any order** (`camera_check --capture 5 1280x720 1` and `camera_check --capture 5 1 1280x720` do the same). It looks at the words from `argv[first]` to the end. A word that is a size (`parse_size`) sets `width` and `height`; every other word is taken as the camera number with `atoi`, so a word that is not a number silently becomes camera 0. The variables keep the values they had when no such word is given (camera 0, 1920 x 1080).

**How it works.** One loop; each word is first tried as a size into two local numbers, and only a successful `parse_size` copies them to the result.

**Used by.** `main` (both `--modes` and `--capture`).

### `main` (camera_check)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command lines:

* `camera_check` lists the camera names and nothing else. **No camera is opened.**
* `camera_check --modes [index] [WxH]` lists the picture formats that camera `index` (default 0) offers (size, frame rate and pixel format) and marks the one that would be switched on for a request of `WxH` (default 1920x1080). The camera is opened to ask it, but its picture stream is **not** started.
* `camera_check --capture N [index] [WxH]` opens camera number `index` (default 0), asking for about `WxH` pixels (default 1920x1080), says which camera mode was switched on, reads `N` frames, prints a speed line and saves the last frame as `build\camera_check.png`. Example: `camera_check --capture 5 0 1280x720`.

It first prints `K camera(s) reported by Windows:` and one line `  [i] <name>` per camera (this is the only output without an option). The camera does **not** have to offer the wanted size: the closest camera mode is used (`pick_camera_mode`, see `camera.md`), so the real size of the frames can differ from `WxH`.

With `--modes` it then prints `camera I offers N picture formats:` and one line per format, `  <width>x<height>  <fps> fps  <format>`, in the order the camera lists them, for example `   1280x720    30.0 fps  NV12`; the format that `pick_camera_mode` chooses for the request gets the ending `   <- chosen`. A last line says `for a request of WxH the camera mode WxH at F fps (FORMAT) is switched on`. If the camera lists no format or cannot be opened it prints `camera I offers no picture format (or cannot be opened)` and exits with code 1.

With `--capture` it prints, before reading any frame, either `asked for WxH; camera mode switched on: WxH at F fps (FORMAT); the frames will be WxH` or, when the camera had to be used with its default (nothing usable was listed), `asked for WxH; the camera listed no usable mode, its default is used; the frames will be WxH`; the last `WxH` is the real size (`FrameSource::frame_size`). It then prints either `cannot open camera I` (exit code 1) or `got G frames of WxH in T ms (A fps; camera clock B fps)`. Reading it: `WxH` is the **real** size of the frames (the camera may not deliver the wanted size). `T` is the wall-clock time from just after the camera was opened until the last frame was read, so it includes the camera's start-up delay for the first frame; `A` is `G * 1000 / T`. `B` is the speed according to the camera's own time stamps between the first and the last frame (needs at least two frames; otherwise 0). Open `build\camera_check.png` to check by eye that the picture is the right way up and that the colours are right (this tests the bottom-up handling described in `camera.md`). The `build\` folder must exist; the result of saving is not checked. The tests never open a camera; this is the only program that does, and only with `--modes` or `--capture`.

Exit codes: 0 when nothing went wrong (also for `--capture` when no frame could be read: the line then says `got 0 frames`, and no picture is saved); 1 when `--modes` finds no format or `--capture` cannot open the camera. `--capture` without a frame count (the word `--capture` as the last argument) is not recognised and only lists the cameras. A frame count that is not a number reads as 0 frames.

**How it works.** `list_cameras()` for the names, then `int index = 0, width = FA_CAMERA_DEFAULT_WIDTH, height = FA_CAMERA_DEFAULT_HEIGHT`. With `--modes`: `read_options` (from the third word on), `list_camera_modes(index)`, `pick_camera_mode(modes, width, height)` and the printing. With `--capture`: `atoi` for the frame count, `read_options` (from the fourth word on), `open_camera_source(index, width, height, &chosen)`, `camera->frame_size(...)` for the line printed before the frames, then a loop `camera->next(frame, t)` that counts frames and remembers the first and last time stamp, timed with `now_ms()`; finally `save_image`.

**Engine.** The integrated project, the camera part (`face_authentication.sln`).

**Used by.** Run by hand (`docs/TESTING.md`).

## tools/check_video.cpp

| Function | Purpose |
|---|---|
| `TruthRow` (struct) | One line of `truth.csv`: person, true face box, visible or not. |
| `ActorReport` (struct) | What was learned about one person while the video was played. |
| `split_line` | Split a text line at a separator character. |
| `main` | Play a video through the whole authentication system and check the result against the truth. |
| `draw_text` (from `src/common/draw_text.h`) | Write text into a picture (used for the annotated video). |

### `TruthRow` (struct)

```cpp
struct TruthRow {
    std::string label;
    Rect box;
    bool visible = false;
};
```

**What it does.** One line of `truth.csv` for one frame: the person's `label`, the exact face `box` and whether the face is `visible` (completely inside the frame).

**How it works.** A plain struct. `main` stores the rows in a map from frame number to a list of rows.

**Used by.** `main` of this file.

### `ActorReport` (struct)

```cpp
struct ActorReport {
    bool registered = false;
    int frames_visible = 0, frames_detected = 0;
    std::set<int> track_ids;
    double first_seen_ms = -1, first_verified_ms = -1;
    std::string verified_as;
    bool wrong_person = false;
    int verified_frames = 0, unverified_frames = 0;
};
```

**What it does.** The notes about one person: whether he is registered (from `scenario.txt`), in how many frames he was visible and in how many of them a detected face matched him, the set of track IDs his face got, the time he was first detected (`first_seen_ms`) and first verified (`first_verified_ms`, -1 if never), the person id he was verified as, whether he was ever verified as somebody else (`wrong_person`), and the number of frames in which his face was verified or not.

**How it works.** A plain struct; one object per person label in a map.

**Used by.** `main` of this file.

### `split_line`

```cpp
static std::vector<std::string> split_line(const std::string& line, char sep);
```

**What it does.** Cuts a text line at every `sep` character and returns the pieces (used for the comma-separated lines of `truth.csv`). Empty pieces at the end are not returned.

**How it works.** A `std::stringstream` and `std::getline` with the separator.

**Used by.** `main` of this file (reading `truth.csv`).

### `main` (check_video)

```cpp
int main(int argc, char** argv);
```

**What it does.** Plays a video, frame by frame, through `AuthPipeline` exactly as camera frames would arrive, and compares what the system found with the truth that `gen_video` wrote. Command line:

`check_video <profile.ini> <video.mp4 | folder of frames> [options]`

| Option | Meaning |
|---|---|
| `--async` | Use the pipeline's worker thread (`init(profile, csv, true)`) and play the video in **real time**. |
| `--realtime` | Play in real time without the worker, to see what the delays are like. (`--async` implies it.) Without it the video is played as fast as possible. |
| `--fps N` | Frame rate of a **folder** of frames (default `15`); a video file brings its own times. The annotated video is also written at this rate (the rate is taken at the first frame). |
| `--scenario FILE` | Who is registered (default: `scenario.txt` next to the video, written by `gen_video`). |
| `--truth FILE` | The true face boxes (default: `truth.csv` next to the video, written by `gen_video`). |
| `--fillers N` | Register `N` more people who are not in the video, so that the matcher has a real gallery to search (default `20`). |
| `--out FILE.mp4` | Where to write the annotated video (default: `annotated_sync.mp4` or, with `--async`, `annotated_worker.mp4` in the folder of the video). |

If the input is a folder it is played with `open_sequence_source(input, fps)`, otherwise with `open_video_file_source(input)` (see `camera.md`).

**What it registers.** The pipeline is created with the given profile and the store file `build\check_video.csv` (deleted first, so every run starts empty). The people marked `registered` in `scenario.txt` are registered from their portrait `data\portraits\<label>\117th.jpg` (person id = the folder name, name `Registered <label>`). Then up to `--fillers` more people who are **not** in the video are registered from their `117th.jpg` (name `Filler <folder>`), so that the M:N matcher searches a real gallery (N = the number of people registered). The people marked `stranger` are not registered. It prints `registered: E people from the scenario + F fillers (the matcher searches S people)`. Note that the actor in the video is the same flat photo that was registered, so this is an integration check, not a measure of recognition accuracy (see `testdata.md`).

**What it prints and checks.** First a line `profile <path>: detector <name>, extractor <name> (<dim> numbers, threshold <t>), <mode>` where the mode is `worker thread, real time`, `synchronous, real time` or `synchronous, as fast as possible`, then the line `all engines run on the CPU: yes (no GPU is used)` (the answer of `AuthPipeline::runs_on_cpu()`: the detector, the pose detector, the extractor and, with `--async`, the worker's own; it prints `NO` instead of `yes (...)` if an engine did not, which cannot happen in this project; the line is information and is not one of the PASS/FAIL checks). After the last frame it prints `played N frames in S s`, and `time per frame in process(): median, p95, p99, max` (the time of `FrameResult::total_ms`). Then one block per person: `- <label> (REGISTERED|stranger): visible in V frames, detected in D, track IDs: ...; first verified as '<id>' T s after he was first seen (N frames verified, M not)` or `; never verified`, followed by a PASS/FAIL list:

* `<label>: face detected in at least 90% of the frames where it is visible`;
* `<label>: the person keeps one track ID`;
* for a registered person: `recognized as the right person`; `never reported as somebody else`; `recognized within 4 s of first being seen`;
* for a stranger: `the stranger is never verified`.

Then `detections that match no true face: A of B` with the check `at most 2% of the detections are false`, and, in `--async` mode, `worker mode: no frame took 60 ms or more`. How the comparison works: for every visible true face of a frame the detected face with the largest IoU above 0.4 is that person's face; detected faces that no true face claims are "false detections". The last line is `VIDEO CHECK PASSED (0 failed checks)` or `VIDEO CHECK FAILED (N failed checks)`. Exit code 0 only when every check passed, 1 when a check failed, 2 for wrong usage, an unreadable profile, a failing `init`, a missing scenario or truth file, a person that cannot be registered, or an input that cannot be opened.

**Files it writes.**

* The annotated video (see `--out`), written with `VideoWriter`: a **green** box means the face is verified (text `#<track>  <person id>  <score>`), a **yellow** box means not (yet) verified (`#<track>  not verified`), a thin **gray** box is the true face position, below each face the head rotation `pitch ... yaw ... roll ...` (with `(outside gate)` when the pose is outside the tolerances), and a header line `frame N  t=...s  WORKER THREAD|synchronous   process X ms` (in synchronous mode with `(pose check + extraction in this frame)` when `gate_ms + extract_ms` is above 1 ms). The writer is opened with the width and height of the first picture rounded down to even numbers (the encoder needs even sizes), but only pictures that are already even in both directions are written; a video with an odd frame size therefore gets no annotated video.
* `build\check_video_frames.csv`: one line per face per frame with the columns `frame,time_ms,track,x,y,w,h,pitch,yaw,roll,verified,person,score,process_ms,jobs_in_flight`.
* Still pictures in `<video folder>\check\`: `verified_<person id>.png` (the first annotated frame in which that person is verified) and `unverified_first.png` (the first annotated frame that contains an unverified face), saved after all faces of the frame are drawn.

How to read it: the PASS/FAIL list is the verdict; the per-person lines show how long recognition took (`first verified ... after he was first seen`); the time-per-frame line shows what a frame costs (in synchronous mode the maximum is the frames in which the pose check and the extraction ran; in worker mode every frame should be about as fast as the detection alone); open the annotated video to see the same thing as pictures.

**How it works.** (1) Read the options; derive the default file names from the input. (2) Load the profile, `AuthPipeline::init(profile, csv, async)`. (3) Read `scenario.txt` (lines `person <label> registered|stranger`) and register the people and fillers. (4) Read `truth.csv` (header line skipped, lines with at least 9 fields) into a map frame to `TruthRow`s. (5) Loop `source->next(frame, t_ms)`; in real-time modes wait with `Sleep` until the wall clock reaches the frame time; `pipeline.process`; add `total_ms` to a `TimingStats`; compare the faces with the truth of this frame; draw the annotations with `draw_rect` and `draw_text`; write the CSV line and the video frame. (6) After the loop `pipeline.flush()` and `writer.close()`; print the report with the local lambda `check` (prints `PASS`/`FAIL` and counts failures); save the stills.

**Engine.** The integrated project (`face_authentication.sln`).

**Used by.** `run_tests.bat` and `scripts\run_tests_vs2015.bat` (with `models\permissive.ini` on the 1920x1080 videos that `scripts\make_fhd_footage.bat` makes: the clean videos `data\videos\fhd_3people\video.mp4` once synchronous and once with `--async`, `fhd_5people` and `fhd_1person` once synchronous; the simulated-camera videos (`gen_video --camera-look`) `fhd_cam_3people` once synchronous and once with `--async`, and `fhd_cam_5people` once synchronous; and on the 1280x720 video `data\videos\demo_720p\video.mp4` once synchronous); by hand. `check_video` works with the picture size of the video it is given: nothing in it is fixed to 1280 x 1080.

### `draw_text` (from `src/common/draw_text.h`)

```cpp
void draw_text(Image& image, int x, int y, const std::string& text, uint8_t b, uint8_t g, uint8_t r, int pixel_height = 22);
```

**What it does.** Writes `text` into `image` with the top-left corner of the text at (`x`, `y`), in the colour `(b, g, r)` and with letters `pixel_height` pixels high (default `22`). A dark outline is added so that the text can be read on any background. Parts outside the picture are cut off. The text is plain ASCII (Windows ANSI). Nothing happens when the image is empty, does not have 3 channels, or the text is empty. It is only for pictures that people look at; no engine uses it.

**How it works.** It uses the text drawing of Windows (GDI, the old Windows graphics interface; `draw_text.cpp` asks the linker for `gdi32.lib` and `user32.lib` with `#pragma comment`, and `build.bat` links them too). Windows draws the text, in the bold font "Segoe UI", anti-aliased, as white letters on a black bitmap that is just big enough (the text size plus 4 pixels of room for the outline). The brightness of each bitmap pixel is then used as the opacity ("alpha") when the wanted colour is blended into the picture. The outline is made by blending the same text four more times, in black and shifted by one pixel left, right, up and down, before the coloured text on top. The blending is done by a small helper in the anonymous namespace of the file, `blend(image, bitmap, width, height, x0, y0, b, g, r)`, which skips every pixel outside the picture.

**Used by.** `tools/check_video.cpp` (`main`, for the labels, the pose text and the header line of the annotated video) and `tests/test_common.cpp` (`main`, one check: green letters appear in a test picture).

## tools/db_convert.cpp

| Function | Purpose |
|---|---|
| `file_size` | Size of a file in bytes (-1 if it cannot be opened). |
| `main` | Convert the database of registered people from one file format to the other, and check the result. |

### `file_size`

```cpp
static long long file_size(const std::string& path);
```

**What it does.** Returns the size of the file in bytes, or -1 if it cannot be opened.

**How it works.** Opens the file in binary mode positioned at its end (`std::ios::ate`) and returns `tellg()`.

**Used by.** `main` of this file (is there an output file already? the sizes that are printed).

### `main` (db_convert)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `db_convert <input> <output> [--format csv|sqlite] [--force]`. The database of registered people can be a CSV file (the default format) or a SQLite database (see `store.md`); this program reads either and writes the other, so a database can be moved from one format to the other. The steps:

1. The **input** is read with `FeatureStore::load`, which recognises the format by the file's content and applies the same rules as when a program opens a database (every feature a finite vector of length 1, one feature length, unique ids and card numbers). A file that cannot be read or is damaged ends the program with `cannot read <input>: <reason>` and exit code 1. The input file is never changed.
2. The **output format** is the one asked for with `--format csv` or `--format sqlite`; without `--format` the file name decides, like everywhere else: `.db`, `.sqlite` and `.sqlite3` (any letter case) mean SQLite, anything else CSV. The output is written with `FeatureStore::save`: completely into `<output>.tmp` first and then swapped in, so a failed conversion leaves no half-written file. Failure: `cannot write <output>: <reason>`, exit code 1. Every person is converted with his id, name, RF card number and the exact feature numbers.
3. The conversion is **checked**: the output file is read back into a second store and compared with the first, person by person (id, name, card number and every feature number). If they differ the program prints `the written file <output> does not read back the same: ...` and exits with code 1.
4. On success it prints `converted N people (C with an RF card, features of D numbers)` and two lines, one for the input and one for the output, with the format (`CSV` or `SQLite`) and the size in MB; the output line ends with `(read back and compared: identical; T ms)`.

An existing output file is not overwritten unless `--force` is given (`<output> exists already (use --force to replace it)`, exit code 2). The input and the output must not be the same file (exit code 2). The usage line is printed, with exit code 2, when the input or the output is missing; `--format` with another value than `csv` or `sqlite` prints `--format needs csv or sqlite` (exit code 2); a third file name prints `too many arguments` (exit code 2). Exit codes: 0 success, 1 a failed read, write or comparison, 2 wrong usage. Standard output is unbuffered.

Examples (from the header comment): `db_convert people.csv people.db` (CSV to SQLite), `db_convert people.db people_copy.csv` (SQLite to CSV), `db_convert people.csv backup.dat --format sqlite` (a SQLite file with a name of your choice).

**How it works.** The options are read in one loop (`--force`, `--format` with its value, and the first two other words as input and output). `FeatureStore store; store.load(input)`; the format of the input is also asked with `is_sqlite_file` for the message; `store.save(output, format)`; a second `FeatureStore check; check.load(output)` and a loop over the records; `is_sqlite_file(output)` for the message. The time printed covers the whole conversion including the check.

**Engine.** Feature matching (`feature_matching.sln`; needs no model and no network library).

**Used by.** `run_tests.bat` (a database of 500 random people made by `gen_features`, converted CSV to SQLite and back to CSV; the two conversions must say `identical`) and `scripts\run_tests_vs2015.bat` (the same with 2,000 people of 256 numbers); by hand.

## tools/detect_image.cpp

| Function | Purpose |
|---|---|
| `main` | Run a face detector on picture files, print what it finds, optionally save annotated copies. |

### `main` (detect_image)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command lines:

* `detect_image <profile.ini> [--type yunet|scrfd|haar] [--draw outfolder] image1 image2 ...`
* `detect_image <profile.ini> --folder data\portraits` (every `.jpg` below the folder, searched recursively)

`--type` overrides the detector type of the profile, so one profile can drive all detectors (`--type haar` uses the classical detector). Standard output is unbuffered. Exit code 2 for wrong usage or unreadable profile, 1 if the detector cannot be created, otherwise 0.

Output: first `detector: <name>`. Then one line per picture:

`<path>  <width>x<height>  <n> face(s)  <ms> ms  [score S box x,y WxH pose pP yY rR]` (one bracket per face; `nolm` is added to the bracket when that face has no landmarks, as can happen with Haar)

`score` is the detector's confidence, `box` is left, top, width and height in pixels of the original picture, and `pose` gives pitch, yaw and roll in degrees from the detector's own landmarks (signs as in `pose.h`; this is the *coarse* pose, not the refined one). A picture that cannot be read prints `cannot read <path>`. The last line is `images with a face: A / B   faces: F   detect time median X ms  p95 Y ms  max Z ms`. With `--draw`, an annotated copy is written for every picture as `<outfolder>\<parent folder name>_<file name without extension>.jpg` (for `data\portraits\axne\116th.jpg` that is `axne_116th.jpg`): a green box of 3 pixels, the two eye landmarks as blue dots and nose and mouth corners as red dots.

How to read it: `images with a face` shows how often the detector finds a face at all; the time is per picture of that size (the detector resizes inside).

**How it works.** The first `detect` call per picture is a warm-up and is thrown away; the second call is timed and reported (so for the Haar detector, which follows faces between full scans, the reported time may be a follow-up scan, not a full one). Results are added to `TimingStats` and to the counters. Drawing uses `draw_rect` and `draw_dot` from `image_ops.h` and `save_image`.

**Engine.** Face detection (`face_detection.sln`).

**Used by.** Run by hand (`docs/TESTING.md`).

## tools/eval_detect.cpp

| Function | Purpose |
|---|---|
| `main` | Measure how well each detector finds faces of six different sizes in generated frames (default 1920 x 1080). |

### `main` (eval_detect)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `eval_detect profile1.ini [profile2.ini ...] [--people N] [--frame WxH]` (default `N = 20`; default frame size `1920x1080`, the default camera size, `FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT`). `--frame` sets the size of the generated frames (for example `--frame 1280x1080`, the size named by the speed requirement, or `--frame 3840x2160`: the smallest face a detector finds depends on the picture size, see `detect.md`, "Detection at different camera picture sizes"); the text is read with `sscanf("%dx%d")` and is not checked any further. Without any profile it prints a usage line and returns 2. The reference detector (YuNet from `models\permissive.ini`, see below) must be creatable: if it is not, the tool prints `cannot create the reference detector (models/permissive.ini)` and returns 1. A profile that cannot be loaded or whose detector cannot be created is skipped (the profile loader or the detector factory prints the reason, the tool itself prints nothing about it). In `run_tests.bat full` it runs with `permissive.ini`, `dev_noncommercial.ini`, `dev_noncommercial_square.ini`, `dev_noncommercial_onnx.ini` and `classical.ini` and `--frame 1920x1080` (the default); `scripts\run_tests_vs2015.bat` runs it with the same five profiles and the default frame size. The two SCRFD profiles on ncnn (`dev_noncommercial.ini`: fitted rectangle, `dev_noncommercial_square.ini`: 640x640 square) are listed side by side to compare them: at 1920x1080 and 20 portraits (`build\eval_detect_fit_vs_square.log`, run of 2026-11-12) both found the same share and gave the same mean IoU from 80 px up (100% and 0.94 at 80 px); at 50 px the rectangle found 85% and the square 90% (one portrait of 20, below the 80 px minimum crop width); the time per frame was 9.1 to 9.6 ms (median) with the rectangle against 17.1 to 18.4 ms with the square.

First line: `N portraits as actors; frames WxH; one face per frame (centre), 12 identical frames per portrait` (with the real frame size in place of `WxH`). Then for every profile a block `=== <profile path> : <detector name> ===` with the table

`face width | found | mean IoU | false/frame | median ms | p95 ms`

and one row for each face width 50, 80, 110, 160, 250 and 400 pixels. How to read it:

* **found** is the percentage of test frames in which the detector returned a box that overlaps the true face box with IoU of at least 0.5.
* **mean IoU** is the average overlap of those found boxes with the true boxes.
* **false/frame** is the number of detections per frame whose IoU with the true box is below 0.5, so it counts extra boxes and badly placed boxes (the header comment says the same: "detections with box overlap (IoU) below 0.5 to the true face").
* **median ms** and **p95 ms** are the time of one detection call over all 12 repeats of all frames of that width.

Accuracy (found, IoU, false/frame) is counted on the **first** of the 12 repeated calls of every portrait only. The twelve repeats exist because the Haar detector follows faces between full scans; showing the same frame 12 times includes both kinds of scans in the timing. The ground-truth box of each portrait is the first face that the reference detector (YuNet from `models\permissive.ini`) finds in the portrait; the comparison is therefore not independent for the YuNet detector. Only portrait folders that contain a readable `117th.jpg` and in which the reference detector finds a face are used (folders with only `116th.jpg` are skipped).

**How it works.** It builds the actor list once (`make_actor` for each usable portrait, at most `N`). For each profile and each width: for each actor it sets a one-point path with the face in the middle of the frame (`{0, frame_w / 2, frame_h / 2, width, 0}`, which is `{0, 960, 540, width, 0}` for the default size), renders a frame of the chosen size with `render_frame`, first runs the detector on an empty frame (so the Haar detector forgets the previous portrait and the first real call is a true full scan), then calls `detect` 12 times, timing each. On repeat 0 it compares every returned box with `truth[0].box` using `iou`.

**Engine.** Face detection (`face_detection.sln`).

**Used by.** `run_tests.bat full`; by hand.

## tools/eval_features.cpp

| Function | Purpose |
|---|---|
| `Sample` (struct) | One photo: person name, file name, feature. |
| `dot` | Dot product of two features (their cosine similarity). |
| `main` | Measure how well the features separate people, on real portraits. |

### `Sample` (struct)

```cpp
struct Sample {
    std::string person, file;
    Feature feature;
};
```

**What it does.** Holds the result for one portrait file: the folder name (`person`), the file name and the computed feature.

**How it works.** A plain struct; `main` collects all samples in a vector and builds a map from person to the indices of that person's samples.

**Used by.** `main` of this file.

### `dot`

```cpp
static float dot(const Feature& a, const Feature& b);
```

**What it does.** Returns the dot product of two features, which equals their cosine similarity because every feature has length 1. Both features must have the same length (not checked).

**How it works.** A loop that sums `a[i] * b[i]` in a `float`.

**Used by.** `main` of this file (all score lists, identification and false-accept counts).

### `main` (eval_features)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line:

`eval_features <profile.ini> [--type T] [--recognizer R] [--letters n-z] [--multi-only] <portrait folder>`

`--type` overrides the detector type (and **clears `pose.detector_profile`**, so the detector of that type does the refinement itself), `--recognizer` the recognizer type (for example `lbp`). `--letters n-z` evaluates only people whose folder name starts with a letter in that range (use the opposite range of the one given to `calibrate_lbp`). `--multi-only` evaluates only people who have at least two photos. The header comment and the usage message list all four options. Exit codes: 2 for wrong usage or unreadable profile, 1 if the detector, the pose detector (`create_pose_detector`) or the extractor cannot be created, if fewer than 2 usable photos were found (it then prints `need at least 2 usable photos`) or if all usable photos belong to one person (it then prints `need photos of at least 2 different people`), else 0.

For every photo it follows the pipeline's steps without its pose gate (the wording of the header comment): detect (main detector), take the largest face, refine its landmarks with the profile's **pose detector** (`refine_face(*pose_detector, ...)`, falling back to the plain landmarks), cut the crop, align and extract the feature. So the alignment landmarks, and with them the feature, come from the pose detector: for the SCRFD profiles from YuNet, not from SCRFD. Every usable photo is therefore used, whatever its head pose. Then it prints, in this order:

1. `detector <name>, extractor <name> (<dim> numbers)`, and, only when the profile names a pose detector (`pose.detector_profile`), a second line `pose check / alignment landmarks from: <pose detector name>`.
2. `photos used N (no usable face in M), people P, people with 2+ photos Q`, and `crop + align + extract: median X ms  p95 Y ms` (time of crop, alignment and network only, not detection).
3. If any impostor pair scores 0.45 or more: the 15 highest-scoring pairs of *different* named people. These may be labelling mistakes (the same person under two names); `scripts/merge_portraits.py` records aliases found this way.
4. `genuine pairs` (count, mean, minimum, median) and `impostor pairs` (count, mean, median, 99th and 99.9th percentile, maximum). Good features give genuine scores clearly higher than impostor scores.
5. A table `threshold | impostor pairs accepted (FAR) | genuine pairs rejected (FRR)` for the thresholds 0.20, 0.30, 0.363, 0.40, 0.45, 0.50, 0.55, 0.60 and the profile's own threshold (marked with `*`; the rows are in ascending order, and if the profile's threshold equals one of the listed values, that row is printed once, with the `*`). "Accepted" means score at or above the threshold; "rejected" means below it.
6. `threshold above every impostor score (FAR 0 on these pairs): T -> FRR a / b = x%`: the smallest threshold that accepts no impostor pair of this data, and what it costs in rejected genuine pairs. A note reminds you that a FAR of 1 in 10,000 needs about 100,000 impostor pairs to measure.
7. **1:N identification.** Everybody is registered with one photo (the first photo found for each person); every other photo of a person is a probe. `rank-1 correct` counts probes whose best match is the right person; the second number also requires the score to reach the profile's threshold.
8. **1:N false accepts.** Each person's first photo is searched in a gallery that does not contain that person; if the best score reaches the threshold the stranger is wrongly accepted. Printed for the thresholds 0.363, 0.45, 0.50, 0.55, 0.60 and the profile's own (in ascending order, the profile's marked with `*`, printed once if it equals a listed one), as `N of M strangers accepted (x%)`. This is the number that matters when a threshold must work among many registered people.

The comment in the file states the honest limits: the portraits are official, straight-on, evenly lit photos taken within a few years, and the number of people with two photos is small, so treat the numbers as indicative (see `docs/RESULTS.md`). The program stops cleanly when fewer than 2 photos are usable, and also when no pair of photos of two different people exists (the impostor list is empty); both cases are described under the exit codes above.

**How it works.** (1) Loop over the sub-folders and `.jpg` files and build the `Sample` vector. (2) All pairs `(i, j)` with `i < j` are scored with `dot`; pairs with the same folder name are genuine, others impostor; impostors from 0.45 up are also kept for step 3. (3) Both lists are sorted; percentiles are read at positions such as `size * 99 / 100`. (4) FAR and FRR use `std::lower_bound` on the sorted lists. (5) Identification loops over probes and gallery. A local `struct Pair` (score and two indices) and two small lambda functions (a mean, and the sort order of the top impostors) are used inside `main`.

**Engine.** Feature extraction (`feature_extraction.sln`; the profile's detector is used to find the face landmarks).

**Used by.** `run_tests.bat full` (with `permissive.ini` and with `dev_noncommercial.ini` on `data\portraits`); by hand.

## tools/eval_footage.cpp

| Function | Purpose |
|---|---|
| `TruthRow` (struct) | One line of `truth.csv`: the label, the exact face box and whether the face is fully visible. |
| `split_line` | Split a line of text at a separator character. |
| `portrait_feature` | The feature of the largest face of a portrait photo, made the way `AuthPipeline::extract_from_image` makes it. |
| `Bucket` (local struct) | One face-width class of the detection table: label, limits, how many true faces, how many found. |
| `main` | Evaluate the engines one by one on recorded footage (a generated 1920x1080 video with its truth). |

### `TruthRow` (struct)

```cpp
struct TruthRow {
    std::string label;
    Rect box;
    bool visible = false;
};
```

**What it does.** One line of `truth.csv` (written by `gen_video`): the person (`label`), the exact face box in pixels and `visible` (1 when the whole box is inside the frame). Only visible rows are evaluated.

**How it works.** A plain struct filled in `main`. (`tools/check_video.cpp` has a struct of the same name for the same file; they are separate copies.)

**Used by.** `main`.

### `split_line`

```cpp
static std::vector<std::string> split_line(const std::string& line, char sep);
```

**What it does.** Splits `line` at every `sep` character and returns the parts (an empty part between two separators is kept; no part after a final separator).

**How it works.** `std::getline` on a `std::stringstream` with `sep` as the delimiter.

**Used by.** `main` (the lines of `truth.csv`, separated by commas).

### `portrait_feature`

```cpp
static bool portrait_feature(FaceDetector& detector, FeatureExtractor& extractor, const std::string& path, Feature& feature);
```

**What it does.** Makes the feature of the largest face in the portrait photo at `path`, the way `AuthPipeline::extract_from_image` does (the plain detection, no second look): load the picture, detect the faces, take the one with the largest box, cut and align it (`make_face_crop`) and extract. Returns `true` and fills `feature`; returns `false` if the picture cannot be read, no face is found, the largest face has no landmarks, or the crop or the extraction fails. The classical (Haar) detector gives landmarks only when it finds both eyes, so for some portraits this is `false` with the classical profile.

**How it works.** As described. The face with the largest `box.area()` wins.

**Used by.** `main` (the registered people of the scenario and the filler people).

### `main` (eval_footage)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `eval_footage <profile.ini> <footage folder> [--fillers N] [--every K] [--fps N]`. The footage folder is what `tools/gen_video` writes (see `testdata.md`; `scripts\make_fhd_footage.bat` makes the six 1920x1080 videos that the evaluation uses: three clean ones and the same three with the **simulated** camera look, `fhd_cam_*`): `video.mp4` (or the numbered frames), `truth.csv` (the exact face box of every person in every frame) and `scenario.txt` (who is registered). The video is played through the same Media Foundation reader that a camera uses, so the frames are what a camera of that size would deliver. For every frame the engines are run **one after the other and timed on their own**, so the result is the engine-by-engine view; `tools/check_video` plays the same footage through the complete system. Nothing here uses a camera. Options: `--fillers N` (default 20: how many people who are not in the video are added to the registered people), `--every K` (default 3: extraction and matching run for every K-th frame), `--fps N` (default 15: the frame rate of a folder of numbered frames; a video file brings its own times). Exit code 2 when the profile cannot be loaded or its engines cannot be created, when `scenario.txt` or `truth.csv` is missing or empty, when the footage cannot be opened, or when fewer than two arguments are given (the usage line is printed); otherwise 0 (it only measures, it has no pass or fail). Standard output is unbuffered.

**How it works.**
1. **Setup.** The profile gives the detector, the extractor and, when the profile names one (`pose.detector_profile`), the pose detector; the first line printed is `profile <path>: detector <name>, pose check by <name>, extractor <name> (<dim> numbers), threshold <t>`, and the second is `all engines run on the CPU: yes (no GPU is used)` (the answers of `detector->runs_on_cpu()`, the pose detector's and `extractor->runs_on_cpu()` joined with "and"; `NO` if one of them were not on the CPU, which cannot happen in this project). `scenario.txt` is read (lines `person <label> registered|stranger`) and `truth.csv` (the columns `frame, time_ms, person, x, y, w, h, roll_deg, visible`).
2. **The registered people.** Every person the scenario marks `registered` is enrolled from his portrait `data/portraits/<label>/117th.jpg` with `portrait_feature`. A person whose portrait gives no face with landmarks for this profile cannot be enrolled; the program prints `cannot register <label>: this profile finds no face with landmarks in his portrait; his faces are left out of the matching figures` and leaves his faces out of the matching figures (this is what happens with the classical profile). Then up to `--fillers` other people from `data/portraits` (not a person of the scenario) are enrolled, so that a face is searched among N = registered + fillers people. It prints `people in the footage: P (R registered), registered in the gallery: R + F fillers = N = N`. (`R` counts the people that could be enrolled.)
3. **Detection and tracking, every frame.** The detector finds the faces and a `FaceTracker` follows them; both are timed. Each visible true face counts as found when a detected box overlaps it with IoU at least 0.5; the found boxes are counted per face width class (under 100 px, 100-199, 200-299, 300 and more; the classes without a true face are not printed) and their mean IoU is kept. A detection that is nobody's face counts as false. For each true face the track whose box overlaps it with IoU at least 0.4 (and that was seen in this frame) is "his" track: the set of track IDs of each person and the number of times his ID changed are kept.
4. **Feature extraction, every K-th frame.** For every found face at least `FA_MIN_CROP_FACE_PIXELS` wide (80), the pipeline's own steps run: the second look with exact landmarks (`refine_face` by the pose detector), the pose gate (`pose_within_tolerance`), the crop (`make_face_crop`) and the extraction; the time of each step is kept, and so is how many faces passed the gate.
5. **Matching.** The features of one frame (M faces) are compared with all N registered people in **one** M:N call, `store.matcher().match(...)`, timed. For a registered person: his similarity to himself (the dot product with his own feature, because the matcher reports only the best one), whether the matcher put him first (rank 1) and whether he was accepted (first and at or above the profile's threshold). For a stranger (a person of the scenario who is not registered): the highest similarity to anybody and whether he would be accepted (a false accept).
6. **Report.** Sections `DETECTION`, `TRACKING`, `FEATURE EXTRACTION` and `MATCHING`. Detection: the share of true faces found (with the numbers), the mean IoU, the share per face width class, the detections without a true face per frame, and the time per frame against the 60 ms limit for detection plus tracking (median, p95, maximum). Tracking: the largest number of track IDs one person got (1 means every person kept one ID), the number of ID changes in all, the tracker time, and detection plus tracking together. Feature extraction: how many faces were looked at and how many passed the pose gate, and the median time of the pose check, of crop and align, and of the extraction (limit 150 ms per face). Matching: for registered people the number of faces, the mean and lowest similarity to himself, the rank-1 and accepted shares; for strangers the number of faces, the highest similarity to anybody and the number accepted; and the M:N call (number of calls, mean M, median and maximum time).

What the numbers mean and do not mean: the actor in the footage is a flat photo, and the registered photo is the very same picture, so the similarity of a registered person to himself (0.93 to 0.97 on the clean footage, 0.91 to 0.97 on the `fhd_cam_*` footage) measures how well the features survive H.264 compression, scaling, tilt and (`fhd_cam_*`) the simulated noise, softness and light, not accuracy on different photos; the `fhd_cam_*` footage is a simulation of camera footage, not footage of a real camera; `tools/eval_features` measures that on separate photos. The strangers' scores are real: they are different people. With the classical profile none of the registered people can be enrolled from the portraits, so only detection, tracking and extraction (and strangers) are reported for it.

**Engine.** The integrated project (`face_authentication.sln`).

**Used by.** `run_tests.bat full` (five profiles, `permissive`, `dev_noncommercial`, `dev_noncommercial_square`, `dev_noncommercial_onnx` and `classical`, on each of the six footages `fhd_1person`, `fhd_3people`, `fhd_5people`, `fhd_cam_1person`, `fhd_cam_3people`, `fhd_cam_5people`) and `scripts\run_tests_vs2015.bat` (the same loop, always); by hand. The results are in `RESULTS.md`, sections 5c (clean footage), 5d (simulated camera look) and 5e (the fitted rectangle against the square on four of the footages).

## tools/gen_features.cpp

| Function | Purpose |
|---|---|
| `main` | Write a database of random registered features (CSV or SQLite) for speed and matching tests. |

### `main` (gen_features)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `gen_features <output.csv|output.db> [count=10000] [dim=512] [seed=1]`. It writes `count` imaginary people with random features of `dim` numbers as a database of registered features to the output file, creating its folder if needed. The **format follows the file name**: `.db`, `.sqlite` and `.sqlite3` give a SQLite database, anything else the default CSV file (a header line `id,name,card_id,dim,values`, then `id,name,card_id,dim,v0,...` per person with an empty `card_id`; see `feature_store.h` and `store.md`). `count` must be greater than 0 and `dim` between 1 and `FA_MAX_FEATURE_DIM` (`512`); otherwise it prints `count must be > 0 and dim in 1..512` and returns 2. A file that cannot be written gives `cannot write <file>: <reason>` and exit code 1. On success it prints `wrote N random features of D numbers to <file> (T ms)`. The same arguments always give the same file (the seed selects the sequence).

**How it works.** `make_synthetic_gallery(count, dim, seed)` (see `testdata.md`), each record is added to a `FeatureStore` (which rejects duplicate ids and features that are not finite vectors of length 1; the synthetic features are normalized, so all are accepted), then `store.save(path)` (the `Auto` format: by the extension). The time covers generation and writing.

**Engine.** Feature matching (`feature_matching.sln`; needs no model and no network library).

**Used by.** `run_tests.bat` (500 people of 128 numbers into `build\convert_demo.csv`, the input of the `db_convert` step) and `scripts\run_tests_vs2015.bat` (2,000 people of 256 numbers, as a CSV file and as a SQLite database); by hand (`docs/TESTING.md`: `gen_features.exe data\synthetic\f.csv 10000 512`, or `f.db` for a SQLite database). A file `data\synthetic\features_10000_d512.csv` exists in the project; it was written by an earlier version (no header line and no `card_id` column: the older format, which the store still reads, with empty cards). No test or tool in this repository reads such a file (the tests generate their data in memory).

## tools/gen_video.cpp

| Function | Purpose |
|---|---|
| `mean_abs_diff` | Mean absolute difference of two pictures, optionally with the second one turned upside down. |
| `main` | Generate a video of people walking toward the camera (frames, MP4, truth, scenario) and check the MP4. |

### `mean_abs_diff`

```cpp
static double mean_abs_diff(const Image& a, const Image& b, bool flip_b);
```

**What it does.** Returns how much two pictures of the same size differ: the average of the absolute difference of every colour value (0 means identical, 255 means opposite). With `flip_b = true` picture `b` is compared turned upside down (row `y` of `a` against row `height - 1 - y` of `b`). Returns -1 when the width, height or number of channels differ.

**How it works.** A double loop over rows and values, summing `abs(a - b)` in a `double`, divided by the number of values.

**Used by.** `main` of this file (the MP4 orientation check).

### `main` (gen_video)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line:

`gen_video <output folder> [seconds=8] [fps=15] [people=2] [first portrait index=0] [registered=1] [--any-pose] [--size WxH] [--camera-look] [--bitrate K]`

The four options `--any-pose`, `--size WxH`, `--camera-look` and `--bitrate K` may stand **anywhere** on the command line; every other word is a positional argument, read in order: output folder, seconds, fps, people, first portrait index, registered. (An option that needs a value, `--size` or `--bitrate`, as the very last word, or with a value that is not accepted, makes the program print the message of that option and exit with code 2.) `--size WxH` sets the picture size of the frames and of the video; the default is **1920 x 1080** (Full HD: `FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT`, the default camera size). Both numbers must be even and at least 64 (an MP4 needs an even size): otherwise the program prints `--size needs WxH with two even numbers of at least 64, for example --size 1920x1080` and exits with code 2.

`--camera-look` makes the footage look like a camera in a room: a **simulation, no camera is used and nothing is recorded**. It gives `render_frame` a `CameraLook` (see `testdata.md`): a cluttered room (furniture, a window, a door, a notice board with text-like lines), uneven light (the left side darker than the right) with darker corners, slow exposure changes, a warm colour cast, a slightly soft lens, motion blur of people who move fast enough (the walks `gen_video` makes are too slow for it to occur, see `testdata.md`) and sensor noise. The look is made in `main` as `CameraLook look; look.frame_ms = 1000 / fps; look.seed = 7 + people * 13 + first portrait index;` (the other members keep their defaults), so that another group of people gets another room and the motion blur uses the real frame time; `render_frame` gets `&look` with `--camera-look` and a null pointer without. The paths, the people and `truth.csv` are the same with and without the option (the exact face boxes stay valid), so `gen_video x 8 15 3 3 2` and `gen_video y 8 15 3 3 2 --camera-look` show the same people walking the same way, once clean and once with the look. The look also changes the **bitrate** of the video (below).

`--bitrate K` sets the H.264 bitrate of `video.mp4` in kilobits per second (`VideoWriter::open`'s `bitrate_kbps`). Without it the default depends on the look: **8000** without `--camera-look`, **4000** with it (a lower bitrate compresses harder and so adds compression damage, as a real camera's stream has). `K` must be a number of at least 500: otherwise the program prints `--bitrate needs the video bitrate in kbit/s, at least 500, for example --bitrate 4000` and exits with code 2.

The program writes into the output folder:

* `frame_0000.jpg`, `frame_0001.jpg`, ...: the numbered frames, `WxH` pixels (default 1920 x 1080), saved as JPEG (so they carry normal JPEG compression);
* `video.mp4`: the same frames as a real H.264 video file (written with `VideoWriter` at `fps` and at the bitrate described above, 8000 or, with `--camera-look`, 4000 kbit/s; if the writer cannot be opened it prints a warning and goes on with the JPEG frames only);
* `truth.csv`: starts with `frame,time_ms,person,x,y,w,h,roll_deg,visible` and has one line per person per frame: the exact face box (`x`, `y`, `w`, `h` in pixels), the head tilt `roll_deg` and `visible` (1 when the whole box is inside the frame, 0 otherwise, including people who have not arrived yet, whose box is all zeros);
* `scenario.txt`: one line per person, `person <label> registered` or `person <label> stranger`. The first `registered` people (the sixth argument, default `1`) are meant to be registered, the others are strangers; `check_video` registers them before it plays the video;
* `mp4_first_frame_readback.png`: the first frame as read back from the MP4 (see the check below).

Nothing here uses a camera. The program prints `chose P people (skipped S whose natural head pose is outside the comfortable range for the pose check[; --any-pose is on])` and, at the end, `wrote F frames of P people to <folder> (truth.csv, scenario.txt[, video.mp4])`. The number of frames is about `seconds * fps + 1` (the exact number follows from adding `1000 / fps` to the time until it passes `seconds * 1000`).

**Who is chosen.** By default only people with a **comfortable head pose** are used: for each portrait the face is found, refined (`refine_face`) and its pose measured; the portrait is used only if the pose is valid and `abs(pitch) <= 3.5`, `abs(yaw) <= 3.5` and `abs(roll) <= 4.0` degrees. The reason (from the code comment): the pose check refuses faces whose head is turned or nodded by more than a few degrees (the tolerances are 5 / 5 / 8), so a person whose natural pose is near the limit would be recognized very late or never. `--any-pose` uses everybody. The first `first portrait index` usable people are skipped.

How the people move and how big they are. The scene is laid out for 1280 x 1080 and **scaled to the chosen size**: `scale_x = width / 1280`, `scale_y = height / 1080` and `scale_face = min(scale_x, scale_y)`, the **smaller** of the two, so a face never grows out of a smaller picture and is not stretched. At the default 1920 x 1080 `scale_x` is 1.5, `scale_y` is 1 and `scale_face` is 1: the people stand wider apart but their faces are as wide as in a 1280 x 1080 picture; at 1280 x 720 `scale_y` is 0.667 and so are the faces. Person number `k` (counting from 0, `people` in all) walks in a straight line from height `420 scale_y` to height `520 scale_y`; the horizontal end position is `width * (k + 0.5) / people` (the people are spread evenly over the picture width), and the start position is the same shifted by `offset` pixels (to the right for odd `k`, to the left for even `k`). The face grows from `w0` to `w1` pixels wide, and the head starts tilted by 22 degrees (negative for odd `k`) and is straight after 35 percent of the video. Person `k` appears `k * 600` ms after the start (people arrive one after the other).

**Crowd-safe layout.** The picture of a person (`make_actor`, see `testdata.md`) is 2.8 face widths wide (head, hair, shoulders), so the picture of one person covers part of the face of his neighbour when the two centres are closer than about 1.9 face widths. With many people side by side the faces therefore stay smaller. With `spacing = width / people` the code uses `w1 = min(340 scale_face, spacing / 1.95)` (the width at the end), `w0 = min(90 scale_face, w1)` (the width at the start) and `offset = min(120 scale_x, max(0, (spacing - 1.95 w0) / 2))`, so that neighbours never cover a face at the start or at the end. At 1920 x 1080: 1 or 2 people reach 340 px, 3 people 328 px, 5 people 197 px. A first version of the 5-people footage had no such rule: faces were covered by the neighbours' pictures and one registered person was detected in only 64 of his 111 visible frames. That was a flaw of the footage, not of the engines; `tools/check_video` found it and the layout was fixed here.

**The MP4 check.** After writing, the program reads the first frame of `video.mp4` back through `open_video_file_source` (the Media Foundation reader that a camera also uses) and compares it with the original first frame (`upright`) and with the original turned upside down (`flipped`) using `mean_abs_diff`. It prints `MP4 check: read back WxH; mean difference to the original frame X, to the original turned upside down Y (0 = identical, 255 = opposite)` and then `MP4 check: PASSED (right way up, compressed copy is close to the original)` or `MP4 check: FAILED (flipped, wrong size or too different)`. It passes when `upright` is not -1 and below 12 and `flipped` is more than twice `upright`. The point (from the header comment): if the upside-down version were closer, camera pictures would arrive flipped. If the first frame cannot be read it prints `MP4 check: FAILED to read the first frame back`. No check is made when no MP4 was written.

Exit codes: 0 success (also when no MP4 was written), 1 when no portraits are found, the detector cannot be created, a frame cannot be written, or the MP4 check fails, 2 for wrong usage (no output folder given), an invalid `--size` or `--bitrate`, or when `models\permissive.ini` cannot be loaded. The usage message and the header comment both list `--any-pose`, `--size WxH`, `--camera-look` and `--bitrate K`.

**How it works.** It first splits the command line: `--any-pose` and `--camera-look` set flags, `--bitrate K` is read with `atoi` and checked (at least 500), `--size WxH` is read with `sscanf("%dx%d")` and checked (two even numbers of at least 64), and every other word goes into a list of positional arguments; the scale factors `scale_x`, `scale_y` and `scale_face` (see above) are computed from the size, the `CameraLook` is filled (frame time and seed, see `--camera-look`) and the bitrate is chosen (the given one, else 4000 with `--camera-look` and 8000 without). It loads `models\permissive.ini` and creates that detector (YuNet). For each folder in `data\portraits` (in directory order, until `people` actors exist) it reads `117th.jpg`, finds the face with the detector (folders without a readable `117th.jpg` or without a found face are skipped), applies the comfortable-pose rule and the `first portrait index` skip, and builds an actor with `make_actor` and `approach_path` (see `testdata.md`). Then it writes `scenario.txt`, opens `truth.csv` and the `VideoWriter` (at the chosen size and bitrate), and for each time `t = 0, 1000 / fps, ...` up to `seconds * 1000` it calls `render_frame` (with the chosen width and height, and with `&look` when `--camera-look` is on), saves the JPEG frame, writes the MP4 frame and appends the truth lines. After closing the video it runs the MP4 check.

**Engine.** The integrated project (`face_authentication.sln`).

**Used by.** `scripts\make_fhd_footage.bat` (which `run_tests.bat` and `scripts\run_tests_vs2015.bat` call: six 1920x1080 videos, `gen_video.exe data\videos\fhd_1person 8 15 1 0 1`, `fhd_3people 8 15 3 3 2` and `fhd_5people 8 15 5 8 3`: 8 seconds, 15 frames per second, the number of people, how many usable portraits are skipped first, how many of the people are registered; and the same three again with `--camera-look` as `fhd_cam_1person`, `fhd_cam_3people` and `fhd_cam_5people`); `run_tests.bat` and `scripts\run_tests_vs2015.bat` (`gen_video.exe data\videos\demo_720p 8 15 3 0 2 --size 1280x720`); by hand (`docs/TESTING.md`: `gen_video.exe data\videos\walk 8 15 2`). Its output is the input of `check_video`. `tests/test_pipeline.cpp` does not use this program's output; it renders its own frames in memory.

## tools/roll_probe.cpp

| Function | Purpose |
|---|---|
| `main` | Check whether the detector's landmarks follow an in-plane head tilt, using one portrait tilted by known angles. |

### `main` (roll_probe)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `roll_probe <profile.ini> <portrait.jpg>`. With fewer than two arguments it returns 2 without printing anything. It prints `portrait pose: pP yY rR` (the pitch, yaw and roll that the detector measures on the untouched portrait; the first detected face is used) and then a table

`face width | true roll | coarse: roll yaw pitch | refined: roll yaw pitch`

with one row for each face width 120, 250 and 400 pixels and each true roll -30, -20, -10, 0, 10, 20 and 30 degrees. "True roll" is the tilt that the scene generator applied; "coarse" is what the plain detection of the whole frame measures (by the profile's main detector), and "refined" is what `refine_face` measures (second, zoomed-in look, including its rotation compensation) **when run by the profile's pose detector** (`create_pose_detector`: for the SCRFD profiles YuNet, otherwise the profile's own detector), as in the pipeline. So for the SCRFD profiles the "refined" columns are YuNet's numbers. Rows say `no face` or `refinement failed` when that step finds nothing.

How to read it: in a good system the roll columns follow the true roll, and the yaw and pitch columns stay near the portrait's own values, because the generator only rolls the picture (no yaw or pitch). The difference between the coarse and the refined roll shows what the refinement adds (the comments in `detector.cpp` say that the refinement exists because landmark networks are least exact for small faces and under-report strong tilts). Positive roll is clockwise on the screen. Exit code 1 if the detector, the pose detector or the portrait cannot be loaded or no face is found in the portrait.

**How it works.** It creates the main detector (`create_detector`) and the pose detector (`create_pose_detector`), detects the face in the portrait with the main detector, makes an actor with `make_actor`, and for every width and roll sets a one-point path at the middle of the frame, renders a 1280 x 1080 frame with `render_frame`, detects with the main detector, and calls `refine_face` **with the pose detector** on the first detected face. Values are printed with one decimal.

**Engine.** Face detection (`face_detection.sln`).

**Used by.** Run by hand. It exercises exactly the one rotation the scene generator can create (in-plane roll, see `testdata.md`).

## tools/bench.cpp

| Function | Purpose |
|---|---|
| `main` | Build the `AuthPipeline` of each profile and time detection, extraction, pose check and matching against the requirements. |

This file holds only `main`. The measuring code (`g_factor`, `row`, `make_frame`, the detection test with its local `Size` list, the extraction, pose check and matching tests) lives in the five headers described in the sections below (`bench_common.h`, `bench_frames.h`, `bench_detect_part.h`, `bench_extract_part.h`, `bench_match_part.h`). The same headers are used by `bench_detect`, `bench_extract` and `bench_match`, so that every benchmark program times exactly the same thing in exactly the same way. `bench` is the one that goes through the complete pipeline and measures all three engines in one run.

### `main` (bench)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `bench [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...` (the options are read by `parse_bench_options`: `--threads` overrides the number of CPU threads of the profile, default: keep the profile's value; `--frames` is the number of timed repetitions, default `40`; `--i5-factor` replaces `g_factor`). Without a profile it prints a usage line and returns 2; a profile that cannot be loaded or whose pipeline cannot be initialised is skipped (the loader or `init` prints the reason). Otherwise it returns 0, also when every profile was skipped. Standard output is unbuffered.

For every profile it prints a heading `=== <profile>: detector <name>, extractor <name> (<dim> numbers), <t> thread(s) ===` and then the rows of the three measurements, in this order:

* `bench_detection`: `detect 1280x1080, 1 face` (limit 60 ms), `detect  640x640, 1 face` (limit 25 ms), `detect 1920x1080, 1 face` (limit 60 ms), `detect 1280x720, 1 face` (limit 60 ms), `detect 1280x1080, 3 faces (found F)` and `detect 1920x1080, 3 faces (found F)` (limit 60 ms each);
* `bench_extraction`: `extract 1 face (crop+align+net)` (limit 150 ms) and below it the line for the "ideal limit" of 50 ms;
* `bench_pose_check`: `pose refinement of 1 face` (no limit): the cost of one pose check of the pipeline's gate, run by the pipeline's pose detector. When that is a different detector from the main one (the SCRFD profiles: YuNet), the row reads `pose refinement of 1 face (other detector)`; the detection rows above it are always the main detector.

After all profiles it prints the matching part (`bench_matching`): the heading `=== matching, M:N AVX2: M query faces against N = 10,000 registered features ===` and nine rows, independent of any profile. The meaning of each row, its limit and what is printed when a row cannot be measured are described under the three functions. How to read it: the limits are the requirements for the **target** PC. The column `this PC` PASS/FAIL refers to the machine you are running on; `est. i5` is only an estimate (see `g_factor`) and is only meaningful when the program runs on a PC that is much faster than the target, such as the development PC. Run the program on the real target PC (`bench.exe --threads 2 ...` as in `docs/TESTING.md`) and read the `this PC` column there. The requirement is "face detection + tracking", but the program times the detection only; the header comment says so and adds that the tracker needs less than 0.01 ms (see the `track` column printed by `test_pipeline`). The timings of the detectors do not include loading the models (a warm-up call is made first).

**How it works.** `parse_bench_options(argc, argv, "bench", true, options)`; if it returns `false`, return 2. For each profile: `load_profile(path, ".", profile)`, then `options.threads` replaces `profile.threads` when it is above 0, and an `AuthPipeline` is created with `init` and the store file `build\bench_store.csv` (synchronous mode, no worker thread) only to get a detector, its pose detector and an extractor; `process` is never called. After the heading it calls `bench_detection(detector, frames)`, `bench_extraction(detector, pipeline.extractor(), frames)` and `bench_pose_check(detector, pose, frames)`. `pose` is a null pointer when `pipeline.pose_detector()` is the same object as `pipeline.detector()` (the profile names no pose detector: the pipeline then uses the main detector for the pose check, and the address comparison tells it) and the address of `pipeline.pose_detector()` otherwise. After the loop `bench_matching()` is called once.

**Engine.** The integrated project, all three engines through the pipeline (`face_authentication.sln`).

**Used by.** `run_tests.bat full` (`--threads 2` and five profiles: `permissive`, `dev_noncommercial`, `dev_noncommercial_square`, `dev_noncommercial_onnx` and `classical`); `scripts\run_tests_vs2015.bat` runs `bench` with two profiles only (permissive and dev_noncommercial); by hand.

## tools/bench_common.h

What all four benchmark programs share: the estimate for the target PC, the result row and the command-line options. A header; every program that includes it is one `.cpp` file, so the `static` items below exist once in each program. The measurements themselves are in `bench_detect_part.h`, `bench_extract_part.h` and `bench_match_part.h`.

| Function | Purpose |
|---|---|
| `g_factor` (global) | The "estimated 4th-generation i5" multiplier. |
| `row` | Print one timing line and, when there is a limit, PASS or FAIL. |
| `BenchOptions` (struct) | The command-line options: thread count, number of repetitions, profile files. |
| `parse_bench_options` | Read the command line of a benchmark program. |

### `g_factor`

```cpp
static double g_factor = 3.5;
```

**What it does.** The factor that turns a time measured on this PC into an *estimate* for the target PC, a 4th-generation Intel Core i5. The default `3.5` can be changed with `--i5-factor F`. The header comment explains the value as "Haswell core at about 2.6-3.3 GHz versus a modern core at about 4.5 GHz or more, same thread count". It is an estimate, not a measurement.

**How it works.** A file-level variable. `parse_bench_options` sets it; `row` and the "ideal limit" line of `bench_extraction` read it.

**Used by.** `row`, `bench_extraction`, `parse_bench_options`.

### `row`

```cpp
static void row(const char* what, const fa::TimingStats& t, double limit_ms);
```

**What it does.** Prints one result line: `<label>  median M  p95 P  max X ms   est. i5 E ms`, where `E = median * g_factor`. The label is padded to 34 characters. If `limit_ms` is greater than 0 it appends `limit L: this PC PASS|FAIL, est. i5 PASS|FAIL`. The comparison uses the **median**, not the p95 or the maximum: PASS means `median <= limit`. A limit of 0 means "no limit, just show the time".

**How it works.** Reads `median()`, `p95()` and `max()` from the `TimingStats` and prints them with `printf`.

**Used by.** `bench_detection`, `bench_pose_check`, `bench_extraction` and `bench_matching` (all timing lines except the "ideal limit" line, which `bench_extraction` prints itself).

### `BenchOptions` (struct)

```cpp
struct BenchOptions {
    int threads = 0;
    int frames = 40;
    std::vector<std::string> profiles;
};
```

**What it does.** The result of reading the command line. `threads` is the number of CPU threads that replaces the profile's `runtime.threads`; 0 (the default) keeps the profile's value. `frames` is the number of timed repetitions of the detection, pose check and extraction tests (default 40); the matching test always makes 50 runs and ignores it. `profiles` are the profile files, in the order given.

**How it works.** A plain struct filled by `parse_bench_options`.

**Used by.** `parse_bench_options` and `main` of `bench`, `bench_detect`, `bench_extract` and `bench_match` (the last one reads none of the fields).

### `parse_bench_options`

```cpp
static bool parse_bench_options(int argc, char** argv, const char* program, bool needs_profile, BenchOptions& options);
```

**What it does.** Reads `--threads N`, `--frames K` and `--i5-factor F` (the last one sets `g_factor`); every other word is a profile file. An option name must be followed by a value: an option name that is the very last word of the command line is not recognised and is taken as a profile file name (so the program then skips it because it cannot be loaded). The values are not checked: `--threads` and `--frames` are read with `atoi` and `--i5-factor` with `atof`, so text that is not a number gives 0. With `--frames 0` nothing is timed and every time is printed as 0. When `needs_profile` is true and no profile was given, it prints `usage: <program> [--threads N] [--frames K] [--i5-factor F] profile.ini ...` (to standard output; `program` is the name that the caller passes) and returns `false`; the caller then exits with code 2. Otherwise it returns `true`.

**How it works.** One loop over `argv[1]` to the end with an `if / else if` chain. The comment in the source and the text that is printed agree: `usage: <program> [--threads N] [--frames K] [--i5-factor F] profile.ini ...`.

**Used by.** `main` of `bench`, `bench_detect` and `bench_extract` (with `needs_profile = true`) and of `bench_match` (with `false`, so it never fails).

## tools/bench_frames.h

The test picture of the detection and extraction benchmarks.

| Function | Purpose |
|---|---|
| `make_frame` | Build a test frame of a given size (1280 x 1080, 1920 x 1080, ...) with a given number of portraits. |

### `make_frame`

```cpp
static bool make_frame(fa::FaceDetector& detector, int width, int height, int faces, fa::Image& frame);
```

**What it does.** Builds a test frame of `width` x `height` pixels that contains `faces` portraits. Returns `false` if fewer than `faces` usable portraits exist. With one face the face is in the middle and its width is 30 percent of the shorter frame side; with several faces the first is at 28 percent of the frame width, the next ones follow 30 percent of the frame width apart, and each face is 20 percent of the shorter side wide. All faces are at half the frame height and not tilted.

**How it works.** For each folder in `data\portraits` whose `117th.jpg` can be read and in which the given detector finds a face, it creates an actor with `make_actor` (from the first face found) and a one-point path, until `faces` actors exist. Then one `render_frame` call draws them. Note that the **profile's own detector** finds the portrait faces here (not YuNet as in `eval_detect`).

**Used by.** `bench_detection` (one-face frames at 1280 x 1080, 640 x 640, 1920 x 1080 and 1280 x 720, three-face frames at 1280 x 1080 and 1920 x 1080), `bench_pose_check` (a one-face frame at 1280 x 1080) and `bench_extraction` (a one-face frame at 1920 x 1080); so by `bench`, `bench_detect` and `bench_extract`.

## tools/bench_detect_part.h

The speed measurements of the face detection engine.

| Function | Purpose |
|---|---|
| `bench_detection` | Time the detection of one face at four frame sizes and of three faces at two frame sizes. |
| `Size` (local struct) | Width, height, limit and label of one detection test. |
| `bench_pose_check` | Time the pose check (`refine_face`) of one face. |

### `bench_detection`

```cpp
static void bench_detection(fa::FaceDetector& detector, int frames);
```

**What it does.** Times `detector.detect` on six test frames and prints one row for each with `row`, in this order:

* `detect 1280x1080, 1 face` against a limit of 60 ms;
* `detect  640x640, 1 face` against 25 ms;
* `detect 1920x1080, 1 face` against 60 ms;
* `detect 1280x720, 1 face` against 60 ms;
* `detect 1280x1080, 3 faces (found F)` against 60 ms (`F` is how many of the three were found in the last run);
* `detect 1920x1080, 3 faces (found F)` against 60 ms (the same at the default camera size).

The limits are the requirements for the target PC (a 4th-generation Intel Core i5): face detection plus tracking at most 60 ms at 1280 x 1080 and 25 ms at 640 x 640. The requirement names only these two sizes; the 60 ms limit is applied **by analogy** to the two added sizes, 1920 x 1080 (the default camera size) and 1280 x 720 (a size that cameras commonly deliver). Measured on this PC at 1920 x 1080 (one face, the run of the 1920x1080 evaluation, `RESULTS.md` section 5c): YuNet 6.6 ms, SCRFD on ncnn 19.3 ms (the 640x640 square; with the fitted rectangle of `dev_noncommercial.ini` 11.5 ms, `RESULTS.md` section 5e, and 17.9 ms for `dev_noncommercial_square.ini` in the same run), SCRFD on ONNX Runtime 10.0 ms, Haar 10.9 ms. A square picture (the `detect 640x640` row) gets no gain from the rectangle: 20.1 ms against 19.2 ms. Only the detection is timed; the tracker needs less than 0.01 ms (see the `track` column printed by `test_pipeline`). The result is the median of `frames` timed calls after one warm-up call. When `make_frame` cannot make one of the one-face frames (not enough usable portraits) it prints `    (no usable test frame)` in place of that row; when it cannot make a three-face frame, that row is left out without a message.

**How it works.** A local list of four `Size` entries (1280x1080, 640x640, 1920x1080, 1280x720). For each: `make_frame` (one face), one warm-up call, then `frames` calls timed with `now_ms()` into a `TimingStats`, and `row(label, t, limit)`. The two three-face rows (1280 and 1920 wide, 1080 high: a loop over the two widths) do the same with `make_frame(..., 3, ...)`, and their label is made with `snprintf` so that it can show how many faces were found.

**Used by.** `main` of `bench` (the profile's main detector from the pipeline) and `bench_detect` (the detector it created itself).

### `Size` (local struct)

```cpp
struct Size { int w, h; double limit; const char* label; };
```

**What it does.** One entry of the detection test list: frame width and height, the time limit in milliseconds and the printed label.

**How it works.** Defined inside `bench_detection`; four entries are used: `1280 x 1080` with limit 60 ms, `640 x 640` with limit 25 ms, `1920 x 1080` with limit 60 ms and `1280 x 720` with limit 60 ms.

**Used by.** `bench_detection`.

### `bench_pose_check`

```cpp
static void bench_pose_check(fa::FaceDetector& detector, fa::FaceDetector* pose_detector, int frames);
```

**What it does.** Times the pose check of the pipeline's gate, `refine_face` (second, zoomed-in look at the face), for one face in a 1280 x 1080 frame. `detector` finds the face (and makes the frame); `pose_detector` runs the refinement. A null `pose_detector` means that the same detector does both, as in the profiles without `pose.detector_profile`. The row is `pose refinement of 1 face`, or `pose refinement of 1 face (other detector)` when `pose_detector` is not null (the SCRFD profiles, where YuNet measures the pose); there is no limit. When no frame can be made it prints `    (no usable test frame)`, and when the detector finds no face in it `    (the detector found no face in the test frame: no pose check to time)`; in both cases it returns without a timing row.

**How it works.** `make_frame` (one face, 1280 x 1080), one `detect` with `detector`, then `frames` timed calls of `refine_face(refiner, frame, faces[0], refined)` where `refiner` is `*pose_detector` or `detector`. There is no separate warm-up call, so the first of the `frames` samples includes the first-call cost of the pose detector (the median hides it). The return value of `refine_face` is not checked.

**Used by.** `main` of `bench` (passes the pipeline's pose detector, or null when it is the main detector) and of `bench_detect` (passes the pose detector it created, or null when the profile names none).

## tools/bench_extract_part.h

The speed measurement of the feature extraction engine.

| Function | Purpose |
|---|---|
| `bench_extraction` | Time cutting out, aligning and extracting one face. |

### `bench_extraction`

```cpp
static void bench_extraction(fa::FaceDetector& detector, fa::FeatureExtractor& extractor, int frames);
```

**What it does.** Prints the row `extract 1 face (crop+align+net)` against a limit of 150 ms, and below it a line `      (ideal limit 50 ms: this PC PASS|FAIL, est. i5 PASS|FAIL)` for the wished-for 50 ms. The time is that of `make_face_crop` plus `extractor.extract` for one face: cut the face out, align it by its landmarks, run the network (or the classical LBP). The result is the median of `frames` runs after one warm-up run. `detector` is used only to find the face and its landmarks in the test frame (1920 x 1080, the default camera size, one portrait); the landmarks are those of the plain detection, without the pose check. When no frame can be made it prints `    (no usable test frame)`, and when the detector finds no face with landmarks in it `    (the detector found no face with landmarks in the test frame: nothing to time)`; in both cases it returns without a timing row. With `models\classical.ini` no extraction row appears (checked on this PC): the Haar detector finds no face with landmarks in that frame, so `bench` and `bench_extract` print the second message above instead of an extraction row for that profile (the other profiles print the row). `test_feature` and `eval_features` run the classical extractor on portraits instead.

**How it works.** `make_frame`, `detect`, `make_face_crop` of the first face and one warm-up `extract`; then `frames` times `now_ms()`, `make_face_crop`, `extract`, `t.add(now_ms() - a)`; `row(..., 150)`; then the ideal-limit line, comparing the median and the median times `g_factor` with 50.

**Used by.** `main` of `bench` (the pipeline's detector and extractor) and of `bench_extract` (the detector and extractor it created itself).

## tools/bench_match_part.h

The speed measurement of the feature matching engine.

| Function | Purpose |
|---|---|
| `bench_matching` | Time one M:N match of M = 1, 4 and 16 faces against N = 10,000 registered features of three lengths. |

### `bench_matching`

```cpp
static void bench_matching();
```

**What it does.** Prints an empty line, the heading `=== matching, M:N AVX2: M query faces against N = 10,000 registered features ===` and nine rows: feature lengths 128, 256 and 512, each with M = 1, 4 and 16 query faces (the label is `<dim> numbers, <q> face(s)`). Each row is the time of one `match` call that compares all M query faces with all N = 10,000 registered people at once (M:N). There are no limits: the requirement for matching is "as fast as possible". It does not depend on a model profile and needs no model, no picture and no network library.

**How it works.** For each length: a `FeatureStore` is filled with `make_synthetic_gallery(10000, dim, 1)` (see `testdata.md`) and its `matcher()` is used. For 1, 4 and 16: `random_feature` makes the query features (random generator seeded with 3), one warm-up `match`, then 50 timed `match` calls (always 50: the `--frames` option does not apply) into a `TimingStats`, and `row(label, t, 0)`.

**Used by.** `main` of `bench` (once, after all profiles) and of `bench_match`.

## tools/bench_detect.cpp

| Function | Purpose |
|---|---|
| `main` | Time the face detection engine alone: detection and pose check, for each profile. |

### `main` (bench_detect)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `bench_detect [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...` (options as for `bench`, see `parse_bench_options`). It measures the **face detection engine alone**: no extractor, no matcher, no pipeline and no store file. For each profile it creates the detector (`create_detector`) and, only if the profile names one (`pose.detector_profile`), the pose detector (`create_pose_detector`), prints `=== <profile>: detector <name>, <t> thread(s) ===` and then the rows of `bench_detection` (six rows) and `bench_pose_check` (one row), the same rows and labels that `bench` prints for the detector. Needs `data\portraits` for the test pictures and the model files of the detectors. Exit codes: 2 when no profile is given (the usage line is printed), otherwise 0; a profile that cannot be loaded, or whose detector or pose detector cannot be created, is skipped (the loader or the factory prints the reason), also when that leaves nothing to measure. Standard output is unbuffered.

**How it works.** `parse_bench_options(argc, argv, "bench_detect", true, options)`; for each profile `load_profile(path, ".", profile)`, `options.threads` replaces `profile.threads` when above 0, `create_detector(profile)`, and `create_pose_detector(profile)` only when `profile.pose_detector_profile` is not empty (otherwise the pointer stays empty and `bench_pose_check` lets the main detector do the pose check). Then `bench_detection(*detector, frames)` and `bench_pose_check(*detector, pose_detector.get(), frames)`.

**Engine.** Face detection (`face_detection.sln`).

**Used by.** Run by hand, for example `bench_detect.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini models\dev_noncommercial_square.ini` (`docs/TESTING.md`; the last two compare the fitted rectangle and the square of SCRFD on ncnn). `scripts\run_tests_vs2015.bat` runs it with `permissive`, `dev_noncommercial`, `dev_noncommercial_square`, `dev_noncommercial_onnx` and `classical`. Neither `run_tests.bat` nor `build.bat` start it (`build.bat` only builds it).

## tools/bench_extract.cpp

| Function | Purpose |
|---|---|
| `main` | Time the feature extraction engine alone: cut out, align and extract one face, for each profile. |

### `main` (bench_extract)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `bench_extract [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...` (options as for `bench`). It measures the **feature extraction engine alone**: no pipeline, no matcher. For each profile it creates the detector and the extractor (`create_detector`, `create_extractor`), prints `=== <profile>: extractor <name> (<dim> numbers), <t> thread(s) ===` and then the rows of `bench_extraction`: `extract 1 face (crop+align+net)` against 150 ms and the line for the ideal limit of 50 ms. The profile's detector is created only to find the face and its landmarks in the test picture (`data\portraits`); it is not measured and not named in the heading. No pose detector is created. Exit codes: 2 when no profile is given (the usage line is printed), otherwise 0; a profile that cannot be loaded, or whose detector or extractor cannot be created, is skipped. With `models\classical.ini` the heading is followed by the message `(the detector found no face with landmarks in the test frame: nothing to time)` instead of a row (see `bench_extraction`). Standard output is unbuffered.

**How it works.** `parse_bench_options(argc, argv, "bench_extract", true, options)`; for each profile `load_profile(path, ".", profile)`, `options.threads` replaces `profile.threads` when above 0, `create_detector(profile)` and `create_extractor(profile)` (both get the same thread count), then `bench_extraction(*detector, *extractor, frames)`.

**Engine.** Feature extraction (`feature_extraction.sln`).

**Used by.** Run by hand, for example `bench_extract.exe --threads 2 models\permissive.ini models\dev_noncommercial.ini` (`docs/TESTING.md`). Not started by `run_tests.bat`.

## tools/bench_match.cpp

| Function | Purpose |
|---|---|
| `main` | Time the feature matching engine alone: one M:N match against 10,000 registered features. |

### `main` (bench_match)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `bench_match [--i5-factor F]`. It prints the matching rows of `bench_matching` (the heading and nine rows: lengths 128, 256 and 512, with 1, 4 and 16 query faces; see there) and nothing else. It needs **no model, no picture and no network library**: the 10,000 registered features are generated (`src/testdata/synthetic.cpp`), so it also runs on a PC that has only the executable. It takes no profile: it calls `parse_bench_options` with `needs_profile = false`, so the function never fails and the program never prints a usage line. The options `--threads` and `--frames` and any other word are read but have no effect here (the matching test always makes 50 runs). Exit code: always 0. Standard output is unbuffered.

**How it works.** `parse_bench_options(argc, argv, "bench_match", false, options)` (only `--i5-factor` matters), then `bench_matching()`.

**Engine.** Feature matching (`feature_matching.sln`, which holds no network library).

**Used by.** Run by hand: `bench_match.exe` (`docs/TESTING.md`). Not started by `run_tests.bat`.

## tests/test_common.cpp

| Function | Purpose |
|---|---|
| `CHECK` (macro), `g_failures` | Print `ok:` or `FAIL:` for one check and count failures. |
| `main` | Test the foundation layer: image operations, picture files, the recorded-frame source, the video file writer and reader, text drawing, the choice of a camera mode for a requested picture size, the simulated camera look of the generated footage, model profile files, loading models with both network libraries, and that everything runs on the CPU. |

### `CHECK` and `g_failures`

```cpp
static int g_failures = 0;
#define CHECK(cond, msg) /* prints "ok:   msg" or "FAIL: msg", counts failures */
```

**What it does.** `CHECK(condition, "text")` prints `ok:   text` when the condition is true and `FAIL: text` (and adds 1 to `g_failures`) when it is false. The program goes on after a failure so you see every problem at once. The same macro, with the same behaviour, is copied at the top of `test_detect_units.cpp`, `test_feature.cpp`, `test_match.cpp` and `test_pipeline.cpp`.

**How it works.** A `do { ... } while (0)` macro around an `if`/`else` that calls `std::printf`.

**Used by.** `main` of each of the five test programs.

### `main` (test_common)

```cpp
int main();
```

**What it does.** Start with `build\bin\test_common.exe` (no arguments; needs `models\permissive\yunet.onnx` and `models\dev_noncommercial\det.param` / `det.bin` and `det_dyn.param` / `det_dyn.bin`, and an existing `build\` folder because it writes `build\test_common.png`). Lines starting with `ok:` or `FAIL:` are checks; indented lines are information. The last line is `ALL TESTS PASSED (0 failures)` or `TESTS FAILED (N failures)`, and the exit code is 0 only when everything passed. The program makes a 1280 x 1080 gradient picture (blue grows to the right, green grows downward, red is constant) and checks, in this order (**74 checks**: 58 of the first groups, 13 of the camera look and 3 of the CPU-only group; the source has 74 `CHECK(` calls. History of the count: 66 before the SCRFD fitted rectangle; 69 with the three checks of group 14 (the run of 2026-11-12, `build\test_common_dyn.log` and the Visual Studio 2019 full run, `build\run_tests_final5.log`); 74 with the two checks of group 3 and the three layout checks of group 11, which were added after those two full runs. The 74 was seen as `ok:` lines in the Visual Studio 2019 build (full rebuild) and the Visual Studio 2015 build (incremental) after that change, not in a full flow):

1. `resize_bilinear` gives the requested 640 x 540, and the middle of the blue gradient is still about 127 (within 2).
2. `letterbox` to 640 x 640 gives that size, a scale of 0.5, `pad_x` 0 and `pad_y` 50. An information line shows the median time of 50 such letterbox runs.
3. **`letterbox_fit`** (2 checks; the rectangle that SCRFD with `detector.layout = chw` uses, see `common.md`). Check 1, `letterbox_fit gives the smallest multiple-of-32 rectangle for a long side of 640 (1920x1080 -> 640x384, 1280x1080 -> 640x544, a square stays 640x640)`: for seven picture sizes the test makes a picture (`Image::create`, contents not looked at), calls `letterbox_fit(src, 640, 32, fitted, ...)` and requires the exact result size, which must also be a multiple of 32 on both sides: 1920x1080 gives 640x384, 1280x1080 gives 640x544, 1280x720 gives 640x384, 640x480 gives 640x480, 640x640 gives 640x640, 480x640 (portrait) gives 480x640 and 2560x1440 gives 640x384; if one size is wrong the test prints which (`letterbox_fit <size>: got WxH`). Check 2, `letterbox_fit: scale 1/3, padding 12 rows above and below, a marker lands where scale and padding say, the padding is gray (114)`: a black 1920x1080 picture with a white 20x20 square at x 960 to 979, y 540 to 559 is fitted; the test requires `scale` equal to 640/1920 (within 1e-6), `pad_x` 0 and `pad_y` 12, the green value at the point where the middle of the square (970, 550) must land (`970 * scale + pad_x`, `550 * scale + pad_y`) above 128, and the green value at row 10, column 100 (inside the padding) exactly 114.
4. `estimate_similarity` recovers the rotation and scale of a made-up transform (rotation 30 degrees, scale 2, shift (10, 20)), and `invert_similarity` undoes a transform (a point goes forward and back to where it started), within 0.001.
5. `save_image` writes `build\test_common.png` and `load_image` reads back exactly the same pixels.
6. **Recorded frames** (4 checks, see `camera.md`). The test deletes and recreates the folder `build/seq`, writes three PNG pictures `frame_0000.png` to `frame_0002.png` into it (copies of the 640 x 540 picture whose first byte is set to 10, 20 and 30 as a frame marker) and calls `open_sequence_source("build/seq", 10.0)`. Checks: the pictures could be written and a source was returned (`open_sequence_source opens a folder of pictures`); three `next` calls return the frames in file order (width 640, correct marker) with the times exactly 0, 100 and 200 ms (10 frames per second); a fourth `next` returns `false` (end of the recording); and `open_sequence_source("build/no_such_folder", 10.0)` returns no source. The folder `build/seq` is left behind afterwards.
7. **Video file** (8 checks, see `camera.md`): a `VideoWriter` / `open_video_file_source` round trip. The test makes 20 pictures of 320 x 240 pixels in which the top half is red, the bottom half is blue (so that a flipped picture can be recognised) and a white bar, 12 pixels wide and in rows 101 to 139, moves 12 pixels to the right with every frame (so that the order of the frames can be recognised). It writes them with `VideoWriter::open("build/test_video.mp4", 320, 240, 10.0)` and `write`. Checks: `VideoWriter opens an .mp4 file`; `20 frames written and the file finished` (all `write` calls succeeded, `close()` returned `true` and `frames_written()` is 20); `open_video_file_source opens the written file`; then the file is played to its end and the test prints `read back N frames, first at ... ms, last at ... ms` and checks `all 20 frames come back at the right size` (20 frames, each 320 x 240); `the pictures come back the right way up (red on top, blue below)` (in every frame a pixel in row 40 is red and a pixel in row 200 is blue, with generous limits because H.264 is lossy); `the frames come back in the order they were written` (the x position of the white bar, found in row 120, grows from frame to frame); `the time stamps are 0 ms ... about 1900 ms at 10 fps` (the first time is exactly 0 and the last is within 60 ms of 1900); and `a missing video file gives no source` (`open_video_file_source("build/no_such_video.mp4")` returns `nullptr`). The file `build/test_video.mp4` is left behind.
8. **Text drawing** (1 check, see `draw_text` under `check_video`): the test draws `ID 7` in green (blue 0, green 255, red 0) with letters 22 pixels high into a black 200 x 60 picture and counts the pixels that are clearly green (green above 200, blue and red below 60); the check `draw_text puts green letters into the picture` requires more than 30 of them.
9. **Which camera mode suits a requested picture size** (18 checks, see `pick_camera_mode` in `camera.md`; no camera is opened, the test builds lists of `CameraMode` entries by hand with `CameraMode(width, height, fps, format)` and calls `pick_camera_mode`; every check text starts with `camera mode:`). The lists and what must come out (the number is the index in the list):
   * `webcam`: 640x480, 1280x720 and 1920x1080 as MJPG at 30 fps (entries 0 to 2), then 640x480 YUY2 at 30 fps, 1280x720 YUY2 at 10 fps and 1920x1080 YUY2 at 5 fps (entries 3 to 5). A request of 1920x1080 gives entry 2 (the 30 fps mode, not the 5 fps one); a request of 0x0 gives entry 2 as well, and the test also checks that `FA_CAMERA_DEFAULT_WIDTH` is 1920 and `FA_CAMERA_DEFAULT_HEIGHT` is 1080 (so the test fails if the defaults are changed); 1280x720 gives entry 1 (not the 10 fps one); 640x480 gives entry 0 (a tie between entries 0 and 3 goes to the earlier entry); 1280x1080, which the camera does not offer, gives entry 2 (1280x720 and 1920x1080 are equally close, the larger picture wins).
   * `small` (640x480 YUY2 and 1280x720 MJPG, both 30 fps): a request of 1920x1080 gives the larger one, 1280x720.
   * `uhd` (3840x2160 and 1920x1080 MJPG): 1920x1080 is asked for and given (the larger picture is not taken just because it is larger); 3840x2160 can be asked for and is given.
   * `four_three` (640x480, 1024x768 and 1600x1200 MJPG, a 4:3 camera): 1920x1080 gives 1600x1200; 800x600 gives 640x480 (1.25 times too small is relatively closer than 1024x768, 1.28 times too large); 960x720 gives 1024x768.
   * `slow` (640x480 YUY2 at 10 fps and 1920x1080 YUY2 at 5 fps): when every mode is slow they are all kept and the closest size, 1920x1080, wins.
   * `coded` (H264 1920x1080 and MJPG 1280x720): a request of 1920x1080 gives the MJPG mode; after the MJPG entry is erased the H264 mode is given, because it is all there is.
   * `same` (1280x720 NV12 at 24 fps and 1280x720 MJPG at 30 fps): the faster mode wins.
   * `unreported` (1920x1080 MJPG with frame rate 0 and 1280x720 MJPG at 30 fps): the first wins, an unreported frame rate is not held against a mode.
   * An empty list gives -1, and so does a list whose only mode has size 0x0.
10. **The camera look of the generated footage** (13 checks, all with a check text that starts with `camera look:`; see `CameraLook` and `render_frame` in `testdata.md`; no camera is involved, the test draws pictures). The test makes a textured sprite 280 x 340 pixels with the face box (90, 80, 100, 120) that moves fast to the right (400 pixels per second: from x = 300 to x = 700 in 1000 ms, face width 120), a copy that stands still, and renders 960 x 540 pictures with `render_frame`. Four small helpers (lambda functions) do the measuring: `quiet()` makes a `CameraLook` with every effect off (no room, no noise, no blur, no motion blur, no vignette, no slope, no drift, no cast), `mean_of(picture, x0, y0, x1, y1, channel)` the average of one colour over a rectangle, `sharpness(picture, x0, y0, x1, y1)` the sum of the horizontal changes of the green value over a rectangle (smaller = softer), and `same_image(a, b)` whether two pictures are identical byte for byte. The checks:
   * the **ground truth is untouched**: with and without the default look (every effect on) at 500 ms the actor is visible and the face box has exactly the same `x`, `y`, `w` and `h`;
   * **deterministic**: rendering the same frame (same time, frame index and look) twice gives byte-identical pictures;
   * **another frame index gives another noise** and **another seed gives another room**: the pictures differ (two checks);
   * **the sensor noise is really there**: two frames of an empty background (frame indexes 1 and 2) with only `noise_sigma = 6` switched on differ, in the green value, with a standard deviation of more than 6 levels, and the same two frames without a look with less than 4 (the measured values are printed: 10.5 against 2.8 in the run of 2026-11-12);
   * **uneven light**: with only `light_slope = 0.2` the average of a rectangle at the left edge is below 0.9 times that of one at the right edge, and in the plain scene it is above 0.97 times (the light is even);
   * **vignette**: with only `vignette = 0.25` the top-left corner (40 x 40 pixels) is below 0.95 times the middle of the top edge, and in the plain scene above 0.97 times;
   * **warm colour cast**: with only `warm = 0.06` the ratio of red to blue in a rectangle is more than 1.08 times the ratio in the plain scene;
   * **lens softness**: the sharpness of the moving person's rectangle with only `blur_sigma = 0.9` is below 0.9 times that of the sharp picture;
   * **motion blur**: with only `motion_blur = 0.4` the same rectangle is below 0.7 times as sharp as the sharp picture (the three sharpness values are printed: sharp 637255, lens softness 527240, motion blur 286953 in the run of 2026-11-12);
   * **a person who stands still is not smeared**: the picture of the still copy with only `motion_blur = 0.4` is byte-identical to the picture with every effect off;
   * **the room has strong edges**: with only `clutter` on, the number of horizontal steps of more than 40 levels in the green value is above 500 (the room: furniture, window, door, notice board), and in the plain scene it is exactly 0 (printed: 1980 against 0);
   * **the room is static**: two frames (indexes 1 and 2) with only the room on differ by less than 4 levels on average (only the small background noise of the plain scene changes).
11. **Model profile files** (10 checks: 7 for the older rules and 3 for `detector.layout`). Two small local helpers (lambda functions) do the work: `write(path, text, bom)` writes a profile file (optionally with a UTF-8 byte order mark in front) and `loads(text)` writes the text to `build\p_case.ini`, reads it with `load_profile` into a **fresh** `ModelProfile` and returns the result. A fresh profile is used for every case so that a value left over from an earlier case cannot decide the result, and each wrong text is paired with the same text corrected, which must be accepted: that shows that the rejection comes from the one wrong value and from nothing else. The checks (see `src/common/profile.cpp`): a profile file `build\p_ok.ini` that starts with a UTF-8 byte order mark (the three bytes some editors put at the start of a file) is read; in the same file the list `detector.outputs = out0, out1 ,out2` gives three names, with the spaces around the commas trimmed (`out1`, `out2`); `detector.small_size = 0` (with `detector.model_small` set) is rejected and `320` is accepted; `detector.small_size = 100`, which is not a multiple of 32, is rejected; `runtime.threads = abc` (not a number) is rejected and `4` is accepted; `detector.backend = onxx` (a misspelled backend) is rejected, `ncnn` and `onnx` are accepted; `recognizer.dim = 0` is rejected and `512` is accepted. **The three checks for `detector.layout`** (with `nine` meaning the line `detector.outputs = 65,85,105,66,86,106,67,87,107`): `detector.layout = chw` with `detector.type = scrfd` and no `detector.outputs` is rejected, and the same with `nine` is accepted (`detector.layout = chw needs nine detector.outputs (nine are accepted)`); `detector.layout = chw` with `detector.type = yunet` and `nine` is rejected, and with `scrfd` accepted (`detector.layout = chw is only for SCRFD (YuNet is rejected)`); `detector.layout = round` is rejected and `flat` is accepted (`a misspelled detector.layout is rejected ('flat' is accepted)`). These are exactly the rules that `load_profile` checks after reading the file: a number read with `atoi` turns text into 0 without complaint, so the thread count (1 to 64), `recognizer.dim` (1 to `FA_MAX_FEATURE_DIM`), the small size, the layout and the backend names are checked explicitly.
12. ONNX Runtime loads `yunet.onnx` and ncnn loads the converted SCRFD detector (`det.param`, `det.bin`; input `in0`, outputs `out0` to `out8`).
13. YuNet runs on the letterboxed picture and returns 12 outputs, the first with 6400 numbers; SCRFD (with the input preparation mean 127.5, scale 1/128 and red/blue swapped) returns 9 outputs, the first with 12800 numbers.
14. **SCRFD with scaling upsampling layers** (3 checks, new with the fitted rectangle; `models\dev_noncommercial\det_dyn.param`, see `MODELS.md`). The test loads the network with `create_ncnn_net("models/dev_noncommercial/det_dyn.param", ".../det_dyn.bin", "in0", {"65", "85", "105", "66", "86", "106", "67", "87", "107"}, 2)`: the nine names are the raw outputs of the last convolutions (scores, boxes, landmarks of the strides 8, 16 and 32), not the reshaped `out0` to `out8`. Checks: (a) `ncnn loads the SCRFD model whose upsampling layers scale instead of using fixed sizes (det_dyn)`. (b) `at 640x640 the raw outputs of det_dyn are exactly the flat outputs of the original model (scores after the sigmoid, boxes, landmarks)`: the original network `det` (the one loaded in group 12, flat outputs) and `det_dyn` run on the same letterboxed 640x640 gradient picture with the SCRFD input preparation; for every stride, cell, anchor `a` and number `k` the test compares `flat[(cell * 2 + a) * per + k]` with `raw[(a * per + k) * cells + cell]` (`per` is 1 for the scores, 4 for the boxes, 10 for the landmarks; the raw score goes through the sigmoid first) and requires the **largest difference to be below 1e-4**; it prints the value (`det_dyn against det at 640x640: largest difference over all scores, boxes and landmarks 3.7e-09` in the run of 2026-11-12). (c) `det_dyn runs on a 640x384 picture and gives 48x80, 24x40 and 12x20 cells at the strides 8, 16 and 32 (the square model cannot)`: rows 128 to 511 of the same letterboxed picture (a 640x384 picture, the shape of a 1920x1080 picture shrunk to a long side of 640 and rounded up to a multiple of 32) are run through `det_dyn` and the test requires nine outputs with `2 * 48 * 80`, `2 * 24 * 40` and `2 * 12 * 20` numbers for the scores, `8 * 48 * 80` for the first box output, `20 * 48 * 80` and `20 * 12 * 20` for the first and the last landmark output. The comparison (b) and the size check (c) are skipped (no check printed) if either network did not load (the block runs only when both `det_dyn` and the original SCRFD network exist); check (a) fails if `det_dyn` does not load. The original square model cannot take the 640x384 picture because its two upsampling layers have a fixed output size (that is the statement in the check text; the test does not run the square model on the rectangle).
15. **Everything runs on the CPU: no GPU anywhere** (3 checks; see `cpu_only_check.h` below, `Net::runs_on_cpu` and `onnxruntime_providers` in `common.md`), after both networks have run:
    * `both networks (ONNX Runtime and ncnn) report that they run on the CPU`: `yunet->runs_on_cpu()` and `scrfd->runs_on_cpu()` are both true (the check also fails if one of the two networks did not load);
    * `the ONNX Runtime library in use offers no GPU or accelerator provider (CPU, and the never-used remote Azure provider, only)`: `onnxruntime_providers()` contains `CPUExecutionProvider` and no provider other than that and `AzureExecutionProvider`. The Azure provider is no GPU: it sends the network's input to a remote service and only works if a program appends it to a session, and nothing in this project does. Any other provider (CUDA, TensorRT, DirectML, OpenVINO, ...) fails the check. The list is printed (`ONNX Runtime offers these execution providers: AzureExecutionProvider, CPUExecutionProvider`);
    * `after both networks have run, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process`: `loaded_gpu_libraries()` is empty. A line says which GPU libraries are loaded (`none`) and which Direct3D 11 / DXGI libraries, for information only: in this test `d3d11.dll` and `dxgi.dll` are loaded (Windows components; this is the one test that writes and reads an MP4 through Media Foundation, and the video writer is what brings them in: measured with the GPU counters, a program that writes an MP4 has an idle Direct3D device, the `3d` engine at 0.0% utilization and no video engine, while it encodes; see `scripts/gpu_probe.ps1` below and `RESULTS.md`, "All engines on the CPU"; the project switches Media Foundation's hardware transforms off and computes nothing with these libraries), which is why they are not on the list of forbidden libraries. `test_pipeline`, which does not decode a video file, loads neither.

These tests give the networks a gradient, not a face: they check that both libraries load and give outputs of the expected shape, not that a face is found.

**How it works.** The checks call the functions of `image_ops.h`, `image_io.h`, `frame_source.h` (`open_sequence_source`, `open_video_file_source` and `pick_camera_mode`), `video_writer.h`, `draw_text.h`, `scene.h` (`render_frame` and `CameraLook`, for the camera look), `profile.h` and `inference.h` (`create_onnx_net`, `create_ncnn_net`, `Net::runs_on_cpu`, `onnxruntime_providers`) directly, and the helpers of `cpu_only_check.h`. The model checks are skipped (no check printed) when the model fails to load; the load check itself already failed in that case.

**Engine.** The foundation layer that all engines use; part of the integrated project (`face_authentication.sln`).

**Used by.** `run_tests.bat` (first test); by hand.

## tests/test_detect_units.cpp

| Function | Purpose |
|---|---|
| `CHECK` (macro), `g_failures` | As in `test_common.cpp`. |
| `face_with_pose` | Build a face with landmarks of a known head rotation. |
| `box_face` | Build a face that has only a box and a score. |
| `main` | Test head-pose maths, the pose gate, NMS, the tracker, the Haar cascades and the pose detector of a profile. |

### `face_with_pose`

```cpp
static Face face_with_pose(float pitch, float yaw, float roll, float eye_distance = 40.0f);
```

**What it does.** Returns a `Face` whose five landmarks are those of an average face rotated by the given pitch, yaw and roll (degrees), centred at (200, 200) with the given eye distance in pixels (default 40). `has_landmarks` is true, the score is 0.9 and the box starts `1.2` eye distances left of and `1.0` eye distance above the centre and is `2.4` eye distances wide and `2.6` high.

**How it works.** Calls `synthesize_landmarks` (`pose.h`) with the default pose model and sets the other fields by hand.

**Used by.** `main` of this file (all pose tests).

### `box_face`

```cpp
static Face box_face(float x, float y, float w, float h, float score = 0.9f);
```

**What it does.** Returns a `Face` with only a box (no landmarks) and a score (default 0.9), for the NMS and tracker tests.

**How it works.** Sets `box` and `score` on a default `Face`.

**Used by.** `main` of this file.

### `main` (test_detect_units)

```cpp
int main();
```

**What it does.** Start with `build\bin\test_detect_units.exe` (no arguments; needs `models\permissive\haar_frontalface.xml` and `haar_eye.xml` for the Haar part, and for the last section the model files of `models\permissive.ini` (YuNet) and `models\dev_noncommercial.ini` (SCRFD with ncnn); no photos). The output format is as for `test_common`. 26 checks:

* **Pose, one axis at a time** (1 check): for angles -20, -10, -5, -2, 0, 2, 5, 10 and 20 degrees, each of pitch, yaw and roll alone must be recovered by `estimate_pose` within 0.5 degrees, and the other two angles must stay below 0.5 degrees. An information line shows the worst error.
* **Pose, all three together** (1 check): 27 combinations of pitch -5, 0, 5, yaw -5, 0, 5 and roll -8, 0, 8 (the tolerance region) must be recovered within 1 degree.
* **Landmark noise** (information only, no check): 2000 faces per eye distance of 30, 60 and 120 pixels, each landmark moved randomly with a standard deviation of 0.5 pixel; the line prints the typical (root-mean-square) error of yaw, pitch and roll. Read it as: how much do small landmark errors disturb the angles that the gate uses? The smaller the face, the larger the error.
* **Tolerance gate** (7 checks): a frontal face is accepted; 4/4/7 degrees is accepted; pitch 7 is rejected; yaw 7 is rejected; roll 10 is rejected (limit 8); roll 7.5 is accepted; a face without landmarks is never accepted.
* **NMS and IoU** (2 checks): of three boxes, two overlapping ones merge and the better score (0.9) is kept; boxes overlapping by half give an IoU of one third.
* **Tracker** (4 checks): two faces moving in opposite directions for 20 frames keep their IDs; a face that disappears for 3 frames and comes back keeps its ID; a face that is missing for `FA_TRACK_MAX_MISSED_FRAMES + 2` frames is removed; a face that jumps 40 pixels per frame (its box is 90 wide) keeps one ID (motion prediction).
* **Haar** (3 checks): the frontal-face cascade loads with a 24 x 24 window, the eye cascade loads with a 20 x 20 window, and a flat gray 320 x 240 image gives no face.
* **The pose detector of a profile** (8 checks; `create_pose_detector` and `pose.detector_profile`): (1) `models\permissive.ini` and `models\dev_noncommercial.ini` both load with `load_profile`; (2) the permissive profile's `pose_detector_profile` is empty; (3) the SCRFD profile (`detector_type == "scrfd"`) has a non-empty `pose_detector_profile`; (4) `create_pose_detector` for the permissive profile (no key) gives a detector whose `type()` is `yunet`, the profile's own type; (5) `create_pose_detector` for the SCRFD profile gives a `yunet` detector; (6) `create_detector` for the SCRFD profile still gives `scrfd`; (7) a profile that names a file that does not exist (`build\does_not_exist.ini`) makes `create_pose_detector` return `nullptr`; (8) a profile that names `build\pose_b.ini`, which itself names `models\permissive.ini`, also gets `nullptr` (only one level is allowed). Checks 7 and 8 write the small profile files `build\pose_missing.ini`, `build\pose_a.ini` and `build\pose_b.ini` first, so the `build` folder must exist; the test prints the reasons of the two refusals to `std::cerr` and they are expected.

**How it works.** All data of the pose, gate, NMS and tracker parts is made in the program (synthetic landmarks and boxes), so those results do not depend on photos. Yaw and pitch can only be tested this way because the scene generator can only roll (see `testdata.md`).

**Engine.** Face detection (`face_detection.sln`).

**Used by.** `run_tests.bat` (second test); by hand.

## tests/test_feature.cpp

| Function | Purpose |
|---|---|
| `CHECK` (macro), `g_failures` | As in `test_common.cpp`. |
| `dot` | Dot product of two features (their cosine similarity). |
| `main` | Test the feature extraction engine alone, on 8 portraits. |

### `dot`

```cpp
static float dot(const Feature& a, const Feature& b);
```

**What it does.** Returns the dot product of two features, which equals their cosine similarity because every feature has length 1. It is the same function as in `eval_features.cpp`, except that it stops at the shorter of the two features instead of reading past the end of the shorter one.

**How it works.** A loop that sums `a.values[i] * b.values[i]` in a `float` for `i` below both sizes.

**Used by.** `main` of this file (the most alike pair of different people, and the checks of the brightened copies).

### `main` (test_feature)

```cpp
int main(int argc, char** argv);
```

**What it does.** Command line: `test_feature [profile.ini]` (default `models/permissive.ini`). It checks the **feature extraction engine on its own**, without the pipeline, the tracker or the matcher. Start with `build\bin\test_feature.exe models\permissive.ini` from the project root. Needs the model files of the profile and `data/portraits`. Run it with each of the four shipped profiles: `models\permissive.ini` (SFace with ONNX Runtime), `models\dev_noncommercial.ini` (ArcFace with ncnn), `models\dev_noncommercial_onnx.ini` (ArcFace with ONNX Runtime) and `models\classical.ini` (LBP). All four pass; `run_tests.bat` runs the first, the second and the last.

The extractor needs a face that is cut out and aligned by its landmarks. So the program takes the first portraits of `data/portraits` (in directory order; up to 8; only folders whose `117th.jpg` can be read and in which the **profile's detector** finds a face with landmarks), runs them through that detector once, only to get the landmarks, cuts out each face with `make_face_crop` and from then on tests only the extractor. The landmarks are those of the plain detection, without the pose check. The program is not a measure of accuracy over many people (that is `eval_features`): it checks that the engine behaves, on 8 people. Output:

* a first line `profile <path>: extractor <name> (<dim> numbers), threshold <t>, landmarks from <detector name>`;
* `portraits used: N`;
* `time of extract(): median X ms` (information only, no limit);
* `most alike pair of different people: S (<person> / <person>)` (information, the number that the "different people" check uses);
* for the deep extractors `brightened copies: least alike to the original S, identified K of N`; for the classical one the line `(classical extractor: the brightness check is only made for the deep extractors)`;
* the checks (`ok:` or `FAIL:`) and, at the end, `ALL TESTS PASSED (0 failures)` or `TESTS FAILED (N failures)`.

The checks, in this order (14 for the deep extractors, 12 for the classical one; counted from the source and from the logs of the four profiles, which printed 14, 14, 14 and 12 `ok:` lines):

1. The extractor's `dim()` is the feature length written in the profile (`recognizer.dim`).
2. **The extractor and the detector run on the CPU** (`extractor->runs_on_cpu()` and `detector->runs_on_cpu()`, see `FeatureExtractor::runs_on_cpu` and `FaceDetector::runs_on_cpu`; no GPU is used). The check text is `the extractor and the detector run on the CPU (no GPU is used)`.
3. At least 4 of the portraits gave a face with landmarks. If not, the test stops here with exit code 1.
4. Every crop gives a feature (`extract` returns `true`).
5. Every feature has the length of the extractor.
6. Every number of every feature is finite (no NaN, no infinity).
7. Every feature has length 1 (within 0.001): the matcher's dot product is a cosine similarity only for such features.
8. The same crop gives the same feature twice (no number differs by 1e-6 or more).
9. A crop without pixels is refused (`extract` returns `false`, no crash).
10. A crop whose face has no landmarks is refused.
11. The features of different people are less alike than the profile's match threshold (`recognizer.threshold`): the highest dot product of any two of the 8 people must be below it. A negative control: with `recognizer.threshold = 0.10` in a copy of `models\permissive.ini` this check fails (the most alike pair of different people scores 0.28 there), which shows that the check can fail.
12. (Deep extractors only, `recognizer.type` is not `lbp`.) A copy of each face that is 15 percent brighter (every pixel times 1.15, at most 255) stays alike: its similarity to the original is at least 0.8 for every face.
13. (Deep extractors only.) Each brightened copy is identified as the right person: of the 8 original features, the one with the highest dot product is the copy's own.
14. **After the extractor has run, no GPU library is loaded in the process** (`loaded_gpu_libraries()` is empty, see `cpu_only_check.h` below): no Vulkan, Direct3D 12, CUDA, cuDNN, TensorRT, OpenCL, DirectML or GPU ONNX Runtime provider library. This is the last check, after the detector (for the landmarks) and the extractor have both run. The check text is `after the extractor has run, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process`.

Exit codes: 0 when every check passed, 1 when a check failed (or fewer than 4 usable portraits), 2 when the profile cannot be loaded or the extractor or the detector cannot be created. Standard output is unbuffered. The brightness checks (12 and 13) are made only for the deep extractors; the classical extractor prints the note instead. The CPU checks (2 and 14) run for every profile, the classical one too (for it they are true by construction: Haar and LBP are plain C++, and the interface's default answer is `true`; the second still shows that nothing loaded a GPU library).

**How it works.** `load_profile`, `create_extractor`, `create_detector` (the detector is used only for the landmarks). A loop over the folders of `data/portraits` fills the vector of crops (`load_image`, `detect`, `make_face_crop`) until 8 are found. Then one loop over the crops runs `extract` twice (timed once with `now_ms()` into a `TimingStats`) and counts how many features are present, of the right length, finite, of length 1 and repeatable. The bad-input checks use an empty `FaceCrop` and a copy of the first crop with `face.has_landmarks = false`. The similarity checks use `dot` over all pairs `(i, j)` with `i < j`.

**Engine.** Feature extraction (`feature_extraction.sln`).

**Used by.** `run_tests.bat` (fourth test: `models\permissive.ini`, `models\dev_noncommercial.ini`, `models\classical.ini`); by hand.

## tests/test_match.cpp

| Function | Purpose |
|---|---|
| `CHECK` (macro), `g_failures` | As in `test_common.cpp`. |
| `median_ms` | Run a piece of code several times and return the median time. |
| `test_matching` | Test the M:N matcher on 10,000 synthetic features of one length. |
| `test_m_to_n_shapes` | Test that M (detected faces) and N (registered faces) are independent: 72 combinations, including M larger than N, M = 0 and N = 0. |
| `test_store` | Test the register / delete / modify functions and the CSV file. |
| `file_is_there` | Does a file exist (can it be opened for reading)? |
| `test_card_first` | Test the card-first comparison: `score_matrix` for M < N, M = N and M > N, `match_cards`, a card that is not in the database, the RF card number in the record (`find_by_card`, `set_card_id`), and the CSV format with its header line. |
| `test_database` | Test the managed database in one format (`DatabaseFormat`): open, write-through, undo when the file cannot be written, the lock. Run for CSV and for SQLite. |
| `test_format_choice` | Test the choice between the two formats: by name, by content, refusals, conversion both ways, damaged and foreign SQLite files. |
| `test_database_speed` | Measure the database file for 10,000 people with 512 numbers, CSV against SQLite. |
| `main` | Run `test_matching(512)`, `test_matching(128)`, `test_m_to_n_shapes()`, `test_store()`, `test_card_first()`, `test_database(DatabaseFormat::Csv)`, `test_database(DatabaseFormat::Sqlite)`, `test_format_choice()` and `test_database_speed()`. |

### `median_ms`

```cpp
template <class F> static double median_ms(int repeats, F fn);
```

**What it does.** Calls `fn()` `repeats` times and returns the median of the measured times in milliseconds.

**How it works.** Uses `now_ms()` around each call and a `TimingStats`.

**Used by.** `test_matching` (speed table).

### `test_matching`

```cpp
static void test_matching(int dim);
```

**What it does.** Builds a store of 10,000 synthetic people with features of `dim` numbers (seed 12345) and tests the matcher. Output lines are `ok:`/`FAIL:` checks and indented information lines. 8 checks per call:

1. The matcher holds 10,000 features of the right length.
2. For M = 1, 4 and 16 query faces at once against N = 10,000 (3 checks), the fast M:N result equals the plain reference result (`match_reference`, M separate 1:N searches; same index, same score within 0.0001).
3. 2000 genuine probes (a registered person plus noise 1.0, cosine about 0.71) are all identified as the right person; the information line shows how many and the mean score.
4. 20,000 random strangers are matched. The information lines show how many reach 3, 4, 5 and 6 "spreads" (a spread is `1 / sqrt(dim)`, the typical size of the similarity between two random directions); the check requires that none reaches 6 spreads. This describes **random** directions only: real faces of different people are more alike, so the real threshold must be found on real data (`docs/RESULTS.md`). With short features (128) the spread is larger, which is one reason to prefer 256 to 512 numbers.
5. The 10,000 features survive saving and loading as CSV exactly (the last record is compared number by number); the information line shows the save and load times (measured on the development PC, run of 2026-11-12: 10,000 people x 512 numbers, a 66 MB file, save 1316 ms and load 1038 ms; 10,000 x 128: save 327 ms and load 264 ms: this is the price of the CSV file that is rewritten as a whole at every change, see `store.md`; `test_database_speed` measures the same against SQLite); the temporary file `build\store_10000.csv` is deleted.
6. A table compares one M:N AVX2 call with **M separate 1:N searches (the reference)** for M = 1, 2, 4, 8 and 16 faces against N = 10,000 registered features (median of 21 and of 5 runs); the check requires the M:N call to be faster for M = 8 faces. The printed header is `faces (M) |  M:N AVX2 (ms) | M separate 1:N searches, reference (ms)`. One run on the development PC, 512 numbers (the medians change a little from run to run; `RESULTS.md` has the numbers of the benchmark):

| M (faces) | M:N AVX2 (ms) | M separate 1:N searches, reference (ms) |
|---|---|---|
| 1 | 0.708 | 2.394 |
| 2 | 0.865 | 4.295 |
| 4 | 1.469 | 9.415 |
| 8 | 2.614 | 17.130 |
| 16 | 3.586 | 34.576 |

   The reference reads the registered list once per face, so its time grows in proportion to M; the M:N call reads it once, so its time grows much more slowly.

**How it works.** Uses `make_synthetic_gallery`, `noisy_copy` and `random_feature` (see `testdata.md`), `FeatureStore::matcher`, `match` and `match_reference`.

**Used by.** `main` of this file (once with 512 and once with 128).

### `test_m_to_n_shapes`

```cpp
static void test_m_to_n_shapes();
```

**What it does.** Tests the M:N matcher with every combination of M (the number of query faces, "currently detected faces") and N (the number of registered faces), because the two numbers must be independent of each other (see the "M:N MATCHING" comment in `matcher.h`). It prints the heading `=== M:N: M detected faces against N registered faces, M and N independent ===` and the information line `72 combinations of M (0..40) and N (0..100), 30 of them with M > N`. 7 checks. The first four cover all 72 combinations, M in {0, 1, 2, 3, 16, 40} and N in {0, 1, 2, 5, 17, 100}, for the feature lengths 128 and 512 (6 x 6 x 2 = 72; M is larger than N in 15 of the 36 combinations of each length, 30 in all):

1. For every M and N, `match` and `match_reference` return exactly M results (so M = 0 gives an empty list).
2. For every M and N, the fast M:N result equals the reference result (same index, same score within 0.0001).
3. Every result points at a registered person (index 0 to N-1), or is -1 when N = 0.
4. Every face is found as the person it was made from, for N up to 17 (each query is a registered feature plus noise 0.3; for N = 0 the queries are random features and have nobody to be found as).

The last three build a small store of N = 5 people with 128 numbers each (`make_synthetic_gallery(5, 128, 77)`) and let the gallery grow while M stays at 16 and then drops to 1:

5. M = 16 faces (three of each of the five people and one more: face `q` is a noisy copy of person `q % 5`, so M is larger than N) are each matched to their own person.
6. After three more people are registered (N = 8; their ids get an `X` in front so that they are new) the same 16 faces get 16 results and the matcher holds 8 features: the matcher follows the changes of the store.
7. One face (M = 1) against N = 8 gives one result, the right person (index 0).

**How it works.** For each length and each N a `FeatureStore` is filled with `make_synthetic_gallery(n, dim, 9000 + n)` (see `testdata.md`) and its `matcher()` is used; for each M the queries are made with `noisy_copy(feature, 0.3, rng)` of a randomly chosen registered person, or `random_feature` when N is 0 (seed 2024). Both `match` and `match_reference` are run, and four flags (right size, equal results, index in range, face found) are cleared on the first violation; the first four checks report the flags after all combinations. A combination whose result list has the wrong size is counted and skipped. The noise 0.3 is smaller than the 1.0 of `test_matching`, so that the right person is found even when the face is one of 17 candidates.

**Used by.** `main` of this file.

### `test_store`

```cpp
static void test_store();
```

**What it does.** Tests the registered-features store with two people (256 numbers each) with awkward names: `Kim, Min-su` (a comma) and `Lee "JJ" Park` (quotes), to test the CSV quoting. 27 checks: both can be registered; an existing id is refused; a feature of another length is refused; `save` writes the file; `load` reads it back with 2 people; names and every number survive exactly; the name can be changed alone (feature untouched) and the feature alone; modifying an unknown id is refused; after the modification the matcher finds the new feature (score above 0.999) and no longer the old one (score below 0.5); a person can be deleted (twice is refused); after the deletion the matcher only knows the remaining person; a damaged CSV (a line that says 3 numbers but has 2) is rejected and the old data is kept; a CSV whose two lines have features of different lengths (2 and 3 numbers, both valid length-1 vectors such as `0.6,0.8`, so that only the length differs) is rejected and the old data is kept, and the reason in `last_error()` is the different length (`differs from the first line`); a CSV line with a non-finite number (`inf`) is rejected and the old data is kept; a CSV line whose feature `30,40` has length 50 instead of 1 is rejected and the old data is kept (such a record would score high against every query); a CSV that starts with a UTF-8 byte order mark is read and the first id is `A`, not a damaged one; `add` refuses a feature `3,4` that is not of length 1 and a feature with a NaN (not a number); a name with a line break is refused by `add` (the CSV has one line per person, so it could not be read back) and also by `update`; `save` can be called twice over the same file (the second call replaces the first file) and the file saved over an existing one loads completely with as many people as the store; a missing file is reported. Temporary files are `build\store_test.csv`, `build\store_bad.csv`, `build\store_mixed.csv`, `build\store_inf.csv`, `build\store_big.csv`, `build\store_bom.csv` and `build\store_replace.csv` (the store itself writes `<path>.tmp` first and swaps it in, see `FeatureStore::save`).

**How it works.** Calls `add`, `save`, `load`, `update`, `find`, `matcher().match` and `remove` of `FeatureStore`. The last checks test the rules that the store enforces: `load` rejects lines with different feature lengths, non-finite numbers and features that are not of length 1 and skips a byte order mark; `add` refuses features that are not finite vectors of length 1 (the length must be 1 within a small tolerance: the squared length must lie between 0.95 squared and 1.05 squared); `add` and `update` refuse line breaks in names; and `save` writes a temporary file and replaces the old file in one step (`MoveFileEx`). The reason for the length-1 rule: the matcher's score is a plain dot product, which is a cosine similarity only for vectors of length 1.

**Used by.** `main` of this file.

### `file_is_there`

```cpp
static bool file_is_there(const std::string& path);
```

**What it does.** Returns `true` if a file at `path` can be opened for reading, otherwise `false`. The tests of the database use it to ask whether the database file or its lock file `<db>.lock` exists, or whether a temporary file `<db>.tmp` or a SQLite journal `<db>-journal` was left behind.

**How it works.** `std::ifstream(path).good()`.

**Used by.** `test_database`, `test_format_choice`.

### `test_card_first`

```cpp
static void test_card_first();
```

**What it does.** Tests the **card-first** comparison. The card information is always identified first: the card is looked up in the database, the feature stored with it is retrieved, and the faces of the current frame are compared with exactly those retrieved features. M (the faces) and N (the cards) are independent, so the test plays M < N, M = N and M > N; and a card that is not in the database must be a failed comparison, with no fallback to the whole database. It prints the heading `=== card first: M faces against the N features of N RF cards (M < N, M = N, M > N) ===`. 47 checks, in groups:

*The matcher: `FeatureMatcher::score_matrix` (6 checks).* A store of N = 200 synthetic people with 256 numbers (`make_synthetic_gallery(200, 256, 4242)`); the faces are noisy copies (noise 1.0) of random registered people and the positions are random.
1. For every combination of M in {0, 1, 3, 7, 16} faces and N_s in {0, 1, 3, 7, 40} positions, `score_matrix` returns exactly M x N_s similarities. The information line says `25 combinations of M (0..16) and N_s (0..40): 10 with M > N_s, 11 with M < N_s, 4 with M = N_s`.
2. Every similarity equals the brute-force dot product (rows = faces, columns = the chosen registered features; within 0.0001).
3. N_s = 1: a face against the one registered feature it was made from gives one similarity above 0.999 (the 1:1 case).
4. A position outside 0 to N-1 (the positions `{-1, 5000, 7}`) gives `kNoScore` in its column, and position 7 still works.
5. A face of the wrong length (64 numbers against 256) gets `kNoScore`.
6. An empty registered set (a `FeatureMatcher` with no gallery, N = 0) gives `kNoScore`.

*The database record holds the card (5 checks).* Six people `P0` to `P5` with 128 numbers; `P0` to `P4` have the cards `CARD-0` to `CARD-4`, `P5` has none.
7. The six people can be registered.
8. A card that belongs to another person is refused (`CARD-1`), and the error names the owner (`P1`).
9. A card number with a line break is refused.
10. `find_by_card` and `index_of_card` look a person up by card (the index is his position in the gallery; an unknown card gives -1).
11. An empty card number is not a card that somebody owns.

*M < N, M = N and M > N with `match_cards` (10 checks).* The faces are new images of registered people (`noisy_copy` with noise 0.3, cosine about 0.96).
12. M = 1 face against N = 3 cards: 3 card entries and 1 face entry (and the call returns `true`).
13. ... the face is most like the owner of the second card, with a similarity above 0.9.
14. ... and only that card has a face that looks like its owner (the other two cards: below 0.5).
15. ... every card says which record it was retrieved from.
16. M = 3 faces against N = 3 cards (the faces of `P4`, `P0` and `P2`, the cards `CARD-0`, `CARD-2` and `CARD-4`, so that the order of the people differs from the order of the cards): 3 faces, 3 cards.
17. ... each face is matched to the card of its own person.
18. ... and each card to the face of its owner.
19. M = 4 faces against N = 2 cards (the faces of `P0`, `P1`, `P3` and `P5`, the cards `CARD-1` and `CARD-3`; `P5` is registered but has no card): 4 faces, 2 cards.
20. ... the two faces whose people own the cards are found (each card's best face, with a similarity above 0.9).
21. ... the other two faces (registered people, but not among the cards) do not look like either owner (below 0.5).

*A card that is not in the database, and other special cases (12 checks).*
22. A registered person with somebody else's card (the face of `P2` with `CARD-4`): the similarity to that owner is low (below 0.5): the face was compared with him only, not searched among all people.
23. A card that is not in the database (`NO-SUCH-CARD`): `match_cards` returns `false`, the card is `found = false` with record -1 and no best face.
24. ... and the face is NOT searched among the registered people instead: the face (of the registered `P2`) gets no match (`best_card` -1, score `kNoScore`).
25. One unknown card among three (`CARD-0`, `NO-SUCH-CARD`, `CARD-3`): the call fails, but the other two cards are marked found.
26. ... the found cards are still compared (their best faces are the right ones), the unknown one is not.
27. No card at all: the comparison fails (no card entries, the face entries are still there).
28. An empty card number is not a card: the comparison fails and the card is not found.
29. M = 0 faces: both cards are found, nobody to compare, the call returns `true`.
30. A face of the wrong length is not compared (the call returns `true`, the face has no best card).
31. The same card given twice is accepted: two entries with the same result.
32. (setup) The owner of `CARD-4` is found by his card.
33. After the owner is deleted (`remove("P4")`) his card is not in the database: the comparison fails.

*Changing the card (6 checks).*
34. `set_card_id` gives a person a card (`find_by_card` finds him).
35. ... but not a card that belongs to another person.
36. Setting the card a person already has is fine.
37. `set_card_id` refuses an unknown id and a card with a line break.
38. An empty card number takes the card away.
39. A card that was taken away can be given again (several moves of the cards between two people, ending where they started).

*The CSV file with the cards (8 checks).* A card with a comma and quotes (`card,with "quote"`) is added first, to test the quoting.
40. `save` writes the database with the cards (`build\store_cards.csv`).
41. The CSV starts with the header line `id,name,card_id,dim,values`.
42. The cards (also the one with a comma and quotes) survive the CSV round trip with the features, and an empty card stays empty.
43. A file in the older format (no header line, no card column: `A,old a,2,0.6,0.8`) is still read, and the cards are empty.
44. ... and it is written in the new format afterwards (saved over itself and read again).
45. A file in which two people have the same card is refused (the error says `duplicate card_id`), and the old data is kept.
46. Many empty card numbers in a file are fine.
47. A file with only the header line is an empty database.

**How it works.** Uses `make_synthetic_gallery`, `noisy_copy` and `random_feature` (see `testdata.md`), `FeatureMatcher::score_matrix` directly for the first group (compared with a plain loop), and `FeatureStore::add`, `find_by_card`, `index_of_card`, `match_cards`, `set_card_id`, `remove`, `save` and `load` for the rest. Two small lambdas build a face of a registered person (`face_of`) and a list of cards (`cardlist`). The temporary files are `build\store_cards.csv`, `build\store_legacy.csv`, `build\store_dup_card.csv`, `build\store_no_cards.csv` and `build\store_header_only.csv`; all are deleted at the end.

**Used by.** `main` of this file.

### `test_database`

```cpp
static void test_database(DatabaseFormat format);
```

**What it does.** Tests the managed database (see `store.md`) in one file format: a database file that a `FeatureStore` opens with `open`, changes write-through, and locks against a second program. The **same checks run for both formats**: `main` calls it with `DatabaseFormat::Csv` and again with `DatabaseFormat::Sqlite`. The heading is `=== managed database, format CSV: open, write-through, undo on failure, lock ===` (or `format SQLite`). The database file is `build\db_test.csv` or `build\db_test.db` (its lock file is `<file>.lock`; all of them are deleted at the end, as is `build\db_bad.csv`). The people have 128 numbers each. 38 checks per format, in five groups:

*Open and write-through (11 checks).*
1. A new store is not open and has no path.
2. `open` of a database that does not exist yet creates it, empty (the store is open, holds 0 people, reports the path and the format that was asked for).
3. The file exists on disk now.
4. Two people can be registered (one with a card).
5. The file holds both at once, without any `save()` call (an independent store reads it with `load`: the names, the first feature and the card agree).
6. A modification (new name and new feature) is in the file at once.
7. Giving a person a card is in the file at once.
8. Taking the card away is in the file at once.
9. A deletion is in the file at once.
10. A refused change (registering an id that exists) leaves the file as it was.
11. No temporary file (`<file>.tmp`) and no SQLite journal (`<file>-journal`) is left behind.

*One program at a time (10 checks).*
12. A second store cannot open the database while the first has it open (`open` returns `false`, the second store is not open, and the error says `another program`).
13. `load()` is refused on an open store (the error says `is open`, and the store still holds its person).
14. Opening the path the store is already bound to reads the file again and keeps the lock.
15. After that the lock is still held (the second store still cannot open).
16. `close()` keeps the records in memory (the store is no longer open).
17. `close()` removes the lock file.
18. After `close()` a change stays in memory: the file is not touched (the independent store still reads one person).
19. Another store can open the database now and sees the saved state (one person, with the modified name).
20. A third store, created in an inner block, opens it (and is destroyed at the end of the block).
21. The lock is released when the store that held it is destroyed: a fourth store opens the database.

*A change that cannot be written is undone (11 checks).* The test first registers one more person, `E005` (check 22: the database holds two people, `E002` and `E005`) and then makes writing impossible (check 23). **CSV:** by creating a **folder** called `<file>.tmp` (`_mkdir`), so that `write_csv_file` cannot create its temporary file and every write fails with `cannot write <file>.tmp`. **SQLite:** a second connection, made with the SQLite C library directly (`sqlite3_open_v2`, then `sqlite3_exec` with `BEGIN IMMEDIATE`), holds the write lock of the file, so that every write fails after the 500 ms the store waits for the lock.
24. Registering a person (`E004`) fails and the registration is undone (the person is not in the store, the count is still 2).
25. The error text says `undone`.
26. Modifying a person fails and the old name and the old feature are restored.
27. The matcher matches the old feature again (index 0, score above 0.999).
28. `set_card_id` fails and the new card is taken back (the person has no card, and nobody owns the new card number).
29. Deleting a person fails and the person is back at the same place in the list (position 0).
30. The matcher knows that person again.
31. The file on disk is still the last good state (the same two people in the same order).
32. After the obstacle is gone (CSV: `_rmdir` of the folder; SQLite: `ROLLBACK` and `sqlite3_close` of the second connection) the same registration works and is written (the independent store reads three people).

*Restart (1 check).*
33. A new store that opens the file finds all three registered people (`E002` with its changed name, `E004`, `E005`).

*Refused openings (5 checks).*
34. A damaged database file (`build\db_bad.csv`, opened as CSV: a line that says 3 numbers but has 2) is refused, the store is not open, and its lock file is gone again.
35. A refused `open` leaves a store that was open bound to its old database, with its old records.
36. An empty path is refused.
37. A database in a folder that does not exist (`build\no_such_folder\db.csv`) is refused, the store is not open, and the error text is not empty.
38. The same for the SQLite format (`build\no_such_folder\db.db`).

**How it works.** An independent store `view` reads the database file with `load` whenever the test wants to see what is on disk (the lock only keeps out `open`, not `load`). The failure of the write in the third group is made as described above for each format. The matcher is used again after each undone change (`d.matcher().match(...)`) to show that its table was rebuilt from the restored list. The helper `file_is_there` tests for the lock, temporary and journal files. The test leaves nothing behind in `build\`.

**Used by.** `main` of this file (twice: CSV, then SQLite).

### `test_format_choice`

```cpp
static void test_format_choice();
```

**What it does.** Tests what is special about the SQLite format and about the **choice between the two formats** (see `store.md`, "Background: choosing the format"). It prints the heading `=== choosing the database format: CSV (default) or SQLite ===`. 20 checks (the last one: an existing `.db` file of zero bytes, which SQLite leaves behind when a database is opened and nothing is written, is an empty SQLite database: it is opened, given its table and can be used). The people are 60 synthetic people with 128 numbers (`make_synthetic_gallery(60, 128, 555)`), two of them with a card (`CARD-3`, and a card with a comma and quotes for a person whose name has a comma and quotes too).
1. A new database is SQLite for the names `.db`, `.sqlite`, `.sqlite3` and `.SQLITE3` and CSV (the default) for `.csv`, `.txt` and a name without an extension (six names, each opened with `Auto` and its `format()` read).
2. The same people can be saved as CSV and as SQLite, the format chosen by the extension.
3. The `.db` file really is a SQLite file and the `.csv` file is text (`is_sqlite_file`).
4. (setup) A CSV file called `.db` and a SQLite file called `.csv` are made (`save` with an explicit format).
5. An existing file is recognised by its **content**, not by its name (`Auto` opens the first as CSV and the second as SQLite).
6. Asking for CSV on a SQLite file is refused, and the reason says `SQLite file`.
7. Asking for SQLite on a CSV file is refused (`CSV file`).
8. ... neither refusal leaves a lock file behind.
9. Both files load (60 people).
10. CSV and SQLite give exactly the same people, names, cards and feature numbers, in the same order.
11. The card look-up works on the SQLite data.
12. SQLite to CSV (save with the other format) loses nothing.
13. CSV to SQLite loses nothing.
14. Saving over an existing SQLite file replaces it completely.
15. A text file that is not a SQLite database is refused when SQLite is asked for.
16. A SQLite file of another application (a table `something_else`, made with the SQLite C library) is refused (`not a face_auth database`).
17. A database of a newer table layout (`PRAGMA user_version = 99`) is refused (`newer`).
18. A SQLite row whose feature is not of length 1 (inserted behind the program's back with a blob of 128 times 5.0) is refused when the file is loaded (as in a CSV file; the message says `length 1`), and the old data of the store is kept.
19. The SQLite table itself refuses a second owner of a card and a second row with the same id (an `UPDATE` and an `INSERT` made directly with the C library fail).

**How it works.** `FeatureStore::open`, `load`, `save`, `format()` and `find_by_card`, `is_sqlite_file`, and the SQLite C library directly (`sqlite3_open_v2`, `sqlite3_exec`, `sqlite3_prepare_v2`, `sqlite3_bind_blob`, `sqlite3_step`, `sqlite3_finalize`, `sqlite3_close`) to make the damaged, foreign and newer files and to try the constraints; for this reason `test_match` includes `sqlite3.h` and links the SQLite library. The files are `build\fmt_*.csv`, `.db` and so on; all are deleted at the end.

**Used by.** `main` of this file.

### `test_database_speed`

```cpp
static void test_database_speed();
```

**What it does.** Measures what keeping the database in a file costs, for **both formats**, with 10,000 registered people of 512 numbers (`make_synthetic_gallery(10000, 512, 99)`). It prints the heading `=== database file speed, 10,000 people x 512 numbers (this PC) ===` and, for each format, one line: the file size, the time to write the whole file (`save`), the time to open it (`open`: read everything, take the lock), and the time of one change written to the file (the median of three) for four kinds of change: register a person, change a name, give a card, delete the person. 3 checks: for CSV and for SQLite that the 10,000 people were written, read and changed without an error, and that **registering one person is faster with SQLite than with CSV** (one row against the whole file). The numbers of the run of 2026-11-12 on the development PC: CSV 66.5 MB, write 1306 ms, open 1023 ms, one change 1269 to 1291 ms; SQLite 39.4 MB, write 97 ms, open 33 ms, one change 3.0 to 3.5 ms (the SQLite file is about 4 KB per person because a feature of 512 numbers is a 2 KB blob that almost fills a 4 KB page; see `store.md`). The 12 CSV changes alone take about 15 seconds (12 x 1.3 s), so this test is the slowest part of `test_match`.

**How it works.** The people are made once and saved to `build\speed_10000.csv` or `build\speed_10000.db`. For each format: `save`, then `open` on a new store, then three rounds of `add`, `update` (a new name, an empty feature), `set_card_id` and `remove` of a person `NEW<i>`, each timed with `now_ms()`. The files are deleted afterwards.

**Used by.** `main` of this file.

### `main` (test_match)

```cpp
int main();
```

**What it does.** Start with `build\bin\test_match.exe` (no arguments, no models, no photos; needs an existing `build\` folder for the temporary files). Runs `test_matching(512)`, `test_matching(128)`, `test_m_to_n_shapes()`, `test_store()`, `test_card_first()`, `test_database(DatabaseFormat::Csv)`, `test_database(DatabaseFormat::Sqlite)`, `test_format_choice()` and `test_database_speed()` and prints `ALL TESTS PASSED (0 failures)` or `TESTS FAILED`; exit code 0 only on success. **195 checks are executed in one run** (8 + 8 + 7 + 27 + 47 + 38 + 38 + 19 + 3, counted from the output of the run of 2026-11-12). The source has 147 `CHECK(` lines: fewer, because `test_matching` and `test_database` each run twice and a loop in `test_matching` runs one `CHECK` three times.

**How it works.** Calls the nine functions and the summary.

**Engine.** Feature matching (`feature_matching.sln`; needs no model and no network library; it needs the SQLite library `fa_sqlite`).

**Used by.** `run_tests.bat` (third test); by hand.

## tests/test_pipeline.cpp

| Function | Purpose |
|---|---|
| `CHECK` (macro), `g_failures` | As in `test_common.cpp`. |
| `kWidth`, `kHeight`, `kFps` | The video size, 1920 x 1080 by default (`--size WxH` changes it; video 5 uses other sizes), and the frame rate, 15 per second. |
| `g_async` | `true` with the option `--async`: worker thread and real-time playback. |
| `g_all_frames` | The `process()` time of every frame of every video. |
| `Portrait` (struct) | A usable portrait: id, picture, detected face. |
| `ActorLog` (struct) | What the test learned about one actor during one video. |
| `CardLog` (struct) | What one RF card of a card-first check did during one video. |
| `RunLog` (struct) | Everything the test learned from one video. |
| `run_video` | Feed one generated video to the pipeline (optionally as a card-first check) and collect an `ActorLog` per actor and a `CardLog` per card. |
| `print_timing` | Print the per-stage times of a run. |
| `main` | The complete system test (synchronous, or with `--async` the worker mode). |

### `kWidth`, `kHeight`, `kFps`

```cpp
static int kWidth = FA_CAMERA_DEFAULT_WIDTH, kHeight = FA_CAMERA_DEFAULT_HEIGHT;
static const double kFps = 15.0;
```

**What it does.** The size of the generated videos, **1920 x 1080 by default** (Full HD: `FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT`, the default camera size; the option `--size WxH` changes it for videos 1 to 4 and 6), and the frame rate of the videos, 15 per second (one frame is 66.7 ms). `run_video` renders at this size unless it is given another one; only video 5 does that (see `main`). The positions of the people in videos 1 to 4 and 6 are the numbers of the old 1280-pixel-wide picture (for example x = 400 to 640), so in a 1920-pixel-wide picture the people stand in the left two thirds; the face widths and everything else are as before.

**How it works.** `kWidth` and `kHeight` are file-level variables (not constants, because `main` sets them from `--size`) and the default values of the `width` and `height` parameters of `run_video`; `kFps` is a constant.

**Used by.** `run_video`, `main`.

### `g_async`

```cpp
static bool g_async = false;
```

**What it does.** Set by the command-line option `--async`. When `true` the pipeline is initialised with its worker thread (`init(profile, path, true)`) and every video is played in **real time** (see `run_video`), because the worker is meant to keep up with a camera. The additional checks of this mode are listed under `main`.

**How it works.** A file-level flag read by `run_video` and `main`.

**Used by.** `run_video`, `main`.

### `g_all_frames`

```cpp
static fa::TimingStats g_all_frames;
```

**What it does.** Collects the `total_ms` of every frame of every video of the default size (1920 x 1080), for the summary "time per frame over all videos" at the end of the test. The frames of video 5 (other picture sizes) are **not** counted: the 60 ms limit and the project-wide timing statistic are for the default size (the requirement names 1280 x 1080; the limit is applied to 1920 x 1080 by analogy; `run_video` is called for video 5 with `count_timing = false`).

**How it works.** `run_video` adds `result.total_ms` after each processed frame when its `count_timing` parameter is true; `main` prints median, p95, p99 and maximum.

**Used by.** `run_video`, `main`.

### `Portrait` (struct)

```cpp
struct Portrait {
    std::string id;
    Image image;
    Face face;
};
```

**What it does.** One portrait that can be used as an actor: the folder name as `id`, the picture and the largest face found in it by the pipeline's own detector.

**How it works.** A plain struct filled in `main`.

**Used by.** `main`.

### `ActorLog` (struct)

```cpp
struct ActorLog {
    std::set<int> track_ids;
    int frames_visible = 0, frames_detected = 0;
    double first_verified_ms = -1;
    std::string verified_as;
    std::vector<double> crop_times;
    std::vector<float> crop_roll;
    int crops_after_verified = 0;
    bool wrong_person = false;
};
```

**What it does.** The test's notes about one actor in one video:

| Field | Meaning |
|---|---|
| `track_ids` | The set of track IDs that this actor's face got (a good run has exactly one). |
| `frames_visible`, `frames_detected` | Frames in which the face was completely inside the frame, and in which a detected face overlapped it. |
| `first_verified_ms` | Time of the first frame in which the actor's track was verified; -1 if never. |
| `verified_as` | The person id that the track was verified as. |
| `crop_times`, `crop_roll` | The capture times at which crops that were stored for this actor were cut from the video (`FaceStatus::crop_time_ms`), and the true head tilt of the actor at those times. |
| `crops_after_verified` | Crops that were cut from a frame **later than** the first verification (must be 0). |
| `wrong_person` | `true` if the track was later verified as a different person than the first time. |

**How it works.** A plain struct; defaults are 0, -1, empty.

**Used by.** `RunLog`, `run_video`, `main`.

### `CardLog` (struct)

```cpp
struct CardLog {
    bool found = false;
    bool ever_matched = false;
    double first_matched_ms = -1;
    int matched_track = -1;
    float best_score = kNoScore;
};
```

**What it does.** The test's notes about one RF card of a card-first check during one video, collected from `FrameResult::cards` (see `CardStatus` in `pipeline.md`):

| Field | Meaning |
|---|---|
| `found` | The card was in the database (the last `CardStatus::found` of the video). |
| `ever_matched` | In some frame a face was verified as the card's owner (`CardStatus::matched`). |
| `first_matched_ms` | The time of the first such frame; -1 if never. |
| `matched_track` | The track ID of that face (`CardStatus::track_id` in that first frame); -1 if never. |
| `best_score` | The highest similarity any face had to the owner during the video (the highest `CardStatus::score`); `kNoScore` if no face was ever compared with him. |

**How it works.** A plain struct; defaults are false, -1, `kNoScore`. `run_video` updates it in every frame.

**Used by.** `RunLog` (`cards`), `run_video`, `main` (video 6).

### `RunLog` (struct)

```cpp
struct RunLog {
    std::map<std::string, ActorLog> actors;
    TimingStats detect, extract, match, total, track, gate, worker;
    int max_faces_in_frame = 0;
    int max_batch = 0;
    int frames = 0;
    std::map<std::string, int> last_track;
    bool card_check_ok = false;
    std::map<std::string, CardLog> cards;
};
```

**What it does.** The test's notes about one whole video: `actors` (a map from actor label to `ActorLog`), `TimingStats` for `detect`, `extract`, `match`, `total`, `track` and `gate` (per-frame times from `FrameResult`; `extract` and `gate` only have values in synchronous mode) and `worker` (the work time of the results that arrived, collected only for frames in which `worker_ms` is above 0), `max_faces_in_frame`, `max_batch` (the most features matched in one M:N call), `frames` (processed frames), `last_track` (for each actor the last track ID, used to register someone from the video), `card_check_ok` (`begin_card_check` said yes: every card of the check was in the database; false when no check was made) and `cards` (a `CardLog` for each card number of a card-first check, empty otherwise).

**How it works.** A plain struct returned by `run_video`.

**Used by.** `run_video`, `print_timing`, `main`.

### `run_video`

```cpp
static RunLog run_video(AuthPipeline& pipeline, std::vector<Actor>& actors, double duration_ms,
                        const std::string& save_folder = "", int width = kWidth, int height = kHeight, bool count_timing = true,
                        const std::vector<std::string>* card_check = nullptr);
```

**What it does.** Plays a generated video through the pipeline and returns the log. The parameters after `duration_ms`: `save_folder` (empty = do not save frames), `width` and `height` (the picture size of the generated frames, 1920 x 1080 by default, passed to `render_frame`; video 5 passes other sizes), `count_timing` (default `true`: the frame times also go into `g_all_frames`; video 5 passes `false`, so its timings are not part of the statistic over all videos; the `RunLog` of the video itself always has its own timing statistics) and `card_check` (default `nullptr`; only video 6 passes it): the RF cards that have been read, as a list of card numbers. When it is given, `run_video` calls `pipeline.begin_card_check(*card_check)` **before the first frame** (right after `reset_tracking`) and keeps the answer in `RunLog::card_check_ok`, so that every face of the video is compared with the features of those cards only (the card information is identified first); after the last frame it calls `pipeline.end_card_check()`. It first calls `pipeline.reset_tracking()`, so every video starts with empty track memory (and the detector forgets what it remembers from earlier frames, which matters for the Haar detector; in worker mode old results are thrown away). For every time `t = 0, 1000 / 15, ...` up to `duration_ms` it renders a frame (`render_frame`) and remembers the true roll of every actor at that time (`roll_history`). In worker mode it then **waits until the wall clock reaches `t`** (with `Sleep`), so the video is played in real time, as a camera would deliver it; the picture is rendered first, as a camera would have it ready. It optionally saves the frame as `frame_XXXX.jpg` in `save_folder` (only video 1 does, into `data\videos\approach_registered`), and calls `pipeline.process(frame, t, result)`. A frame for which `process` returns `false` is skipped. It adds the frame's stage times to the `RunLog`, and the largest `faces.size()` and `matched_queries` (`max_batch`). For every card in `FrameResult::cards` it updates the card's `CardLog` (found, the best similarity, and the first frame in which a face is verified as the owner, with that face's track). For every actor that is **visible** in the frame it finds the detected face (`FaceStatus`) with the largest IoU with the true box (the IoU must be above 0.4) and updates the actor's log: detected frames, track IDs, first verification and wrong-person verification, and, when a crop was stored in this frame, the crop's capture time and tilt (below).

**How it works.** The time `t` passed to `process` is the generated time, so the 100 ms and 1000 ms rules of the pipeline run on video time. In synchronous mode the test does not need to run in real time. When a crop is reported (`crop_stored`), the time that counts is `FaceStatus::crop_time_ms`, the time of the frame the crop was **cut from**: in synchronous mode that is the current frame, in worker mode an earlier one. That time is stored in `crop_times`, and the true tilt at that time (the roll of the nearest frame in `roll_history`) in `crop_roll`; a crop counts as "after verification" only if its crop time is later than the first verification time. Everything is also added to the `RunLog` timing statistics, and (when `count_timing` is true) to `g_all_frames`.

**Used by.** `main` (twenty calls: videos 1 to 3, the "registered from the camera" video, the video after deletion, one call for each of the five picture sizes of video 5, video 4, and the nine walks of video 6, eight of them with a `card_check`).

### `print_timing`

```cpp
static void print_timing(const char* title, const RunLog& log);
```

**What it does.** Prints `<title>: per frame (ms)  detect D | track T | pose check G (max) | extract E (max) | match M | TOTAL median X  p95 Y  max Z`. These are medians of the stage times from `FrameResult` (with the maximum for the pose check and the extraction); TOTAL is the median, p95 and maximum of `total_ms`. Pose check and extract are 0 in worker mode. In worker mode a second line follows, `worker thread: N results arrived, work per result median ... ms (max ...), not counted in the frame time`. Read it as the cost per frame of every stage at the size of that video (1920 x 1080 by default for videos 1 and 3, the size named by the title for video 5) on this PC; for limits use `bench`.

**How it works.** Reads the `TimingStats` of the log.

**Used by.** `main` (after video 1, video 3 and each of the five sizes of video 5).

### `main` (test_pipeline)

```cpp
int main(int argc, char** argv);
```

**What it does.** Start with `build\bin\test_pipeline.exe [profile.ini] [--async] [--sqlite] [--size WxH]` (default profile `models/permissive.ini`; the options may stand anywhere, every other word is the profile). `--async` uses the worker thread and plays the videos in real time. `--sqlite` keeps the registered people in a **SQLite database** (`build\test_pipeline.db`) instead of the default CSV file (`build\test_pipeline.csv`); everything else, including every check, is the same, so the whole system is tested with both database formats. `--size WxH` sets the picture size of videos 1 to 4 and 6 (default 1920 x 1080, at least 320 x 240; video 5 always plays its five sizes). `run_tests.bat` runs it for `models\permissive.ini` and `models\dev_noncommercial.ini` (synchronous), for `models\permissive.ini --sqlite`, and for both profiles with `--async`. It needs the models of the profile, the portraits in `data\portraits\*\117th.jpg` and an existing `build\` folder (it deletes and recreates the database file: `build\test_pipeline.csv` or `.db`, with `--async` `build\test_pipeline_async.csv` or `.db`). Exit code 2 if the profile cannot be loaded or the pipeline cannot be initialised, 1 if the test fails, 0 if all checks pass: **67 checks** in synchronous mode and **72** with `--async` for a profile with the `flat` detector layout (`permissive.ini`, `dev_noncommercial_square.ini`, `dev_noncommercial_onnx.ini`), and **3 more** (**70** and **75**) for a profile with `detector.layout = chw` (`dev_noncommercial.ini`), which also runs the three checks of step 1b. Counted from the output of the Visual Studio 2019 full flow of 2026-11-12 (`build\run_tests_final5.log`): `test_pipeline` permissive 67, dev 70, permissive with `--sqlite` 67, permissive with `--async` 72, dev with `--async` 75 (the same 70 was printed by the dev synchronous run `build\test_pipeline_dyn.log`, and the Visual Studio 2015 programs passed the dev synchronous run after the last small changes). The count is the same after the changes that were made after those flows (the comparison block was rewritten, but it still makes its three checks). Video 5 runs three checks for each of five picture sizes, 15 in all; video 6 runs 22; the source has 63 `CHECK(` lines, five of them reached only with `--async` and three of them only for a `chw` profile, and the three checks of video 5 run once per size. Output: first `database: <file> (CSV, the default format)` or `(SQLite)`, then `profile ...: detector ..., extractor ... (N numbers), threshold T, mode: synchronous` or `mode: WORKER THREAD, videos played in real time`, then `pose check by: <pose detector name>` (which detector measures the head pose: the one named by the profile's `pose.detector_profile`, YuNet for the SCRFD profiles, otherwise the profile's own detector) and the first check, `the pipeline has a separate pose detector exactly when the profile names one` (`&pipeline.pose_detector() != &pipeline.detector()` must be equal to "`profile.pose_detector_profile` is not empty"); every `refine_face` call of the test uses `pipeline.pose_detector()`, so the test measures the pose the same way the pipeline's gate does. Then the lines below. The steps:

1. **Portraits.** For each portrait folder it runs the pipeline's main detector (`pipeline.detector()`), takes the largest face and keeps the portrait only if that face has landmarks (enrolling from a photo uses the plain detection, so it needs them) and the **refined** pose of the face passes the pose gate (`refine_face` run by `pipeline.pose_detector()`, the second zoomed-in look, then `pose_within_tolerance`; this is the same pose the pipeline's gate uses, whereas the pose of the plain detection can differ by a few degrees), up to 24 portraits. A portrait for which the refinement fails also counts as rejected. It prints how many were considered, rejected by the gate and usable. Check: at least 12 usable (otherwise the program stops with exit code 1).
1b. **SCRFD: the fitted rectangle against the square** (3 checks, only when the profile has `detector.layout = chw`, that is `dev_noncommercial.ini`; no check is printed for the other profiles). The detector of the pipeline runs the network on the smallest multiple-of-32 rectangle (640x384 for a 1920x1080 picture, `letterbox_fit`), the original SCRFD conversion on a 640x640 square: the test must show that this finds the same faces. It loads `models/dev_noncommercial_square.ini` with `load_profile` and creates its detector with `create_detector`: check `the square variant of the SCRFD profile (dev_noncommercial_square.ini) loads`. It then renders one picture of `kWidth` x `kHeight` (1920x1080 by default) with the first three usable portraits as actors that stand still (centres at 22%, 50% and 78% of the picture width, at half the picture height; face widths 190, 260 and 330 pixels times `min(kWidth / 1920, kHeight / 1080)`, which at 1920x1080 are the centres x = 422, 960 and 1498, y = 540 and the widths 190, 260 and 330; `render_frame` at time 0; the portraits are registered only in step 2) and runs both detectors on it (`pipeline.detector()` and the square one). The positions and sizes follow the picture size, so the comparison also holds with `--size` (an earlier version of the test used fixed positions written for 1920x1080; it was changed after the two full flows of RESULTS.md section 5e). It prints `fitted rectangle N faces, square N faces; worst box overlap ..., largest landmark difference ... px`, where for every face of the rectangle detector the best-overlapping box (IoU) of the square detector is taken, the smallest of these overlaps is the "worst box overlap", and the "largest landmark difference" is the largest distance between a landmark and the same landmark of that partner (over all five landmarks of all faces). Checks: `the fitted rectangle and the square find the same three faces in a 1920x1080 picture` (both detectors ran and each found exactly 3 faces) and `... with the same boxes (overlap above 0.95) and the same landmarks (within 3 pixels)` (the two printed numbers). Measured: 3 faces and 3 faces, worst box overlap 0.994, largest landmark difference 0.26 pixels in the two full flows (fixed positions) and 0.23 pixels with the scaled positions (the test was run at the default size only, not with another `--size`). This compares the two layouts on one made-up picture with three faces; it is not an accuracy measurement (that is `eval_detect` and `eval_footage`, `RESULTS.md` section 5e).
2. **Register.** The first `min(20, usable - 3)` portraits are registered with `enroll_image`; the median registration time is printed. Check: all were registered. The next two portraits are the "strangers"; the first two registered are the known actors.
3. **Video 1, a registered person approaches** (6 s, face 110 to 420 pixels wide, tilted 25 degrees at the start, straight after 2.5 s). 7 checks: the face is detected in at least 80 percent of the frames in which it is visible (and more than 60 frames are visible); the whole walk has exactly one track ID; the person is verified as the right registered person and not as anybody else; a verification happened at all and within 4.5 s (the check requires `first_verified_ms >= 0` as well as `<= 4500`, so a person who is never verified fails it); every stored crop was cut while the true head tilt was within the roll tolerance plus 3 degrees of detector noise (`FA_POSE_ROLL_TOL_DEG + 3`; the tilt is looked up at the crop's capture time); crops are at least `FA_CROP_STORE_INTERVAL_MS - 1` ms apart (measured between capture times); after verification no more crops are stored. The crop times with the true roll in brackets and the frame saving are printed.
4. **Video 2, a stranger approaches** (7 s). 2 checks: the stranger is never verified; he gets between 3 and 8 crops (about one per second once the head is straight).
5. **Video 3, two people at once** (6 s: a registered person from the left, a stranger from the right, tilted in opposite directions). 4 checks: at least two faces in one frame; the registered person is verified correctly; the stranger is not verified; the two people keep one track ID each and the IDs differ. The line printed also shows the largest number of features matched in one M:N call (`max_batch`); this number is shown for information and is not checked.
6. **Register, modify, delete.** 8 checks: `enroll_track` registers the stranger from the crop stored for his track (`enroll_track` calls `flush()` first, so this also works in worker mode); the store has one person more; when he walks in again (5 s) he is verified as himself; `rename_or_refresh` with `nullptr` changes his name in the store; **a face that is still being tracked as this person shows the new name at once** (after the rename the test renders one more frame at 5066 ms, calls `process` without `reset_tracking`, and requires a verified face with the person name `New Name`, which tests the rule of `rename_or_refresh`); `remove_person` deletes him; when he walks in again he is not verified; and the database file read back (with a second store, in whichever format) has as many people as the memory store.
7. **Video 5, the same walk at several camera picture sizes** (6 s each; 15 checks). The system must work with any camera size (the default is 1920 x 1080, see `camera.md`), so the first registered person (`known_a`) walks toward the camera once at each of five sizes: 640x480, 1280x720, 1920x1080 (`FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT`, the default), 1600x1200 (a 4:3 picture) and 2560x1440. The scene is laid out for 1280 x 1080 and scaled to each size the way `gen_video --size` does it: with `sx = width / 1280`, `sy = height / 1080` and `sf = min(sx, sy)` the path goes from `(400 sx, 500 sy)` to `(640 sx, 540 sy)`, the face grows from `110 sf` to `420 sf` pixels wide (so a 1920 x 1080 picture shows the same faces as 1280 x 1080, only wider), the head starts tilted by 25 degrees and is straight after 2.5 s. Each size calls `run_video(pipeline, actors, 6000, "", width, height, false)` (the frame times do not go into the statistic over all videos, which is for the default size) and prints a line `WxH: frames visible V, detected D, track ids N; verified at T ms as <person>` and the timing line (`print_timing`). Three checks per size, whose text starts with the size (for example `1920x1080: ...`): the face is detected in at least 80 percent of the frames in which it is visible (and more than 60 frames are visible); one single track ID for the whole walk; verified as the correct registered person within 5 s (`first_verified_ms` between 0 and 5000, the right person id, never a wrong person). Measured on this PC for both deep profiles (the run of 2026-11-12, synchronous): at all five sizes the face was detected in 90 of 90 frames, one track, verified after 1.3 to 1.8 s. In `--async` mode the video is played in real time like all the others.
8. **Video 4, a stranger takes the place of a verified person** (8 s; placed last because every `run_video` starts with new track IDs). 4 checks (one about the choice of the actor and three about the outcome). Two actors stand still at the same place, in the middle of the frame, with a face 300 pixels wide and no tilt (the path `{0, 640, 540, 300, 0}` to `{8000, 640, 540, 300, 0}`). The first is a **registered** person who disappears at 3000 ms; the second is the **stranger** (the first unregistered portrait) who appears at 3500 ms, so the place is empty for 0.5 s (about 7 frames). Because the two faces have the same position and size, the tracker gives the stranger the same track ID. The registered actor is chosen by measuring: for each registered portrait the test renders the scene, detects the face (`pipeline.detector()`), runs `refine_face` with `pipeline.pose_detector()` and takes the first portrait whose refined pose in this scene is within 3 degrees of pitch, 3 of yaw and 4 of roll (a margin inside the 5 / 5 / 8 degree tolerances; a portrait right at the limit would never be stored). The first check is `a registered person with a comfortable pose margin exists for this test`: if no portrait qualifies it fails and the first registered portrait is used. The three outcome checks: the registered person is verified before 3000 ms; the stranger gets the **same** track ID (the situation the test is about); the stranger is **not** verified, i.e. he does not inherit the verification of the person who left. The last check tests the re-verification rule of `AuthPipeline::process` (a verified track that was missed for 3 or more frames loses its verification when found again; the gap of about 7 frames is below the 15 missed frames after which the tracker would drop the track).
9. **Video 6, card first: the RF cards are identified first** (22 checks; nine walks of 6 s). The test is about the rule that **the card information is always identified first**: the cards are looked up in the database, the features stored with them are retrieved, and the faces of the frames are compared with exactly those N features; a card that is not in the database is a failed comparison with no fallback to a search among all people; M (faces) and N (cards) are independent. It first gives `known_a` the card `CARD-A-0001` and `known_b` the card `CARD-B-0002` with `pipeline.store().set_card_id` (3 checks: the two cards are set; the card number is in the database file on disk, read with a second `FeatureStore`; `enroll_image` of the stranger's picture with `known_a`'s card is refused and the stranger is not registered, because a card cannot belong to two people). Each walk is one or two actors who approach the camera as in video 1 (`approach_path(6000, x0, 520, x1, 540, 110, 330, roll, 2000)`) and `run_video` is given the list of cards of the check (`card_check`), so `begin_card_check` runs before the first frame. Every walk prints a line with what happened. The nine walks:
   * **(1) M = 1 face, N = 1 card: the owner with his own card** (`known_a`, `CARD-A-0001`; 3 checks): `begin_card_check` finds the card in the database; he is verified as himself (and not as anybody else) within 4.5 s; the frame result reports the card as matched, with the track of his face (`CardStatus::matched`, `track_id`).
   * **(2) M = 1, N = 1: a registered person with somebody else's card** (`known_b` with `known_a`'s card; 1 check): he is **not** verified and the card is never matched, because his face is compared with `known_a`'s feature only (a search among all people would have found him; the best similarity to the owner is printed, 0.18 or 0.01 in the run of 2026-11-12).
   * **(3) M = 1, N = 1: a card that is not in the database** (`known_b` with `CARD-NOBODY`; 2 checks): `begin_card_check` returns `false`, `CardStatus::found` is `false` (the comparison failed); and the face is **not** searched among all people instead: the registered `known_b` is **not** verified, although the same walk in the normal mode verifies him (the last scenario below proves that).
   * **(4) M = 2 faces, N = 1 card (M > N)** (`known_a` and `known_b`, the card of `known_a`; 4 checks): two faces are in the frames; the owner's face is verified and the card is matched; the other face (registered, but his card is not in this check) is not verified; the card is matched to the owner's track and not to the other face.
   * **(5) M = 1 face, N = 2 cards (M < N)** (`known_b`, the cards of `known_a` and `known_b`; 2 checks): the one face is verified as the owner of the card it is most like (`known_b`) within 4.5 s; card B is matched, card A (its owner is not in the picture) is not.
   * **(6) M = 2 faces, N = 2 cards (M = N)** (both, both cards; 2 checks): both faces are verified, each as the owner of its own card, and each card is matched to its owner's track.
   * **(7) M = 2 faces, N = 2 cards, one card not in the database** (the cards `CARD-A-0001` and `CARD-NOBODY`; 2 checks): `begin_card_check` returns `false` and says which card failed (`CardStatus::found`); the card that was found still verifies its owner, and the face whose card is unknown is not verified (no fallback).
   * **Back to the normal search** (`known_b`, no card check; 1 check): after `end_card_check` the pipeline is no longer in card-first mode (`card_check_active()` is `false`) and the registered person is verified by the search among all people again.
   * **The deleted owner's card is worthless** (2 checks): after `remove_person(known_a)` the card belongs to nobody (`find_by_card` finds nobody), and a walk of `known_a` with that card ends in a failed comparison (`begin_card_check` false, card not found) and no verification.
   Measured on this PC (the run of 2026-11-12): the owner with his own card was verified after 1.07 s with the permissive profile (similarity 0.942 to himself) and after 1.33 s with the dev profile (0.928); a registered person with another's card reached a best similarity of 0.18 (permissive) or 0.01 (dev) to the owner and was not verified; in the 2-by-2 walk both faces were verified after 1.07 s and 0.80 s (permissive).
10. **Time per frame over all videos.** Prints `process(): median ... p95 ... p99 ... max ...` over all frames of all videos (`g_all_frames`). With `--async` two more checks follow: `worker mode: 99% of the frames take less than 40 ms` and `worker mode: no frame takes 60 ms or more` (the requirement for detection plus tracking at 1280 x 1080 is 60 ms and is applied to 1920 x 1080 by analogy; with the worker a frame only pays for detection, tracking and matching). **Known problem on the development PC (2026-11-12):** with the dev profile (SCRFD on ncnn) `--async` fails the second check and sometimes the first: the slowest frame took 59 to 139 ms and the 99th percentile 38 to 48 ms. An interleaved comparison on the same PC showed that the last commit before the card-first, SQLite and 1920x1080 changes fails the same check with the same numbers (p99 37.8 and 48.3 ms, slowest frame 76.8 and 83.6 ms there; 38.4 and 42.7 ms, 115.6 and 95.8 ms with the new code), and the same check at 1280 x 1080 fails with the new code too, so it is **not caused by the 1920x1080 size or by these changes**: the worker-thread tail latency of the dev profile is currently too noisy on this PC (it has fast and slow cores; earlier the same day the test passed with a slowest frame of 34.8 ms). The limits were not loosened. The permissive profile passes (99th percentile 15.0 ms, slowest frame 17.2 ms).
11. **Worker lifecycle** (only with `--async`; 3 checks). A face is rendered once (a registered portrait, 300 pixels wide) and an empty frame. (a) `reset_tracking()`, then `process` of the face frame at time 100000 ms: an unverified track appears and a job goes to the worker; the test reads `jobs_in_flight` from the result, calls `reset_tracking()` again (the old track no longer exists), then `flush()` and `process` of the empty frame. Checks: `a pose-check job was handed to the worker` (`jobs_in_flight >= 1`); `after reset_tracking() the old result is ignored and nothing is left of the old track` (the flush returned within 5000 ms, the empty frame has no faces, and `pipeline.tracker().tracks()` is empty). (b) A second `AuthPipeline` is created in worker mode (`build\test_pipeline_async2.csv`, or `.db` with `--sqlite`), given two frames (so a job is queued or running) and destroyed. Check: `a pipeline with a busy worker can be destroyed (no hang, no crash)` (the whole thing took less than 30000 ms; a running job may finish, queued jobs are dropped).
12. **Everything runs on the CPU** (the very last 2 checks, after all videos and, with `--async`, after the worker lifecycle test, so the pipeline has done everything it does: detection, pose check, extraction, matching, the worker thread, the database): `every engine of the pipeline (detector, pose detector, extractor, and the worker's own) runs on the CPU` (`pipeline.runs_on_cpu()`, see `AuthPipeline::runs_on_cpu` in `pipeline.md`; in worker mode this includes the worker's own pose detector and extractor); and `after all the videos, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process` (`loaded_gpu_libraries()` is empty, see `cpu_only_check.h` below). A line before the second check says which GPU libraries are loaded (`none`) and which Direct3D 11 / DXGI libraries, for information only: in this test none of them is loaded either (`GPU libraries loaded in this process: none; Direct3D 11 / DXGI libraries (informational): none`), because the test renders its frames in memory and decodes no video file. The check is the same in every mode: synchronous, `--async` and `--sqlite`.

How to read it: `ok:` lines are passed checks; any `FAIL:` line names what broke. The timing lines (`print_timing` and the summary) are information. The test checks the *integration* of the system: the actor is the same flat photo that was registered, so it does not measure recognition accuracy (that is `eval_features`), and it can only tilt heads, not turn them (see `testdata.md`). The program counts stored crops to show that a verified track is no longer cropped; that it is no longer extracted or matched follows in the code because only new crops are extracted and matched (the header comment of the test says the same).

**How it works.** It reads the options, loads the profile, calls `AuthPipeline::init(profile, database file, g_async, format)` (the format is passed explicitly: `DatabaseFormat::Sqlite` with `--sqlite`, otherwise `DatabaseFormat::Csv`), prepares portraits and actors with `make_actor` and `approach_path`, and runs `run_video` for each scenario, evaluating the logs with `CHECK`.

**Engine.** The integrated project (`face_authentication.sln`).

**Used by.** `run_tests.bat` (fifth to ninth test: the two profiles synchronous, the permissive profile with `--sqlite`, and each profile with `--async`) and `scripts\run_tests_vs2015.bat` (the same five runs); by hand.

## tests/cpu_only_check.h

The proof, from the outside, that nothing ran on a GPU. Every engine of the project runs on the CPU (the target PC has no GPU): ncnn is told not to use Vulkan, ONNX Runtime gets no execution provider but its CPU one, the matcher is AVX2 code and the classical engines (Haar, LBP) are plain C++. Besides asking the engines (`runs_on_cpu`), which only says what the code asked for, the tests look at the **process itself**: a network that had used a GPU would have loaded a GPU library, so after the engines have run **none** of the libraries below may be loaded in the process. It is a header only, with three `static` functions, included by `test_common.cpp`, `test_feature.cpp` and `test_pipeline.cpp`; `#pragma comment(lib, "psapi.lib")` asks the linker for the Windows library `psapi.lib` (the process-status functions), so neither `build.bat` nor the Visual Studio projects list it (the same way `draw_text.cpp` asks for `gdi32.lib` and `user32.lib`).

The libraries that must **not** be loaded (the name is compared in lower case, by the start of the file name, so `vulkan-1.dll` and `d3d12.dll` and a versioned `cudnn64_8.dll` all count):

| Name (start of the file name) | What it is |
|---|---|
| `vulkan-1` | Vulkan, what ncnn would load for GPU computing |
| `d3d12` | Direct3D 12 (DirectML, hardware video processing) |
| `nvcuda`, `cudart`, `cublas`, `cudnn`, `nvinfer` | NVIDIA CUDA, its libraries, and TensorRT |
| `opencl` | OpenCL |
| `directml` | DirectML |
| `onnxruntime_providers_cuda`, `onnxruntime_providers_tensorrt`, `onnxruntime_providers_dml`, `onnxruntime_providers_openvino` | the GPU and accelerator execution providers of ONNX Runtime |

Direct3D 11, DXGI and `dxva2` are **not** on the list: Windows loads them for many ordinary reasons (the video writer of Media Foundation does: a running `gen_video`, which writes and reads an MP4, has `d3d11.dll`, `dxgi.dll`, `dxcore.dll` and `d3d10warp.dll` loaded, and the Windows GPU counters show an idle `3d` device while the writer encodes, see `scripts/gpu_probe.ps1` below; whether the reader alone loads them was not separated out; `test_pipeline`, which uses neither, loads none). They are only reported. (`onnxruntime_providers_shared.dll`, which ships next to the programs, is no GPU provider and is not on the list.)

| Function | Purpose |
|---|---|
| `loaded_library_names` | The file names of all libraries this process has loaded, in lower case. |
| `loaded_gpu_libraries` | Those of them that are on the list above. Empty = nothing used a GPU. |
| `loaded_graphics_libraries` | The Direct3D 11 / DXGI / `dxva2` libraries that are loaded (information only). |

### `loaded_library_names`

```cpp
static std::vector<std::string> loaded_library_names();
```

**What it does.** Returns the file names, without the folder and in lower case (`onnxruntime.dll`, `d3d11.dll`, ...), of every library (DLL) that this process has loaded at the moment of the call, including the program itself. It takes no input. Returns an empty list when Windows refuses to list the modules.

**How it works.** `EnumProcessModules(GetCurrentProcess(), ...)` fills an array of up to 2048 module handles (the number actually returned is `bytes / sizeof(HMODULE)`, at most 2048). For each handle `GetModuleFileNameA` gives the full path; the part after the last `\` or `/` is kept and converted to lower case with `std::tolower`. A module whose name cannot be read is skipped.

**Used by.** `loaded_gpu_libraries` and `loaded_graphics_libraries`.

### `loaded_gpu_libraries`

```cpp
static std::vector<std::string> loaded_gpu_libraries();
```

**What it does.** Returns the names of the loaded libraries that are GPU or accelerator libraries (the table above). **An empty list is the proof that nothing used a GPU** in this process so far. It takes no input.

**How it works.** For every name from `loaded_library_names` and every forbidden prefix (`vulkan-1`, `d3d12`, `nvcuda`, `cudart`, `cublas`, `cudnn`, `nvinfer`, `opencl`, `directml`, `onnxruntime_providers_cuda`, `onnxruntime_providers_tensorrt`, `onnxruntime_providers_dml`, `onnxruntime_providers_openvino`), the name counts when it starts with the prefix (a `compare` of the first characters). Note what this does and does not show: it is a snapshot of the moment of the call, so the test calls it **after** the engines have run (a library loaded and then unloaded again would not be seen), and a GPU that is used through a library that is not on the list would not be seen either; the list holds the libraries that ncnn, ONNX Runtime and the usual GPU frameworks load.

**Used by.** `tests/test_common.cpp` (`main`: after both networks have run), `tests/test_feature.cpp` (`main`: after the extractor has run), `tests/test_pipeline.cpp` (`main`: at the very end, after all the videos).

### `loaded_graphics_libraries`

```cpp
static std::vector<std::string> loaded_graphics_libraries();
```

**What it does.** Returns the names of the loaded libraries that start with `d3d11`, `dxgi` or `dxva2`: Direct3D 11, the DirectX graphics infrastructure and DirectX video acceleration. **Information only:** the tests print the list but do not check it, because Windows loads these libraries for ordinary reasons (in `test_common`, which writes and reads an MP4, `d3d11.dll` and `dxgi.dll` are loaded, because of the video writer's idle Direct3D device; in `test_pipeline` none of them is) and having them loaded does not mean that a GPU computed anything.

**How it works.** The same loop as `loaded_gpu_libraries`, with the three prefixes `d3d11`, `dxgi`, `dxva2` (the code compares the first 5, 4 and 5 characters).

**Used by.** `tests/test_common.cpp` (`main`) and `tests/test_pipeline.cpp` (`main`), for the information line `GPU libraries loaded in this process: ...; Direct3D 11 / DXGI libraries (informational): ...`.

## run_tests.bat

| Function | Purpose |
|---|---|
| main script | Build everything, run all tests, the generated-video check, optionally the evaluations. |
| `:failed` (label) | Print the failure banner and exit with code 1. |

### main script (run_tests.bat)

```bat
run_tests.bat          all tests
run_tests.bat full     the tests, then the evaluations
```

**What it does.** Builds the project (`call build.bat`; stops with exit code 1 if the build fails), then runs, in this order:

1. `test_common`, `test_detect_units`, `test_match` (the last one: M:N matching against 10,000 features, card first, and the managed database in both formats, CSV and SQLite);
2. `test_feature models\permissive.ini`, `test_feature models\dev_noncommercial.ini` and `test_feature models\classical.ini` (the feature extraction engine alone; `models\dev_noncommercial_onnx.ini` is not in the script, run it by hand);
3. `test_pipeline models\permissive.ini` and `test_pipeline models\dev_noncommercial.ini` (synchronous);
4. `test_pipeline models\permissive.ini --sqlite` (the same test with the registered people in a SQLite database instead of the default CSV file);
5. `test_pipeline models\permissive.ini --async` and `test_pipeline models\dev_noncommercial.ini --async` (worker thread, videos played in real time; the echo line says about 1 minute for the first);
6. the **1920x1080 footage**: `scripts\make_fhd_footage.bat` makes six videos with the exact truth: three clean ones (`data\videos\fhd_1person`, `fhd_3people`, `fhd_5people`) and the same three with the **simulated** camera look (`fhd_cam_1person`, `fhd_cam_3people`, `fhd_cam_5people`, made with `gen_video --camera-look`; no camera is used); each `gen_video` also checks that its MP4 is the right way up. Then `check_video` plays them through the complete system with `models\permissive.ini`: `fhd_3people` synchronous (`--out data\videos\fhd_3people\annotated_sync.mp4`) and with `--async` (worker thread in real time, `annotated_worker.mp4`), then `fhd_5people` and `fhd_1person` synchronous; then, after an echo line that names the simulated look, `fhd_cam_3people` synchronous and with `--async`, and `fhd_cam_5people` synchronous (`fhd_cam_1person` is not played through `check_video` by the script; `eval_footage` evaluates it with `full`);
7. the same system at 1280x720, a size cameras also deliver: `gen_video.exe data\videos\demo_720p 8 15 3 0 2 --size 1280x720`, then `check_video.exe models\permissive.ini data\videos\demo_720p\video.mp4 --out data\videos\demo_720p\annotated_sync.mp4`;
8. `db_convert`: `gen_features.exe build\convert_demo.csv 500 128 7` makes a database of 500 random people, `db_convert.exe build\convert_demo.csv build\convert_demo.db --force` converts it to SQLite and `db_convert.exe build\convert_demo.db build\convert_demo_back.csv --force` back to CSV (each conversion reads its result back and compares it).

`scripts\run_tests_vs2015.bat` (the Visual Studio 2015 counterpart, see `docs/VISUAL_STUDIO.md`) runs the same tests and video checks from `build\vs2015\<configuration>`.

It stops at the first program that returns a non-zero exit code. With the argument `full` it afterwards runs, for each of the five profiles (`permissive`, `dev_noncommercial`, `dev_noncommercial_square`, `dev_noncommercial_onnx`, `classical`; `dev_noncommercial_square` is the SCRFD profile with the 640x640 square, listed next to the fitted-rectangle `dev_noncommercial` to compare them), `eval_footage` on each of the six footages (`fhd_1person`, `fhd_3people`, `fhd_5people`, `fhd_cam_1person`, `fhd_cam_3people`, `fhd_cam_5people`; the engines one by one: detection and tracking, feature extraction, M:N matching; the output of each starts with `---- <profile> on <footage> ----`), then `eval_detect` (the same five profiles, `--frame 1920x1080`), `eval_features` (permissive and dev_noncommercial on `data\portraits`) and `bench --threads 2` (the same five profiles). The evaluations only print numbers and are **not** checked for success (the header comment of the script says the same). The single-engine benchmarks `bench_detect`, `bench_extract` and `bench_match` are not started by the script (run them by hand, see `docs/TESTING.md`). It ends with `ALL DONE`. The version with the five-profile lists (SCRFD fitted rectangle) was run in full with Visual Studio 2019 on 2026-11-12 (`build\run_tests_final5.log`, with `test_common` at 69 checks at that time): `ALL DONE`, 11 test runs `ALL TESTS PASSED` and all 8 video checks `VIDEO CHECK PASSED`; the dev worker-thread timing check passed (`process()` median 11.7 ms, p95 17.8, p99 21.3, max 24.4). The five `test_common` checks added afterwards were run with a full rebuild and `test_common` / `test_pipeline` (dev, synchronous), not by repeating the full script.

**How it works.** Changes into the folder of the script, calls `build.bat`, and uses `|| goto :failed` after each test and after each of the footage, video and conversion commands. The test durations in the header comment (about 4 minutes, and about 15 minutes more with `full`) are the author's estimates; the worker and video runs listed above come on top of that (the echo line of the first worker run says about 1 minute).

**Used by.** Run by hand (`docs/TESTING.md`).

### `:failed`

**What it does.** Prints `*** A TEST FAILED ***` and exits with code 1.

**How it works.** The target of every `|| goto :failed`.

**Used by.** `run_tests.bat` only.

## build.bat

| Function | Purpose |
|---|---|
| step 1 | Compile every engine source file into `build\obj`. |
| step 1b | Compile SQLite (`third_party\sqlite\sqlite3.c`) as plain C into `build\obj\sqlite3.obj`, once. |
| step 2 | Put the object files into one library `build\face_auth.lib`. |
| step 3 | Compile and link each program of `tools\` and `tests\` (or the ones named); program objects go to `build\objprog`. |
| DLL copy | Copy the ONNX Runtime DLLs next to the programs. |

### Build steps (build.bat)

```bat
build.bat              build the library and every program in tools\ and tests\
build.bat test_match   build the library and only tests\test_match.cpp
```

**What it does.** Builds the whole project with Visual Studio 2019 (x64) and prints `BUILD OK`, or `BUILD FAILED` with the compiler log and exit code 1. Results: `build\face_auth.lib` and one `.exe` per program in `build\bin\`. Extra compiler switches can be passed in the environment variable `FA_EXTRA`, for example `set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f` to change a setting of `config.h` without editing it. The Visual Studio start script is taken from `C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat`.

**How it works.**
1. Sets the compiler switches `/std:c++14 /O2 /arch:AVX2 /MD /EHsc /W3 /openmp /MP` plus three defines (`NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `WIN32_LEAN_AND_MEAN`), the include folders of all engine folders and of SQLite (`third_party\sqlite`), ncnn and ONNX Runtime, and the libraries (ncnn and its glslang parts, `onnxruntime.lib`, and the Windows libraries `mfplat`, `mf`, `mfreadwrite`, `mfuuid` for Media Foundation, `windowscodecs` for picture files, `ole32`, `oleaut32`, `uuid`, `shlwapi`, and **`gdi32` and `user32`** for the text drawing of `draw_text`; `psapi.lib`, which `tests\cpu_only_check.h` needs, is not in the list: that header asks the linker for it itself with `#pragma comment(lib, "psapi.lib")`). C++14 is chosen on purpose (comment in the script): the code must also compile with Visual Studio 2015, which has no C++17.
2. Creates the folders `build\obj` (engine objects), `build\objprog` (program objects) and `build\bin`. It deletes leftovers `build\obj\prog_*.obj` of older builds: everything in `build\obj` is packed into the library, and the `main()` of a program must not end up there.
3. Compiles all `.cpp` files of `src\common`, `src\detect`, `src\feature`, `src\match`, `src\store`, `src\camera`, `src\pipeline` and `src\testdata` into `build\obj` (log in `build\compile.log`; this includes `video_writer.cpp` and `draw_text.cpp`). Then SQLite (step 1b, the optional database format; log in `build\compile_sqlite.log`): if `build\obj\sqlite3.obj` does not exist yet, `third_party\sqlite\sqlite3.c` is compiled **as plain C** (`/TC`, `/O2 /MD`) with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_THREADSAFE=1`, `SQLITE_DEFAULT_MEMSTATUS=0` and `SQLITE_OMIT_SHARED_CACHE`; the object is kept between builds (delete it to compile SQLite again). Then all objects of `build\obj`, SQLite's too, are put into `build\face_auth.lib`.
4. For every program (all `tools\*.cpp` and `tests\*.cpp`, or only the names given on the command line, looked up in `tests\` first and then `tools\`; the headers `tools\bench_*.h` are not programs, they are found by the `#include` of the benchmark program next to them) it compiles and links one `.exe` against the library (the program's own object file is `build\objprog\<name>.obj`; log in `build\link_<name>.log`) and prints `built build\bin\<name>.exe`. An unknown name prints `no such program: <name>` and stops.
5. Copies `onnxruntime.dll` and `onnxruntime_providers_shared.dll` to `build\bin\`.

The Media Foundation libraries are linked into every program, but only `camera_check`, `check_video`, `eval_footage`, `gen_video` and `test_common` use Media Foundation (the camera, video files and the video writer; the two readers and the writer are created with hardware transforms switched off, `docs/camera.md`). The harmless message `'vswhere.exe' is not recognized` is explained in `docs/TESTING.md`.

**Used by.** `run_tests.bat`; by hand.

## scripts/make_fhd_footage.bat

| Function | Purpose |
|---|---|
| main script | Make the six 1920x1080 videos of the evaluation: three clean ones and the same three with the simulated camera look. |

### main script (make_fhd_footage.bat)

```bat
scripts\make_fhd_footage.bat [folder of gen_video.exe]
```

**What it does.** Creates the 1920x1080 (Full HD) test footage that the evaluation of the engines uses: **six** videos of people walking toward the camera, each in its own folder under `data\videos\` (the folder `data\` is ignored by git, so the footage is recreated by this script). The first three are the clean footage, each with its own people; the last three show the same people and walks again, with the look of a camera in a room (`gen_video --camera-look`; a **simulation**: no camera is switched on and nothing is recorded), so that the two sets can be compared:

| Folder | People | Registered | `gen_video` call |
|---|---|---|---|
| `fhd_1person` | 1 | 1 | `gen_video.exe data\videos\fhd_1person 8 15 1 0 1` |
| `fhd_3people` | 3 | 2 (and 1 stranger) | `gen_video.exe data\videos\fhd_3people 8 15 3 3 2` |
| `fhd_5people` | 5 | 3 (and 2 strangers) | `gen_video.exe data\videos\fhd_5people 8 15 5 8 3` |
| `fhd_cam_1person` | 1 | 1 | `gen_video.exe data\videos\fhd_cam_1person 8 15 1 0 1 --camera-look` |
| `fhd_cam_3people` | 3 | 2 (and 1 stranger) | `gen_video.exe data\videos\fhd_cam_3people 8 15 3 3 2 --camera-look` |
| `fhd_cam_5people` | 5 | 3 (and 2 strangers) | `gen_video.exe data\videos\fhd_cam_5people 8 15 5 8 3 --camera-look` |

A `fhd_cam_*` video is made of the same portraits, paths and truth as the clean video of the same size, with a static room behind the people (a different room for each video), uneven light with darker corners, slow exposure changes, a warm cast, a slightly soft lens, sensor noise and a harder compression: 4000 instead of 8000 kbit/s, so the files are about 4.2 million bytes instead of 7.0 to 7.6 million (motion blur is part of the look, but the walks are too slow to trigger it; `testdata.md`). Each video is 8 seconds at 15 frames per second, 120 frames of 1920x1080 (`gen_video` writes this size by default), and each folder holds `video.mp4` (H.264), `frame_*.jpg` (the same pictures), `truth.csv` (the exact face box of every person in every frame) and `scenario.txt` (who is registered); see `gen_video` and `testdata.md`. The people come from the portraits in `data\portraits`; the fourth number of each call skips usable portraits so that the three sizes of footage use different people (a `fhd_cam_*` video repeats the people of the clean video with the same number). Nobody is filmed with a camera. The optional argument is the folder that holds `gen_video.exe`: by default `build\bin` (`build.bat` must have been run); the Visual Studio 2015 solutions put their programs in `build\vs2015\Release`, which `scripts\run_tests_vs2015.bat` passes. If `gen_video.exe` is not there the script says so and exits with code 1; a `gen_video` that fails (including its own check that the MP4 is the right way up) ends the script with code 1. On success it prints `footage written to data\videos\fhd_1person, fhd_3people, fhd_5people and fhd_cam_1person, fhd_cam_3people, fhd_cam_5people (1920x1080)` and exits with 0. Between the two sets it prints a line that says that the next three videos have the simulated camera look. The header comment names the next steps: `eval_footage.exe <profile.ini> data\videos\fhd_3people` (engine by engine) and `check_video.exe <profile.ini> data\videos\fhd_3people\video.mp4` (the complete system).

**How it works.** `setlocal`, `cd /d "%~dp0\.."` (so it can be started from anywhere), `BIN` from the first argument or `build\bin`, a check that `%BIN%\gen_video.exe` exists, then the six `gen_video` calls (the clean ones with a "footage 1/3 ... 3/3" echo line each, then the three with `--camera-look`), each followed by `|| exit /b 1`.

**Used by.** `run_tests.bat` and `scripts\run_tests_vs2015.bat` (`call scripts\make_fhd_footage.bat`); by hand.

## scripts/run_tests_vs2015.bat

| Function | Purpose |
|---|---|
| main script | Build the four solutions with Visual Studio 2015 only and run every program of every project. |
| `:debug_tests`, `:no_vs2015`, `:build_failed`, `:failed` (labels) | The Debug tests, and the three ways to stop with exit code 1. |

### main script (run_tests_vs2015.bat)

```bat
scripts\run_tests_vs2015.bat          Release: rebuild the four solutions, then run every program of every project
scripts\run_tests_vs2015.bat debug    Debug: rebuild, then run only the functional tests
```

**What it does.** The Visual Studio 2015 counterpart of `run_tests.bat full`: **nothing of Visual Studio 2019 is used** (not `build.bat`, not `run_tests.bat`). It rebuilds `face_detection.sln`, `feature_extraction.sln`, `feature_matching.sln` and `face_authentication.sln` with the IDE's own command line (`devenv.com ... /Rebuild "Release|x64"`, after deleting `build\vs2015`) and runs every program from `build\vs2015\Release`. A build is judged by the line `, 0 failed,` in its log `build\vs2015_build_<solution>.log`, not by the exit code, because `devenv.com` can print an `AccessViolationException` after a finished build. The script stops at the first failing build or program (the tests print `ALL TESTS PASSED` or `TESTS FAILED`; the other programs must end with exit code 0). The Release run, in this order:

* `face_detection.sln`: `test_detect_units`, `eval_detect` (five profiles: permissive, dev, dev square, dev on ONNX Runtime, classical), `detect_image`, `calibrate_pose`, `roll_probe`, `bench_detect` (the same five profiles);
* `feature_extraction.sln`: `test_feature` (four profiles: permissive, dev, dev on ONNX Runtime, classical), `eval_features` (two profiles), `calibrate_lbp` (writing `build\vs2015_lbp_calibration.bin`, not over `models\lbp_calibration.bin`), `bench_extract`;
* `feature_matching.sln`: `test_match`, `gen_features` (2,000 people of 256 numbers, as a CSV file and as a SQLite database), `db_convert` (CSV to SQLite and back), `bench_match`;
* `face_authentication.sln`: `test_common`, `test_pipeline` (permissive, dev, permissive with `--sqlite`, permissive with `--async`, dev with `--async`), `make_fhd_footage.bat` (with the Visual Studio 2015 folder as its argument; six videos), `eval_footage` (five profiles, the same as for `eval_detect`, on each of the six footages: the three clean ones and the three `fhd_cam_*` with the simulated camera look), `check_video` (the 1920x1080 footage: `fhd_3people` synchronous and with `--async`, `fhd_5people`, `fhd_1person`; the simulated camera look: `fhd_cam_3people` synchronous and with `--async`, `fhd_cam_5people`; and a 1280x720 video), `camera_check` and `bench` (two profiles).

The Debug run (`debug`) runs only the functional tests (`test_detect_units`, `test_feature` with the permissive profile, `test_match`, `test_common`, `test_pipeline` with the permissive profile); the timing tests and the benchmarks make no sense without optimization. The worker-thread tests have timing limits: on a PC with fast and slow cores run the script pinned to the fast cores, `cmd /c "start /wait /b /affinity FFF scripts\run_tests_vs2015.bat"` (`docs/TESTING.md`). The header comment says the Release run takes about 15 minutes. It ends with `ALL DONE (Visual Studio 2015, Release)` (or `Debug`). The script was changed for the fitted SCRFD rectangle (the profile `dev_noncommercial_square` was added to `eval_detect`, `bench_detect` and the `eval_footage` loop, which are now five profiles instead of four); **that version was run with Visual Studio 2015 on 2026-11-12 (`build\run_vs2015_final4.log`) and went through in one go**: the four solutions rebuilt (10, 9, 8 and 17 projects, 0 failed), 12 test runs `ALL TESTS PASSED`, 8 video checks `VIDEO CHECK PASSED`, the five-profile lists ran in `eval_detect`, `bench_detect` and the `eval_footage` loop, and the script ended `ALL DONE (Visual Studio 2015, Release)` (`docs/VISUAL_STUDIO.md`). Small changes made to the tests after that run (the five new `test_common` checks, the scaled positions of `test_pipeline` step 1b) were checked with incremental Visual Studio 2015 builds and runs of `test_common` and `test_pipeline` with the dev profile, **not** by repeating the full script.

**How it works.** `cd /d "%~dp0\.."`, `CONFIG` is `Release` or, with the argument `debug`, `Debug`; `DEVENV` is `%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\Common7\IDE\devenv.com` (if it is missing: `:no_vs2015`, exit code 1); `BIN` is `build\vs2015\<configuration>`. A `for` loop over the four solutions builds each and checks its log with `findstr /c:", 0 failed,"` (`:build_failed` otherwise); then every program is run with `|| goto :failed`.

**Used by.** By hand (`docs/VISUAL_STUDIO.md`, `docs/TESTING.md`).

## scripts/fetch_portraits.py

| Function | Purpose |
|---|---|
| `api` | One request to the Wikimedia Commons API, with retries. |
| `category_files` | List all files of a Commons category. |
| `image_info` | Licence information and download address for up to 50 files. |
| `person_name` | Turn a Commons file title into a person's name. |
| `slug` | Turn a name into a folder name. |
| `main` | Download the portraits and write `LICENSES.csv`. |

The script downloads openly licensed official portraits of members of the 116th and 117th United States Congress (works of the US federal government) from Wikimedia Commons into `data\portraits`, one folder per person. A person who has a portrait in both congresses gives two different photos of the same face, which the "same person" tests need. It needs Python 3 with the `requests` package and an internet connection.

### `api`

```python
def api(params): ...
```

**What it does.** Sends one request to `https://commons.wikimedia.org/w/api.php` and returns the answer as JSON. It adds `format=json`, identifies itself with a User-Agent that names the project, waits 0.25 s after each good answer to be polite to the server, and tries up to 4 times (waiting 2, 4, 6 and 8 s after the failed tries) before raising the error.

**How it works.** `requests.get` in a loop on the status code.

**Used by.** `category_files`, `image_info`.

### `category_files`

```python
def category_files(category): ...
```

**What it does.** Returns the titles of all files in a Commons category (the two categories are "Official portraits of members of the 117th / 116th United States Congress").

**How it works.** Requests 500 at a time and follows the `continue` token until there are no more pages.

**Used by.** `main`.

### `image_info`

```python
def image_info(titles): ...
```

**What it does.** For up to 50 file titles returns the licence information (`extmetadata`), image size, type and the address of a reduced copy that is 800 pixels wide (`IMAGE_WIDTH`, enough for face detection).

**How it works.** One `imageinfo` request with `iiurlwidth = 800`; files without information are left out.

**Used by.** `main`.

### `person_name`

```python
def person_name(title): ...
```

**What it does.** Turns a title such as `File:Pete Aguilar 117th congress (cropped).jpeg` into `Pete Aguilar` so that files of the same person get the same name, also when the title carries a prefix such as `Rep.` or `Senator`.

**How it works.** Removes the `File:` prefix and the extension, cuts the name at the first decoration (comma, ` 11?th`, `official`, `portrait`, `Congress`, `headshot`, ...) with a regular expression, removes a leading title word and collapses spaces.

**Used by.** `main`.

### `slug`

```python
def slug(name): ...
```

**What it does.** Makes a folder name: lower case, every run of characters other than letters and digits replaced by `_` (for example `Pete Aguilar` becomes `pete_aguilar`).

**How it works.** A regular expression and `strip`.

**Used by.** `main`.

### `main` (fetch_portraits)

```python
def main(): ...
```

**What it does.** Command line: `python scripts\fetch_portraits.py [--max-people 150] [--out data/portraits]`. It prints the number of files per category, the number of files skipped because of the licence, how many people were chosen (and how many of them have two different photos) and, at the end, `downloaded N images of M people -> <folder>`. Each person gets a folder with `117th.jpg` and/or `116th.jpg` (the tests use `117th.jpg`; a person who has only a 116th portrait is skipped by every program that asks for `117th.jpg`). The file `data\portraits\LICENSES.csv` lists every kept image with person, file, Commons title, source page, licence, licence URL and author.

**How it works.**
1. List both categories and read the licence data in blocks of 50.
2. Keep only images whose licence name matches public domain, `PD...` or CC0 and that are not marked `Copyrighted`, are JPEG or PNG and at least 400 pixels wide; skip title variants such as `(cropped)`, `(3x4)`, `(alternate)`.
3. Sort people by the number of congresses in which they have a portrait (two first) and then by name; take the first `--max-people`.
4. Download the first file per person and congress as `<congress>th.jpg` (existing files are not downloaded again), waiting and retrying on HTTP 429 (5, 15, 30 s) and pausing 1.5 s between downloads, and write `LICENSES.csv` (if nothing was downloaded the script stops with an error when it tries to write the header).

**Used by.** Run by hand (`docs/TESTING.md`); `merge_portraits.py` is run afterwards.

## scripts/merge_portraits.py

| Function | Purpose |
|---|---|
| `normalize` | Reduce a folder name to the plain person name. |
| `import_staged` | Move portraits downloaded into a staging folder into `data/portraits` (new `--import` option). |
| `main` | Optionally import a staging folder, then merge folders of the same person and rewrite `LICENSES.csv`. |

### `normalize`

```python
def normalize(folder_name): ...
```

**What it does.** Returns the folder name a person should have: removes a leading `rep_`, `sen_`, `congresswoman_`, `congressman_`, `representative_` or `senator_`, up to three trailing decorations (`_official_portrait`, `_headshot`, `_117thcong`, `_senatorial`, `_full`, `_u_s_senator`, `_<number>`), a trailing number glued to a letter (`..._greene2` becomes `..._greene`), and finally applies a small table of aliases (`mes`, `auchincloss`, `michellesteel`, `durbin`) for names that rules cannot derive. The aliases were found with `eval_features`, where photos in these folders scored 0.73 to 0.96 against the other folder, which different people practically never reach (comment in the script).

**How it works.** Regular expression substitutions and a dictionary lookup.

**Used by.** `main`.

### `import_staged`

```python
def import_staged(staging): ...
```

**What it does.** Moves the portraits that `fetch_portraits.py` downloaded into a separate **staging folder** (for example `fetch_portraits.py --out data/portraits_new`) into `data/portraits`, without creating duplicates. A photo whose bytes already exist anywhere in `data/portraits` is skipped (it is the same Commons file). A new photo keeps its person folder name; if the person already has a file with that name, the photo becomes `alt.jpg`, then `alt2.jpg` and so on. The licence rows of the photos that were taken over are added to `data/portraits/LICENSES.csv`. It prints `imported N new photos from <staging> (M licence rows added)`.

**How it works.**
1. `known` = the SHA-256 hash of every `.jpg` below `data/portraits` (recursive), to recognise a photo that already exists.
2. For every folder of the staging folder (sorted) and every `.jpg` in it (sorted): skip it if its hash is in `known`; otherwise create the person folder in `data/portraits`, choose the file name (`alt.jpg`, `alt2.jpg` ... if the name is taken), **copy** the file (`shutil.copy2`; the staging folder is left as it is), add the hash to `known` and remember "staging path -> new path".
3. If `LICENSES.csv` exists in the staging folder, every row whose `file` (with `/` separators) is a path that was copied gets the new path and is collected.
4. The main `LICENSES.csv` is read, and written again with its old rows followed by the collected rows.

**Used by.** `main` (only with `--import`).

### `main` (merge_portraits)

```python
def main(): ...
```

**What it does.** Command lines:

* `python scripts\merge_portraits.py` (run after a download);
* `python scripts\merge_portraits.py --import data/portraits_new` (after a download that went into a staging folder: first import it with `import_staged`, then merge as usual).

The first download split some people over two folders (for example `rep_andrew_clyde` and `andrew_clyde`). For every folder whose normalized name differs from its own name, the pictures are moved into the folder with the normalized name: a picture that is byte-for-byte identical (same SHA-256 hash) to one already there is deleted (a photo compared with itself would fake a perfect match); a picture whose file name is already taken is stored as `alt.jpg`, then `alt2.jpg` and so on. The old folder is removed and `merged <old> -> <new>` is printed. Finally `LICENSES.csv` is rewritten with the new paths and person names, and `N files moved, M licence rows kept` is printed.

**How it works.** If the arguments are `--import <folder>`, it calls `import_staged` first. Then it walks `data\portraits` in sorted order, keeps a dictionary `moves` of old to new paths, and rewrites the CSV with `csv.DictReader` / `csv.DictWriter`.

**Used by.** Run by hand after `fetch_portraits.py` (`docs/TESTING.md`).

## scripts/gpu_probe.ps1

| Function | Purpose |
|---|---|
| main script | Start a program of the project, read the Windows GPU counters repeatedly while it runs, and report whether the program had GPU engine instances. |

The script has no functions; it is one PowerShell script with a `param` block (the program and its arguments) and one main part. It is the **outside** proof that a program used no GPU: the programs prove it from the inside (`Net::runs_on_cpu`, no GPU library loaded: `cpu_only_check.h` above), and this script asks Windows. It is a PowerShell script (it was written and run with the Windows PowerShell of Windows 11, `powershell -NoProfile`) and needs a Windows with the `GPU Engine` counter set (Windows 10 or later with a WDDM 2.0 graphics driver). Nothing in the build or in `run_tests.bat` runs it.

### main script (gpu_probe.ps1)

```
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 <program.exe> -ProgramArguments 'arg1','arg2',..."
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\check_video.exe -ProgramArguments 'models\permissive.ini','data\videos\fhd_cam_3people\video.mp4','--async'"
powershell -NoProfile -Command "& scripts\gpu_probe.ps1 build\bin\test_pipeline.exe -ProgramArguments 'models\permissive.ini'"
```

**Parameters.** `-Program` (the first, mandatory: the path of the program, relative to the project root) and `-ProgramArguments` (a list of text values, default empty: the arguments of that program). **The arguments must be passed as one quoted list**, as in the examples, because PowerShell takes a bare word that starts with `-` or `--` (for example `--async`) for one of its own parameters instead of handing it to the program.

**What it does.** Starts the program, samples the Windows performance counter `\GPU Engine(*)\Utilization Percentage` back to back until the program ends (there is no pause in the loop, so the time one `Get-Counter` call takes is the interval: about 3 s, as the script's header says; the archive has 80 samples for a 237 s run of `test_pipeline`, one reading per 3.0 s, and 2 to 4 samples for the 7 to 12 s runs of `gen_video`, `check_video` and `eval_footage`), and prints one summary line (and appends it to `build\gpu_probe_summary.log`):

* how many seconds the program ran and its exit code;
* how many counter samples were taken;
* the number of **GPU engine instances of this program** (0 = in no sample did the program have a GPU engine) and the **highest utilization** seen;
* the **engine types** seen with the highest utilization of each (`3d`, `copy`, `videodecode`, `videoencode`, `videoprocessing`, `compute`, ... in the lower case that the counter names have): a `3d` instance at 0.0% shows that a Direct3D device exists; a video, compute or copy instance would show work done by a GPU;
* the **control**: how many other processes had GPU engine instances in the same samples (27 or 28 on the development PC). If this is 0 the counters may not work, and a result of 0 instances proves nothing; the verdict says so;
* a verdict: `NO GPU USE (the counters work: other processes were visible)`, `no GPU instance seen, but NO other process was visible either: the counters may not work, so this proves nothing`, or `THE PROGRAM HAD GPU ENGINE INSTANCES`.

The program's own standard output goes to `build\gpu_probe_<program>.log` (the next run of the same program overwrites it; only its standard output is redirected). The script does not return the probed program's exit code (that is only reported in the line) and does not exit with a code for the verdict; it ends with 2 when the program does not exist (`no such program: ...`) or no process could be started; and with 3 when the `GPU Engine` counter set is missing (the message says that nothing can be measured). How to read the line and what the results were: `docs/TESTING.md`, "Proving that everything runs on the CPU", and `docs/RESULTS.md`, "All engines on the CPU".

**How it works.**
1. `$root` is the parent of the script's folder and the script changes into it, so that `build\...` and the program paths are relative to the project root wherever it is started from. If the program path does not exist it prints an error and ends with exit code 2.
2. `Get-Counter -ListSet 'GPU Engine'` checks that the counter set exists; if not, it prints the message that nothing can be measured and ends with exit code 3.
3. It creates `build` if needed, names the log `build\gpu_probe_<program name without .exe>.log` and starts the program with `Start-Process -PassThru -NoNewWindow -RedirectStandardOutput` (with `-ArgumentList` when there are arguments). `$null = $p.Handle` keeps the process handle open: without it .NET cannot read the exit code of a program that has already ended (the earlier archive `build\gpu_probe_summary_earlier.log` has a line with an empty exit code, probably from a version of the script that did not yet keep the handle).
4. The instances of the program are recognised by the text `pid_<process id>_` in the counter instance name. The loop runs while the program has not exited: one `Get-Counter` call (there is no pause in the loop, so the time one call takes is the sampling interval: about 3 s), counted as a sample only when it succeeded; for every counter instance: if its name holds `pid_<id>_` it goes into the set of the program's instances, its value is compared with the highest utilization, and the engine type (the text after `engtype_`) gets its own highest value; otherwise, if the name starts with `pid_<number>_`, that other process id goes into the control set. A failing `Get-Counter` call is ignored (no sample). Only the process that the script started is counted, not a program that it might start itself.
5. After the program has ended (`WaitForExit`) it computes the seconds and the verdict from the number of instances and the size of the control set, builds the line (with the engine types sorted by name), prints it and appends it to `build\gpu_probe_summary.log`.

**Limits.** Each sample is one reading, so a short GPU burst between two readings is not seen, and a program that ends almost at once is not sampled at all (then the control set is empty and the verdict says that the result proves nothing); the loop checks whether the program is still running only before each reading, so a short program gets at least one reading if it is still alive when the loop starts. The sampling uses CPU itself, so **timing checks of the probed program can fail while it is probed** (for example the worker-thread limits of `test_pipeline --async`): use the script for the GPU question only, never together with a timing measurement. Results on the development PC (2026-11-12, `build\gpu_probe_summary.log`): the engine programs `test_pipeline` (237 s, 80 samples) and `eval_footage` (12 s, 4 samples) had 0 instances; `gen_video` and `check_video` (both profiles), which write an MP4, had 21 `3d` instances at 0.0% utilization and no video, compute or copy instance (`docs/RESULTS.md`).

**Used by.** Run by hand (`docs/TESTING.md`, `docs/RESULTS.md`, `README.md`).

## Where code and comments disagree

The comments that were wrong or incomplete earlier have been corrected and now agree with the code: the `eval_detect`, `bench`, `eval_features`, `calibrate_lbp`, `calibrate_pose`, `run_tests.bat` and `test_pipeline` headers and usage messages (including `--async`, video 4, the `singles`, `--letters` and `--multi-only` options, and the `--refine` output and pose detector of `calibrate_pose`). What is left is small:

* `gen_video`: the usage message and the header comment agree (both list `--any-pose`, `--size WxH`, `--camera-look` and `--bitrate K`). A `--size` or `--bitrate` with no value after it (as the very last word) is refused like a bad value: message, exit code 2. The header comments of `gen_video` and `scene.h` call the look a simulation and say that the motion blur does not occur in these slow walks (less than the 2 pixels per frame it needs; see `testdata.md`).
* `check_video`: the header says `--fps N` is "frame rate of a folder of frames (default 15); a video file brings its own times". That is true for reading; but the annotated video is always written at the `--fps` rate (default 15), taken when its first frame is written, whatever the frame rate of the input video is.
