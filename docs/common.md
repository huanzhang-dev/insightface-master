# Common layer (`src/common/`)

## What this part is for

`src/common/` is the foundation of the project. Every other folder (`detect/`, `feature/`, `match/`, `store/`, `camera/`, `pipeline/`, `testdata/`, `tools/`, `tests/`) uses it, and it uses none of them. It holds the small shared data types (`Image`, `Face`, `Feature`, ...), the image operations written by hand without OpenCV (resize, crop, rotate, face alignment), reading and writing picture files through the Windows Imaging Component (WIC), one `Net` interface that hides the difference between the two neural network libraries (ncnn and ONNX Runtime), the model profile reader that decides which models are used (`models/*.ini`), a helper that writes text into pictures (only for annotated result videos), `fs_compat.h` (one name for the file-system library of every supported compiler), a stopwatch for timing, and `config.h` with every tunable number. In the overall picture of `docs/ARCHITECTURE.md` it is the "common" box: the detector, the extractor and the pipeline all call these functions to prepare images and to run networks, and all engines exchange data using the types from `types.h`.

Conventions used by every file in this folder (so they are explained only once):

* Everything is in the C++ namespace `fa`.
* An `Image` stores 8-bit pixels, interleaved, in **B, G, R** order (blue first, like a Windows bitmap), or one gray value per pixel.
* Coordinates are in pixels. The origin is the **top-left** corner of the image, x grows to the right, y grows downward. Boxes and points use `float`, so they can sit between two pixels.
* Angles are in degrees.
* Ordinary failures (missing file, model that cannot be loaded) are reported by returning `false` (or `nullptr` for the `create_*` factories). Some of these functions also print the reason to `std::cerr`.
* Functions with an output parameter such as `Image& dst` overwrite it completely. `dst` must not be the same object as `src`, because the function re-creates `dst` before it has finished reading `src`.
* A "macro" is a name that the compiler replaces by its value before compiling. A "struct" is a plain bundle of named values (fields).

## config.h

`config.h` is the single place for the numbers you may want to change. Every setting is written as

```
#ifndef NAME
#define NAME value
#endif
```

so the default in the file is used unless the compiler is told otherwise. There are two ways to change a setting: edit the value in `config.h`, or pass `/DNAME=value` to the compiler. `build.bat` does the second one with the `FA_EXTRA` environment variable, for example `set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f` before running `build.bat` (the value is added to the compile command of the library and of every program, so all files see the same value). Because the values are compiled in, **the project must be rebuilt after a change**. There are exceptions, which the first lines of `config.h` name: the speed settings of the Haar detector (`FA_HAAR_WORK_WIDTH`, `FA_HAAR_REFERENCE_WIDTH`, `FA_HAAR_MAX_WORK_WIDTH`, `FA_HAAR_FULL_SCAN_INTERVAL`, `FA_HAAR_SCALE_STEP`) are not here but at the top of `src/detect/detector_haar.cpp`, and a few implementation details are fixed numbers in the code (for example the 3 degree limit of the rotation compensation inside `refine_face`, and the matching IoU used there). Other defaults that are not in `config.h` are those of `ModelProfile` in `profile.h` (for example the feature length 128, the match threshold 0.363f and 2 threads), which the profile files override.

| Macro | Default | Purpose |
|---|---|---|
| `FA_CONFIG_H` | (empty) | Include guard of the file. |
| `FA_POSE_PITCH_TOL_DEG` | `5.0f` | Largest allowed nod (up/down), degrees. |
| `FA_POSE_YAW_TOL_DEG` | `5.0f` | Largest allowed head turn (left/right), degrees. |
| `FA_POSE_ROLL_TOL_DEG` | `8.0f` | Largest allowed head tilt toward a shoulder, degrees. |
| `FA_DETECT_INPUT_SIZE` | `640` | Side of the square image given to the deep detectors. |
| `FA_DETECT_SCORE_THRESHOLD` | `0.6f` | Default minimum detection score of a profile. |
| `FA_DETECT_NMS_IOU` | `0.3f` | Overlap above which two detected boxes are the same face. |
| `FA_MIN_FACE_PIXELS` | `40` | Smallest face the detectors report. |
| `FA_MAX_FACES` | `32` | Most faces a detector returns per image. |
| `FA_CAMERA_DEFAULT_WIDTH` | `1920` | Picture width that `open_camera_source` asks the camera for when the caller names no size. |
| `FA_CAMERA_DEFAULT_HEIGHT` | `1080` | Picture height that `open_camera_source` asks the camera for when the caller names no size. |
| `FA_CAMERA_MIN_FPS` | `15` | Camera modes slower than this (frames per second) are used only when the camera has no faster mode. |
| `FA_REFINE_FACE_WIDTH` | `320` | Width the face is enlarged to in the second look (`refine_face`). |
| `FA_MIN_CROP_FACE_PIXELS` | `80` | Narrowest face that may be stored for feature extraction. |
| `FA_POSE_CHECK_INTERVAL_MS` | `100` | Shortest time between two pose checks of one track. |
| `FA_TRACK_MATCH_IOU` | `0.3f` | Overlap needed to continue an existing track. |
| `FA_TRACK_MAX_MISSED_FRAMES` | `15` | How long a track survives without a detection. |
| `FA_CROP_STORE_INTERVAL_MS` | `1000` | Shortest time between two stored crops of one track. |
| `FA_REVERIFY_AFTER_MISSED_FRAMES` | `3` | Missed frames in a row after which a verified track must be verified again. |
| `FA_WORKER_NETWORK_THREADS` | `1` | CPU threads of the worker thread's own networks (worker mode of `AuthPipeline`). |
| `FA_WORKER_QUEUE_SIZE` | `4` | How many jobs may wait for the worker thread. |
| `FA_MAX_FEATURE_DIM` | `512` | Largest feature length. |
| `FA_ALIGNED_FACE_SIZE` | `112` | Side of the aligned face image given to the recognizers. |

Two words used below: **IoU** (intersection over union) measures how much two boxes overlap, from 0 (no overlap) to 1 (identical boxes). **Non-maximum suppression (NMS)** removes duplicate detections of the same face by keeping the best-scoring box and dropping boxes that overlap it too much.

### FA_CONFIG_H

```cpp
#define FA_CONFIG_H
```

**What it does**: the include guard. It makes sure the compiler reads `config.h` only once per source file, even if several headers include it (for example `types.h` and `profile.h` both do).

**How it works**: the first time the file is read, `FA_CONFIG_H` is not defined, so the content is used and the name is defined; the next time the whole content is skipped. Every header in the project has a guard like this (`FA_TYPES_H`, `FA_TIMER_H`, `FA_IMAGE_OPS_H`, ...). It carries no value and must not be changed.

**Used by**: the preprocessor only.

### FA_POSE_PITCH_TOL_DEG

```cpp
#define FA_POSE_PITCH_TOL_DEG 5.0f
```

**What it does**: the largest allowed pitch, in degrees. Pitch is nodding: looking up or down. A face crop is stored for feature extraction only while the absolute values of pitch, yaw and roll are all inside their limits (this is the "pose gate", step 3 of `AuthPipeline::process` in `ARCHITECTURE.md`).

**How it works**: `pose_within_tolerance()` compares the absolute value of `Pose::pitch` with this number (`<=`, so exactly 5.0 is still accepted).

**If you change it**: raising it lets more tilted faces through, so crops are stored sooner and more often, but the faces given to the recognizer are less frontal. Lowering it makes the gate stricter, so a person must hold the head more precisely and it takes longer until a crop is stored. Very small values may never pass because the measured angles contain some noise (`tests/test_pipeline.cpp` allows 3 degrees of detector noise on top of the roll limit when it checks the stored crops).

**Used by**: `src/detect/pose.cpp` (`pose_within_tolerance`); documented in `src/detect/pose.h`.

### FA_POSE_YAW_TOL_DEG

```cpp
#define FA_POSE_YAW_TOL_DEG 5.0f
```

**What it does**: the largest allowed yaw, in degrees. Yaw is turning the head left or right.

**How it works**: same as the pitch limit: `std::fabs(pose.yaw) <= FA_POSE_YAW_TOL_DEG` inside `pose_within_tolerance()`.

**If you change it**: same trade-off as pitch. This is the example used in `build.bat` and `TESTING.md` (`/DFA_POSE_YAW_TOL_DEG=3.0f`).

**Used by**: `src/detect/pose.cpp` (`pose_within_tolerance`).

### FA_POSE_ROLL_TOL_DEG

```cpp
#define FA_POSE_ROLL_TOL_DEG 8.0f
```

**What it does**: the largest allowed roll, in degrees. Roll is tilting the head toward a shoulder. It is looser than the other two. The code does not say why; a likely reason is that a tilted face can be turned upright again by the alignment step (`align_face`), while nodding and turning cannot be undone by a flat image transform.

**How it works**: `std::fabs(pose.roll) <= FA_POSE_ROLL_TOL_DEG` inside `pose_within_tolerance()`.

**If you change it**: same trade-off as pitch.

**Used by**: `src/detect/pose.cpp` (`pose_within_tolerance`) and `tests/test_pipeline.cpp` (checks the stored crops against this limit plus 3 degrees).

### FA_DETECT_INPUT_SIZE

```cpp
#define FA_DETECT_INPUT_SIZE 640
```

**What it does**: the side, in pixels, of the square image given to the main (full-size) network of the deep detectors (YuNet, SCRFD). The camera image is shrunk to fit this square with `letterbox()`. (For SCRFD with `detector.layout = chw` the macro is the length of the **long side** of the rectangle given to the network, which is `letterbox_fit(frame, FA_DETECT_INPUT_SIZE, 32, ...)`: 640x384 for a 1920x1080 picture; the scale is the same as with the square.) A profile may also name a second, faster network for small camera images (`detector.model_small`, with its own square size `detector.small_size`, see the profile section); that network does not use this macro.

**How it works**: `DeepDetector::detect` chooses the square size: `FA_DETECT_INPUT_SIZE` for the main network, or `profile.detector_small_size` when the small network is used (small network loaded, `accurate` not requested, and the camera image's longer side at most `detector_small_max_side`). It calls `letterbox(frame, size, ...)` and passes the same `size` to `decode_yunet` / `decode_scrfd`, which compute the number of prediction cells per row as `size / stride` for the strides 8, 16 and 32. With `detector.layout = chw` it calls `letterbox_fit(frame, size, 32, ...)` instead and passes the width and the height of the result to `decode_scrfd_chw`, which uses `width / stride` columns and `height / stride` rows.

**If you change it**: a larger square finds smaller faces but is slower, a smaller one is faster but may miss small faces. Only 640 is tested here: `tests/test_common.cpp` expects 6400 predictions at stride 8 for YuNet (80 x 80 cells) and 12800 for SCRFD (80 x 80 x 2 anchors). The original SCRFD conversion (`det.param`, used by `models/dev_noncommercial_square.ini`, whose comment says "input fixed to 640x640") needs a 640x640 input, so that model would need to be re-exported for another size; `det_dyn.param` (`models/dev_noncommercial.ini`) accepts any size that is a multiple of 32, but only 640 as the long side has been measured (the top comment of `dev_noncommercial.ini` says so: any input size that is a multiple of 32, a 640x384 rectangle for a 16:9 picture). Whether the YuNet file accepts other sizes has not been checked. Because the code divides by the strides with integer division, the value should be a multiple of 32 (this is a deduction from the code, not stated in a comment). The Haar detector does not use this setting.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`; the decode functions receive the chosen size as a parameter).

### FA_DETECT_SCORE_THRESHOLD

```cpp
#define FA_DETECT_SCORE_THRESHOLD 0.6f
```

**What it does**: the default for the minimum detection score. Detections with a lower score are ignored.

**How it works**: it is only used as the initial value of `ModelProfile::detector_score` (`profile.h`). The detectors read the threshold from the profile, not from this macro. The profile key `detector.score` overrides it; the shipped profiles set 0.6 (`permissive.ini`) and 0.5 (the two `dev_noncommercial` profiles). A detection whose score is exactly equal to the threshold is kept (the code skips `score < threshold`). The Haar detector does not use a score threshold at all.

**If you change it**: raising it gives fewer false detections but may miss real faces; lowering it does the opposite. Editing the macro only affects profiles that do not contain `detector.score`.

**Used by**: `src/common/profile.h` (default value of `detector_score`).

### FA_DETECT_NMS_IOU

```cpp
#define FA_DETECT_NMS_IOU 0.3f
```

**What it does**: two detected boxes that overlap more than this (IoU) are treated as the same face, and only the better-scoring one is kept.

**How it works**: passed as the threshold of `non_max_suppression()`, which drops a candidate when its IoU with an already kept box is strictly greater than the threshold.

**If you change it**: a lower value removes boxes more eagerly, so two people standing close together may be merged into one. A higher value lets more near-duplicate boxes survive (probably leading to extra tracks later).

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`).

### FA_MIN_FACE_PIXELS

```cpp
#define FA_MIN_FACE_PIXELS 40
```

**What it does**: faces smaller than this many pixels in the ORIGINAL camera image are ignored.

**How it works**: the deep detectors drop a face when its box width or its box height is below this value, after the box has been mapped back to camera pixels. The Haar detector also uses it (scaled to its smaller working image) as the smallest window size of a full scan, but only together with the minimum window size of the cascade file, whichever is larger. `config.h` says that the Haar detector cannot see faces below 24 working pixels, which is 96 camera pixels for a 1280 pixel wide image (see `FA_HAAR_WORK_WIDTH`); for a 1280 pixel wide camera image and the default 40, the scaled value is 40 x 320/1280 = 10, below the cascade window of 24, so the cascade window decides (my reading of the numbers, not stated in the code). The working width grows with the camera picture above 1280 pixels (`haar_work_width`, see `detect.md`): at 1920 pixels it is 480, the scale is 480/1920 = 0.25, the scaled value is again 10 and the cascade window again decides, so the smallest face stays 24 x 1920/480 = 96 camera pixels. For a camera image 320 pixels wide or narrower the scale is 1 and the 40 decides.

**If you change it**: raising it ignores small, far-away faces; lowering it finds smaller faces but small faces give less reliable landmarks and more false detections. For the deep detectors this is only a filter after detection, so it does not make them faster. For Haar with a wide camera image, values below the default change little, because the cascade window limits first.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`) and `src/detect/detector_haar.cpp` (`detect`).

### FA_MAX_FACES

```cpp
#define FA_MAX_FACES 32
```

**What it does**: the most faces the detectors return per frame (the best ones; the rest are dropped).

**How it works**: after filtering, the detectors stop adding faces to the result when the list reaches this size. For the deep detectors the boxes are already sorted by score (done by `non_max_suppression`), so the best-scoring faces are the ones kept.

**If you change it**: lowering it limits the work of the later stages (tracking, pose check, extraction) in a crowded scene, but extra people are ignored. Raising it only matters when more than 32 faces are visible.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`) and `src/detect/detector_haar.cpp` (`detect`).

### FA_CAMERA_DEFAULT_WIDTH

```cpp
#define FA_CAMERA_DEFAULT_WIDTH 1920
```

**What it does**: the picture width, in pixels, that `open_camera_source` asks the camera for when the caller gives no size. Together with `FA_CAMERA_DEFAULT_HEIGHT` it makes the default camera size, **1920 x 1080**. The camera need not offer exactly this size: the camera mode that is closest is used (`pick_camera_mode`), and the whole system works with any picture size, for example 640x480, 1280x720, 1920x1080 or 2560x1440 (see "Camera picture sizes" in `camera.md`).

**How it works**: it is the default value of the `width` parameter of `open_camera_source`, and `pick_camera_mode` uses it (with the height) when it is asked for a size of 0 x 0. `tools/camera_check.cpp` uses it as the default size of `--modes` and `--capture`.

**If you change it**: together with the height; the wanted size is only a wish for the camera, so the frames you get can still differ (`camera_check --modes` shows what the camera would give). A larger default costs more time per frame; a smaller one needs people closer to the camera (the 80 pixel minimum face width, `FA_MIN_CROP_FACE_PIXELS`, is fixed in pixels). The test `tests/test_common.cpp` ("camera mode: no wish at all means the default size 1920x1080") expects exactly 1920 and 1080 and fails after a change.

**Used by**: `src/camera/frame_source.h` (default arguments of `open_camera_source`), `src/camera/frame_source.cpp` (`pick_camera_mode`), `tools/camera_check.cpp` (`main`), `tests/test_common.cpp` (`main`).

### FA_CAMERA_DEFAULT_HEIGHT

```cpp
#define FA_CAMERA_DEFAULT_HEIGHT 1080
```

**What it does**: the picture height, in pixels, of the default camera size (see `FA_CAMERA_DEFAULT_WIDTH`).

**How it works**: the default value of the `height` parameter of `open_camera_source`; used by `pick_camera_mode` for a request of 0 x 0.

**If you change it**: same as the width.

**Used by**: the same places as `FA_CAMERA_DEFAULT_WIDTH`.

### FA_CAMERA_MIN_FPS

```cpp
#define FA_CAMERA_MIN_FPS 15
```

**What it does**: camera modes that deliver fewer frames per second than this are used only when the camera has no faster mode. The comment in `config.h` gives the reason: large uncompressed pictures often come at 5 or 10 frames per second through USB, so the faster mode of a smaller size is preferred to the slow mode of the exact size.

**How it works**: rule 2 of `pick_camera_mode`: modes with a frame rate below this value are dropped when at least one other mode remains. A frame rate of 0 (the camera does not report one) is not dropped. If every mode is slow, the closest size is chosen among them.

**If you change it**: raising it avoids more slow modes but may make the camera give a smaller picture than wanted (a 1920x1080 mode at 10 fps loses against a 1280x720 mode at 30 fps); lowering it (even to 0) lets a slow mode with the exact size win.

**Used by**: `src/camera/frame_source.cpp` (`pick_camera_mode`).

### FA_REFINE_FACE_WIDTH

```cpp
#define FA_REFINE_FACE_WIDTH 320
```

**What it does**: in the second look at a face (`refine_face`), the face is enlarged until it is about this many pixels wide before the detector looks for its landmarks again. A bigger face gives more exact landmarks and therefore more exact pitch, yaw and roll.

**How it works**: `refine_face` cuts out a region twice as wide and as high as the face box (half a face of margin on every side) and enlarges it by `zoom = max(1, FA_REFINE_FACE_WIDTH / box width)`. A face that is already wider than this value is not shrunk (zoom stays 1). Note that 320 x 2 = 640, which is `FA_DETECT_INPUT_SIZE`: the enlarged region then fills the detector's square (this is my reading of the numbers, the code does not say it).

**If you change it**: a larger value enlarges more, but a region wider than the detector's square is shrunk again by `letterbox`, so values above 320 probably bring no new detail; a smaller value enlarges less and the landmarks get less exact for small faces.

**Used by**: `src/detect/detector.cpp` (`refine_face`); documented in `src/detect/detector.h`.

### FA_MIN_CROP_FACE_PIXELS

```cpp
#define FA_MIN_CROP_FACE_PIXELS 80
```

**What it does**: a face narrower than this (in the camera image) is never stored for feature extraction, because it has too few pixels for a reliable feature.

**How it works**: in `AuthPipeline::process`, a crop is "due" only when the tracked box width is at least this value (only the width is compared) and the other conditions hold.

**If you change it**: raising it means a person must come closer before being verified; lowering it lets far-away faces be processed, with less reliable features and therefore more wrong or missed matches. The value is in **camera pixels**, so it does not follow the picture size: if the camera shows the same view, a face that is 80 pixels wide in a 1920 pixel wide picture is about 53 pixels wide in a 1280 pixel wide one and 27 in a 640 pixel wide one (calculated, proportional to the width). A smaller camera picture therefore needs people closer to the camera. The deep detectors still find an 80 pixel face at 1920 wide (see `detect.md`, "Detection at different camera picture sizes").

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::process`).

### FA_POSE_CHECK_INTERVAL_MS

```cpp
#define FA_POSE_CHECK_INTERVAL_MS 100
```

**What it does**: a track that is still waiting for an acceptable head pose is re-checked at most once per this many milliseconds. The check is expensive because it runs `refine_face`, which runs the detector again: one extra detector run, or two when the head is tilted by 3 degrees or more (the rotation compensation step inside `refine_face`).

**How it works**: in `AuthPipeline::process` the check runs when a crop is due and `time_ms - state.last_check_ms >= FA_POSE_CHECK_INTERVAL_MS`.

**If you change it**: a smaller value finds the moment of a good pose sooner but costs more CPU per frame; a larger value saves CPU but may let a short good moment pass.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::process`).

### FA_TRACK_MATCH_IOU

```cpp
#define FA_TRACK_MATCH_IOU 0.3f
```

**What it does**: a new detection joins an existing track when the overlap (IoU) between the detection and the track's predicted box is at least this value.

**How it works**: `FaceTracker::update` computes the IoU of every track/detection pair and keeps the pairs with `overlap >= FA_TRACK_MATCH_IOU` as candidates for the greedy matching.

**If you change it**: a higher value is stricter, so fast-moving faces may lose their ID and start a new track (which then has to be verified again). A lower value is more forgiving but two faces close together may swap IDs.

**Used by**: `src/detect/tracker.cpp` (`FaceTracker::update`).

### FA_TRACK_MAX_MISSED_FRAMES

```cpp
#define FA_TRACK_MAX_MISSED_FRAMES 15
```

**What it does**: a track is deleted when it has been missed in MORE than this many frames in a row, so it survives exactly this many missed frames and is removed on the next one (the 16th with the default). The time this means is the number of frames divided by the camera frame rate (15 frames are half a second at 30 frames per second).

**How it works**: `FaceTracker::update` counts `missed_frames` for every track without a detection and removes tracks with `missed_frames > FA_TRACK_MAX_MISSED_FRAMES`.

**If you change it**: a larger value keeps the ID (and the "verified" state) of a person whose detection flickers or who is briefly hidden, but a person who left stays in the list longer. A smaller value forgets people sooner, so someone who reappears is a new track and must be verified again.

**Used by**: `src/detect/tracker.cpp` (`FaceTracker::update`); `tests/test_detect_units.cpp` (runs `FA_TRACK_MAX_MISSED_FRAMES + 2` empty updates and expects no tracks).

### FA_CROP_STORE_INTERVAL_MS

```cpp
#define FA_CROP_STORE_INTERVAL_MS 1000
```

**What it does**: while a track is not verified, its crop is refreshed at most once per this many milliseconds (the older crop is overwritten).

**How it works**: in `AuthPipeline::process`, `crop_due` is true when `time_ms - state.last_crop_ms >= FA_CROP_STORE_INTERVAL_MS`, together with the track being unverified and wide enough. A track that has never stored a crop is due immediately (`last_crop_ms` starts at -1e9).

**If you change it**: a larger value means fewer feature extractions and less CPU but a slower first verification; a smaller value means more extractions and more matching work.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::process`); `tests/test_pipeline.cpp` (checks the time between stored crops).

### FA_REVERIFY_AFTER_MISSED_FRAMES

```cpp
#define FA_REVERIFY_AFTER_MISSED_FRAMES 3
```

**What it does**: a track that is already **verified** and that was not found for this many frames in a row loses its verification at the moment it is found again. The reason (from the comment in `config.h`): the tracker may give a face that appears in the same place after a short gap the same track ID, so it may now be another person standing there. Verification is meant to be a statement about one continuously followed face. A shorter gap, for example one missed detection, keeps the verification.

**How it works**: `AuthPipeline::process` counts, per track, the frames in a row in which the track was not updated (`missed_run`). When the track is found again and `state.verified` is true and `missed_run >= FA_REVERIFY_AFTER_MISSED_FRAMES`, the pipeline sets the track back to "not verified", clears the stored person id, name and last score, and resets the crop timer (`last_crop_ms`) so that a new crop is due at once; the normal gate, extraction and matching then run again for that track. `missed_run` is reset to 0 whenever the track is found.

**If you change it**: a larger value tolerates longer detection gaps without asking for a new verification. A smaller value is stricter: with 1 even a single missed frame drops the verification. Do not set it to 0: the test is `>=`, so it would be true on every frame and a verified track could never stay verified (my reading of the code). Values above `FA_TRACK_MAX_MISSED_FRAMES` (15) have no effect, because the tracker deletes the track (and with it the pipeline state) first.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::process`); `docs/RESULTS.md` describes why it was added, and `test_pipeline` video 4 covers it.

### FA_WORKER_NETWORK_THREADS

```cpp
#define FA_WORKER_NETWORK_THREADS 1
```

**What it does**: the number of CPU threads used by the worker thread's own copies of the networks. The "worker mode" of the authentication pipeline (`AuthPipeline::init(..., async_worker = true)`) moves the slow steps for unverified tracks (the pose check with `refine_face`, the crop and the feature extraction) to a second thread. That thread must not share the main thread's detectors and extractor, because those keep internal buffers and are not thread-safe, so it gets its own pose detector (the detector that runs `refine_face`, see `pose.detector_profile`) and its own extractor. This macro says how many threads those own networks may use. The comment in `config.h` explains the default: 1 leaves the other core(s) to the detection in the main thread; more threads make the pose check and the extraction finish sooner but compete with the detection for the CPU.

**How it works**: `AuthPipeline::init` copies the `ModelProfile`, sets `worker_profile.threads = FA_WORKER_NETWORK_THREADS` and creates the worker's pose detector (`create_pose_detector`, which copies this thread count into the pose profile it loads) and extractor from that copy. The main thread's networks keep using `runtime.threads` from the profile. The range check that `load_profile` applies to `runtime.threads` (1 to 64) is not applied to this macro, so keep it at 1 or more.

**If you change it**: a larger value shortens the time from "good head pose seen" to "feature ready" but slows the detection on a PC with few cores. Worker mode only; it has no effect in the normal synchronous mode.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::init`).

### FA_WORKER_QUEUE_SIZE

```cpp
#define FA_WORKER_QUEUE_SIZE 4
```

**What it does**: the largest number of pose-check jobs that may wait for the worker thread at the same time (see `FA_WORKER_NETWORK_THREADS`). When the queue is full the new job is not accepted, and the track simply tries again in the next frame.

**How it works**: `AuthPipeline::Worker::enqueue` returns `false` when the waiting list already holds this many jobs; the job being run at that moment is not counted, because it has already been taken off the list. `AuthPipeline::process` then sets the track's `last_check_ms` back to -1e9, so the 100 ms `FA_POSE_CHECK_INTERVAL_MS` pause does not delay the retry. A track that has a job queued or running (`job_pending`) is not given a second one.

**If you change it**: a larger value lets more tracks (people) be queued while the worker is busy, but a job that waits long is based on an older frame. A smaller value drops more requests in a crowded scene, which only delays those tracks by a frame at a time. Worker mode only.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`AuthPipeline::Worker::enqueue`).

### FA_MAX_FEATURE_DIM

```cpp
#define FA_MAX_FEATURE_DIM 512
```

**What it does**: the largest feature length (number of values in a `Feature`) that profile loading, the classical extractor and the test-data generator accept (SFace gives 128, ArcFace 512, the classical extractor 256). `config.h` adds that the deep extractors, the matcher and the store do not check this limit themselves.

**How it works**: `load_profile` rejects a profile whose `recognizer.dim` is outside 1..`FA_MAX_FEATURE_DIM` (see the profile section), so the limit now applies to every recognizer type that comes from a profile file. `create_lbp_extractor` also uses `profile.feature_dim` only if it is between 1 and this value, otherwise it falls back to 256 (a profile loaded with `load_profile` cannot reach that fallback; a profile built in code can). `tools/gen_features` refuses a `dim` above it. No fixed-size array in the code is tied to this number.

**If you change it**: raising it allows longer features. **Lowering it below 512 makes the ArcFace profiles (`recognizer.dim = 512`) fail to load**, and makes `gen_features` unable to create 512-value features.

The deep extractor takes its length from the profile (`recognizer.dim`) and only checks that the network returns exactly that many numbers; neither the matcher nor the store checks a maximum. A profile changed in code after `load_profile` is not checked again.

**Used by**: `src/common/profile.cpp` (`load_profile`), `src/feature/extractor_lbp.cpp` (`create_lbp_extractor`), `tools/gen_features.cpp`.

### FA_ALIGNED_FACE_SIZE

```cpp
#define FA_ALIGNED_FACE_SIZE 112
```

**What it does**: the side, in pixels, of the aligned face image given to the recognizers (SFace and ArcFace expect 112 x 112; the classical extractor uses the same size).

**How it works**: `align_face` creates an output of this size, and the extractors reject an aligned image of any other size.

**If you change it**: **do not**, unless you also change the template and the networks. The 5 template points returned by `aligned_template_112()` are fixed numbers inside a 112 x 112 picture and are not scaled with this macro, so another size would give a wrongly placed face.

**Used by**: `src/common/image_ops.cpp` (`align_face`), `src/feature/extractor_deep.cpp` (`extract_aligned`), `src/feature/extractor_lbp.cpp` (`extract_aligned`, `lbp_raw_histogram`).

## types.h

`types.h` holds the small data types that every engine shares. They are all plain structs: no inheritance and no templates, so each can be understood in a few seconds. A struct has fields (named values) with default values, and a few have small helper methods.

| Item | Purpose |
|---|---|
| `Image` | An 8-bit picture (BGR or gray) with its size and the pixel bytes. |
| `Image::empty` | Tells whether the image holds no picture. |
| `Image::create` | Sets the size and fills the image with zeros. |
| `Image::row` (two versions) | Gives a pointer to the first byte of a row. |
| `Rect` | A rectangle: left/top corner plus size. |
| `Rect::right`, `Rect::bottom`, `Rect::area` | Right edge, bottom edge, area. |
| `Point` | A position (x, y). |
| `Pose` | Head rotation in degrees (pitch, yaw, roll). |
| `Face` | One detected face: box, score, 5 landmarks, pose. |
| `Track` | A face followed from frame to frame. |
| `FaceCrop` | The part of the camera image around one face, stored for later. |
| `Feature` | A feature vector (embedding) of a face. |
| `Feature::dim` | Length of the feature vector. |

### Image

```cpp
struct Image {
    int width = 0;
    int height = 0;
    int channels = 3;  // 3 = BGR color, 1 = gray
    int stride = 0;
    std::vector<uint8_t> data;
    ...
};
```

**What it does**: the picture type used by the whole project. The fields are:

| Field | Meaning |
|---|---|
| `width`, `height` | Size in pixels. |
| `channels` | Bytes per pixel: 3 for BGR color, 1 for gray. |
| `stride` | Number of bytes from the start of one row to the start of the next. Normally `width * channels`. |
| `data` | All pixel bytes, row after row (B, G, R, B, G, R, ... for color). |

A default-constructed `Image` has no pixels (it is `empty()`). Copying an `Image` copies every pixel (a 1280 x 1080 color image is about 4 MB), so the code passes images by reference.

**How it works**: the pixel at column `x` of row `y` starts at byte `y * stride + x * channels` of `data`. All code in the project goes through `row(y)` so the stride is always respected; `create()` always makes `stride == width * channels`, with no padding between rows.

**Used by**: practically every file: all image operations, WIC input/output, both network backends, the detectors, the extractors, the camera sources, the test scene generator and the tools.

### Image::empty

```cpp
bool empty() const { return data.empty() || width <= 0 || height <= 0; }
```

**What it does**: returns `true` when the image holds no usable picture: no bytes, or a width or height of zero or less.

**How it works**: a plain check of the three conditions. It is the standard "was this image really loaded?" test.

**Used by**: `resize_bilinear` and `save_image` (this folder), `refine_face` and both detectors (`detector.cpp`, `detector_deep.cpp`, `detector_haar.cpp`), `haar_cascade.cpp`, `make_face_crop` (`extractor.cpp`), `extractor_deep.cpp`, `extractor_lbp.cpp`.

### Image::create

```cpp
void create(int w, int h, int c) {
    width = w;
    height = h;
    channels = c;
    stride = w * c;
    data.assign(static_cast<size_t>(stride) * h, 0);
}
```

**What it does**: makes the image `w` x `h` pixels with `c` channels and sets every byte to 0 (black). Anything that was in the image before is lost.

**How it works**: it stores the new size, sets `stride = w * c`, and calls `data.assign(stride * h, 0)`, which resizes the byte vector and fills it with zeros. If the vector is already large enough it reuses its memory, so re-creating an image of the same size every frame does not allocate again.

**Used by**: `resize_bilinear`, `letterbox`, `to_gray`, `crop`, `warp_affine` (all in `image_ops.cpp`), `load_image` (`image_io.cpp`), `src/camera/frame_source.cpp` (camera frames), `src/testdata/scene.cpp` (generated frames), `tests/test_common.cpp`, `tests/test_detect_units.cpp`.

### Image::row

```cpp
uint8_t* row(int y) { return data.data() + static_cast<size_t>(y) * stride; }
const uint8_t* row(int y) const { return data.data() + static_cast<size_t>(y) * stride; }
```

**What it does**: returns a pointer to the first byte of row `y` (counted from 0 at the top). The first version is for writing pixels, the second one (used on a `const Image`) is for reading only.

**How it works**: start of `data` plus `y * stride` bytes. There is **no range check**: `y` must be between 0 and `height - 1`, and the image must not be empty.

**Used by**: `image_ops.cpp` (almost every function), `inference.cpp` (`OnnxNet::run`), `extractor_lbp.cpp`, `src/camera/frame_source.cpp`, `src/testdata/scene.cpp`, `tests/test_common.cpp`.

### Rect

```cpp
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() {}
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    ...
};
```

**What it does**: a rectangle in pixel coordinates: `x`, `y` are the left and top corner, `w`, `h` the width and height. Used for face boxes, search regions and crop regions. It can hold fractional values and can reach outside the image (functions that need a valid region clip it first).

**Used by**: `Face::box`, the detectors, `iou()` and `non_max_suppression()` in `detector.cpp`, the tracker, `crop`, `draw_rect`, `make_face_crop`, `src/testdata/scene.cpp`.

### Rect::Rect

```cpp
Rect() {}
Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
```

**What it does**: the first version makes an empty box (all four numbers 0, from the default values of the members). The second makes the box `x, y, w, h`, so the code can write `Rect{x, y, w, h}` or `Rect r = {x, y, w, h};`.

**How it works**: plain member initialization. The constructors exist for **Visual Studio 2015**: it does not allow braces with values for a struct that has default member values (`float x = 0`), which later compilers accept. With the constructor the same line of code is valid in every compiler (`docs/VISUAL_STUDIO.md`). As with any braces, a conversion that loses information (for example a non-constant `int` into a `float`) is an error: cast it (`static_cast<float>(...)`).

**Used by**: everything that builds a box, for example `refine_face` (`detector.cpp`), `crop` callers in `auth_pipeline.cpp` (`make_job`), `scene.cpp`, `extractor.cpp`.

### Rect::right

```cpp
float right() const { return x + w; }
```

**What it does**: the x coordinate of the right edge of the box.

**How it works**: `x + w`.

**Used by**: `detector.cpp` (`iou`, `refine_face`), `detector_deep.cpp` (`to_original`), `detector_haar.cpp`, `haar_cascade.cpp`, `extractor.cpp` (`make_face_crop`), `image_ops.cpp` (`draw_rect`), `scene.cpp`.

### Rect::bottom

```cpp
float bottom() const { return y + h; }
```

**What it does**: the y coordinate of the bottom edge of the box (y grows downward).

**How it works**: `y + h`.

**Used by**: the same files as `Rect::right`.

### Rect::area

```cpp
float area() const { return (w > 0 && h > 0) ? w * h : 0.0f; }
```

**What it does**: the area in square pixels. A box with a zero or negative width or height has area 0.

**How it works**: `w * h` when both are positive, otherwise 0.

**Used by**: `iou()` in `detector.cpp`; the tools and the pipeline use it to pick "the largest face" (`auth_pipeline.cpp`, `eval_features.cpp`, `calibrate_pose.cpp`, `calibrate_lbp.cpp`, `tests/test_pipeline.cpp`).

### Point

```cpp
struct Point {
    float x = 0, y = 0;
    Point() {}
    Point(float x_, float y_) : x(x_), y(y_) {}
};
```

**What it does**: a position in pixel coordinates. Used for the 5 face landmarks and for the points of the similarity transform. `Point()` makes (0, 0); `Point(x, y)` makes the point `x, y`, so `Point{x, y}` works. The constructors are there for Visual Studio 2015, for the same reason as `Rect::Rect`.

**Used by**: `Face::landmarks`, `estimate_similarity`, `draw_dot`, `aligned_template_112`, `pose.cpp`, `detector_haar.cpp`.

### Pose

```cpp
struct Pose {
    float pitch = 0, yaw = 0, roll = 0;
    bool valid = false;  // false when the detector gave no landmarks
};
```

**What it does**: the head rotation in degrees. `pitch` is nodding (looking up or down), `yaw` is turning left or right, `roll` is tilting toward a shoulder. `valid` is `false` when no angles could be computed. The sign conventions are written in `src/detect/pose.h` (for example roll above 0 means the head is tilted clockwise as seen on the screen).

**How it works**: filled by `estimate_pose()` (in `pose.cpp`) from the 5 landmarks of a face. The comment says it is invalid when the detector gave no landmarks; `pose.h` adds that it is also invalid when the eyes coincide.

**Used by**: `Face::pose`, `src/detect/pose.cpp` / `pose.h`, `detector_deep.cpp`, `detector.cpp` (`refine_face`), `auth_pipeline.h` (`FaceStatus`, `TrackState`).

### Face

```cpp
struct Face {
    Rect box;
    float score = 0;
    Point landmarks[5];
    bool has_landmarks = false;
    Pose pose;
};
```

**What it does**: one detected face.

| Field | Meaning |
|---|---|
| `box` | The face rectangle, in the coordinates of the image that was given to the detector. |
| `score` | Detector confidence. For the deep detectors it is the network score; the Haar detector derives it from how many windows agreed. |
| `landmarks[0..4]` | Right eye, left eye, nose tip, right mouth corner, left mouth corner. "Right eye" means the eye on the right side of the person, which appears on the left side of the image (the order used by YuNet and SCRFD). |
| `has_landmarks` | `true` when `landmarks` hold real values. |
| `pose` | Head rotation computed from the landmarks. |

**How it works**: filled by the detectors; the box and landmarks are always in the pixel coordinates of the picture that was analysed. After `make_face_crop` the copy stored in a `FaceCrop` is shifted into the crop's own coordinates.

**Used by**: `detector.h` (`FaceDetector::detect`), the tracker, `pose.h`, `extractor.h` (`make_face_crop`), `align_face` in `image_ops.cpp`, the pipeline and the tools.

### Track

```cpp
struct Track {
    int id = 0;
    Face face;
    int missed_frames = 0;
    int age_frames = 0;
    bool updated = false;
    float vx = 0, vy = 0;
};
```

**What it does**: a face that is followed from frame to frame, so that "the same person" keeps the same number.

| Field | Meaning |
|---|---|
| `id` | The track number. The tracker hands out 1, 2, 3, ... |
| `face` | The most recent detection of this face. |
| `missed_frames` | Number of frames in a row without a matching detection (0 when seen). |
| `age_frames` | Number of frames since the track was created (the tracker counts it on every `update`, seen or not; a new track starts at 1). |
| `updated` | `true` if the latest frame found this face. |
| `vx`, `vy` | Smoothed movement of the box centre, in pixels per frame. Used to predict where the box will be in the next frame. |

**How it works**: created and updated only by `FaceTracker::update` (`src/detect/tracker.cpp`); the speed is half the old speed plus half the newest measured speed.

**Used by**: `src/detect/tracker.h` / `tracker.cpp`, `src/pipeline/auth_pipeline.cpp`.

### FaceCrop

```cpp
struct FaceCrop {
    Image image;
    Face face;                 // box covers the face inside the crop; landmarks are crop-relative
    int track_id = 0;
    double time_ms = 0;        // when the crop was taken
};
```

**What it does**: a stored face segment: the part of the camera image around one face. The box and landmarks inside `face` are given in the coordinates of `image` (the crop), not of the camera image, so the crop can be processed on its own later (aligned and turned into a feature without the original frame).

| Field | Meaning |
|---|---|
| `image` | The cut-out pixels. |
| `face` | The face inside the crop, with crop-relative coordinates. |
| `track_id` | The track this crop belongs to. |
| `time_ms` | When the crop was taken, in milliseconds. |

**How it works**: `make_face_crop` (`extractor.cpp`) fills `image` (the face box plus 30 percent margin on every side, kept inside the frame) and `face`. `track_id` and `time_ms` are set afterwards by `AuthPipeline::process`.

**Used by**: `src/feature/extractor.h` (`FeatureExtractor::extract`, `make_face_crop`), `extractor_deep.cpp`, `extractor_lbp.cpp`, `src/pipeline/auth_pipeline.h` / `.cpp`, `tools/calibrate_lbp.cpp`, `tools/bench_extract_part.h`, `tests/test_feature.cpp`.

### Feature

```cpp
struct Feature {
    std::vector<float> values;
    ...
};
```

**What it does**: a feature vector (also called an embedding): a list of numbers that describes a face so that two photos of the same person give similar lists. `values` is meant to be normalized to length 1, so the cosine similarity of two features is just their dot product (multiply matching numbers and add them up).

**How it works**: the struct itself does not enforce the length-1 rule; the extractors do it by calling `normalize_feature` (`extractor.cpp`) before returning a feature.

**Used by**: `src/feature/` (all extractors), `src/match/matcher.h`, `src/store/feature_store.h`, `src/testdata/synthetic.h`, the pipeline and the tools.

### Feature::dim

```cpp
int dim() const { return static_cast<int>(values.size()); }
```

**What it does**: the length of the feature (128 for SFace, 512 for ArcFace, 256 for the classical extractor).

**How it works**: the size of `values`, converted to `int`.

**Used by**: `matcher.cpp` (wrong-length queries are skipped), `feature_store.cpp` (all stored features must have the same length), `auth_pipeline.cpp`, `synthetic.cpp`, `tools/eval_features.cpp`, `tests/test_feature.cpp`.

## timer.h

`timer.h` is a tiny stopwatch used by the benchmarks, the tools and the tests (and by the pipeline to fill the `*_ms` fields of `FrameResult`). It is header-only (all code is inline).

| Function | Purpose |
|---|---|
| `now_ms` | Current time in milliseconds, from a clock that never goes backwards. |
| `TimingStats` | Collects timing samples. |
| `TimingStats::add` | Stores one sample. |
| `TimingStats::count` | Number of samples. |
| `TimingStats::median` | The middle sample (50th percentile). |
| `TimingStats::p95` | The 95th percentile. |
| `TimingStats::max` | The largest sample. |
| `TimingStats::percentile` | Any percentile. |

### now_ms

```cpp
inline double now_ms()
```

**What it does**: returns the current time in milliseconds as a `double`. The clock is monotonic: it never goes backwards, even if somebody changes the Windows clock. The starting point of the clock is not meaningful, so only **differences** of two calls are useful (for example `now_ms() - start`).

**How it works**: it reads `std::chrono::steady_clock::now()` and converts the time since the clock's epoch to milliseconds.

**Used by**: `src/pipeline/auth_pipeline.cpp` (`process`), `tools/` (`bench_detect_part.h`, `bench_extract_part.h` and `bench_match_part.h`, which the programs `bench`, `bench_detect`, `bench_extract` and `bench_match` share; `eval_detect`, `eval_features`, `detect_image`, `camera_check`, `check_video`, `gen_features`), `tests/` (`test_common`, `test_feature`, `test_match`, `test_pipeline`).

### TimingStats

```cpp
class TimingStats
```

**What it does**: a list of timing measurements (in milliseconds) that can report the median, the 95th percentile and the maximum. The median ignores one-off slow runs (for example the first call that loads things into the CPU cache), so it describes typical speed better than the average.

**How it works**: it only stores the samples in a `std::vector<double>`; every report is computed on demand from that list.

**Used by**: `tools/bench_common.h` (`row`), `tools/bench_detect_part.h`, `tools/bench_extract_part.h`, `tools/bench_match_part.h`, `tools/eval_detect.cpp`, `tools/eval_features.cpp`, `tools/detect_image.cpp`, `tools/check_video.cpp`, `tests/test_common.cpp`, `tests/test_feature.cpp`, `tests/test_match.cpp`, `tests/test_pipeline.cpp`.

### TimingStats::add

```cpp
void add(double ms) { samples_.push_back(ms); }
```

**What it does**: stores one measurement.

**How it works**: appends to the list.

**Used by**: all users of `TimingStats`, always as `stats.add(now_ms() - start)`.

### TimingStats::count

```cpp
size_t count() const { return samples_.size(); }
```

**What it does**: the number of measurements stored so far.

**How it works**: the size of the list.

**Used by**: no caller was found in `src/`, `tools/` or `tests/` (the `.count()` calls found there belong to `FeatureStore`).

### TimingStats::median

```cpp
double median() const { return percentile(50); }
```

**What it does**: the middle measurement: half of the runs were faster, half slower. Returns 0 when there are no samples.

**How it works**: calls `percentile(50)`. For an even number of samples it returns the upper of the two middle values, not their average (see `percentile`).

**Used by**: `bench_common.h` (`row`), `bench_extract_part.h` (the "ideal limit" line), `eval_detect.cpp`, `eval_features.cpp`, `detect_image.cpp`, `check_video.cpp`, `test_common.cpp`, `test_feature.cpp`, `test_match.cpp`, `test_pipeline.cpp`.

### TimingStats::p95

```cpp
double p95() const { return percentile(95); }
```

**What it does**: the 95th percentile: 95 percent of the runs were as fast as this or faster. It shows how slow the slower runs are. Returns 0 when there are no samples.

**How it works**: calls `percentile(95)`.

**Used by**: `bench_common.h` (`row`), `eval_detect.cpp`, `eval_features.cpp`, `detect_image.cpp`, `check_video.cpp`, `test_pipeline.cpp`.

### TimingStats::max

```cpp
double max() const { return samples_.empty() ? 0 : *std::max_element(samples_.begin(), samples_.end()); }
```

**What it does**: the slowest measurement, or 0 when there are no samples.

**How it works**: scans the list once with `std::max_element`.

**Used by**: `bench_common.h` (`row`), `detect_image.cpp`, `check_video.cpp`, `test_pipeline.cpp`.

### TimingStats::percentile

```cpp
double percentile(double p) const {
    if (samples_.empty()) return 0;
    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    size_t index = static_cast<size_t>((p / 100.0) * (sorted.size() - 1) + 0.5);
    return sorted[index];
}
```

**What it does**: returns the value below which `p` percent of the samples lie. `p` must be between 0 and 100 (there is no range check; a larger value would read outside the list). Returns 0 for an empty list.

**How it works**:
1. Copy the samples and sort the copy from smallest to largest (the stored list is not changed).
2. Compute the position `p / 100 * (n - 1)` and round it to the nearest whole number.
3. Return the sample at that position. No averaging between neighbours is done. Example: for 4 samples and `p = 50` the position is 1.5 + 0.5 = 2, so the third smallest sample is returned.

Because it sorts on every call, call it a few times at the end, not inside a measuring loop.

**Used by**: `median()` and `p95()` only (no direct outside caller).

## image_ops.h / image_ops.cpp

Basic image processing written by hand, with no OpenCV: resizing, padding to a square or to a rectangle, gray conversion, cutting out a region, the face-alignment maths, a few drawing helpers for debug pictures, and the integral image for the Haar detector. All functions work on `fa::Image` (8-bit, interleaved, BGR or gray). Bilinear interpolation means that a value between pixels is computed as a weighted average of the four nearest pixels.

| Function | Purpose |
|---|---|
| `resize_bilinear` | Resize an image to a new size (bilinear, fixed-point, fast). |
| `letterbox` | Fit an image into a square without changing its shape, pad with gray. |
| `letterbox_fit` | The same, but into the smallest rectangle whose sides are multiples of 32 (640x384 for a 1920x1080 picture), not a square. |
| `to_gray` | BGR to gray. |
| `crop` | Copy a rectangular part of an image; outside pixels become 0. |
| `estimate_similarity` | Find the rotation + scale + shift that maps 5 points onto 5 other points. |
| `invert_similarity` | Reverse such a transform. |
| `warp_affine` | Build a new image by reading the source at transformed positions. |
| `align_face` | Cut out and straighten a face to the standard 112 x 112 layout. |
| `aligned_template_112` | The standard 5 landmark positions inside a 112 x 112 face. |
| `put_pixel` (internal) | Set one pixel with a bounds check. |
| `draw_rect` | Draw a box outline (debug pictures). |
| `draw_dot` | Draw a filled dot (debug pictures). |
| `integral_image` | Running sums for adding up any rectangle in constant time. |

### resize_bilinear

```cpp
void resize_bilinear(const Image& src, Image& dst, int new_width, int new_height);
```

**What it does**: makes `dst` a `new_width` x `new_height` copy of `src`, scaled up or down. It works for any channel count in practice (the header promises 1 or 3). If `src` is empty, `dst` is left as a black image of the requested size; if a new size is 0, `dst` ends up empty; in both cases nothing else happens. Negative sizes are not supported.

**How it works**: for every output pixel the four nearest input pixels and their weights are needed. To make this fast:
1. The horizontal part is computed once per output **column** (the two input columns `x0`, `x1` and one weight) and stored in tables; the vertical part is computed once per output **row**.
2. The position uses the "pixel centre" rule `(x + 0.5) * ratio - 0.5`, so the picture does not drift by half a pixel.
3. Weights are whole numbers 0 to 255 instead of floats: 8 fractional bits. The two interpolation steps give a result with 16 fractional bits, and `(v + 32768) >> 16` rounds back to a byte.
4. The two pixels at the right/bottom edge are clamped to the last column/row, so nothing outside the image is read.

**Used by**: `letterbox` (this file), `refine_face` in `detector.cpp` (enlarging the face), `detector_haar.cpp` (shrinking the gray image), `tests/test_common.cpp`.

### letterbox

```cpp
void letterbox(const Image& src, int size, Image& dst, float& scale, int& pad_x, int& pad_y);
```

**What it does**: shrinks or enlarges `src` so that it fits inside a `size` x `size` square **without changing its aspect ratio**, and fills the rest with gray (value 114). This is how a 1280 x 1080 camera image becomes the 640 x 640 image the deep detectors need. Three results are returned so that detections can be mapped back: `scale` (the factor applied to the original), `pad_x` and `pad_y` (width of the gray bars at the left and at the top). To map a point back to the camera image: `original_x = (x - pad_x) / scale`, and the same with y.

**How it works**:
1. `scale = min(size / width, size / height)`, so the longer side just fits.
2. The new size is the original size times `scale`, rounded, at least 1. The image is resized with `resize_bilinear`.
3. `dst` is created as a `size` x `size` square and filled with 114.
4. The resized picture is copied into the middle: `pad_x = (size - new_w) / 2`, `pad_y = (size - new_h) / 2` (integer division, so an odd gap puts the extra pixel on the right/bottom side).

Example (checked in `tests/test_common.cpp`): 1280 x 1080 into 640 gives `scale = 0.5`, a 640 x 540 picture, `pad_x = 0`, `pad_y = 50`.

The function does not check for an empty `src` (the width would be a division by zero); the caller `DeepDetector::detect` checks `frame.empty()` first.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`, for YuNet and for SCRFD with `detector.layout = flat`; then `to_original` undoes it), `tests/test_common.cpp`.

### letterbox_fit

```cpp
void letterbox_fit(const Image& src, int long_side, int multiple, Image& dst, float& scale, int& pad_x, int& pad_y);
```

**What it does**: the same job as `letterbox`, but the result is not a square: it is the **smallest rectangle that holds the shrunk picture and has both sides a multiple of `multiple`** (32 for the face networks), and the rest of that rectangle is gray (114). `src` is shrunk or enlarged so that its **longer side** becomes `long_side` pixels, keeping the aspect ratio. `scale`, `pad_x` and `pad_y` mean exactly what they mean for `letterbox`, so `original_x = (x - pad_x) / scale` maps a point back. **Why it exists:** a 1920 x 1080 picture shrunk to a long side of 640 is only 640 x 360, so the square of `letterbox` is 44% gray padding, and a network that accepts any size (SCRFD with `det_dyn.param`, `detector.layout = chw`) does the work for those gray pixels for nothing. With `letterbox_fit` the picture is 640 x 384: 40% fewer pixels at exactly the same `scale`, so a face is the same size in the network input and the result does not change.

**How it works**:
1. `scale = long_side / max(width, height)` (a float; a picture smaller than `long_side` is enlarged, as with `letterbox`).
2. The new size is the original size times `scale`, rounded, at least 1; the image is resized with `resize_bilinear`.
3. The result size is each new side rounded **up** to the next multiple of `m = max(1, multiple)`: `out_w = (new_w + m - 1) / m * m`, the same for the height. A `multiple` of 0 or less therefore means 1 (no rounding).
4. `dst` is created with that size and filled with 114; the resized picture is copied into the middle: `pad_x = (out_w - new_w) / 2`, `pad_y = (out_h - new_h) / 2` (integer division, the extra pixel of an odd gap is on the right / bottom side).

Examples (calculated from the code with `long_side = 640`, `multiple = 32`; the first is the one that matters in practice): 1920 x 1080 gives `scale` 1/3, a 640 x 360 picture and a **640 x 384** result with `pad_x = 0`, `pad_y = 12`; 1280 x 720 and 2560 x 1440 give the same 640 x 384; 1280 x 1080 gives `scale = 0.5`, 640 x 540 and a **640 x 544** result (`pad_y = 2`, 15% fewer pixels than the square); a 4:3 picture such as 640 x 480 or 1600 x 1200 gives 640 x 480; a **square picture stays 640 x 640** (no gain). If `long_side` is itself not a multiple of `multiple` the result is larger than `long_side` on that side (for example 100 and 32 give 128).

The function does not check for an empty `src` (division by zero, like `letterbox`); `DeepDetector::detect` checks `frame.empty()` first. **Tested** by two checks in `tests/test_common.cpp` (`docs/tools.md`, group 3): the result size for seven picture sizes with a long side of 640 and a multiple of 32 (1920x1080 gives 640x384, 1280x1080 gives 640x544, 1280x720 gives 640x384, 640x480 gives 640x480, 640x640 gives 640x640, the portrait 480x640 gives 480x640, 2560x1440 gives 640x384), and a 20x20 marker in a 1920x1080 picture that lands where `scale` (1/3) and the padding (`pad_x` 0, `pad_y` 12) say, with the padding gray (114); and through `DeepDetector::detect` (every test that uses `dev_noncommercial.ini`, in particular the comparison of the fitted rectangle with the square in `tests/test_pipeline.cpp`). A `multiple` other than 32, a `long_side` that is not a multiple of it, and an empty picture are not tested.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector::detect`, when the profile is SCRFD with `detector.layout = chw`; `to_original` undoes it), `tests/test_common.cpp` (two checks).

### to_gray

```cpp
void to_gray(const Image& src, Image& dst);
```

**What it does**: converts a BGR image to a one-channel gray image of the same size. A gray input is simply copied.

**How it works**: for each pixel `gray = (29 * B + 150 * G + 77 * R) >> 8`. The three weights add up to 256 and approximate the usual 0.114, 0.587 and 0.299, so a white pixel stays 255. Whole-number maths is used for speed. Any input that is not 1-channel is read as 3-channel BGR (4-channel images are not supported).

**Used by**: `src/detect/detector_haar.cpp` (`detect`), `src/feature/extractor_lbp.cpp` (`extract_aligned`, `lbp_raw_histogram`).

### crop

```cpp
void crop(const Image& src, const Rect& box, Image& dst);
```

**What it does**: copies the part of `src` inside `box` into `dst`. Parts of the box that lie outside `src` become 0 (black), so the result always has the size of the box, even when the box sticks out of the picture.

**How it works**: the left/top corner is rounded down (`floor`) and the size is rounded up (`ceil`, at least 1 pixel); `dst` is created and zero-filled. For each row that lies inside `src`, the range of columns that overlaps `src` is computed and copied in one `memcpy`. Example: a box `(10.5, 20.2, 30.1, 40)` gives a 31 x 40 result starting at pixel (10, 20). Callers that need exact control (`refine_face`, `make_face_crop`) round the region themselves before calling.

**Used by**: `src/detect/detector.cpp` (`refine_face`), `src/detect/detector_haar.cpp` (`detect`, `find_eyes_and_pose`), `src/feature/extractor.cpp` (`make_face_crop`), `src/testdata/scene.cpp`.

### estimate_similarity

```cpp
void estimate_similarity(const Point from[5], const Point to[5], float m[6]);
```

**What it does**: finds the **similarity transform** (a rotation, one uniform scale factor and a shift; no mirroring and no stretching) that moves the 5 points `from` as close as possible to the 5 points `to`, in the least-squares sense (smallest sum of squared distances). Point `i` of `from` is paired with point `i` of `to`. The answer is written to `m = [a, -b, tx, b, a, ty]`, meaning:

```
x' = a*x - b*y + tx
y' = b*x + a*y + ty
```

The scale is `sqrt(a*a + b*b)` and the rotation angle is `atan2(b, a)`. Example from `tests/test_common.cpp`: points rotated by 30 degrees and scaled by 2 give `a = 2 cos 30` and `b = 2 sin 30`.

**How it works** (the comment names the Umeyama method for 2-D points):
1. Compute the mean (centre) of each point set and subtract it from the points.
2. Sum three numbers over the 5 pairs: the dot products, the cross products and the squared lengths of the centred `from` points.
3. `a = dot / norm` and `b = cross / norm`. If `norm` is almost 0 (all points equal) it is set to 1e-6 to avoid dividing by zero.
4. The shift is chosen so that the centre of `from` lands on the centre of `to`.

**Used by**: `align_face` (this file), `tests/test_common.cpp`.

### invert_similarity

```cpp
void invert_similarity(const float m[6], float inverse[6]);
```

**What it does**: computes the reverse of a similarity matrix in the same 6-number format: if `m` maps (x, y) to (x', y'), then `inverse` maps (x', y') back to (x, y).

**How it works**: the 2 x 2 part `[a -b; b a]` has the inverse `[a b; -b a] / (a*a + b*b)`; the shift is then `-(inverse matrix * old shift)`. If `a*a + b*b` is almost 0 it is set to 1e-12 to avoid dividing by zero. (In the `.cpp` the output parameter is named `inv`.)

**Used by**: `align_face` (this file), `tests/test_common.cpp` (checks that a point goes forward and back to the start).

### warp_affine

```cpp
void warp_affine(const Image& src, const float dst_to_src[6], Image& dst, int width, int height);
```

**What it does**: produces a `width` x `height` image. For every output pixel (x, y) the matrix `dst_to_src` (6 numbers, same layout as in `estimate_similarity`) says **where in `src` to read**, and the value there is taken with bilinear interpolation. Output pixels whose source position falls outside the picture stay 0 (black). Note the direction: the matrix goes from output to source, not from source to output (the reason `align_face` inverts its transform).

**How it works**:
1. `dst` is created and zero-filled.
2. For each output pixel compute `sx = t0*x + t1*y + t2` and `sy = t3*x + t4*y + t5`, and the whole-number pixel `(floor(sx), floor(sy))` to the upper left.
3. If that pixel is outside `0 .. width-2` / `0 .. height-2` of the source, skip it (the output pixel stays 0). The last row and column of the source are only used as the "next" neighbour, so a border about one pixel wide is lost.
4. Otherwise blend the four neighbouring pixels using the fractional parts of `sx` and `sy`, with float maths and `+ 0.5` rounding.

**Used by**: `align_face` (this file) and `refine_face` in `src/detect/detector.cpp` (turning the enlarged face upright before detecting again).

### align_face

```cpp
void align_face(const Image& src, const Face& face, Image& aligned);
```

**What it does**: cuts out the face and rotates, scales and shifts it so that its 5 landmarks land on the standard positions of a 112 x 112 aligned face (the ArcFace template, `aligned_template_112()`). The result, `aligned`, has the size `FA_ALIGNED_FACE_SIZE` x `FA_ALIGNED_FACE_SIZE` and is what the recognizers expect: every face is then upright, the same size, with eyes, nose and mouth in the same places. `face.landmarks` must be real landmarks (`has_landmarks == true`) in the same coordinates as `src`; the function does not check this, the callers do.

**How it works**:
1. Copy the 5 landmarks of the face.
2. `estimate_similarity(landmarks, template)` gives the transform from the face to the aligned picture.
3. `invert_similarity` reverses it, because `warp_affine` needs the "output to source" direction.
4. `warp_affine` produces the aligned picture. Parts that fall outside the source stay black.

**Used by**: `src/feature/extractor_deep.cpp` (`extract`), `src/feature/extractor_lbp.cpp` (`extract`), `tools/calibrate_lbp.cpp`.

### aligned_template_112

```cpp
const Point* aligned_template_112();
```

**What it does**: returns a pointer to 5 `Point`s: the standard landmark positions inside a 112 x 112 aligned face, in the usual order (right eye, left eye, nose tip, right mouth corner, left mouth corner; "right" is the person's right, the image's left):

| Landmark | x | y |
|---|---|---|
| right eye | 38.2946 | 51.6963 |
| left eye | 73.5318 | 51.5014 |
| nose tip | 56.0252 | 71.7366 |
| right mouth corner | 41.5493 | 92.3655 |
| left mouth corner | 70.7299 | 92.2041 |

**How it works**: the numbers are stored in a function-local `static const` array, so they exist once for the whole program run and the pointer stays valid. The caller must not write through it.

**Used by**: `align_face` (this file) and `src/detect/detector_haar.cpp` (`find_eyes_and_pose` uses the template's proportions to place nose and mouth from two detected eyes).

### put_pixel (internal)

```cpp
static void put_pixel(Image& image, int x, int y, uint8_t b, uint8_t g, uint8_t r)
```

**What it does**: sets one pixel to the color (b, g, r). It silently does nothing when (x, y) is outside the picture or when the image is not a 3-channel color image (so drawing on a gray image has no effect).

**How it works**: a bounds and channel check, then three byte writes. `static` means it is visible only inside `image_ops.cpp`.

**Used by**: `draw_rect`, `draw_dot`.

### draw_rect

```cpp
void draw_rect(Image& image, const Rect& box, uint8_t b, uint8_t g, uint8_t r, int thickness = 2);
```

**What it does**: draws the outline of `box` on a color image, in color (b, g, r), `thickness` pixels wide (default 2). Used to make debug pictures for humans (for example faces marked in green). Parts outside the picture are skipped.

**How it works**: the corners are truncated to whole pixels. For each of the `thickness` rounds `t`, four lines are drawn pixel by pixel with `put_pixel`: the top line `t` pixels below the top edge, the bottom line `t` above the bottom edge, and the left and right lines `t` inside the sides. So a thick outline grows **inward**.

**Used by**: `tools/detect_image.cpp` (`--draw` option).

### draw_dot

```cpp
void draw_dot(Image& image, Point center, int radius, uint8_t b, uint8_t g, uint8_t r);
```

**What it does**: draws a filled circle of the given radius (pixels) around `center`, for example to mark a landmark. Radius 0 draws one pixel.

**How it works**: the centre is truncated to whole pixels; every pixel in the surrounding square whose distance fulfils `dx*dx + dy*dy <= radius*radius` is set with `put_pixel`.

**Used by**: `tools/detect_image.cpp` (landmarks drawn in red/blue).

### integral_image

```cpp
void integral_image(const Image& gray, std::vector<int64_t>& sum, std::vector<int64_t>& sqsum);
```

**What it does**: builds two lookup tables from a gray image so that the sum of the pixel values inside **any** rectangle can be found with four table reads, no matter how big the rectangle is. Both tables have `(width + 1) * (height + 1)` entries (a first row and first column of zeros), stored row by row: `sum` holds the running sums of the pixel values and `sqsum` the running sums of the squared values (the Haar detector needs the squares to judge the contrast of a window).

Entry `S[y][x]` is the sum of all pixels above and to the left of position (x, y), not including row `y` and column `x`. Example, for a 2 x 2 image with rows `1 2` and `3 4`, `sum` is the 3 x 3 table

```
0  0  0
0  1  3
0  4 10
```

and the sum of any rectangle with corner positions (x0, y0) and (x1, y1) is `S[y1][x1] - S[y0][x1] - S[y1][x0] + S[y0][x0]`.

**How it works**: both vectors are resized and zeroed. Then, row by row, a running total of the row so far is kept, and each entry is that running total plus the entry directly above it (`s[x+1] = s_prev[x+1] + row_sum`). Every pixel is therefore visited once. The 64-bit integers cannot overflow for any realistic image. `gray` must be a 1-channel image (the Haar code checks this before calling).

**Used by**: `src/detect/haar_cascade.cpp` (`HaarCascade::detect`).

## image_io.h / image_io.cpp

Reads and writes picture files (JPEG, PNG, BMP and whatever else the installed WIC decoders support) with the **Windows Imaging Component (WIC)**, which is part of Windows, so no extra library is needed. WIC is a COM library. COM ("Component Object Model") is the older Windows technology in which libraries hand out objects that must be started once per thread and released when finished. The two `#pragma comment(lib, ...)` lines at the top of `image_io.cpp` tell the Microsoft linker to link `windowscodecs.lib` and `ole32.lib` automatically.

| Function | Purpose |
|---|---|
| `load_image` | Read a picture file into a BGR `Image`. |
| `save_image` | Write an `Image` as PNG or JPEG. |
| `ComPtr` (internal) | Releases a COM object automatically. |
| `widen` (internal) | UTF-8 text to Windows wide text. |
| `ensure_com` (internal) | Starts COM once per thread. |
| `ends_with_ci` (internal) | Case-insensitive "file name ends with". |

### load_image

```cpp
bool load_image(const std::string& path, Image& out);
```

**What it does**: reads the picture file `path` (UTF-8 text) into `out` as a 3-channel BGR image, whatever the file's own format (gray, palette, with transparency ...). Returns `true` on success and `false` on any failure (file not found, not a picture, no WIC factory ...). Only the first frame of a file is read. The code does not read or apply an EXIF "rotate" flag, so a photo is returned as it is stored in the file. On failure `out` may be untouched; on a failure inside the last step it may hold a black image of the right size.

**How it works**:
1. `ensure_com()` starts COM for this thread.
2. Create the WIC factory (`CoCreateInstance`).
3. Create a decoder for the file (`CreateDecoderFromFilename`, path converted by `widen`) and take frame 0.
4. Create a format converter and initialise it to 24-bit BGR (`GUID_WICPixelFormat24bppBGR`, no dithering). This byte order is exactly the layout of `fa::Image`, so the pixels can be copied directly.
5. Read the size, `out.create(w, h, 3)`, and `CopyPixels` straight into `out.data` using `out.stride`.

Every `FAILED(...)` check returns `false` immediately; the `ComPtr` objects release everything on the way out.

**Used by**: `src/camera/frame_source.cpp` (recorded-frame source), `tools/` (`roll_probe`, `gen_video`, `eval_features`, `eval_detect`, `detect_image`, `calibrate_pose`, `calibrate_lbp`, and `bench_frames.h`, which `bench`, `bench_detect` and `bench_extract` use), `tests/test_pipeline.cpp`, `tests/test_common.cpp`, `tests/test_feature.cpp`.

### save_image

```cpp
bool save_image(const std::string& path, const Image& image);
```

**What it does**: writes `image` to the file `path` (UTF-8 text). A name ending in `.jpg` or `.jpeg` (any letter case) is written as JPEG with WIC's default quality; **every other name is written as PNG**, even `.bmp`, so a file called `x.bmp` would contain PNG data. A 1-channel image is stored as 8-bit gray, anything else as 24-bit BGR. Returns `false` for an empty image or any WIC failure. The folder must already exist; the function does not create folders and an existing file is overwritten.

**How it works**:
1. Return `false` if `image.empty()`; start COM with `ensure_com()`.
2. Create the WIC factory and a file stream (`InitializeFromFilename`, write access).
3. Choose the container format (JPEG or PNG) from the file name with `ends_with_ci`; create and initialise the encoder on the stream.
4. Create a frame, set its size and pixel format, and `WritePixels` from `image.data` with `image.stride`.
5. Commit the frame, then the encoder (the file is complete only after the final commit).

`tests/test_common.cpp` checks that a PNG written and read back has exactly the same pixels.

**Used by**: `tools/gen_video.cpp`, `tools/detect_image.cpp`, `tools/camera_check.cpp`, `tests/test_pipeline.cpp`, `tests/test_common.cpp`.

### ComPtr (internal)

```cpp
template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};
```

**What it does**: a minimal "smart pointer" for COM objects: a small wrapper that calls `Release()` on the object when the variable goes out of scope. Without it every early `return false` in `load_image` / `save_image` would need to release each object by hand. `operator&` returns the address of the inner pointer `p`, so the variable can be passed directly to COM functions that hand back a new object (for example `IID_PPV_ARGS(&factory)`). `operator->` lets you call methods as `factory->CreateStream(...)`. The plain pointer is available as `.p` (used where an API wants the raw pointer, e.g. `converter->Initialize(frame.p, ...)`). `ComPtr` is a template: `T` is replaced by the COM interface type, such as `IWICImagingFactory`.

**How it works**: the destructor releases `p` if it is not null. The struct has no protection against being copied (a copy would release the same object twice), so it must only be used as a local variable that is never copied, which is how it is used.

**Used by**: `load_image` and `save_image` (this file only; it is in an anonymous namespace).

### widen (internal, image_io.cpp)

```cpp
std::wstring widen(const std::string& s)
```

**What it does**: converts a UTF-8 `std::string` to the UTF-16 `std::wstring` that Windows file functions need, so file names with non-English characters work. An empty string gives an empty wide string.

**How it works**: calls `MultiByteToWideChar` twice: the first call (output size 0) returns the number of wide characters needed including the terminating zero, the second does the conversion into a string of that size, and the final `resize(n - 1)` removes the terminating zero.

**Used by**: `load_image`, `save_image`. A second function with the same name and purpose exists in `inference.cpp` (see below); the two copies are independent.

### ensure_com (internal)

```cpp
void ensure_com()
```

**What it does**: makes sure COM is started on the current thread before any WIC call. COM must be started once per thread.

**How it works**: a `static thread_local bool done` remembers per thread whether it already ran; if not, it calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`. The return value is not checked, and `CoUninitialize` is never called (COM stays started until the thread ends). `src/camera/frame_source.cpp` starts COM with the same `COINIT_MULTITHREADED` mode.

**Used by**: `load_image`, `save_image`.

### ends_with_ci (internal)

```cpp
bool ends_with_ci(const std::string& s, const char* suffix)
```

**What it does**: returns `true` if the text `s` ends with `suffix`, ignoring upper/lower case of `s`.

**How it works**: compares the last `strlen(suffix)` characters of `s`, converted to lower case with `std::tolower`, to the characters of `suffix`. Only `s` is lower-cased, so `suffix` must be written in lower case (the calls use `".jpg"` and `".jpeg"`).

**Used by**: `save_image` (to choose JPEG or PNG).

## draw_text.h / draw_text.cpp

Writes a text label into a picture, for the annotated result videos that people look at (no engine uses it). It lets Windows draw the text with GDI (the Windows Graphics Device Interface, the old built-in drawing library) so that the letters are smooth (anti-aliased), and then blends them into the `Image` with the wanted colour. The two `#pragma comment(lib, ...)` lines at the top of `draw_text.cpp` link `gdi32.lib` and `user32.lib` automatically.

| Function | Purpose |
|---|---|
| `draw_text` | Draw a text with an outline into a BGR picture. |
| `blend` (internal) | Blend a colour into the picture where the text bitmap is bright. |

### draw_text

```cpp
void draw_text(Image& image, int x, int y, const std::string& text, uint8_t b, uint8_t g, uint8_t r, int pixel_height = 22);
```

**What it does**: draws `text` into `image` in the colour (b, g, r), with a dark one-pixel outline so that it can be read on any background. `pixel_height` is the height of the letters in pixels (default 22). The picture must be a 3-channel BGR image; an empty picture, a gray picture or an empty text makes the function do nothing and return. Parts that fall outside the picture are cut off. The text is meant to be plain ASCII (the header says so; the code uses the ANSI versions of the Windows functions, so other characters depend on the Windows code page). The font is bold "Segoe UI". The label's top-left corner is given as (x, y), but the code puts a 2-pixel margin around the text inside its bitmap, so the visible letters begin about 2 pixels to the right of and below (x, y) (my reading of the code).

**How it works** (the comment at the top of `draw_text.cpp` describes the idea):
1. Ask Windows for a font of the requested height and measure the text. Make a small off-screen 32-bit bitmap (a "DIB section", which gives direct access to its pixel bytes) that is 4 pixels wider and higher than the text, so there is room for the outline.
2. Fill the bitmap with black, set a transparent background and white text colour, and let Windows draw the text into it (`TextOutA`), then call `GdiFlush` so that the drawing is finished before the bytes are read. The edges of the letters are now gray levels.
3. Use the brightness of each bitmap pixel as its opacity (see `blend`).
4. Make the outline: blend the text in **black** four times, shifted by one pixel left, right, up and down, then blend the text in the wanted colour on top, unshifted.
5. Release the Windows objects (bitmap, font, drawing contexts). If the bitmap cannot be created nothing is drawn and no error is reported.

**Used by**: `tools/check_video.cpp` only (labels above the face boxes, pose values below them and a heading line).

### blend (internal)

```cpp
void blend(Image& image, const uint8_t* bitmap, int bw, int bh, int x0, int y0, uint8_t b, uint8_t g, uint8_t r)
```

**What it does**: paints the colour (b, g, r) into `image` wherever the text bitmap (`bitmap`, `bw` x `bh` pixels, 4 bytes per pixel) is bright, with its top-left corner placed at (`x0`, `y0`) in the picture. Pixels of the bitmap outside the picture are skipped.

**How it works**: for each bitmap pixel the opacity ("alpha", 0 to 255) is the largest of its three colour bytes (white text on black makes the brightness the coverage of the letter). Alpha 0 is skipped. Each colour channel of the picture pixel becomes `(old * (255 - alpha) + new * alpha) / 255`, so alpha 255 replaces the pixel and a middle value mixes old and new. It does not check the channel count of `image`; `draw_text` has already done that. It is in an anonymous namespace, so only `draw_text.cpp` can call it.

**Used by**: `draw_text` (five calls: four black shifted ones for the outline, then the coloured one).

## fs_compat.h

A header with no functions: it gives the file-system library (folders, paths, directory listing) one name for every compiler the project supports. C++17 compilers have `<filesystem>` (`std::filesystem`). Visual Studio 2015 has only the older version of the same library, `<experimental/filesystem>` (`std::experimental::filesystem`), which is what the project needs it for. The header includes the right one and makes `fa::fs` an alias of it:

```cpp
#include "fs_compat.h"
namespace fs = fa::fs;          // then: fs::path, fs::directory_iterator, fs::exists, fs::create_directories, ...
```

**How it works**: `_MSVC_LANG` (the language version Visual Studio reports; `__cplusplus` stays at 199711 in Microsoft compilers unless a special flag is set) is compared with 201703: C++17 or newer includes `<filesystem>`, anything else includes `<experimental/filesystem>` after defining `_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING`, which stops newer Visual Studio versions from warning about the old header. The project is built as C++14 (`build.bat` uses `/std:c++14`), so the old header is the one in use everywhere; the C++17 branch only matters if someone compiles the sources as C++17.

**What is the same and what is not**: everything the project uses (`path`, `directory_iterator`, `recursive_directory_iterator`, `exists`, `is_directory`, `create_directories`, `remove_all`, `path::filename`, `stem`, `extension`, `parent_path`, `operator/`) exists in both. One difference: `directory_entry::is_directory()` is C++17 only, so the tools write `fs::is_directory(entry.path())`.

**Used by**: `src/camera/frame_source.cpp` (listing the pictures of a folder as a video), `tools/bench_frames.h`, `calibrate_lbp.cpp`, `calibrate_pose.cpp`, `check_video.cpp`, `detect_image.cpp`, `eval_detect.cpp`, `eval_features.cpp`, `gen_features.cpp`, `gen_video.cpp`, `tests/test_common.cpp`, `tests/test_feature.cpp`, `tests/test_pipeline.cpp`.

## inference.h / inference.cpp

"Inference" means running a trained neural network on an input to get its answer (as opposed to training it). The project can run networks with two libraries: **ncnn** (a lightweight CPU library; a model is two files, `model.param` with the structure of the network and `model.bin` with its learned numbers) and **ONNX Runtime** (Microsoft's library; a model is one `model.onnx` file). The engines (detector, extractor) only talk to the `Net` interface defined here, so changing the library is a one-line change in the profile (`detector.backend = ncnn` or `onnx`). **Both backends run on the CPU only, and this is enforced and tested** (the target PC has no GPU): `NcnnNet::load` switches ncnn's Vulkan (GPU) path off, `OnnxNet` appends no execution provider to its session, every network answers `Net::runs_on_cpu()`, and the tests also check that the ONNX Runtime library offers no GPU provider (`onnxruntime_providers`) and that no GPU library is loaded in the process (`tests/cpu_only_check.h`; `docs/TESTING.md`, `docs/MODELS.md`).

| Item | Purpose |
|---|---|
| `Tensor` | One network output: a flat list of floats. |
| `InputPrep` | How the 8-bit image is turned into the network input numbers. |
| `Net` | The interface every backend implements. |
| `Net::~Net` | Virtual destructor. |
| `Net::run` | Run the network on one image. |
| `Net::backend` | Name of the library behind the object. |
| `Net::runs_on_cpu` | True when the network runs on the CPU (always the case in this project). |
| `onnxruntime_providers` | The execution providers the ONNX Runtime library offers (used by the tests to show it has no GPU provider). |
| `create_ncnn_net` | Factory: load an ncnn model. |
| `create_onnx_net` | Factory: load an ONNX Runtime model. |
| `NcnnNet` and its `load`, `run`, `backend`, `runs_on_cpu` | The ncnn backend (internal). |
| `widen` (internal) | UTF-8 text to Windows wide text. |
| `OnnxNet` and its `~OnnxNet`, `ok`, `load`, `run`, `backend`, `runs_on_cpu` | The ONNX Runtime backend (internal), written on the library's C interface. |

### Tensor

```cpp
struct Tensor {
    std::vector<float> data;
};
```

**What it does**: one output of a network, as a flat list of numbers. A "tensor" is a block of numbers with a shape (a list, a table, a stack of tables ...); the shape is not kept here, because each engine knows what its model produces and where each number sits (for example the YuNet decoder reads the four box numbers of cell `i` at positions `i * 4` to `i * 4 + 3` of the box outputs).

**How it works**: a single `std::vector<float>`. For ncnn outputs with several channels the channels are placed one after the other (channel 0 first); for ONNX Runtime the numbers are copied in the tensor's own memory order.

**Used by**: `Net::run` (the type of its output), `detector_deep.cpp`, `extractor_deep.cpp`, `tests/test_common.cpp`.

### InputPrep

```cpp
struct InputPrep {
    float mean[3] = {0, 0, 0};
    float norm[3] = {1, 1, 1};
    bool swap_rb = false;
};
```

**What it does**: describes how the 8-bit image bytes are converted into the numbers the network wants:

```
value = (pixel - mean[c]) * norm[c]
```

for each channel `c`. With `swap_rb = true` the network gets **R, G, B** order instead of the image's B, G, R, and `mean[c]` / `norm[c]` then refer to the channels in the network's order (index 0 is red). The defaults (mean 0, norm 1, no swap) pass the raw 0..255 BGR values through unchanged.

| Model | Settings (as set in the engines) |
|---|---|
| YuNet | defaults: plain BGR 0..255 |
| SCRFD | `swap_rb = true`, mean 127.5, norm 1/128 |
| SFace | `swap_rb = true`, raw 0..255 |
| ArcFace | `swap_rb = true`, mean 127.5, norm 1/127.5 (values -1..1) |

**Used by**: `Net::run`; `detector_deep.cpp` and `extractor_deep.cpp` fill it in `load()`; `tests/test_common.cpp`.

### Net

```cpp
class Net {
public:
    virtual ~Net() {}
    virtual bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) = 0;
    virtual const char* backend() const = 0;
    virtual bool runs_on_cpu() const = 0;
};
```

**What it does**: the common interface ("abstract base class") of a loaded network. Code that holds a `std::unique_ptr<Net>` does not need to know which library is behind it. The `= 0` means "no implementation here": each backend provides its own. Objects are created only by `create_ncnn_net` and `create_onnx_net`. The header does not promise that one `Net` object may be used from several threads at once, and `OnnxNet` reuses an internal buffer between calls, so use one object per thread.

**How it works**: `NcnnNet` and `OnnxNet` (below) derive from it. Calls through a `Net` pointer reach the right backend by C++'s virtual-function mechanism.

**Used by**: `src/detect/detector_deep.cpp` (`DeepDetector`), `src/feature/extractor_deep.cpp` (`DeepExtractor`), `tests/test_common.cpp`.

### Net::~Net

```cpp
virtual ~Net() {}
```

**What it does**: makes sure that deleting a `Net` through a base-class pointer destroys the real backend object (and so frees the model). Without `virtual` only the base part would be cleaned up.

**How it works**: used automatically when a `std::unique_ptr<Net>` goes out of scope.

**Used by**: implicitly, by every `std::unique_ptr<Net>`.

### Net::run

```cpp
virtual bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) = 0;
```

**What it does**: runs the network on one image and returns all outputs, in the model's order, in `outputs` (resized to the number of outputs). Returns `false` when the network fails. The image must be a **3-channel BGR** image: both backends read it as 3 bytes per pixel (the header comment says so; the deep detector and the deep extractor also check `channels == 3` before calling). The image size must be what the model expects (640 x 640 for the main detector network, 320 x 320 for the optional small one in the permissive profile, 112 x 112 for the recognizers).

**How it works**: see `NcnnNet::run` and `OnnxNet::run`.

**Used by**: `DeepDetector::detect` (`detector_deep.cpp`), `DeepExtractor::extract_aligned` (`extractor_deep.cpp`), `tests/test_common.cpp`.

### Net::backend

```cpp
virtual const char* backend() const = 0;
```

**What it does**: returns the name of the library behind the object: `"ncnn"` or `"onnxruntime"`.

**How it works**: each backend returns a fixed text.

**Used by**: `detector_deep.cpp` and `extractor_deep.cpp` (to build names such as `yunet/onnxruntime` shown in logs and benchmarks).

### Net::runs_on_cpu

```cpp
virtual bool runs_on_cpu() const = 0;
```

**What it does**: answers "does this network run on the CPU?". It takes no input and returns `true` when the network runs on the CPU and `false` when it would use a GPU. **Every network of this project runs on the CPU** (the target PC has no GPU), so the answer is `true`; the function exists so that this is a checked statement and not only a comment. The tests assert it for every network (`tests/test_common.cpp`), and `DeepDetector`, `DeepExtractor` and `AuthPipeline` pass the question on (`FaceDetector::runs_on_cpu`, `FeatureExtractor::runs_on_cpu`, `AuthPipeline::runs_on_cpu`).

**How it works**: pure virtual; each backend answers from what it set up. `NcnnNet` returns `!net_.opt.use_vulkan_compute` (ncnn's own switch for GPU computing, set to `false` in `NcnnNet::load`); `OnnxNet` returns `true`, because its session options never name an execution provider other than ONNX Runtime's built-in CPU one. The answer says what the code asked for. That nothing used a GPU in fact is checked from the outside by the tests: they look at the libraries the process has loaded (`tests/cpu_only_check.h`).

**Used by**: `DeepDetector::runs_on_cpu` (`detector_deep.cpp`), `DeepExtractor::runs_on_cpu` (`extractor_deep.cpp`), `tests/test_common.cpp`.

### onnxruntime_providers

```cpp
std::vector<std::string> onnxruntime_providers();
```

**What it does**: returns the names of the **execution providers** that the ONNX Runtime library in use offers. An execution provider is the part of ONNX Runtime that actually computes a network: the CPU one, or one for a GPU (CUDA, TensorRT, DirectML, OpenVINO, ...). The library bundled with the project (1.17.3, the CPU build) offers `CPUExecutionProvider` and `AzureExecutionProvider`. The Azure provider is **no GPU**: it sends the network's input to a remote service and only works if a program appends it to a session, which nothing in this project does. The list is empty when the library does not offer the API version of the header or the call fails.

**How it works**: asks the library for its function table (`OrtGetApiBase()->GetApi(ORT_API_VERSION)`), calls `GetAvailableProviders`, copies the names into the result and gives the library's list back with `ReleaseAvailableProviders`; an error status is released and gives an empty list. It creates no session and loads no model.

**Used by**: `tests/test_common.cpp` (`main`: the check that the provider list holds the CPU provider and nothing but, at most, the Azure one, so that a GPU build of the library would be noticed).

### create_ncnn_net

```cpp
std::unique_ptr<Net> create_ncnn_net(const std::string& param_path, const std::string& bin_path,
                                     const std::string& input_name,
                                     const std::vector<std::string>& output_names, int threads);
```

**What it does**: loads an ncnn model and returns it as a `Net`, or `nullptr` if loading fails (the reason is printed to `std::cerr`). `param_path` and `bin_path` are the two model files. `input_name` is the name of the network's input "blob" (a named data buffer between layers) and `output_names` are the names of the output blobs, all as written in the `.param` file; the output order you give is the order of `Net::run`'s result (this is why `detector.outputs` in the profile must be listed in the order the detector decoder expects). `threads` is the number of CPU threads the network may use.

**How it works**: creates an `NcnnNet`, calls its `load()` and returns it, or `nullptr` when `load()` fails.

**Used by**: `DeepDetector::load` (`detector_deep.cpp`) and `DeepExtractor::load` (`extractor_deep.cpp`) when the profile says `backend = ncnn` (they derive the `.bin` name from the `.param` name by replacing the extension), `tests/test_common.cpp`.

### create_onnx_net

```cpp
std::unique_ptr<Net> create_onnx_net(const std::string& onnx_path, int threads);
```

**What it does**: loads an ONNX Runtime model from one `.onnx` file and returns it as a `Net`, or `nullptr` on failure (the reason is printed). Input and output names are read from the file itself, so they need not be given.

**How it works**: creates an `OnnxNet`, calls its `load()` and returns it, or `nullptr` when `load()` fails.

**Used by**: `DeepDetector::load` (for the main model, and a second time for the optional small model `detector.model_small`), `DeepExtractor::load` (any `backend` other than `ncnn` ends up here), `tests/test_common.cpp`.

### NcnnNet (internal class)

```cpp
class NcnnNet : public Net
```

**What it does**: the ncnn implementation of `Net`. It keeps the loaded `ncnn::Net`, the input blob name and the list of output blob names. It lives in an anonymous namespace, so it can only be reached through `create_ncnn_net`.

**Used by**: `create_ncnn_net`.

#### NcnnNet::load

```cpp
bool load(const std::string& param, const std::string& bin, const std::string& input,
          const std::vector<std::string>& outputs, int threads)
```

**What it does**: stores the blob names, loads the model files, and returns `false` if either file cannot be read (it prints `ncnn: cannot read <file>`).

**How it works**:
1. Save `input` and `outputs`.
2. Set `net_.opt.num_threads = threads` and switch the GPU path off (`use_vulkan_compute = false`; the comment says the target PC has no GPU). **A fact about the bundled library:** the ncnn library in `third_party/ncnn` was *built with* its Vulkan code (its `platform.h` has `NCNN_VULKAN 1`, which is why `build.bat` and the Visual Studio projects also link the glslang libraries). Because `use_vulkan_compute` is `false` for every network, ncnn never creates a Vulkan instance and never opens a GPU; the tests check that `vulkan-1.dll` is not loaded into the process. Rebuilding ncnn with `NCNN_VULKAN=OFF` would remove the GPU code path from the library altogether; that was not done.
3. `load_param(param)` reads the structure, `load_model(bin)` reads the learned numbers; a non-zero result is a failure.

The paths are given to ncnn as plain narrow strings (the ONNX backend, in contrast, converts to UTF-16); whether non-English characters in a path work with ncnn has not been checked.

**Used by**: `create_ncnn_net`.

#### NcnnNet::run

```cpp
bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) override
```

**What it does**: converts the image to ncnn's float format, runs the network and copies every requested output blob into `outputs`. Returns `false` if any output cannot be extracted (then `outputs` may be partly filled).

**How it works**:
1. `ncnn::Mat::from_pixels(...)` turns the BGR bytes into a float image (channel after channel), swapping to RGB when `prep.swap_rb` is set; it honours `image.stride`.
2. `substract_mean_normalize(prep.mean, prep.norm)` applies `(value - mean) * norm` (the name `substract` is spelled that way in ncnn).
3. A new `ncnn::Extractor` is created for this call, the input is set by name and each output is extracted by name.
4. An ncnn `Mat` can have several channels, each padded in memory, so a single flat copy would include padding. The code therefore copies channel by channel (`w * h * d` floats each) into one flat vector. For the raw SCRFD outputs of `detector.layout = chw` (2, 8 and 20 channels of rows x columns numbers) this gives channel 0 first, then channel 1 and so on, each as `rows * cols` numbers row by row: the layout `DeepDetector::decode_scrfd_chw` reads (`docs/detect.md`).

**Used by**: through `Net::run`, by `detector_deep.cpp` and `extractor_deep.cpp`.

#### NcnnNet::backend

```cpp
const char* backend() const override { return "ncnn"; }
```

**What it does**: returns the text `"ncnn"`.

**Used by**: through `Net::backend`.

#### NcnnNet::runs_on_cpu

```cpp
bool runs_on_cpu() const override { return !net_.opt.use_vulkan_compute; }
```

**What it does**: returns `true` while ncnn is not allowed to use Vulkan (the GPU interface), which is always, because `load` sets `use_vulkan_compute = false` before the model is read.

**How it works**: it reads the option back from the `ncnn::Net` instead of returning a fixed `true`, so the answer follows the setting that ncnn really uses; if someone later turned the option on, the function, and with it the tests, would report a GPU network.

**Used by**: through `Net::runs_on_cpu`.

### widen (internal, inference.cpp)

```cpp
std::wstring widen(const std::string& s)
```

**What it does**: converts a UTF-8 path to a Windows wide string, because ONNX Runtime on Windows takes its model path as `wchar_t` text.

**How it works**: the same two-call `MultiByteToWideChar` technique as in `image_io.cpp` (first ask for the length, then convert, then drop the terminating zero). Unlike the copy in `image_io.cpp` it has no special case for an empty string (it still works for one, giving an empty result).

**Used by**: `OnnxNet::load`.

### OnnxNet (internal class)

```cpp
class OnnxNet : public Net
```

**What it does**: the ONNX Runtime implementation of `Net`. It holds the library's function table (`OrtApi`), an ONNX environment (`OrtEnv`, logging only errors, named `"face_auth"`), the session (`OrtSession`, the loaded model), the input name, the output names and a reusable float buffer for the input. Anonymous namespace: reachable only through `create_onnx_net`.

**ONNX Runtime is used through its C interface** (`onnxruntime_c_api.h`), not through the C++ wrapper header (`onnxruntime_cxx_api.h`). The wrapper header of version 1.17 uses C++14 "relaxed `constexpr`", which the compiler of Visual Studio 2015 does not have (it stops with error C3249), whereas the C header compiles with every compiler (`docs/VISUAL_STUDIO.md`). Every C call returns an `OrtStatus*`: null means success, anything else is an error that must be read with `GetErrorMessage` and freed with `ReleaseStatus` (that is what `OnnxNet::ok` does), and objects the library hands out (`OrtValue`, session options, ...) must be released with the matching `Release...` call, which the C++ wrapper used to do automatically.

**Used by**: `create_onnx_net`.

#### OnnxNet::~OnnxNet

```cpp
~OnnxNet()
```

**What it does**: gives the session and the environment back to the library.

**How it works**: when the function table was obtained, calls `ReleaseSession` and `ReleaseEnv` for the objects that were created (a net whose `load` failed halfway releases only what exists).

**Used by**: whenever the `std::unique_ptr<Net>` that `create_onnx_net` returned is destroyed.

#### OnnxNet::ok (private)

```cpp
bool ok(OrtStatus* status)
```

**What it does**: turns the answer of a C call into a `bool`: `true` when the call succeeded, otherwise `false` after printing `onnxruntime: <message>` to `std::cerr`.

**How it works**: a null status is success. Otherwise it prints `GetErrorMessage(status)` and calls `ReleaseStatus(status)`.

**Used by**: `OnnxNet::load` and `OnnxNet::run`, around every call to the library.

#### OnnxNet::load

```cpp
bool load(const std::string& path, int threads)
```

**What it does**: loads the `.onnx` model and reads its input and output names. Returns `false` (after printing `onnxruntime: <message>`) if ONNX Runtime reports an error, for example when the file is missing or damaged, or when the library does not offer the API version of the header (17).

**How it works**:
1. Ask the library for its function table (`OrtGetApiBase()->GetApi(ORT_API_VERSION)`) and create the environment.
2. Create session options: `SetIntraOpNumThreads(threads)` (threads inside one operation), `SetInterOpNumThreads(1)`, and the highest graph optimisation level (`ORT_ENABLE_ALL`); create the session from the wide-character path; release the options (the session keeps what it needs).
3. Take the library's default allocator, then the name of input number 0 (only the first input is supported) and the names of all outputs, in the model's order; each name is copied into a `std::string` and the library's copy is freed with the allocator.

**Used by**: `create_onnx_net`.

#### OnnxNet::run

```cpp
bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) override
```

**What it does**: converts the image to the planar float layout the model expects, runs it and copies all outputs into `outputs`. Returns `false` (and prints the message) if ONNX Runtime reports an error. The outputs are assumed to be `float` tensors.

**How it works**:
1. Build the input in the member buffer `input_` (reused between calls to avoid allocating every frame) in **channel-first** layout: all values of channel 0, then channel 1, then channel 2. For network channel `c` the source byte is `2 - c` when `swap_rb` is set, otherwise `c`; each value is `(pixel - mean[c]) * norm[c]`.
2. Wrap the buffer as a tensor of shape `{1, 3, height, width}` without copying (batch of 1, 3 channels): a CPU memory description is created, `CreateTensorWithDataAsOrtValue` makes the tensor around `input_`, and the memory description is released.
3. Run the session with the stored input name and all output names; the library fills one `OrtValue` per output.
4. For each result, read the number of elements (`GetTensorTypeAndShape`, `GetTensorShapeElementCount`, then release the shape information) and copy the floats (`GetTensorMutableData`) into `outputs[i].data`.
5. Release the input tensor and every output `OrtValue` (also when a step failed).

**Used by**: through `Net::run`.

#### OnnxNet::backend

```cpp
const char* backend() const override { return "onnxruntime"; }
```

**What it does**: returns the text `"onnxruntime"`.

**Used by**: through `Net::backend`.

#### OnnxNet::runs_on_cpu

```cpp
bool runs_on_cpu() const override { return true; }
```

**What it does**: returns `true`.

**How it works**: a fixed answer, with a reason: `load` sets only the thread counts (`SetIntraOpNumThreads`, `SetInterOpNumThreads`) and the optimisation level, and appends **no execution provider** to the session options (no `SessionOptionsAppendExecutionProvider_*` call exists in the project), so ONNX Runtime uses its built-in CPU provider and nothing else. The bundled library (1.17.3) is the CPU build anyway: `onnxruntime_providers` shows `CPUExecutionProvider` and the remote `AzureExecutionProvider` only, and the folder holds no GPU provider library (`onnxruntime_providers_cuda.dll` and the like).

**Used by**: through `Net::runs_on_cpu`.

## profile.h / profile.cpp

A **model profile** is a small text file in `models/` that tells the engines which models to use, so switching models needs no code change: pass another profile file. `load_profile()` reads such a file into a `ModelProfile` struct. The file has lines `key = value`; lines starting with `#` and empty lines are ignored.

Example, taken from `models/permissive.ini` (comments shortened):

```
# Permissive profile: models with MIT / Apache-2.0 licences
detector.type    = yunet
detector.backend = onnx
detector.model   = models/permissive/yunet.onnx
detector.score   = 0.6
# Faster 320x320 version of the same network, used when the camera image is at most
# 640 pixels on its longer side ...
detector.model_small = models/permissive/yunet_320.onnx
detector.small_size  = 320
detector.small_max_side = 640

detector.haar_face = models/permissive/haar_frontalface.xml
detector.haar_eye  = models/permissive/haar_eye.xml

recognizer.type      = sface
recognizer.backend   = onnx
recognizer.model     = models/permissive/sface.onnx
recognizer.dim       = 128
recognizer.threshold = 0.55

runtime.threads = 2
```

The permissive profile names no pose detector, so its example has no `pose.*` line. The three `dev_noncommercial` profiles (`dev_noncommercial.ini`, `dev_noncommercial_square.ini` and `dev_noncommercial_onnx.ini`: SCRFD + ArcFace) each contain one extra line (with a comment above it that explains why):

```
pose.detector_profile = models/permissive.ini
```

It means: "the head-pose check (the zoomed-in second look, `refine_face`) is done by the detector of `models/permissive.ini`", that is by YuNet. SCRFD still finds the faces; only the pose is measured by the other detector, because the SCRFD landmarks give a much noisier pitch (the comment in `dev_noncommercial.ini` says about 2.5 times noisier than YuNet's, and that with SCRFD only 28% of straight-on portraits passed the +-5 degree gate against 57% with YuNet). `classical.ini` has no such line either.

All keys (anything else is an error):

| Key | Field of `ModelProfile` | Meaning |
|---|---|---|
| `detector.type` | `detector_type` | `yunet`, `scrfd` (deep) or `haar` (classical). |
| `detector.backend` | `detector_backend` | `onnx` or `ncnn`. Any other text (including different letter case, such as `ONNX`) makes `load_profile` fail. |
| `detector.model` | `detector_model` | Path of the `.onnx` file, or of the `.param` file for ncnn (the `.bin` next to it is found by replacing `.param` with `.bin`). Relative paths start at the project root. |
| `detector.model_small` | `detector_model_small` | Optional path of a faster version of the same network with a smaller input square (see below). Relative paths start at the project root. Empty (the default) means "always use the main model". |
| `detector.small_size` | `detector_small_size` | Side, in pixels, of the square given to the small network. Default 320. Whole number; when `detector.model_small` is set it must be at least 32 and a multiple of 32, otherwise `load_profile` fails. |
| `detector.small_max_side` | `detector_small_max_side` | The small network is used only when the longer side of the camera image is at most this many pixels. Default 640. Whole number; when `detector.model_small` is set it must be at least 1, otherwise `load_profile` fails. |
| `detector.input` | `detector_input` | ncnn only: name of the input blob. |
| `detector.outputs` | `detector_outputs` | ncnn only: names of the output blobs, separated by commas, in decoder order. Spaces around each name are removed, so `out0, out1` works. |
| `detector.layout` | `detector_layout` | How the SCRFD outputs are laid out and, with it, the shape of the network input. `flat` (the default): the nine reshaped outputs of the original conversion (`out0` to `out8`; scores after the sigmoid, boxes and landmarks as lists with one row per anchor), network input a 640x640 square. `chw`: the nine **raw** outputs of the last convolutions (`65,85,105,66,86,106,67,87,107` in `dev_noncommercial.ini`: channels x rows x columns, scores before the sigmoid); the network accepts any input whose sides are multiples of 32, so the picture is shrunk to the smallest such rectangle (`letterbox_fit`) instead of a square. `chw` needs a model whose upsampling layers scale (`models/dev_noncommercial/det_dyn.param`). Any other text makes `load_profile` fail; `chw` is accepted only for `detector.type = scrfd` with exactly nine `detector.outputs`, otherwise `load_profile` fails (see below). |
| `detector.score` | `detector_score` | Minimum detection score. |
| `detector.haar_face` | `haar_face_model` | Haar cascade XML for faces (path). |
| `detector.haar_eye` | `haar_eye_model` | Haar cascade XML for eyes (path; optional). |
| `pose.detector_profile` | `pose_detector_profile` | Optional path of **another profile file** whose detector does the pose check (`refine_face`) instead of this profile's own detector. Relative paths start at the project root, like the model paths. Empty (the default) means the profile's own detector does it. Only the `detector.*` keys of the named profile are used, and the named profile may not name a further one itself (see `create_pose_detector` in `detect.md`). `load_profile` only stores the path; whether the file exists and is valid is checked later, when the pose detector is created. |
| `recognizer.type` | `recognizer_type` | `sface`, `arcface` (deep) or `lbp` (classical). |
| `recognizer.backend` | `recognizer_backend` | `onnx` or `ncnn`; any other text makes `load_profile` fail. |
| `recognizer.model` | `recognizer_model` | Model path, same rules as `detector.model`. |
| `recognizer.input` | `recognizer_input` | ncnn only: input blob name. |
| `recognizer.output` | `recognizer_output` | ncnn only: output blob name. |
| `recognizer.dim` | `feature_dim` | Length of the feature vector (128, 512 or 256 in the shipped profiles). Whole number from 1 to `FA_MAX_FEATURE_DIM` (512), otherwise `load_profile` fails. |
| `recognizer.threshold` | `match_threshold` | Cosine similarity needed to accept a match. |
| `runtime.threads` | `threads` | CPU threads used by each network (and by the Haar detector). Whole number from 1 to 64, otherwise `load_profile` fails. |

Things to know when writing a profile (all follow from the code):

* Keys are case-sensitive; spaces and tabs around the key and around the value are ignored.
* The value is everything after the first `=`, so a value may contain `=`. **A comment after a value on the same line is not removed** (`detector.model = a.onnx  # note` would give the path `a.onnx  # note`; for numbers the reader stops at the first character that is not part of the number, so a trailing comment happens to be harmless there).
* Keys you leave out keep the default of the struct (shown below). For example `classical.ini` sets no model paths because the classical engines need none.
* The two backend names are checked after the whole file has been read. Both `detector_backend` and `recognizer_backend` must then be exactly `onnx` or `ncnn`; this also applies to a profile that does not set them (the defaults are `onnx`, so they pass). The check exists because a misspelled backend would otherwise silently select ONNX Runtime. It is the last of the six checks that `load_profile` makes after reading the file (the others are the ranges of `runtime.threads`, `recognizer.dim` and the small-model numbers, see the key table, and the two checks of `detector.layout` described in the next point).
* **`detector.layout`** is checked twice after the file has been read, in this order, each with its own message naming the file: the text must be exactly `flat` or `chw` (`detector.layout must be 'flat' or 'chw', not '...'`; a profile that does not set the key has the default `flat`, which passes), and `chw` is only accepted when `detector.type` is `scrfd` **and** `detector.outputs` has exactly nine names (`detector.layout = chw is for SCRFD and needs nine detector.outputs (scores, boxes, landmarks of the three strides)`). The reason for the second check is that the decoder for `chw` reads nine raw outputs in the order scores, boxes, landmarks of the strides 8, 16 and 32 and would otherwise read the wrong tensors; the check cannot tell whether the nine names are the right ones, nor whether the model accepts a rectangle (`load_profile` does not open the model), and it does not look at `detector.backend`. The model has to be one whose upsampling layers scale (`det_dyn.param`), and its output names are the blob numbers of the last convolutions (`65,85,105,66,86,106,67,87,107`, read from `det_dyn.param`). Three checks in `tests/test_common.cpp` provoke the refusals (`docs/tools.md`, group 11): `chw` with `scrfd` and no outputs is rejected and with nine outputs accepted; `chw` with `yunet` and nine outputs is rejected; `detector.layout = round` is rejected and `flat` accepted. Setting `detector.layout = chw` for a YuNet profile is refused by the second check, and a YuNet detector ignores the key anyway.
* A key that is given twice: the later line wins.
* The file is read as plain text. A UTF-8 byte-order mark (BOM, the three bytes some editors put at the start of a file) on the first line is skipped, so a profile saved with a BOM works.
* Whole numbers (`runtime.threads`, `recognizer.dim`, `detector.small_size`, `detector.small_max_side`) are read with `std::atoi`, which turns text that is not a number into 0 without complaint. `load_profile` therefore range-checks them after reading the file (the ranges are in the key table). `detector.score` and `recognizer.threshold` are read with `std::atof` and are **not** range-checked: text that is not a number becomes 0.
* Profile files do not check that the files they name exist: that is reported later, when the detector or extractor is created. This holds for `pose.detector_profile` too: a wrong path is reported by `create_pose_detector` (`detector: cannot use pose.detector_profile '...'`), not by `load_profile`.
* `pose.detector_profile` names a whole other profile, not a model. Only the `detector.*` keys of that profile are used (its recognizer, threshold and `runtime.threads` are ignored: the thread count of the profile that names it is used), and it may not contain a `pose.detector_profile` line itself. A profile that does not need a different pose detector simply leaves the key out. Whoever changes `detector.type` in code after loading (the `--type` option of some tools) must also clear `pose_detector_profile`, or the named detector would still do the pose check.
* The three `detector.*small*` keys are optional and work together. In the current code they are used only by the deep detector (`DeepDetector` in `detector_deep.cpp`), and only when `detector.backend` is `onnx`: with `ncnn` the small model is ignored. If the small model cannot be loaded, the detector prints `detector: cannot load <file> (ignored)` and carries on with the main model only. When a camera image qualifies (longer side at most `detector.small_max_side`) and the caller did not ask for the `accurate` mode, the small network is used with a `detector.small_size` square; otherwise the main network is used with the `FA_DETECT_INPUT_SIZE` square. `refine_face` always asks for the accurate mode, so the second look at a face uses the main network. The comment in `models/permissive.ini` says the small model was made from `yunet.onnx` by changing the input size and that faces below about 40 px are found less reliably with it. `detector.small_size` must match the input size the small model file was made for (`load_profile` only checks that it is at least 32 and a multiple of 32, not that it matches the file).

| Function | Purpose |
|---|---|
| `ModelProfile` | The settings of one profile, with defaults. |
| `load_profile` | Read a profile file into a `ModelProfile`. |
| `trim` | Remove spaces, tabs and line ends from both ends of a text. |
| `split` | Cut a text into parts at a separator character. |
| `is_absolute` (internal) | Tells whether a path starts at a drive or root. |
| `make_path` (internal) | Prefixes a relative path with the project root. |

### ModelProfile

```cpp
struct ModelProfile {
    // ---- detector ----
    std::string detector_type = "yunet";
    std::string detector_backend = "onnx";
    std::string detector_model;
    std::string detector_model_small;
    int detector_small_size = 320;
    int detector_small_max_side = 640;
    std::string detector_input = "in0";
    std::vector<std::string> detector_outputs;
    std::string detector_layout = "flat";
    std::string haar_face_model;
    std::string haar_eye_model;
    float detector_score = FA_DETECT_SCORE_THRESHOLD;

    std::string pose_detector_profile;

    // ---- recognizer (feature extractor) ----
    std::string recognizer_type = "sface";
    std::string recognizer_backend = "onnx";
    std::string recognizer_model;
    std::string recognizer_input = "in0";
    std::string recognizer_output = "out0";
    int feature_dim = 128;
    float match_threshold = 0.363f;

    // ---- runtime ----
    int threads = 2;

    std::string root;
};
```

**What it does**: holds every setting that can be chosen in a profile file. The key-to-field table above shows which key fills which field; the defaults are the values in the code above (`detector_score` starts as `FA_DETECT_SCORE_THRESHOLD`, i.e. 0.6f; `match_threshold` starts at 0.363f, the value the SFace authors recommend, but every shipped profile sets its own). Strings that are empty by default (model paths) mean "not set". The three small-model fields are explained in the comment of `profile.h` as: an optional faster model for SMALL camera images (YuNet on ONNX Runtime only), the same network with a smaller input square, used when the longer side of the camera image is at most `detector_small_max_side` pixels; leave `detector_model_small` empty to always use the big one. Note that `detector_small_size` (320) and `detector_small_max_side` (640) have defaults even when no small model is given; they have no effect while `detector_model_small` is empty. `detector_layout` (key `detector.layout`, default `"flat"`) says how the SCRFD outputs are laid out, as the comment of `profile.h` explains: `"flat"` is the nine outputs of the original conversion (scores after the sigmoid, boxes and landmarks as lists with one row per anchor) and needs a 640x640 square as input; `"chw"` is the nine raw outputs of the last convolutions (channels x rows x columns, scores before the sigmoid), which accepts any input whose sides are multiples of 32, so the picture is shrunk to the smallest such rectangle (640x384 for a 16:9 camera image) instead of a square: the same scale and result with about 40% less work for 16:9; it needs a model whose upsampling layers scale (`models/dev_noncommercial/det_dyn.param`). `pose_detector_profile` (key `pose.detector_profile`) is the optional path of another profile file whose detector does the pose check instead of this profile's own; the comment in `profile.h` gives the reason (the head rotation is read from the 5 landmarks, and SCRFD's landmarks give a much noisier pitch than YuNet's, so `dev_noncommercial.ini` lets YuNet measure the pose), and says that only the `detector.*` keys of the named profile are used and that it may not name a further profile. Empty (the default) means "this profile's own detector does it". `root` is not a file key: `load_profile` fills it with the project root folder given by the caller.

**How it works**: a plain struct. The engines receive it by `const ModelProfile&` and copy what they need. `root` is also used by the classical extractor to find `models/lbp_calibration.bin` (`root + "/models/lbp_calibration.bin"`).

**Used by**: `src/detect/detector.h` (`create_detector`; `create_pose_detector` reads `pose_detector_profile`, `root` and `threads`), `detector_deep.cpp` (including the three small-model fields, read in `load()` and `detect()`, and `detector_layout`, read in `detect()` to choose between `letterbox` and `letterbox_fit` and between `decode_scrfd` and `decode_scrfd_chw`), `detector_haar.cpp`, `src/feature/extractor.h` (`create_extractor`), `extractor_deep.cpp`, `extractor_lbp.cpp`, `src/pipeline/auth_pipeline.cpp` (`init`; it keeps `match_threshold` and tests `pose_detector_profile` to decide whether to build a separate pose detector), and the tools and tests (some change fields after loading, for example `profile.threads` in `tools/bench.cpp`, `tools/bench_detect.cpp` and `tools/bench_extract.cpp`, which set it from `--threads`; `tools/bench_detect.cpp` also reads `pose_detector_profile`, `tests/test_feature.cpp` reads `feature_dim`, `match_threshold` and `recognizer_type`, and `tests/test_pipeline.cpp` reads `detector_layout` to decide whether to run the comparison of the fitted rectangle with the square).

### load_profile

```cpp
bool load_profile(const std::string& path, const std::string& root, ModelProfile& profile);
```

**What it does**: reads the profile file `path` and stores its settings in `profile`. `root` is the folder from which relative model paths start (all the tools and tests pass `"."`, which is why programs must be run from the project root). Returns `true` when the whole file was read. Returns `false` and prints the reason to `std::cerr` when the file cannot be opened, when a line is not of the form `key = value` (message `expected key = value`, with the file name and line number), when a key is unknown (message `unknown key '...'`, again with file and line), or when one of the checks after the whole file has been read fails. Those checks, in this order, each with its own message naming the file: `runtime.threads` outside 1..64; `recognizer.dim` outside 1..`FA_MAX_FEATURE_DIM`; when `detector.model_small` is set, a `detector.small_size` below 32 or not a multiple of 32, or a `detector.small_max_side` below 1 (one shared message); a `detector_layout` that is not exactly `flat` or `chw` (message `detector.layout must be 'flat' or 'chw', not '...'`); `detector_layout = chw` together with a `detector_type` other than `scrfd` or with a number of `detector_outputs` other than nine (message `detector.layout = chw is for SCRFD and needs nine detector.outputs (scores, boxes, landmarks of the three strides)`); and a `detector_backend` or `recognizer_backend` that is not exactly `onnx` or `ncnn` (message `backend must be 'onnx' or 'ncnn', not '...'`). The strict handling of unknown keys, bad numbers, layout and backend names is deliberate: a typo or a text that is not a number is reported instead of silently ignored (`atoi` would turn it into 0, and a misspelled backend would otherwise select ONNX Runtime). A UTF-8 byte-order mark at the start of the first line is skipped. Values changed in the struct after loading (some tools override `detector_type` or `threads`) are not checked again. The key `pose.detector_profile` is only stored (as a path, through `make_path`); `load_profile` does not open the file it names. On a `false` result `profile` may already be partly filled.

**How it works**:
1. Open the file; remember `root` in `profile.root`.
2. For every line: count the line number; on line 1, remove a UTF-8 byte-order mark (the bytes EF BB BF) if there is one; `trim` the line; skip it if it is empty or starts with `#`.
3. Find the first `=`; if there is none, report the error and stop.
4. `trim` the key (left of `=`) and the value (right of `=`).
5. Compare the key with the known names one by one (a long `if / else if` chain, 21 keys, `detector.layout` among them: its text is stored as it is, the check comes in step 7) and store the value in the matching field. Path-type values (`detector.model`, `detector.model_small`, `detector.haar_face`, `detector.haar_eye`, `pose.detector_profile`, `recognizer.model`) go through `make_path`. `detector.small_size` and `detector.small_max_side` are whole numbers read with `std::atoi`. `detector.outputs` is cut at commas with `split`, and then each name is trimmed, so `out0, out1` is fine. Numbers are converted with `std::atof` (float) or `std::atoi` (integer), which do not report errors: a value that is not a number silently becomes 0 (the checks in step 7 catch this for the whole numbers; `detector.score` and `recognizer.threshold` are not checked).
6. An unknown key stops the reading with an error.
7. After the last line, check the values in `profile` (they also include defaults and anything that was in the struct before the call):
   * `threads` must be 1 to 64, otherwise print `profile: <file>: runtime.threads must be 1..64` and return `false`.
   * `feature_dim` must be 1 to `FA_MAX_FEATURE_DIM`, otherwise print `profile: <file>: recognizer.dim must be 1..512` (the number is the macro's value) and return `false`.
   * Only if `detector_model_small` is not empty: `detector_small_size` must be at least 32 and a multiple of 32, and `detector_small_max_side` at least 1, otherwise print `profile: <file>: detector.small_size must be a multiple of 32 (at least 32) and detector.small_max_side at least 1` and return `false`.
   * `detector_layout` must be exactly `flat` or `chw`, otherwise print `profile: <file>: detector.layout must be 'flat' or 'chw', not '<value>'` and return `false`.
   * If `detector_layout` is `chw`: `detector_type` must be `scrfd` and `detector_outputs` must hold exactly nine names, otherwise print `profile: <file>: detector.layout = chw is for SCRFD and needs nine detector.outputs (scores, boxes, landmarks of the three strides)` and return `false`.
   * `detector_backend` and `recognizer_backend` must each be exactly `onnx` or `ncnn`, otherwise print `profile: <file>: backend must be 'onnx' or 'ncnn', not '<value>'` and return `false`.

The existing contents of `profile` are not reset first, so loading on top of an already filled profile only overwrites the keys that appear in the file.

**Used by**: `tools/` (`roll_probe`, `detect_image`, `gen_video`, `eval_features`, `eval_detect`, `calibrate_pose`, `calibrate_lbp`, `bench`, `bench_detect`, `bench_extract`) and `tests/test_pipeline.cpp`, `tests/test_detect_units.cpp` (the pose-detector section), `tests/test_common.cpp` (the profile-file checks) and `tests/test_feature.cpp`. In the library only `create_pose_detector` (`src/detect/detector.cpp`) calls it, to load the profile named by `pose.detector_profile` (with `root` of the profile that names it); `AuthPipeline::init` receives an already loaded profile.

### trim

```cpp
std::string trim(const std::string& s);
```

**What it does**: returns a copy of `s` without leading and trailing white space. Only space, tab, carriage return (`\r`) and line feed (`\n`) count as white space. The `\r` handling matters for files written on Windows.

**How it works**: moves a start index forward while the character is white space, moves an end index backward the same way, and returns the text between them (an all-white-space text gives an empty string).

**Used by**: `load_profile` (this file) and `src/store/feature_store.cpp` (to skip empty lines of the CSV).

### split

```cpp
std::vector<std::string> split(const std::string& s, char separator);
```

**What it does**: cuts `s` at every `separator` and returns the pieces. `"out0,out1,out2"` with `','` gives three pieces. Empty pieces are kept (`"a,,b"` gives `"a"`, `""`, `"b"`) and an empty text gives one empty piece. The pieces are **not** trimmed by `split` itself, so `"out0, out1"` gives `"out0"` and `" out1"` (with a leading space); `load_profile` therefore trims each piece after calling it.

**How it works**: repeatedly searches for the next separator from the current start position with `std::string::find`; each piece before it is stored; the remainder after the last separator is the final piece.

**Used by**: `load_profile` only (for `detector.outputs`).

### is_absolute (internal)

```cpp
static bool is_absolute(const std::string& p)
```

**What it does**: tells whether a path is already absolute (does not depend on the current folder).

**How it works**: a path counts as absolute if its second character is `:` (a drive letter such as `C:\...`) or if it starts with `/` or `\` (which also covers network paths like `\\server\share`). It does not check that the file exists.

**Used by**: `make_path`.

### make_path (internal)

```cpp
static std::string make_path(const std::string& root, const std::string& p)
```

**What it does**: turns a path from the profile into the path the engines use.

**How it works**: an empty path or an absolute path is returned unchanged. A relative path gets the root in front: `root + "/" + p` (or `p` itself if `root` is empty). With `root = "."` the result is `./models/permissive/yunet.onnx`, which is still a relative path, so it is only valid while the program's working directory is the project root.

**Used by**: `load_profile`, for `detector.model`, `detector.model_small`, `detector.haar_face`, `detector.haar_eye` and `recognizer.model`.

Numeric profile values must parse completely: trailing text, empty values, overflow, NaN and infinity are rejected. `detector.score` accepts 0..1 and `recognizer.threshold` accepts -1..1 (cosine similarity). Existing thread/dimension/optional-small-model bounds also apply. Invalid settings make `load_profile` return false with the key and line when applicable.

ONNX network outputs must be float32 tensors. The backend checks each output element type before reading its data; unsupported types (including double and integer outputs) return failure, report the output name/type, and clear the output list.
