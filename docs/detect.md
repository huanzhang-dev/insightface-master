# Face detection engine (`src/detect/`)

Reference documentation for every function, method, struct and macro of the face
detection engine. It is written for readers who are new to C++ and to computer
vision: each word that is not everyday English is explained the first time it
appears (see the glossary below). Everything stated here can be checked in the
source files named in each section. Where something is a conclusion drawn by the
author of this document (for example a hand calculation) instead of a fact written in
the code, the text says so.

## What this engine is for

The engine takes one camera image of any size (a BGR picture, see `Image` in
`src/common/types.h`) and returns a list of `Face` records. Each `Face` has a
box around the face, a score between 0 and 1, five landmarks (right eye, left
eye, nose tip, right mouth corner, left mouth corner) and the head rotation
(pitch, yaw and roll in degrees). All coordinates are pixels of the image that
was passed in. Two families of detectors hide behind the one interface
`FaceDetector`: the deep-learning detectors YuNet and SCRFD
(`detector_deep.cpp`), and the classical Haar cascade detector, also called
Viola-Jones (`detector_haar.cpp` and `haar_cascade.cpp`). `create_detector` picks
the family from the model profile (`detector.type = yunet`, `scrfd` or `haar`). The
engine has two more parts that work on the result: a head-pose estimator that turns
the five landmarks into three angles (`pose.cpp`), and a tracker that gives every
face an ID that stays the same from frame to frame (`tracker.cpp`). A helper called
`refine_face` takes a second, zoomed-in look at one face to get more exact landmarks
and therefore a more exact pose. The detector that runs `refine_face` in the pipeline is
the profile's **pose detector**, built by `create_pose_detector`: normally a second
instance of the profile's own detector, but a profile can name another one with
`pose.detector_profile` (the SCRFD profiles let YuNet measure the pose, because the
landmarks of SCRFD give a noisier pitch). The main detector still finds the faces in whole
frames.

### How the files fit together

```
 camera image
      |
      v
 FaceDetector::detect()          detector.h  (interface)
   |-- DeepDetector              detector_deep.cpp   (YuNet, SCRFD; uses pose.cpp for the angles)
   '-- HaarDetector              detector_haar.cpp   (uses haar_cascade.cpp)
      |
      v   std::vector<Face>
 FaceTracker::update()           tracker.cpp         (adds a track ID to each face)
      |
      v
 AuthPipeline (src/pipeline)  --> refine_face()      detector.cpp  (second look, exact pose; run by the
                                                                   pose detector: create_pose_detector())
```

| File | Role |
|---|---|
| `detector.h`, `detector.cpp` | the interface, the factories `create_detector` and `create_pose_detector`, `refine_face`, `iou`, `non_max_suppression` |
| `detector_deep.cpp` | YuNet and SCRFD (neural networks): run the network, decode its numbers into boxes and landmarks |
| `detector_haar.cpp` | the classical detector: find the face and the eyes with Haar cascades, estimate an approximate pose |
| `haar_cascade.h`, `haar_cascade.cpp` | the Viola-Jones algorithm and the loader of OpenCV cascade XML files |
| `pose.h`, `pose.cpp` | head pose (pitch, yaw, roll) from the five landmarks, and the measured constants |
| `tracker.h`, `tracker.cpp` | follows faces from frame to frame |

### Glossary

| Word | Meaning here |
|---|---|
| bounding box, box | the rectangle around a face: left/top corner (`x`, `y`) plus `w` (width) and `h` (height), in pixels. `y` grows DOWNWARD, as in every image |
| landmark | one named point of the face. There are 5: right eye, left eye, nose tip, right mouth corner, left mouth corner |
| "right eye" | the eye on the right side of the PERSON, which is on the LEFT side of the image (`landmarks[0]`); `landmarks[1]` is the eye on the right side of the image |
| score | how sure the detector is that a face is there (0 to 1; larger is better) |
| IoU | "intersection over union": area where two boxes overlap divided by the area covered by at least one of them. 0 means no overlap, 1 means identical boxes |
| NMS | "non-maximum suppression": when several boxes cover the same face, keep only the best one |
| stride, anchor | the neural networks look at the picture on a grid. The stride is the grid spacing in pixels (8, 16 or 32). An anchor is one grid position (here: the corner of a grid cell) where the network makes one prediction |
| letterbox | shrink the picture to fit into a square without changing its shape and fill the remaining space with gray bars, like a wide movie on a square screen. `letterbox_fit` does the same into the smallest rectangle whose sides are multiples of 32 (640x384 for 1920x1080), so there is much less gray |
| grid, channels x rows x columns (`chw`) | the raw output of a network layer: `channels` pictures of `rows x columns` numbers, one picture after the other. SCRFD with `detector.layout = chw` is decoded from these (`decode_scrfd_chw`); the `flat` layout is the same numbers already reshaped into one row per anchor |
| integral image | a table in which each entry is the sum of all pixels above and to the left of it; with it the sum of ANY rectangle costs only 4 reads |
| cascade, stage, stump | a Haar cascade is a list of stages; each stage adds up the answers of many tiny yes/no tests called stumps; if a stage's total is too low the window is rejected at once |
| pitch, yaw, roll | head nodding (looking up/down), head turning (left/right), head tilting toward a shoulder; all in degrees |
| track | a face that is followed over several frames and has an ID |

### Data types used by the engine (`src/common/types.h`)

| Type | Fields | Meaning |
|---|---|---|
| `Rect` | `x, y, w, h` (floats) | box; `right()` = `x + w`, `bottom()` = `y + h`, `area()` = `w * h` (0 when `w` or `h` is not positive) |
| `Point` | `x, y` | one point in pixels |
| `Pose` | `pitch, yaw, roll`, `valid` | head rotation in degrees; `valid` is false when no landmarks were available |
| `Face` | `box`, `score`, `landmarks[5]`, `has_landmarks`, `pose` | one detected face. Haar faces have `has_landmarks == false` and an invalid pose when the eyes could not be found |
| `Track` | `id`, `face`, `missed_frames`, `age_frames`, `updated`, `vx`, `vy` | a followed face; `face` is the latest detection, `vx`/`vy` the smoothed speed of the box in pixels per frame |
| `Image` | `width, height, channels, stride, data` | 8-bit picture, BGR (3 channels) or gray (1 channel), stored row by row |

### Angle sign conventions (from `pose.h`)

| Angle | Positive when | Zero when |
|---|---|---|
| roll | the head is tilted clockwise as seen on the screen (the eye on the right side of the image is lower) | the eyes are level |
| yaw | the nose points toward the RIGHT side of the image | the nose points at the camera |
| pitch | the person looks UP (the chin rises) | the head is not nodded |

Each header and source file also has an include guard macro (`FA_DETECTOR_H`,
`FA_HAAR_CASCADE_H`, `FA_POSE_H`, `FA_TRACKER_H`). It only stops a header from being
read twice by the compiler and has no other effect.

---

## `detector.h` and `detector.cpp`

The header declares the interface that all detectors implement, the two factory functions
(`create_detector` and `create_pose_detector`) and three helpers. `detector.cpp` implements
the factories and the helpers.
`create_deep_detector` and `create_haar_detector` are declared here but defined in
`detector_deep.cpp` and `detector_haar.cpp`; they are described in those sections.

| Function | Purpose |
|---|---|
| `FaceDetector::~FaceDetector` | virtual destructor so a detector can be deleted through a base pointer |
| `FaceDetector::detect` | find all faces in one image (pure virtual) |
| `FaceDetector::reset` | forget what was remembered from earlier frames (virtual, does nothing by default) |
| `FaceDetector::name` | short text for logs (pure virtual) |
| `FaceDetector::type` | the profile's detector type, used to select the pose model (pure virtual) |
| `FaceDetector::runs_on_cpu` | true when the detector runs on the CPU (virtual, default `true`; every detector of the project does) |
| `create_detector` | build the detector described by a model profile |
| `create_pose_detector` | build the detector that does the pose check (`refine_face`) for a profile: the one named by `pose.detector_profile`, or a second instance of the profile's own detector |
| `refine_face` | second, zoomed-in look at one face for exact landmarks and pose |
| `iou` | overlap of two boxes, 0 to 1 |
| `non_max_suppression` | keep the best box of every group of overlapping boxes |
| `create_deep_detector`, `create_haar_detector` | declared here, see `detector_deep.cpp` and `detector_haar.cpp` |

### `FaceDetector::~FaceDetector`

```cpp
virtual ~FaceDetector() {}
```

**What it does.** Makes it safe to destroy a detector through a pointer of type
`FaceDetector`. `create_detector` and `create_pose_detector` return a `std::unique_ptr<FaceDetector>`, so
without a virtual destructor the derived class (`DeepDetector` or `HaarDetector`)
would not be cleaned up correctly.

**How it works.** It is empty; the members of the derived classes clean up after
themselves.

**Used by.** Implicitly whenever the `unique_ptr` returned by `create_detector` or
`create_pose_detector` goes out of scope (for example `AuthPipeline::detector_` and
`AuthPipeline::pose_detector_` in `src/pipeline/auth_pipeline.cpp`).

### `FaceDetector::detect`

```cpp
virtual bool detect(const Image& frame, std::vector<Face>& faces, bool accurate = false) = 0;
```

**What it does.** Finds the faces in one BGR camera image of any size. `faces` is
cleared first and then filled; the boxes and landmarks are in the pixel coordinates of
`frame`. Returns `true` when the detector ran (even if it found no face, then `faces`
is empty) and `false` when it could not run. The interface comment says the detector
resizes the image itself, so the caller never needs to.

The third parameter `accurate` (default `false`) asks for the most exact result even if
it is slower: a detector that has a fast mode for small images must then use its
full-size network. Callers that pass only two arguments get `accurate = false`;
`refine_face` always passes `true` (both of its calls, see `detector.cpp` lines 62 and
87). Detectors that have only one mode ignore the flag (the Haar detector does).

**How it works.** The interface only promises the behaviour; the work is done by the
two implementations: `DeepDetector::detect` (`detector_deep.cpp`) and
`HaarDetector::detect` (`detector_haar.cpp`). Differences that a caller should
know about: the deep detector refuses an image that is not 3-channel and returns
`false`; the Haar detector accepts a gray image too and returns `false` only for an
empty image. Both limit the result to `FA_MAX_FACES` faces. Both keep internal
buffers (the Haar detector also keeps memory of the previous frame), so one detector
object must not be called from two threads at the same time (this is not written in the
code; it follows from the shared buffers). A C++ detail for beginners: the default
value `= false` of a virtual function is chosen at compile time from the type of the
variable used in the call, not from the real object. That is harmless here because the
interface and both implementations all declare the same default `false`.

**Used by.**
- `src/pipeline/auth_pipeline.cpp`: `AuthPipeline::process` (line 409) and
  `AuthPipeline::extract_from_image` (line 539), both with the default
  `accurate = false` and both with the MAIN detector (never the pose detector: that one
  only runs `refine_face`).
- `src/detect/detector.cpp`: `refine_face` (lines 62 and 87), with `accurate = true`.
- Tools: `tools/detect_image.cpp`, `tools/eval_detect.cpp`, `tools/eval_features.cpp`,
  `tools/calibrate_pose.cpp`, `tools/calibrate_lbp.cpp`, `tools/roll_probe.cpp`,
  `tools/gen_video.cpp`; the benchmark headers `tools/bench_frames.h` (`make_frame`, line 28, to find
  the face box of each portrait), `tools/bench_detect_part.h` (`bench_detection`, lines 29, 31, 38 and
  40, and `bench_pose_check`, line 56) and `tools/bench_extract_part.h` (`bench_extraction`, line 24),
  which are used by the programs `bench`, `bench_detect` and `bench_extract`; tests
  `tests/test_pipeline.cpp` (lines 245, 291 and 496; line 291 is the comparison of the fitted rectangle with the square,
  which also calls `detect` of a second detector made from `dev_noncommercial_square.ini`) and `tests/test_feature.cpp`
  (line 66, to get the landmarks of the portraits).

### `FaceDetector::reset`

```cpp
virtual void reset() {}
```

**What it does.** Tells the detector to forget everything it remembers from earlier
frames. Only a detector that follows faces from frame to frame has something to forget;
the default version in the interface does nothing, so the deep detectors simply inherit
it. It takes no arguments and returns nothing.

**How it works.** It is a normal virtual function with an empty body (not a pure virtual
one), so a derived class may override it but does not have to. Only `HaarDetector`
overrides it (see `HaarDetector::reset`): it clears the remembered face boxes so that the
next `detect` call is a full scan.

**Used by.** `AuthPipeline::reset_tracking` (`src/pipeline/auth_pipeline.cpp`, line 250),
right after `tracker_.reset()`, so that a new tracking session starts without old
detector memory (only the main detector `detector_` is reset, not a separate pose detector). No other caller was found by searching `src/`, `tools/` and `tests/`.

### `FaceDetector::name`

```cpp
virtual const char* name() const = 0;
```

**What it does.** Returns a short text for logs, for example `"yunet/onnxruntime"`,
`"scrfd/ncnn"` or `"haar/classical"`.

**How it works.** Implemented by `DeepDetector::name` (built from the detector type
and the backend that loaded the network) and `HaarDetector::name` (a fixed string).

**Used by.** `tools/detect_image.cpp` (line 42), `tools/eval_detect.cpp` (line 61),
`tools/bench.cpp` (line 40), `tools/bench_detect.cpp` (line 32), `tools/eval_features.cpp` (line 72,
and line 73 for the pose detector), `tools/calibrate_pose.cpp` (line 65, and line 66 for the pose
detector), `tests/test_feature.cpp` (line 53, the detector that gives the landmarks),
`tests/test_pipeline.cpp` (line 227, and line 231 for the pose detector).

### `FaceDetector::type`

```cpp
virtual const char* type() const = 0;
```

**What it does.** Returns the detector type of the profile: `"yunet"`, `"scrfd"` or
`"haar"`. The text is used to choose the right pose model.

**How it works.** `DeepDetector::type` returns the profile's `detector_type` string;
`HaarDetector::type` returns the fixed text `"haar"`.

**Used by.** `refine_face` (`src/detect/detector.cpp`, lines 120 and 131): it compares the
text with `"haar"` and, for the other detectors, appends `"-refined"` and passes the
result to `pose_model_for`. No other caller was found by searching the source folders.

### `FaceDetector::runs_on_cpu`

```cpp
virtual bool runs_on_cpu() const { return true; }
```

**What it does.** Answers "does this detector run on the CPU?": `true` when it does, `false` when
it would use a GPU. **Every detector of this project runs on the CPU** (the target PC has no
GPU), so the answer is always `true`; the function makes that a checked statement. It takes no
input.

**How it works.** A virtual function with a default body, not a pure virtual one. The default
`true` is the right answer for the classical Haar detector, which is plain C++ code on the CPU
(`HaarDetector` does not override it). `DeepDetector` overrides it and asks its network or
networks (`Net::runs_on_cpu`, see `common.md`): the main network, and the small 320 x 320
network too when one was loaded. The detector's answer says what the code asked for; that no GPU
library is loaded into the process is checked from the outside by the tests
(`tests/cpu_only_check.h`).

**Used by.** `AuthPipeline::runs_on_cpu` (the main detector and the pose detector, and in
worker mode the worker's pose detector), `tools/eval_footage.cpp` (`main`, the line `all engines run
on the CPU`), `tests/test_feature.cpp` (`main`, the check that the extractor and the detector run on
the CPU).

### `create_detector`

```cpp
std::unique_ptr<FaceDetector> create_detector(const ModelProfile& profile);
```

**What it does.** The factory: reads `profile.detector_type` and builds the matching
detector. Returns the detector, or `nullptr` when the type is unknown (the message
`detector: unknown detector.type '...'` is printed) or when the model cannot be
loaded (the message is printed by the builder).

**How it works.**
1. `"yunet"` or `"scrfd"` goes to `create_deep_detector`.
2. `"haar"` goes to `create_haar_detector`.
3. Anything else prints the error and returns `nullptr`.

Callers must test the returned pointer before using it.

**Used by.** `AuthPipeline::init` (`src/pipeline/auth_pipeline.cpp`, line 197, the main
detector); `create_pose_detector` (`detector.cpp`, lines 144 and 156); tools
`detect_image`, `eval_detect` (two profiles), `eval_features`, `calibrate_pose`,
`calibrate_lbp`, `roll_probe`, `gen_video`, `bench_detect` (line 28), `bench_extract` (line 27, only
to find the face landmarks); tests `tests/test_detect_units.cpp` (line 190) and
`tests/test_feature.cpp` (line 50, only to find the face landmarks).

### `create_pose_detector`

```cpp
std::unique_ptr<FaceDetector> create_pose_detector(const ModelProfile& profile);
```

**What it does.** Builds the detector that does the **pose check** for a profile, that is
the detector that runs `refine_face` and so measures pitch, yaw and roll. Normally this is
a second instance of the profile's own detector. A profile can name another one with the
key `pose.detector_profile = <path of another profile .ini>` (field
`ModelProfile::pose_detector_profile`); then the detector of THAT profile is built instead.
The shipped SCRFD profiles (`models/dev_noncommercial.ini`,
`models/dev_noncommercial_square.ini` and `models/dev_noncommercial_onnx.ini`) name `models/permissive.ini`, so for them the pose
detector is YuNet. The reason (from the comments in `profile.h` and in the `.ini` file):
the head rotation is read from the 5 landmarks and the landmarks of SCRFD give a much noisier
pitch than those of YuNet, so with SCRFD only 28% of straight-on portraits passed the +-5
degree gate (YuNet: 57%); SCRFD still finds the faces, YuNet only measures the pose.

The result always has its **own networks**, separate from the objects returned by
`create_detector`, so it may be used from another thread than the main detector (the
detectors keep internal buffers and are not thread-safe). It gets the thread count
(`runtime.threads`) of the `profile` passed in. Returns the detector, or `nullptr` with a
message on `std::cerr` when the named profile cannot be read, when the named profile
itself names a pose profile, or when a model cannot be loaded (the message of
`create_detector` or of the builder).

**How it works.**
1. If `profile.pose_detector_profile` is empty: return `create_detector(profile)`, a new
   instance of the profile's own detector. (This is also what happens for `permissive.ini`
   and `classical.ini`, which name no pose detector.)
2. Otherwise read the named file into a new `ModelProfile` with
   `load_profile(profile.pose_detector_profile, profile.root, pose)`. The path was already
   resolved against the project root when the key was read (see `load_profile`), and
   `profile.root` is passed on so that the relative model paths of the named file start at
   the same project root. If the file cannot be read, print
   `detector: cannot use pose.detector_profile '<path>'` (after the message of `load_profile`)
   and return `nullptr`.
3. If the named profile has a `pose.detector_profile` of its own, print
   `detector: <path> names a pose.detector_profile itself; only one level is allowed` and
   return `nullptr` (no chains, so there is no loop to guard against).
4. `pose.threads = profile.threads`: the caller decides how many CPU threads the networks
   may use (the pipeline's worker passes `FA_WORKER_NETWORK_THREADS`).
5. Return `create_detector(pose)`. Only the `detector.*` keys of the named profile are
   used (type, backend, model, score, the small model, the Haar files); its recognizer
   settings and its threshold do not matter here.

A consequence for the head-pose numbers: `refine_face` picks the pose model by the type of
the detector it is given (`<type>-refined`, see `pose_model_for`), so with a YuNet pose
detector the constants `yunet-refined` are used even though the profile's own detector is
SCRFD. A tool that overrides `detector_type` after loading the profile (the `--type` option
of `calibrate_pose` and `eval_features`) must also clear `pose_detector_profile`, otherwise
the named detector would still do the pose check; those tools do.

**Used by.** `AuthPipeline::init` (`src/pipeline/auth_pipeline.cpp`, line 201 for the pose
detector of the main thread, only when the profile names one, and line 234 for the worker's,
in worker mode only, with the profile copy that has `FA_WORKER_NETWORK_THREADS` threads, whether
or not the profile names a pose detector); tools
`calibrate_pose` (line 63, with `--refine`), `eval_features` (line 69), `roll_probe`
(line 19) and `bench_detect` (line 31, only when the profile names a pose detector); test `tests/test_detect_units.cpp` (lines 185, 188, 197 and 202, the section
"the pose detector of a profile").

### `iou`

```cpp
float iou(const Rect& a, const Rect& b);
```

**What it does.** Returns how much two boxes overlap: the area they have in common
divided by the area covered by at least one of them. 0 = no overlap, 1 = identical.
Returns 0 when the union has no area.

**How it works.**
1. The overlap rectangle runs from the larger of the two left edges to the smaller
   of the two right edges (same for top and bottom). If the edges cross, the width or
   height is clamped to 0.
2. `inter` = overlap width times overlap height.
3. `uni` = area of `a` plus area of `b` minus `inter` (the overlap would otherwise be
   counted twice).
4. The answer is `inter / uni`.

Example (checked in `tests/test_detect_units.cpp`): two 10x10 boxes shifted by 5
pixels overlap in 5x10 = 50, the union is 100 + 100 - 50 = 150, so IoU = 1/3.

**Used by.** `non_max_suppression` and `refine_face` (both in `detector.cpp`),
`FaceTracker::update` (`tracker.cpp`, line 39), `tools/eval_detect.cpp` (line 90),
`tests/test_pipeline.cpp` (line 168), `tests/test_detect_units.cpp` (line 115).

### `non_max_suppression`

```cpp
void non_max_suppression(std::vector<Face>& faces, float iou_threshold);
```

**What it does.** Removes duplicate detections. A neural network usually reports the
same face several times from neighbouring grid cells; this keeps only the best-scoring
box of each group of overlapping boxes. The list is changed in place; afterwards it is
sorted from the highest to the lowest score.

**How it works.**
1. Sort the faces by score, best first.
2. Walk through the sorted list. For each candidate, compare its box with every box
   already kept.
3. If the IoU with any kept box is greater than `iou_threshold` (strictly greater), the
   candidate is dropped; otherwise it is kept.
4. Replace the list by the kept faces.

The deep detector calls it with `FA_DETECT_NMS_IOU` (default 0.3), so boxes that
overlap by more than 30 percent count as the same face. The cost grows with the square
of the number of candidates, which is small because most grid cells are discarded by
the score threshold before this step.

**Used by.** `DeepDetector::detect` (`detector_deep.cpp`, line 102);
`tests/test_detect_units.cpp` (line 113).

### `refine_face`

```cpp
bool refine_face(FaceDetector& detector, const Image& frame, const Face& coarse, Face& refined);
```

**What it does.** Takes a second, zoomed-in look at ONE face to get more exact
landmarks, and with them a more exact head pose. Inputs: the `detector` to use, the full
camera image `frame`, and the `coarse` detection (box in camera pixels). Output:
`refined`, which holds the box, the landmarks and the pose in the coordinates of the
camera image. Callers in the pipeline (and the tests and tools that imitate it) pass the
**pose detector** here (`AuthPipeline::pose_detector()`, built by `create_pose_detector`):
the profile's `pose.detector_profile` detector, which is YuNet for the SCRFD profiles, or
the profile's own detector when the profile names none. Any detector works; the one passed
decides which landmarks, and so which pose, come out (step 6). Returns `false` (and leaves
`refined` untouched) when the refinement
could not be done: empty frame, a box smaller than 8x8 pixels, a cut-out smaller than
8x8 pixels after clipping to the image, the detector failing, no face found in the
enlarged cut-out, or no found face overlapping the expected position with IoU above
0.3.

**Why it exists.** A landmark network is least exact for small faces. In the 640x640
network image a face that is 120 pixels wide in the camera picture can be only 60
pixels wide (see `letterbox` in `src/common/image_ops.h`: a 1280x1080 camera image is
scaled by 0.5). The head angles are computed from tiny differences between
landmarks. As an illustration (calculated from the formulas in `pose.cpp`, not
measured): a yaw of 5 degrees moves the nose tip sideways by only about 0.039 eye
distances, which is about 1.2 pixels when the eyes are 30 pixels apart. A landmark
error of one pixel would therefore be as large as the whole angle tolerance. So the
face is cut out, enlarged, and detected again.

**How it works.**
1. **Cut out.** The region is the coarse box enlarged by half its width and half its
   height on every side (so the region is twice as wide and twice as high as the box).
   It is clipped to the camera image, rounded outward to whole pixels (floor for the
   left/top corner, ceil for the right/bottom corner) and copied with `crop`.
2. **Enlarge.** `zoom = max(1, FA_REFINE_FACE_WIDTH / coarse.box.w)`. If that value is
   1.01 or less, `zoom` is set to exactly 1.0 (a change that small is not worth a resize,
   and the true scale stays 1). The cut-out is enlarged by `zoom` with `resize_bilinear`
   only when `zoom` is greater than 1.0; otherwise the cut-out is used as it is. It is
   never shrunk. Because `zoom` is exactly 1.0 in the not-resized case, the later
   conversions (the expected position in step 3 and the mapping back in step 5) use the
   scale that was really applied. With the defaults the enlarged
   picture is about 640 pixels wide (2 x 320), which happens to equal
   `FA_DETECT_INPUT_SIZE`; this is the author's observation, the code does not state it.
3. **Detect again.** `detector.detect(big, found, true)` is run on the enlarged picture.
   The last argument is the `accurate` flag: it forces a detector that has a fast
   small-image mode (see `DeepDetector::detect`) to use its full-size network, because
   the whole point of this step is the most exact landmarks. (The author's inference:
   the enlarged picture often has a longer side of 640 pixels or less, which would
   otherwise qualify for the small network. The code comment only says that
   `refine_face` uses the flag.) The expected
   position of the face in this picture is `(coarse.box - region corner) * zoom`. Of all
   detected faces the one with the highest IoU against the expected box is chosen; its
   IoU must exceed 0.3 (a fixed number in the code).
4. **Rotation compensation** (see below).
5. **Back to camera coordinates.** For the box and every landmark:
   `x_camera = x / zoom + region.x`, `y_camera = y / zoom + region.y`.
6. **Pose.** Two cases, chosen by `detector.type()`:
   - Every detector except Haar (`"yunet"`, `"scrfd"`): `refined.pose` is calculated with
     `estimate_pose` from the final landmarks, using the pose model named
     `<detector type>-refined` (`"yunet-refined"` or `"scrfd-refined"`), where the type is
     that of the `detector` passed in, i.e. of the pose detector. These constants
     were measured on landmarks from this very step (see `pose_model_for`). For a SCRFD
     profile that names YuNet as its pose detector the model is therefore `"yunet-refined"`.
   - Haar (`detector.type() == "haar"`): the landmark formulas are NOT used, because the
     Haar detector only finds the eyes and places nose and mouth like an average face (see
     `HaarDetector::find_eyes_and_pose`), so the formulas would read yaw and pitch out of
     that assumption and not out of the picture. Instead `refined.pose` is a copy of the
     pose of the face that was finally chosen (`best->pose`, the detector's own yaw and
     pitch estimated from where the eyes sit in the face box), and only the roll is
     replaced by the angle of the eye line after mapping back,
     `atan2(landmarks[1].y - landmarks[0].y, landmarks[1].x - landmarks[0].x)` in
     degrees. `pose.valid` becomes `best->has_landmarks`, so a Haar face whose eyes were not
     found gets an invalid pose.

**Rotation compensation (step 4) in detail.** A landmark network is trained mostly on
upright faces; for a strongly tilted head it reads the tilt with a bias, the comment
in the code says it under-reports it. The remedy is to turn the picture so the face
stands upright, detect once more, and add the turn back:
1. It runs only if the first detection has a valid pose and `|roll| >= 3` degrees. For
   smaller tilts the first detection is kept.
2. The picture is turned about the centre of the first detected box by the measured
   roll, in the direction that undoes the tilt (a head tilted clockwise, positive roll,
   is turned back counter-clockwise; this direction was derived from the matrix, the
   code comment only says "turn by +theta" for the matrix that maps output pixels to
   input pixels). The six numbers `to_source` describe this turn
   for `warp_affine`, which for every pixel of the new picture reads the old picture at
   the position given by the matrix. Pixels whose source lies outside the old picture
   stay black. The turned picture has the same size as the enlarged one.
3. `detector.detect(turned, again, true)` runs on the turned picture (again with
   `accurate = true`). A found face is accepted when its IoU
   with the first box exceeds 0.3 and it has landmarks.
4. The five landmarks found in the turned picture are moved back into the un-turned
   picture by applying the SAME matrix to them (this is the transform from the turned
   picture to the original one). The box is not taken from the second detection; the box of the first detection
   is kept.
5. If the second detection finds nothing acceptable, the result of the first detection
   is used silently.

Because the pose in step 6 is calculated from the landmarks after they were moved back
(and, for Haar, the roll is taken from the mapped eye line), the roll that comes out is
automatically "the turn plus the small remaining tilt". Yaw and pitch are not changed by
turning the picture in its own plane, because `landmark_geometry` removes the roll before
measuring.

**Behaviour with the Haar detector (note).** `refine_face` works with any detector. For
`HaarDetector` the nose and mouth landmarks are not detected, they are placed like an
average face (see `find_eyes_and_pose`), which is why step 6 takes only the roll from the
refined landmarks and keeps the detector's own yaw and pitch. With this exception the
pose gate no longer rejects every Haar face: `tests/test_pipeline.cpp` run with
`models/classical.ini` now stores crops (reported by the maintainers; not re-run for this
document). Yaw and pitch of a Haar face stay rough estimates (see
`find_eyes_and_pose`). If `has_landmarks` is false (eyes not found) the function still
returns `true`, but the pose is invalid. Also, `refine_face` calls the Haar detector on
pictures of a different size, which makes the next normal frame a full scan (see
`HaarDetector::detect`).

**Used by.** the pipeline function `run_gate_job` (`src/pipeline/auth_pipeline.cpp`,
line 61), which `AuthPipeline::process` runs for every unverified track whose crop is due,
either inline (line 469, with the pipeline's pose detector, `pose_detector()`) or on the
worker thread (line 170, with the worker's own pose detector instance, so two threads never
share one detector). Both pass the POSE detector, not the main detector; the pose detector
is a separate YuNet instance when the profile names one (`pose.detector_profile`), and the
main detector itself otherwise. It does
not pass the whole camera frame: `make_job` copies a sub-image around the face (the track's
box enlarged by one box width and height on every side, clipped to the frame) and
`refine_face` is called on that copy, with the coarse face given in the copy's coordinates;
this works because `refine_face` itself only needs a margin of half a box around the face
(step 1), and every coordinate it returns is then relative to the sub-image, not to the
camera frame. Other callers: tools `tools/calibrate_pose.cpp` (line 80, with
`--refine`, with the pose detector), `tools/eval_features.cpp` (line 96, pose detector),
`tools/roll_probe.cpp` (line 39, pose detector), `tools/bench_detect_part.h` (line 61, in
`bench_pose_check`: the programs `bench` and `bench_detect` hand it the pose detector, `bench`
`pipeline.pose_detector()` when that is not the main detector, `bench_detect` the one it created,
or nothing, which means the main detector), `tools/calibrate_lbp.cpp` (line 58) and `tools/gen_video.cpp`
(line 121), both of which pass the main detector, not a pose detector (`gen_video` always loads
`models/permissive.ini`, which names none; `calibrate_lbp` uses the profile it is given, so
with a profile that names a pose detector it would still refine with the main detector);
test `tests/test_pipeline.cpp` (lines 255 and 497, with `pipeline.pose_detector()`).

---

## `detector_deep.cpp`

The deep-learning detectors. YuNet and SCRFD are different networks but work in the
same way (the long comment at the top of the file explains it):
1. The camera image is shrunk into a square (640x640, `FA_DETECT_INPUT_SIZE`) without
   changing its shape. Optionally, for small camera images, a faster second network with
   a smaller square (320x320 by default) is used instead, see below. **Exception:** SCRFD
   with `detector.layout = chw` (`models/dev_noncommercial.ini`) uses the smallest
   rectangle whose sides are multiples of 32 instead of the square (`letterbox_fit`,
   `common.md`): 640x384 for a 1920x1080 image, 640x544 for 1280x1080, 640x480 for 4:3 and
   still 640x640 for a square image. The scale is the same as with the square (the long
   side is 640), only the gray padding that the square would add above and below is left
   out, so 16:9 pictures cost 40% fewer pixels.
2. The network looks at the picture on three grids, with strides 8, 16 and 32. At
   stride 8 it makes one prediction for every 8x8 pixel cell (small faces), at stride 32
   for every 32x32 cell (large faces).
3. Each prediction says how sure it is that a face is there, how big the box is and
   where the 5 landmarks are, relative to its cell.
4. The predictions are converted into pixel coordinates of the original camera image
   and duplicates are removed.

**The optional small-image network.** A `DeepDetector` owns one main network `net_` and,
only if the profile asks for it, a second network `net_small_`. The second one is "the same
model with a smaller input square" (comment in the code; `models/permissive.ini` says it
is `models/permissive/yunet_320.onnx`, made from `yunet.onnx` by changing the input size
because the network is fully convolutional, and that faces below about 40 pixels are found
less reliably with it). It runs faster because the picture it looks at has a quarter of
the pixels. It is used when ALL of these are true (see `DeepDetector::detect`): the
profile has `detector.model_small` and it loaded, the caller did not ask for
`accurate = true`, and the longer side of the camera image is at most
`detector.small_max_side` pixels (default 640). The side of its input square is
`detector.small_size` (default 320). Because of this, the decode functions no longer
assume the 640 square: they receive the square's side as the parameter `size`.

The data members of `DeepDetector` are: `profile_` (copy of the profile), `is_yunet_`,
`net_`, `net_small_`, `prep_` (input conversion), `name_`, and two buffers reused for
every frame, `square_` (the letterboxed image; with `letterbox_fit` it is a rectangle,
the name was kept) and `outputs_` (the network outputs).

Everything is inside an anonymous namespace (private to this file) except
`create_deep_detector`.

| Function | Purpose |
|---|---|
| `kStrides` | the three grid spacings 8, 16, 32 |
| `DeepDetector::DeepDetector` | constructor, remembers the profile and which network it is |
| `DeepDetector::load` | load the network (and the optional small one) and set up the input conversion |
| `DeepDetector::name` | text such as `"yunet/onnxruntime"` |
| `DeepDetector::type` | the profile's detector type |
| `DeepDetector::runs_on_cpu` | true when the main network (and the small one, if loaded) run on the CPU |
| `DeepDetector::detect` | the full detection of one image; chooses the big or the small network |
| `DeepDetector::decode_yunet` | turn the YuNet numbers of one stride into faces |
| `DeepDetector::decode_scrfd` | turn the SCRFD numbers of one stride into faces (layout `flat`, square picture) |
| `DeepDetector::decode_scrfd_chw` | the same for the raw SCRFD outputs of layout `chw` and a picture of any multiple-of-32 size |
| `DeepDetector::to_original` | undo the letterbox: network picture (square or rectangle) back to camera pixels |
| `create_deep_detector` | factory called by `create_detector` |

### `kStrides`

```cpp
const int kStrides[3] = {8, 16, 32};
```

**What it does.** The three grid spacings in pixels. `level` 0, 1, 2 in the decode
functions selects stride 8, 16, 32. A grid has `size / stride` columns and as many rows,
where `size` is the side of the network input square. For the 640x640 input the grids
have 80x80 = 6400, 40x40 = 1600 and 20x20 = 400 cells; for the 320x320 small network
they have 40x40 = 1600, 20x20 = 400 and 10x10 = 100 cells. For a rectangle (layout `chw`) the
columns are `width / stride` and the rows `height / stride`: 640x384 gives 80x48 = 3840,
40x24 = 960 and 20x12 = 240 cells (the test in `tests/test_common.cpp` checks 48x80,
24x40 and 12x20 as rows x columns).

**Used by.** `decode_yunet`, `decode_scrfd` and `decode_scrfd_chw`.

### `DeepDetector::DeepDetector`

```cpp
DeepDetector(const ModelProfile& profile, bool is_yunet) : profile_(profile), is_yunet_(is_yunet) {}
```

**What it does.** Stores a copy of the model profile and a flag: `true` for YuNet,
`false` for SCRFD. It does not load anything yet; `load()` does that.

**Used by.** `create_deep_detector`.

### `DeepDetector::load`

```cpp
bool load() {
```

**What it does.** Loads the neural network (and the optional small-image network) and
chooses how a picture is converted into numbers for it. Returns `false` if the MAIN
network could not be created. A failure of the small network does not make `load()` fail.

**How it works.**
1. If the profile's backend is `"ncnn"`, the model file name (ending in `.param`) is
   turned into the matching `.bin` name and `create_ncnn_net` is called with the
   input name, the list of output names and the number of threads from the profile.
   Otherwise (`"onnx"`) `create_onnx_net` is called; the ONNX file contains the names
   itself.
2. Small network: only when `profile.detector_model_small` is not empty AND the backend
   is `"onnx"` (the small network is only supported on ONNX Runtime), `net_small_` is
   created from that file with `create_onnx_net`. If it cannot be loaded the message
   `detector: cannot load <file> (ignored)` is printed and the detector simply works
   without it (`net_small_` stays empty).
3. Input conversion (`InputPrep`, see `src/common/inference.h`; the network input is
   `(pixel - mean) * norm`). It is shared by both networks:
   - YuNet: the plain BGR values 0..255 (nothing to set).
   - SCRFD: red and blue swapped (`swap_rb`), `mean = 127.5`, `norm = 1/128` for each
     channel, i.e. `(pixel - 127.5) / 128`.
4. `name_` is built as `"yunet/"` or `"scrfd/"` plus the backend name reported by the
   main network (`"ncnn"` or `"onnxruntime"`). The name does not say whether the small
   network exists.

**Used by.** `create_deep_detector`.

### `DeepDetector::name`

```cpp
const char* name() const override { return name_.c_str(); }
```

**What it does.** Returns the text built in `load()`, for logs and tool output.

**Used by.** see `FaceDetector::name`.

### `DeepDetector::type`

```cpp
const char* type() const override { return profile_.detector_type.c_str(); }
```

**What it does.** Returns `"yunet"` or `"scrfd"` (the profile's `detector.type`).

**Used by.** see `FaceDetector::type` (`refine_face`).

### `DeepDetector::runs_on_cpu`

```cpp
bool runs_on_cpu() const override { return net_ && net_->runs_on_cpu() && (!net_small_ || net_small_->runs_on_cpu()); }
```

**What it does.** Returns `true` when every network of this detector runs on the CPU: the main
network, and the small 320 x 320 network when one was loaded. It returns `false` if the main
network does not exist (a detector whose `load()` failed is never handed out by
`create_deep_detector`, so that case is not seen from outside). All YuNet and SCRFD networks of
the project do run on the CPU: YuNet on ONNX Runtime, SCRFD on ncnn (Vulkan switched off) or on
ONNX Runtime (no GPU provider).

**How it works.** Asks `net_->runs_on_cpu()` and, if `net_small_` exists, `net_small_->runs_on_cpu()`
(see `Net::runs_on_cpu` in `common.md`) and combines the answers with "and".

**Used by.** Through `FaceDetector::runs_on_cpu` (see there).

### `DeepDetector::detect`

```cpp
bool detect(const Image& frame, std::vector<Face>& faces, bool accurate = false) override {
```

**What it does.** Finds all faces in a camera image. Returns `false` when the image is
empty or does not have 3 channels, when the network run fails, or when the network gives
a different number of outputs than expected (12 for YuNet, 9 for SCRFD); in these cases
`faces` is empty. Otherwise it returns `true` with 0 to `FA_MAX_FACES` faces, each with
box, score, five landmarks and pose. Boxes and landmarks are in pixels of `frame`.
`accurate = true` forbids the small network (see step 1).

**How it works.**
1. **Choose the network.** The small network is used when `net_small_` exists, `accurate`
   is `false` and `max(frame.width, frame.height) <= profile_.detector_small_max_side`
   (default 640). Then the input square has the side `profile_.detector_small_size`
   (default 320); otherwise the main network is used with the side `FA_DETECT_INPUT_SIZE`
   (640). The side is stored in the local variable `size`.
2. **Letterbox.** `letterbox` (in `image_ops.cpp`) shrinks or enlarges the image to fit
   a `size` x `size` square with the same aspect ratio, centres it and fills
   the rest with gray (value 114). It reports `scale` and the padding `pad_x`, `pad_y`.
   Example: a 1280x1080 image (longer side above 640, so the main network) gets
   `scale = 0.5`, becomes 640x540 and gets 50 gray rows above and below
   (`pad_y = 50`, `pad_x = 0`). A 640x480 image would qualify for the small network (if
   configured): `scale = 0.5`, it becomes 320x240 with 40 gray rows above and below.
   **For SCRFD with `detector.layout = chw`** the local flag `fitted` is true
   (`!is_yunet_ && profile_.detector_layout == "chw"`) and `letterbox_fit(frame, size, 32, ...)`
   is called instead: the same `scale` (the long side becomes `size` = 640), but the result
   is the smallest rectangle with both sides a multiple of 32. A 1920x1080 image becomes
   640x360 inside a 640x384 picture (`pad_y = 12`), a 1280x1080 image 640x540 inside
   640x544 (`pad_y = 2`), a 4:3 image 640x480 and a square image 640x640 as before. The
   gray padding that is left is only what is needed to reach a multiple of 32.
3. **Run the network** (the chosen one) on the picture with the input conversion set in
   `load()`. The reused buffers `square_` and `outputs_` avoid memory allocation on every
   frame. With `chw` the picture may be a rectangle: this works only because the model of
   that profile (`det_dyn.param`) has upsampling layers that scale by 2 instead of
   producing a fixed size.
4. **Decode.** For each of the three strides call `decode_yunet(s, size, found)`,
   `decode_scrfd(s, size, found)` or, when `fitted` is true,
   `decode_scrfd_chw(s, square_.width, square_.height, found)` (the real width and height
   of the picture that was given to the network).
   All of them only keep predictions whose score is at least `profile_.detector_score`
   (see the settings section) and append them to one list `found`.
5. **Remove duplicates.** `non_max_suppression(found, FA_DETECT_NMS_IOU)`. This is done
   in the network picture's coordinates (640x640, 320x320, or the fitted rectangle).
5. **Map back.** For each remaining face `to_original` converts to camera pixels. A face
   whose box is narrower or lower than `FA_MIN_FACE_PIXELS` (40) in the camera image is
   skipped.
6. **Pose.** `estimate_pose(f, pose_model_for(profile_.detector_type))`, the "coarse"
   pose model of this detector (not the "-refined" one).
7. Faces are added until `FA_MAX_FACES` (32) is reached. Because of the sorting inside
   NMS the faces with the best scores are the ones that are kept.

**Used by.** Through the `FaceDetector` interface; see `FaceDetector::detect`.

### `DeepDetector::decode_yunet`

```cpp
void decode_yunet(int level, int size, std::vector<Face>& out) {
```

**What it does.** Converts the YuNet output numbers of one stride (`level` 0, 1 or 2
for stride 8, 16, 32) into `Face` records and appends them to `out`, in the coordinates
of the network input square. `size` is the side of that square in pixels: 640
(`FA_DETECT_INPUT_SIZE`) for the main network or `detector.small_size` (default 320) for
the small one; it must be the size of the picture that was really given to the network,
because it decides how many grid cells to read. Before decoding it checks the sizes of
ALL four tensors of the stride: with `count = (size / stride)^2` cells, `cls` and `obj`
must each have at least `count` numbers, `bbox` at least `4 * count` and `kps` at least
`10 * count`. If any of them is too small it prints
`detector: output sizes do not fit a <size>x<size> YuNet network (profile / model mismatch)`
and returns without adding any face (the other strides are still decoded). This protects
against reading past the end of a buffer when the model and the profile do not match, for
example when the network outputs come in a different order.

**How it works.** YuNet gives 12 outputs in this fixed order: `cls_8, cls_16, cls_32,
obj_8, obj_16, obj_32, bbox_8, bbox_16, bbox_32, kps_8, kps_16, kps_32` (the decoder
depends on this order). For a stride `s` the picture is split into
`cols = size / s` columns and rows (80, 40, 20 for `size` 640); cell `i` lies at `row = i / cols`, `col = i % cols`.
There is ONE prediction per cell. For cell `i`:
1. Score = square root of (`cls` times `obj`), where `cls` ("is it a face?") and `obj`
   ("is there an object?") are first clamped into 0..1. If the score is below
   `profile_.detector_score` the cell is skipped. Most cells end here, so the rest of the
   work is only done for a few cells.
2. Box centre: `((col + bbox[0]) * s, (row + bbox[1]) * s)`. The network gives the
   offset inside the cell in units of the cell size.
3. Box size: `(exp(bbox[2]) * s, exp(bbox[3]) * s)`. The network gives the logarithm of
   the size relative to the stride; `exp` undoes the logarithm.
4. The box is stored as left/top corner plus size (centre minus half the size).
5. Landmark `k` (0 to 4): `((col + kps[2k]) * s, (row + kps[2k+1]) * s)`.
6. `has_landmarks` is set to true.

Example: stride 16, cell row 10, column 20, `bbox = (0.3, 0.4, 0.5, 0.6)` gives centre
`((20 + 0.3) * 16, (10 + 0.4) * 16) = (324.8, 166.4)` and size
`(exp(0.5) * 16, exp(0.6) * 16)`, about 26.4 x 29.2 pixels.

**Used by.** `DeepDetector::detect` (when `is_yunet_` is true).

### `DeepDetector::decode_scrfd`

```cpp
void decode_scrfd(int level, int size, std::vector<Face>& out) {
```

**What it does.** The same job for SCRFD. Appends the faces of one stride to `out` in the
coordinates of the network input square of side `size` (as for `decode_yunet`). With
`anchors = (size / stride)^2 * 2` it first checks that the score tensor has at least
`anchors` numbers, the bbox tensor at least `4 * anchors` and the landmark tensor at least
`10 * anchors`. If not, it prints
`detector: output sizes do not fit a <size>x<size> SCRFD network (check detector.outputs)`
and returns without adding any face. The comment in
`profile.h` describes the small network as "YuNet on ONNX Runtime only", so SCRFD is
normally decoded with `size = 640`; the code itself only checks for the ONNX backend and
would also load a small network for SCRFD if a profile gave one.

**How it works.** SCRFD gives 9 outputs: `score_8, score_16, score_32, bbox_8..32,
kps_8..32`. It makes TWO predictions ("anchors") per grid cell, so there are
`cols * cols * 2` anchors per stride (the two anchors of one cell are neighbours in the
list: cell = `i / 2`). For anchor `i`:
1. The score is used as it is (no square root). Below `profile_.detector_score` the
   anchor is skipped.
2. The anchor point `(ax, ay)` is the TOP-LEFT CORNER of its cell:
   `((cell % cols) * s, (cell / cols) * s)`. (YuNet instead adds offsets to the cell
   index; the effect is the same idea, a reference position plus an offset.)
3. The box is stored by the network as four distances from the anchor point to the
   left, top, right and bottom edge, in units of the stride:
   `x1 = ax - bbox[0] * s`, `y1 = ay - bbox[1] * s`, `x2 = ax + bbox[2] * s`,
   `y2 = ay + bbox[3] * s`. The box is `(x1, y1, x2 - x1, y2 - y1)`.
4. Landmark `k` is an offset from the anchor point: `(ax + kps[2k] * s, ay + kps[2k+1] * s)`.
5. `has_landmarks` is set to true.

**Used by.** `DeepDetector::detect` (when `is_yunet_` is false and the profile's layout is
`flat`; for `chw` it calls `decode_scrfd_chw`).

### `DeepDetector::decode_scrfd_chw`

```cpp
void decode_scrfd_chw(int level, int width, int height, std::vector<Face>& out) {
```

**What it does.** The same job as `decode_scrfd`, for SCRFD profiles with
`detector.layout = chw` (`models/dev_noncommercial.ini`), and for a network picture of any
`width` x `height` that is a multiple of 32 on both sides, not only a square. `level` 0, 1
or 2 is the stride 8, 16 or 32; `width` and `height` are the real size of the picture that
was given to the network (`square_.width`, `square_.height`). Appends the faces of one
stride to `out` in the coordinates of that picture. With `cols = width / stride`,
`rows = height / stride` and `cells = cols * rows` it first checks that the score tensor
has at least `2 * cells` numbers, the bbox tensor at least `8 * cells` and the landmark
tensor at least `20 * cells`. If not, it prints
`detector: output sizes do not fit a <width>x<height> SCRFD network (check detector.outputs and detector.layout)`
and returns without adding any face.

**How it works.** The nine outputs are the **raw outputs of the last convolutions** of the
three strides (the blobs `65, 85, 105` for the scores, `66, 86, 106` for the boxes and
`67, 87, 107` for the landmarks in `det_dyn.param`), not the reshaped `out0` to `out8` of the
`flat` layout. Each is a stack of `channels` pictures of `rows x columns` numbers, one picture
after the other (the wrapper copies an ncnn output channel by channel, `NcnnNet::run` in
`common.md`), so the number of channel `c` at the cell `cell` (row `cell / cols`, column
`cell % cols`) is at index `c * cells + cell`:

* score: 2 channels, one per anchor, the value **before** the sigmoid (a "logit");
* bbox: 8 channels, channel `a * 4 + k` is the distance number `k` of anchor `a`;
* kps: 20 channels, channel `a * 10 + j` is landmark number `j` of anchor `a` (`j = 2m`
  is x and `j = 2m + 1` is y of landmark `m`).

That is the same information as the flat outputs, which is checked numerically (see
below). In words: `flat[(cell * 2 + a) * per + k] = raw[(a * per + k) * cells + cell]`
(`per` is 1, 4 or 10), and the flat score is the sigmoid of the raw one. For each cell, in
the order row by row, and for each anchor `a` = 0 and 1 (so the faces come out in the same
order as with `decode_scrfd`: cell by cell, anchor 0 before anchor 1):
1. The threshold is turned into the logit domain once per call:
   `t = clamp(profile_.detector_score, 1e-6, 1 - 1e-6)` and
   `logit_threshold = log(t / (1 - t))` (0 for a score threshold of 0.5), because
   `score >= t` is the same as `logit >= log(t / (1 - t))` (up to float rounding right at
   the threshold). A logit below that is skipped **before** any `exp` is computed, so the
   sigmoid `1 / (1 + exp(-logit))`, which is the face's `score`, is only evaluated for the
   few anchors that pass.
2. The anchor point `(ax, ay)` is the top-left corner of the cell,
   `((cell % cols) * s, (cell / cols) * s)`, as in `decode_scrfd`.
3. The box: `x1 = ax - b[0] * s`, `y1 = ay - b[cells] * s`, `x2 = ax + b[2 * cells] * s`,
   `y2 = ay + b[3 * cells] * s`, where `b` points to channel `a * 4` of the bbox tensor at
   this cell.
4. Landmark `j` (0 to 4): `(ax + k[2j * cells] * s, ay + k[(2j + 1) * cells] * s)`, where `k`
   points to channel `a * 10` of the landmark tensor at this cell.
5. `has_landmarks` is set to true.

**Checked.** The layout was checked against the flat outputs of the original model at all
three strides (a throw-away comparison, not part of the project): the largest difference was
0 for boxes and landmarks and about 1e-9 for the scores. The same check is now a test:
`tests/test_common.cpp` runs `det_dyn` and the original `det` on one 640x640 picture and
finds the largest difference over all scores, boxes and landmarks to be 3.7e-9, and runs
`det_dyn` on a 640x384 picture and checks the sizes of the nine outputs (48x80, 24x40 and
12x20 cells; 2, 8 and 20 channels). `tests/test_pipeline.cpp` checks that the fitted
rectangle and the square find the same three faces in a picture of the test size, 1920x1080 by
default (overlap of the boxes 0.994, largest landmark difference 0.26 pixels in the full flows of
2026-11-12 and 0.23 pixels after the test's positions were made to follow the picture size).
`letterbox_fit` itself has two checks in `tests/test_common.cpp` (`common.md`).

**Limits.** Nothing here checks that the model really is `det_dyn` or that the output names
are the right ones: `load_profile` only checks that there are nine names, and the size check
above only protects against reading past a buffer. If `detector.outputs` names other tensors
of the right size the faces would simply be wrong. The `flat` layout needs a 640x640 square;
this one accepts any multiple of 32 in principle, but it has only been tested with a long
side of 640 (640x640 and 640x384).

**Used by.** `DeepDetector::detect` (when `is_yunet_` is false and the layout is `chw`).

### `DeepDetector::to_original`

```cpp
static void to_original(Face& f, float scale, int pad_x, int pad_y, int width, int height) {
```

**What it does.** Converts a face from the letterbox picture (640x640, the fitted rectangle,
or the small network's square) back into the pixel coordinates of the original camera image
(`width` x `height`). The function does not need the picture's size: `scale` and the
padding, which `letterbox` or `letterbox_fit` reported for the picture that was used, already contain it. The
box is also clipped to the image; the landmarks are not clipped.

**How it works.** Two small helpers `fx` and `fy` (lambdas) apply
`original = (network - pad) / scale`: first the gray bars are removed by subtracting the
padding, then the shrinking is undone by dividing by the scale. The four box edges are
converted, then clamped into `0..width` and `0..height`, and stored again as
left/top/width/height. A box that lies completely outside the image can end up with a
negative width or height; such boxes are removed by the `FA_MIN_FACE_PIXELS` test in
`detect`. All five landmarks are converted with the same two lambdas.

**Used by.** `DeepDetector::detect`.

### `create_deep_detector`

```cpp
std::unique_ptr<FaceDetector> create_deep_detector(const ModelProfile& profile)
```

**What it does.** Builds a `DeepDetector`, calls `load()` and returns it. If loading
the main network fails it prints `detector: cannot load <model file>` and returns
`nullptr`. (A failing optional small network does not cause this; see `load`.)

**How it works.** `is_yunet` is `profile.detector_type == "yunet"`; every other type that
reaches this function is treated as SCRFD (`create_detector` only sends `"yunet"` and
`"scrfd"` here).

**Used by.** `create_detector` (`detector.cpp`, line 137).

---

## `detector_haar.cpp`

The classical detector. It uses no neural network. For one camera image it:
1. converts to gray and shrinks the picture to a working width of `FA_HAAR_WORK_WIDTH`
   pixels (320; wider for pictures wider than 1280 pixels, see `haar_work_width`) for
   speed;
2. runs the face cascade (`HaarCascade`) to find face boxes;
3. inside each box searches the two eyes with the eye cascade;
4. from the eyes calculates the roll (exact) and approximate yaw and pitch, and
   builds the five landmarks that the recognizers need for alignment.

Limits written in the file on purpose: a Haar cascade only fires for faces that look
almost straight at the camera, and without a landmark network the yaw and pitch are
rough (the file says +-5..10 degrees). The deep detectors are the accurate path.

The Haar detector is plain C++ code on the CPU, with no network and no library that could use a GPU. It
therefore does not override `FaceDetector::runs_on_cpu`: the interface's default answer, `true`, is its answer.

### Macros at the top of the file

All five can be changed at build time with `/DNAME=value` (see the settings section).

| Macro | Default | Purpose |
|---|---|---|
| `FA_HAAR_WORK_WIDTH` | `320` | the camera image is shrunk to this width (in pixels) before scanning, for a picture up to `FA_HAAR_REFERENCE_WIDTH` wide (wider pictures get a proportionally wider working image, see `haar_work_width`). Smaller is much faster but small faces vanish: the smallest face the cascade can see is 24 working pixels wide, which is `24 * (camera width / working width)` camera pixels (96 px for a 1280 px camera image; checked: 24 x 1280 / 320 = 96). An image that is not wider than the working width is not shrunk |
| `FA_HAAR_REFERENCE_WIDTH` | `1280` | a camera image wider than this gets a proportionally wider working image (`FA_HAAR_WORK_WIDTH * width / FA_HAAR_REFERENCE_WIDTH`), so that the smallest visible face stays about 96 camera pixels instead of growing with the picture (it was 144 px at 1920 wide with the fixed 320). Up to this width the working width is `FA_HAAR_WORK_WIDTH` |
| `FA_HAAR_MAX_WORK_WIDTH` | `480` | the working width never exceeds this, which bounds the scan time. The cap is reached at 1920 pixels (320 * 1920 / 1280 = 480); above that the smallest face grows again: `24 * width / 480` = 128 camera pixels at 2560 wide and 192 at 3840 wide |
| `FA_HAAR_FULL_SCAN_INTERVAL` | `8` | every this many frames the whole image is scanned for NEW faces; frames in between only follow the known faces |
| `FA_HAAR_SCALE_STEP` | `1.25f` | size ratio between two scan levels of the face search. Larger means fewer levels and faster, but a face of an in-between size is matched less well |

### Other items

| Function | Purpose |
|---|---|
| `kPi` | the number pi, used to convert radians to degrees |
| `haar_work_width` | the width a camera image is shrunk to before it is scanned (320 up to 1280 wide, proportionally more above, at most 480) |
| `HaarDetector::load` | load the face cascade and (optionally) the eye cascade |
| `HaarDetector::name` | returns `"haar/classical"` |
| `HaarDetector::type` | returns `"haar"` |
| `HaarDetector::reset` | forget the faces of earlier frames so that the next call is a full scan |
| `HaarDetector::detect` | find faces (full scan or follow scan), then eyes and pose |
| `HaarDetector::find_eyes_and_pose` | find both eyes in a face box, build landmarks and an approximate pose |
| `create_haar_detector` | factory called by `create_detector` |

### `kPi`

```cpp
const float kPi = 3.14159265358979f;
```

**What it does.** pi as a float, inside the anonymous namespace. Used in
`find_eyes_and_pose` to convert radians to degrees (`radians * 180 / kPi`). (`pose.cpp`
has its own private `kPi`.)

### `haar_work_width`

```cpp
int haar_work_width(int camera_width) {
```

**What it does.** Returns the width, in pixels, that a camera image of `camera_width`
pixels is shrunk to before the Haar cascade scans it. The answer is `FA_HAAR_WORK_WIDTH`
(320) for pictures up to `FA_HAAR_REFERENCE_WIDTH` (1280) wide, grows in proportion to
the picture width above that (`320 * camera_width / 1280`: 480 at 1920, 640 at 2560) and
is never more than `FA_HAAR_MAX_WORK_WIDTH` (480). It is only a width: whether the image
is shrunk at all is decided in `detect` (a picture that is not wider than the result is
used as it is, so the caller sees the smaller of the two).

**How it works.** `wanted = FA_HAAR_WORK_WIDTH * max(1, camera_width / FA_HAAR_REFERENCE_WIDTH)`
in `double` arithmetic; the result is `max(FA_HAAR_WORK_WIDTH, min(wanted, FA_HAAR_MAX_WORK_WIDTH))`
converted to `int` (the outer `max` only matters if somebody builds with a cap below
320). Examples (calculated from the formula): 640 -> 320, 1280 -> 320, 1600 -> 400,
1920 -> 480, 2560 -> 480, 3840 -> 480.

**Why it exists.** With the old fixed working width of 320 pixels the smallest visible
face grew with the picture: the cascade sees faces from 24 working pixels, which is
`24 * camera_width / working width` camera pixels, so 96 pixels at 1280 wide but 144 pixels
at 1920 wide. Now the working image grows with the picture and the smallest face stays
96 camera pixels up to 1920 wide. Measured with `eval_detect models\classical.ini --frame 1920x1080`
(share of faces found, before / after the change): face width 80 px 0% / 90%; 110 px
85% / 90%; 160 px 90% / 100%; 250 px and 400 px stay 100%. At 1280x1080 nothing changed
(80 px: 95%). The Haar detection takes about 8.7 ms (median) at 1920x1080; before it was
lower, because the image was shrunk more. Above 1920 wide the cap applies and the
smallest face grows again: 128 camera pixels at 2560 wide and 192 at 3840 wide (calculated:
`24 * width / 480`).

**Used by.** `HaarDetector::detect` (step 1, `detector_haar.cpp`, line 89).

### `HaarDetector::load`

```cpp
bool load(const ModelProfile& profile) {
```

**What it does.** Stores the number of threads from the profile, loads the face cascade
from `profile.haar_face_model` and, if a path is given, the eye cascade from
`profile.haar_eye_model`. Returns `false` only when the FACE cascade cannot be loaded.

**How it works.** `has_eyes_` becomes true only when the eye path is not empty and the
eye cascade loads. If the eye cascade is missing or damaged there is no error: the
detector still works but returns faces without landmarks and without pose.

**Used by.** `create_haar_detector`.

### `HaarDetector::name`

```cpp
const char* name() const override { return "haar/classical"; }
```

**What it does.** Returns the fixed text `"haar/classical"`.

### `HaarDetector::type`

```cpp
const char* type() const override { return "haar"; }
```

**What it does.** Returns `"haar"`. `refine_face` tests for this text to apply the Haar
exception in its pose step (it keeps the detector's own yaw and pitch, see `refine_face`
step 6); `pose_model_for` is not consulted for Haar there.

### `HaarDetector::reset`

```cpp
void reset() override {
```

**What it does.** Makes the detector forget the faces of the previous frames. After it,
the next call of `detect` is a full scan of the whole image. It overrides the empty
`FaceDetector::reset`.

**How it works.** Three things are cleared: the remembered face boxes `last_boxes_`, the
frame counter `frame_counter_` (back to 0) and the remembered working-image size
`last_width_` and `last_height_` (back to 0). The full scan then happens for two reasons
at once: there are no remembered boxes, and the remembered size (0) differs from the real
size, so `size_changed` is true (see `detect`, step 2). The loaded cascades, the number of
threads and the reusable image buffers are not touched.

**Used by.** Through `FaceDetector::reset`, which `AuthPipeline::reset_tracking` calls
(`src/pipeline/auth_pipeline.cpp`, line 250).

### `HaarDetector::detect`

```cpp
bool detect(const Image& frame, std::vector<Face>& faces, bool /*accurate*/ = false) override {
```

**What it does.** Finds faces in a camera image of any size (gray or BGR). `faces` is
cleared first. Returns `false` only for an empty image, otherwise `true` (possibly with
no faces). The third parameter exists only to match the interface (see
`FaceDetector::detect`); its name is commented out, so it is not used: the Haar detector
has a single mode and `refine_face`'s `accurate = true` changes nothing for it. Each face has a box in camera pixels, a score
`min(1, neighbors / 12)` (the number of agreeing search windows divided by 12; it is not
comparable to the score of the deep detectors), and, if both eyes were found, landmarks
and a pose. At most `FA_MAX_FACES` faces are returned.

**How it works.**
1. **Gray and shrink.** `to_gray`, then the working width is worked out with
   `haar_work_width(frame.width)` (320 up to 1280 wide, proportionally more above that, at
   most 480). If the image is wider than that, `resize_bilinear` shrinks it to that width
   (`ratio = work_width / frame.width`; the height is shrunk by the same ratio). The shrunk
   image is called the working image `work_`. If the image is not wider, `ratio` is 1.
   The working width changes with the picture width, so the working image changes size
   when the camera size changes (the next call is then a full scan, see step 2).
2. **Decide the scan mode.** The detector remembers the face boxes of the previous
   frame (`last_boxes_`, working-image coordinates), a frame counter and the size of
   the previous working image. A **full scan** is done when there are no remembered
   faces, or the working image changed size, or the frame counter is a multiple of
   `FA_HAAR_FULL_SCAN_INTERVAL`. Otherwise a **follow scan** is done.
3. **Full scan.** One call of `HaarCascade::detect` over the whole working image: smallest
   face `max(window width, FA_MIN_FACE_PIXELS * ratio)`, largest face the whole image
   width, scale step `FA_HAAR_SCALE_STEP`, `min_neighbors = 4`, using `threads_` CPU
   threads. This is the only way to find NEW faces.
4. **Follow scan.** For each remembered box: build a search window = the old box
   enlarged by 30 percent on every side (so 1.6 times its width and height), clipped to
   the image. If it is smaller than the cascade window it is skipped. Copy that window
   with `crop` and run the cascade only for face widths between 0.75 and 1.35 times the
   old width, `min_neighbors = 3`, one thread. If several hits are found, take the
   one with the most neighbours and shift it back by the window's position. A face for
   which nothing is found in its window is dropped (it can only come back through a
   later full scan). The idea: a face cannot move far between two frames, so this is
   much cheaper than a full scan.
5. **Remember.** `last_boxes_` is replaced by the new boxes (so if all faces vanish the
   next frame is a full scan).
6. **Build the `Face` records.** The box is divided by `ratio` to get camera pixels.
   The score is computed as described above. If `has_eyes_`, `find_eyes_and_pose` fills in
   landmarks and pose.

Side effects to be aware of. The detector keeps state between calls, so it should be
called with consecutive frames of the same camera. Calling it with unrelated pictures of
the same size can run the cheap follow scan instead of a full scan. `reset()` (above)
clears this memory; `tools/eval_detect.cpp` (line 72) does not call it but shows a blank
frame first, which has the same effect there (it clears what the detector remembers).
`refine_face` also calls
`detect` with an enlarged cut-out whose size differs from the camera frame, so the next
normal frame sees `size_changed` and does a full scan again.

**Used by.** Through the `FaceDetector` interface; see `FaceDetector::detect`.

### `HaarDetector::find_eyes_and_pose`

```cpp
void find_eyes_and_pose(const Rect& box_work, float ratio, Face& f) {
```

**What it does.** For one face box (`box_work`, in working-image coordinates), looks for
the two eyes with the eye cascade and, if both are found, fills in `f.landmarks`,
`f.has_landmarks` and `f.pose` (camera-pixel coordinates; `f.box` must already be set).
If anything is missing the function simply returns and `f` stays without landmarks and
with an invalid pose. `ratio` is the shrink factor used to get back to camera pixels.

**How it works.**
1. **Eye search region.** From 15 to 60 percent of the face height, over the full
   face width (the upper half of the face, where the eyes are). The left and top corner
   of the region are first rounded DOWN to whole pixels (`floor`), and then the region is
   copied with `crop`. The rounding is done here, before `crop`, because `crop` itself
   rounds the corner down; the eye positions found inside the copy are converted back to
   the working image by adding this very corner (step 5), so the two must be the same
   number. (An unrounded corner would shift both eyes by up to one working pixel and bias
   yaw and pitch; this is what the comment in the code says.)
2. **Eye size range.** At least `max(eye cascade window width, 0.15 * face width)`
   and at most `0.45 * face width`. If the maximum is below the minimum the function
   returns.
3. **Search.** `eyes_.detect` with scale step 1.15 and `min_neighbors = 2`.
4. **Pick one eye per side.** Of all hits, the one with the most neighbours whose centre
   lies in the left half of the region becomes the image-left eye, which is the PERSON'S
   RIGHT eye (`landmarks[0]`); the strongest hit in the right half becomes
   `landmarks[1]`. If one side has no hit the function returns.
5. **Eye centres in camera pixels.** The (rounded) region corner plus the hit centre,
   divided by `ratio`. `eye_distance` is the distance between them. The function returns if
   `eye_distance < 4` pixels or the second eye is not to the right of the first.
6. **Nose and mouth.** They are not detected. They are placed like an average face: the
   standard 112x112 aligned-face template (`aligned_template_112`, from
   `image_ops.h`) is scaled by `k = eye_distance / template eye distance` and rotated by
   the difference of the two eye-line angles, anchored at the first eye. So landmarks
   2, 3, 4 (nose, right mouth corner, left mouth corner) always have the proportions
   of the template, only the eyes are measured.
7. **Pose.** Roll = angle of the eye line, `atan2(dy, dx)` in degrees (this is exact).
   Yaw and pitch are rough estimates from where the eyes sit inside the face box:
   `head_depth = 0.6 * eye_distance` is taken as the distance from the eye plane to the
   head centre;
   `yaw = asin(clamp((eye_mid_x - box_centre_x) / head_depth))`;
   `pitch = asin(clamp(-((eye_mid_y - box_centre_y) + 0.10 * box_height) / head_depth))`.
   The 0.10 is there because in a frontal Haar box the eyes sit about 10 percent of the
   box height above the box centre. `clamp` limits the value to -1..1. `pose.valid` is set to
   true.

Note: yaw and pitch here come from the box position, so they are only as good as the
Haar box. A later `refine_face` call keeps these yaw and pitch values (from the final
detection it chose) and only replaces the roll by the angle of the mapped eye line (see
`refine_face`, step 6).

**Used by.** `HaarDetector::detect`.

### `create_haar_detector`

```cpp
std::unique_ptr<FaceDetector> create_haar_detector(const ModelProfile& profile)
```

**What it does.** Builds a `HaarDetector` and loads the cascades. On failure prints
`detector: cannot load Haar cascade <path>` and returns `nullptr`.

**Used by.** `create_detector` (`detector.cpp`, line 138).

---

## `haar_cascade.h` and `haar_cascade.cpp`

The Viola-Jones object detector, written by hand (no OpenCV). How it works, in short:

* A **Haar feature** is two or three rectangles. Its value is the sum of the pixels
  in the dark rectangle(s) minus the sum in the bright one(s), with weights. A face has
  typical patterns, for example the band of the eyes is darker than the cheeks below it.
* The **integral image** lets us get the sum of any rectangle with 4 memory reads.
* A **stump** is a tiny test: `feature value < threshold ? left number : right number`.
* A **stage** adds up the answers of many stumps. If the total is below the stage's
  threshold, the window is not a face and the test stops. Most windows fail in the first
  stages, which is why the method is fast.
* The window is slid over the image at many sizes (scales).

The cascade files are the "stump-based" XML format of OpenCV
(`models/permissive/haar_frontalface.xml`, window 24x24; `haar_eye.xml`, window 20x20, see
`tests/test_detect_units.cpp`).

### Data structures (`haar_cascade.h`)

| Struct | Meaning |
|---|---|
| `HaarRect` | one rectangle of a feature, in the coordinates of the training window (for example 24 x 24): `x, y, w, h` (ints) and `weight` (float) |
| `HaarFeature` | up to three rectangles: `count` (2 or 3, read from the file) and `rect[3]` |
| `HaarStump` | one tiny test: `feature` (index into the feature list), `threshold`, `left` (answer when the normalized feature value is below the threshold) and `right` (answer otherwise) |
| `HaarStage` | `threshold` (the stage passes when the sum of its stump answers reaches it) and the list `stumps` |
| `HaarHit` | a detected object after grouping: `box` and `neighbors`, the number of raw windows that agreed on it |
| `ScaledRect`, `ScaledFeature` (private to `haar_cascade.cpp`) | a feature prepared for ONE window size: the four corner positions of each rectangle as offsets into the integral image (`tl`, `tr`, `bl`, `br`) and a `weight` that already includes the division by the area |

The class `HaarCascade` holds `window_w_`, `window_h_` (the training window size),
`stages_` and `features_`.

| Function | Purpose |
|---|---|
| `HaarCascade::load` | read an OpenCV cascade XML file |
| `HaarCascade::window_width` | width of the training window |
| `HaarCascade::window_height` | height of the training window |
| `HaarCascade::detect` | search a gray image for objects at many sizes |
| `parse_numbers` | read all numbers out of a piece of text (private helper) |
| `next_between` | get the text between two tags (private helper) |
| `find_root` | union-find "find" used to group hits (private helper) |
| `similar` | are two boxes close enough to be the same object (private helper) |

### `parse_numbers`

```cpp
std::vector<double> parse_numbers(const std::string& text)
```

**What it does.** Returns all numbers found in `text`. Numbers are separated by any
characters that are not part of a number (spaces, newlines); values such as `-1.` are
fine.

**How it works.** It repeatedly calls `strtod` at the current position. If a number was
read, it is stored and reading continues behind it; if not, one character is skipped.

**Used by.** `HaarCascade::load` (for the weak-classifier lines, the leaf values and
the rectangle lines).

### `next_between`

```cpp
bool next_between(const std::string& s, size_t& pos, size_t limit, const char* open, const char* close,
                  std::string& content)
```

**What it does.** A very small XML reader: finds the next `open` tag at or after `pos`
(but before `limit`), then the next `close` tag, and puts the text between them into
`content`. On success `pos` moves behind the closing tag and `true` is returned; if a
tag is missing or lies beyond `limit` it returns `false`.

**How it works.** `std::string::find` for both tags, `substr` for the text. The helper
exists because the project reads the cascade XML without any XML library.

**Used by.** `HaarCascade::load` (many times).

### `HaarCascade::load`

```cpp
bool load(const std::string& xml_path);
```

**What it does.** Reads an OpenCV Haar cascade XML file into memory. Returns `true` on
success. Returns `false` (printing a message in the first three cases) when the file cannot be opened,
when it is not a `BOOST` / `HAAR` cascade, when a tree has more than one node (only
single-node "stump" classifiers are supported), or when the file is incomplete or
inconsistent (see step 6). Existing stages and features are cleared at the start, so a
failed load leaves the object in a half-filled state that must not be used.

**How it works.**
1. Read the whole file into a string.
2. Window size: the first `<height>` and `<width>` after `<opencv_storage>` (or from the
   start of the file if that tag is missing).
3. Check that the text contains `<stageType>BOOST` and `<featureType>HAAR`.
4. Stages: for every `<stageThreshold>` inside `<stages>` read the threshold, then every
   weak classifier of the stage. A weak classifier is an `<internalNodes>` line of four
   numbers (the third is the feature index, the fourth the threshold; the first two
   are not used) followed by a `<leafValues>` line of two numbers (`left`, `right`).
5. Features: for every `<rects>` block read up to three rectangles; each rectangle is a
   `<_>` item with five numbers `x y w h weight`.
6. Final checks: every stump must point to an existing feature, the window size must be
   positive, and there must be at least one stage and one feature.

**Used by.** `HaarDetector::load` (`detector_haar.cpp`, lines 68 and 69, for the face and
eye cascade); `tests/test_detect_units.cpp` (lines 161 and 163).

### `HaarCascade::window_width`

```cpp
int window_width() const { return window_w_; }
```

**What it does.** Returns the width of the training window in pixels (24 for the face
cascade, 20 for the eye cascade). It is the smallest object width the cascade can find at
scale 1.

**Used by.** `detector_haar.cpp` (lines 109, 118, 160); `tests/test_detect_units.cpp` (lines
160, 162).

### `HaarCascade::window_height`

```cpp
int window_height() const { return window_h_; }
```

**What it does.** Returns the height of the training window in pixels.

**Used by.** `detector_haar.cpp` (line 118).

### `HaarCascade::detect`

```cpp
void detect(const Image& gray, int min_size, int max_size, float scale_step, int min_neighbors,
            std::vector<HaarHit>& hits, int threads = 1) const;
```

**What it does.** Searches a one-channel gray image for objects of this cascade.
Parameters: `min_size` and `max_size` are the smallest and largest object width in
pixels, `scale_step` is the size ratio between two scan levels (for example 1.2),
`min_neighbors` is how many raw windows a group needs to count, `threads` is the number
of CPU threads over which the scan levels are spread. `hits` is cleared and then filled
with the grouped detections (box in pixels of `gray`, and neighbour count). If the image
is empty or not single-channel, `hits` stays empty.

**How it works.**
1. **Integral images.** One for the pixel sums and one for the sums of squares
   (`integral_image`). The squares give the contrast of a window.
2. **Scan sizes.** The first scale is `max(1, min_size / window width)`. The scale is
   multiplied by `scale_step` until the window would be larger than the image (minus 1
   pixel) or wider than `max_size`. Window size at a scale is `round(window size * scale)`.
3. **For every scale (in parallel with OpenMP; each thread handles its own scales).**
   - *Scale the features.* Each rectangle's position and size are multiplied by the scale
     and rounded to whole pixels, and its four corners are converted into offsets in the
     integral image. The weight is divided by the area of the window's inner part (see
     next point). Rounding changes the rectangle sizes, so the weight of the first
     (large) rectangle is recomputed so that the weights times the areas still add up to
     zero (a flat gray area then gives exactly 0).
   - *Contrast measurement.* Measured on the window without a 1-pixel border: mean and
     variance (from the sums and sums of squares).
   - *Slide the window.* Step in pixels is `round(2 * scale)` when `scale <= 2` and
     `round(scale)` otherwise. (This gives 2 pixels at scale 1 and denser steps again just
     above scale 2; the file's comment "small windows move in steps of 2 pixels, large
     ones faster" is only a rough description.)
   - *Skip flat windows.* If the variance is below 25 (standard deviation below 5) the
     window cannot be a face and is skipped.
   - *Run the stages.* For each stump: compute the feature value
     (`sum of weight * rectangle sum`, where a rectangle sum is `br - bl - tr + tl`, four
     reads), compare it with `stump.threshold * standard deviation` (the threshold is scaled by the contrast,
     so the answer does not depend on how bright or contrasty the picture is) and add `left`
     or `right` to the stage total. If the total is below the stage threshold, stop with
     "not a face". A window that passes all stages is saved as a raw hit.
   - Raw hits of all scales are gathered into one list inside an OpenMP critical section.
4. **Sort the raw hits.** With several threads the scan levels can finish in any order,
   so the list would come out in a different order from run to run. The raw hits are
   therefore sorted by top edge `y`, then by left edge `x`, then by width `w`, so that the
   grouping in the next step (and with it the result) is the same on every run.
5. **Grouping.** Many overlapping windows fire around one real face. Windows are
   joined into groups with a union-find structure (`find_root`) when `similar` says
   they are close. For each group the average box is computed. Groups with fewer than
   `min_neighbors` members are discarded. `neighbors` of the result is the group size.
6. **Duplicate removal.** A box that lies (with a margin of 10 percent of the bigger
   box's width) completely inside a bigger box that has at least as many neighbours, and
   the bigger box is at least 1.1 times wider, is dropped.

Remarks: the grouping compares all pairs of raw windows, so its cost grows with the square
of their number. Because of the sort in step 4 the result does not depend on thread
timing (the number of threads can still change how fast it is, not what is found). If the compiler is run without OpenMP the pragmas are ignored and the loop runs on one
thread (`build.bat` passes `/openmp`).

**Used by.** `HaarDetector::detect` (`detector_haar.cpp`, line 110 for the full scan, line 122
for the follow scan) and `find_eyes_and_pose` (line 163); `tests/test_detect_units.cpp`
(line 169).

### `find_root`

```cpp
int find_root(std::vector<int>& parent, int i)
```

**What it does.** The "find" operation of a union-find structure. Every raw window starts
as its own group (`parent[i] == i`); joining two groups sets the parent of one root to the
other root. `find_root` follows the parents upward and returns the root (the group's
representative) of window `i`.

**How it works.** A loop that moves `i` to its parent until `parent[i] == i`; on the way
each entry is re-pointed to its grandparent ("path compression"), which keeps later
searches short.

**Used by.** `HaarCascade::detect` (grouping step).

### `similar`

```cpp
bool similar(const Rect& a, const Rect& b)
```

**What it does.** Tells whether two boxes are so close that they probably mark the same
object: all four edges (left, top, right, bottom) must differ by at most
`delta = 0.2 * (min(width a, width b) + min(height a, height b)) / 2`.

**How it works.** Four absolute differences compared with `delta`. Since `delta` is
relative to the size of the boxes, big and small boxes use the same relative tolerance
(20 percent).

**Used by.** `HaarCascade::detect` (grouping step).

---

## `pose.h` and `pose.cpp`

Head rotation from the five landmarks. The method is a closed-form formula (no
iterations) based on a simple 3-D model of an average face; it costs a few
multiplications and so runs for every face on every frame.

### The 3-D model (from the top of `pose.cpp`)

Everything is measured in units of the eye distance `D`, so the face size does not
matter. The coordinates are: x to the right in the image, y DOWN, z toward the camera. The
origin is the middle between the eyes. An average frontal face then has:

| Point | x | y | z |
|---|---|---|---|
| right eye (person's right) | -0.5 | 0 | 0 |
| left eye | +0.5 | 0 | 0 |
| nose tip | `nose_x0` (about -0.02) | `nose_y` (about 0.57) | `nose_z` (0.45) |
| middle of the mouth corners | 0 | `mouth_y` (about 0.96) | `mouth_z` (0) |

`nose_z` says how far the nose sticks out in front of the eye plane; `mouth_z` how far the
mouth is in front of that plane. They are rough anatomical values and were not measured.
`nose_y` and `mouth_y` and the sideways offset `nose_x0` (the detector tends to put the
nose tip slightly off-centre even for a frontal face) are measured from real frontal faces
for each landmark detector.

### The formulas (beginner version)

The camera is assumed "orthographic": it has no perspective, so a point's place in the
image is simply its x and y (depth z is dropped). The head is turned first by yaw, then
by pitch, then by roll.

* **Roll** is the angle of the line from the right eye to the left eye. It is exactly 0
  when the eyes are level. This is `atan2(dy, dx)` with y pointing down, so a lower
  right-side-of-image eye gives a positive angle.
* **Yaw.** When the head turns by `yaw`, the nose tip (sticking out by `nose_z`) moves
  sideways by `nose_z * sin(yaw)` while the distance between the eyes shrinks to
  `D * cos(yaw)`. Divide the sideways shift by the eye distance and
  `tan(yaw) = (sideways shift in eye distances) / nose_z`. The measured sideways shift is
  corrected by `nose_x0` first.
* **Pitch.** When the head nods, the nose tip and the mouth move up or down by different
  amounts, because the nose sticks out and the mouth does not. The ratio
  `r = (nose height below the eyes) / (mouth height below the eyes)` therefore changes,
  and it does not depend on the face size (both heights are divided by the same eye
  distance). With the rotation applied the model says
  `r = (nose_y - nose_z * cos(yaw) * tan(p)) / (mouth_y - mouth_z * cos(yaw) * tan(p))`.
  Solving that for `tan(p)` gives the formula used in the code:
  `tan(p) = (nose_y - r * mouth_y) / (cos(yaw) * (nose_z - r * mouth_z))`.
  For the average frontal face `r = nose_y / mouth_y` and the numerator is 0, so the
  pitch is 0.

`tests/test_detect_units.cpp` checks on faces built with `synthesize_landmarks` that
single rotations are recovered within 0.5 degrees and combined rotations (inside the
tolerance box) within 1 degree. Real landmarks are of course noisier; the same test prints
the effect of 0.5 pixel landmark noise.

| Function | Purpose |
|---|---|
| `PoseModel` | the proportions of an average frontal face for one landmark detector |
| `deg` | radians to degrees (private helper) |
| `rad` | degrees to radians (private helper) |
| `pose_model_for` | choose the `PoseModel` for a detector type |
| `landmark_geometry` | measure nose and mouth position relative to the eyes, with the roll removed |
| `estimate_pose` (with model) | the three angles from the landmarks |
| `estimate_pose` (default model) | the same, with the default model |
| `pose_within_tolerance` | is the pose inside the allowed limits |
| `synthesize_landmarks` | build landmarks of an average face with a known rotation (for tests) |

### `PoseModel`

```cpp
struct PoseModel {
    float nose_y = 0.570f;   // nose tip below the line through the eyes
    float mouth_y = 0.960f;  // middle of the mouth corners below that line
    float nose_z = 0.45f;    // how far the nose sticks out in front of the eye plane (anatomy)
    float mouth_z = 0.0f;    // how far the mouth is in front of the eye plane (anatomy)
    float nose_x0 = -0.024f;  // sideways offset of the nose tip of a frontal face (a detector habit)
    PoseModel() {}
    PoseModel(float nose_y_, float mouth_y_, float nose_z_, float mouth_z_, float nose_x0_);  // sets the five numbers
};
```

**What it does.** The proportions of an average FRONTAL face as one particular landmark
detector reports them, in units of the eye distance. The default values equal the
YuNet numbers (checked: they are the same as `kYunet` in `pose.cpp`). `PoseModel()` keeps
these defaults; the constructor with five numbers (in the order of the members) is what
`pose_model_for` uses to write `{0.570f, 0.960f, 0.45f, 0.0f, -0.024f}`. It exists because
Visual Studio 2015 does not accept braces with values for a struct that has default
member values (`docs/VISUAL_STUDIO.md`).

**Why there are several models.** Different detectors place the nose and mouth landmarks
slightly differently. If the model does not match the detector, a perfectly frontal face
is read as slightly turned or nodded. A worked example from the table below (calculated
here, not measured): feed the formula with a frontal face that has SCRFD proportions
(`nose_y` 0.604, `mouth_y` 0.973) but use the YuNet model (0.570, 0.960): `r = 0.604 / 0.973`
= 0.621, `tan(p) = (0.570 - 0.621 * 0.960) / (0.45 * cos(yaw))`, which is about -0.058, i.e.
a false pitch of about -3.3 degrees, which is already two thirds of the 5-degree
tolerance. That is why each detector has its own model.

**Why there is a separate "refined" model.** The zoomed-in second look
(`refine_face`) sees the face larger and its landmarks sit slightly differently from the
coarse ones (for YuNet `mouth_y` changes from 0.960 to 0.986). The authentication gate uses the refined
landmarks, so it must use constants measured on the refined landmarks. Which "refined" model
applies depends on the type of the detector that ran `refine_face`, i.e. the pose detector:
for the shipped SCRFD profiles (which name YuNet as their pose detector, see
`create_pose_detector`) it is `yunet-refined`; `scrfd-refined` is used only when SCRFD itself
does the refinement (a profile without `pose.detector_profile`, or `--type scrfd` in the tools).

**Used by.** `estimate_pose`, `pose_model_for`, `synthesize_landmarks` (the default
parameter value `PoseModel()`), `tests/test_detect_units.cpp` (through the default).

### `deg` and `rad`

```cpp
float deg(float rad) { return rad * 180.0f / kPi; }
float rad(float degrees) { return degrees * kPi / 180.0f; }
```

**What they do.** Convert radians to degrees and degrees to radians. They live in the
anonymous namespace of `pose.cpp` together with the private constant
`const float kPi = 3.14159265358979f;`.

**Used by.** `estimate_pose` (`deg`) and `synthesize_landmarks` (`rad`).

### `pose_model_for`

```cpp
const PoseModel& pose_model_for(const std::string& detector_type);
```

**What it does.** Returns the `PoseModel` to use for a detector type. Known texts:
`"yunet"`, `"scrfd"`, `"yunet-refined"`, `"scrfd-refined"` (the `-refined` models are for the
landmarks of the refinement step). Anything else (for example `"haar"`) returns the default
model, which holds the YuNet numbers. The header comment adds that the Haar detector does
not use this function (its pose is handled in `detector_haar.cpp` and, for the refinement
step, in `refine_face`).

**How it works.** It compares the text with the four names and returns a reference to a
function-level `static const PoseModel`. The five numbers of each model are, in the order
`{nose_y, mouth_y, nose_z, mouth_z, nose_x0}`:

| Model | `nose_y` | `mouth_y` | `nose_z` | `mouth_z` | `nose_x0` | Measured on |
|---|---|---|---|---|---|---|
| `kDefault` (= YuNet numbers, the struct defaults) | 0.570 | 0.960 | 0.45 | 0.0 | -0.024 | (same as `kYunet`) |
| `kYunet` | 0.570 | 0.960 | 0.45 | 0.0 | -0.024 | coarse detection |
| `kScrfd` | 0.604 | 0.973 | 0.45 | 0.0 | -0.034 | coarse detection |
| `kYunetRefined` | 0.567 | 0.986 | 0.45 | 0.0 | -0.017 | `refine_face` landmarks |
| `kScrfdRefined` | 0.607 | 0.976 | 0.45 | 0.0 | -0.036 | `refine_face` landmarks |

**How the constants were measured, with `tools/calibrate_pose`.** The comment in
`pose_model_for` says: measured with `tools/calibrate_pose` on the frontal portraits in
`data/portraits` (`n = 135` faces at the time of the measurement); `nose_y`, `mouth_y` and
`nose_x0` are the MEANS; the "refined" rows were measured with
`tools/calibrate_pose --refine` on the same portraits. The tool (see
`tools/calibrate_pose.cpp`) works like this:
1. Usage: `calibrate_pose <profile.ini> [--type T] [--refine] <folder>` (this is the usage
   text printed by the tool). The profile selects the detector; `--type` overrides its type
   text; `--refine` measures the landmarks after `refine_face` instead of the plain
   detection.
2. For every `.jpg` file below the folder it runs the detector and takes the largest face
   of the picture. With `--refine` it runs `refine_face` on it with the profile's pose
   detector (`create_pose_detector`; pictures where that fails are skipped), so the numbers
   belong to the `<pose detector type>-refined` model. `--type T` measures the detector `T` alone
   and clears `pose.detector_profile`.
3. It calls `landmark_geometry` to get `nose_x`, `nose_y` and `mouth_y` in eye distances,
   and also the eye distance in pixels.
4. It prints mean, median, standard deviation and the count for `nose_x`, `nose_y`,
   `mouth_y` and `eye_px`.
5. The idea: the portraits are official, straight-on photos, so pitch and yaw are close to
   zero ON AVERAGE (not for each photo). The average measured positions are therefore the
   proportions of a frontal face as this detector reports it, and become the model: the
   mean `nose_y` becomes `PoseModel::nose_y`, the mean `mouth_y` becomes `mouth_y`, the mean `nose_x`
   becomes `nose_x0` (it should be near 0; a non-zero value is the detector's habit).
6. `nose_z` and `mouth_z` are NOT measured by the tool; the 0.45 and 0.0 are "rough
   anatomical values" (comment in `pose.cpp`).

Where the results are listed: the numbers are in the code comments and constants at the top
of `pose_model_for` (`pose.cpp`, about lines 38 to 56) and the defaults in `PoseModel`
(`pose.h`). `pose.cpp` also points to `docs/RESULTS.md` for the numbers and their spread.
I did not run the tool again, so the numbers above are quoted from the code, not
re-measured.

**Used by.** `DeepDetector::detect` (`detector_deep.cpp`, line 106, with the plain detector
type) and `refine_face` (`detector.cpp`, line 131, with `<type>-refined`; not for Haar).

### `landmark_geometry`

```cpp
bool landmark_geometry(const Face& face, float& nose_x, float& nose_y, float& mouth_y);
```

**What it does.** Measures where the nose and the mouth are relative to the eyes, after
turning the face so that the eye line is horizontal, in units of the eye distance. Outputs:
`nose_x` (sideways from the middle of the eyes, positive toward the right side of the
image), `nose_y` and `mouth_y` (downwards; `mouth_y` is the middle of the two mouth
corners). Returns `false` when the face has no landmarks or the eyes are less than 0.001
pixels apart.

**How it works.**
1. Take the eye vector from `landmarks[0]` to `landmarks[1]`; its length is the eye
   distance and its angle is the roll.
2. The middle of the two eyes is the new origin.
3. A small lambda `unrolled` moves a point to the origin, turns it by minus the roll
   (using the cosine and sine of the roll) and divides by the eye distance. This is done
   for the nose tip and both mouth corners.
4. `mouth_y` is the average of the two corner heights.

Because the roll is removed before measuring, turning the head in the image plane does not
disturb yaw and pitch.

**Used by.** `estimate_pose`; `tools/calibrate_pose.cpp` (line 84).

### `estimate_pose` (with a model)

```cpp
Pose estimate_pose(const Face& face, const PoseModel& model);
```

**What it does.** Returns the head rotation of `face` in degrees, using the given model.
Returns a `Pose` with `valid == false` (all angles 0) when the face has no landmarks or
the eyes coincide.

**How it works.**
1. `landmark_geometry` gives `nose_x`, `nose_y`, `mouth_y`.
2. Roll = `atan2` of the eye vector (the angle of the eye line).
3. Yaw = `atan((nose_x - model.nose_x0) / model.nose_z)`.
4. Pitch: with `r = nose_y / mouth_y` and `denominator = cos(yaw) * (model.nose_z - r *
   model.mouth_z)`, pitch = `atan((model.nose_y - r * model.mouth_y) / denominator)`. If
   `|mouth_y|` or `|denominator|` is below 0.001 the pitch is left at 0 (a safety check
   against dividing by almost zero); the pose is still valid.
5. All angles are converted to degrees and `valid` is set to true.

Angle signs are as in the table at the top of this document.

**Used by.** `DeepDetector::detect` (`detector_deep.cpp`, line 106), `refine_face`
(`detector.cpp`, line 131, not for Haar) and the one-argument overload below.

### `estimate_pose` (default model)

```cpp
Pose estimate_pose(const Face& face);  // uses the default model
```

**What it does.** The same, with `PoseModel()`, which is the default model (the YuNet
numbers).

**Used by.** `tests/test_detect_units.cpp` (the pose tests). No use inside `src/` was
found by searching.

### `pose_within_tolerance`

```cpp
bool pose_within_tolerance(const Pose& pose);
```

**What it does.** The "gate" test: returns `true` when the pose is valid and
`|pitch| <= FA_POSE_PITCH_TOL_DEG`, `|yaw| <= FA_POSE_YAW_TOL_DEG` and
`|roll| <= FA_POSE_ROLL_TOL_DEG` (defaults 5, 5 and 8 degrees; the limits themselves are
accepted). A face without landmarks (invalid pose) is never accepted.

**Used by.** `AuthPipeline::process` (`src/pipeline/auth_pipeline.cpp`, line 64, inside `run_gate_job`) to decide
whether a crop may be stored; `tests/test_pipeline.cpp` (line 256);
`tests/test_detect_units.cpp` (gate tests).

### `synthesize_landmarks`

```cpp
void synthesize_landmarks(float pitch_deg, float yaw_deg, float roll_deg, float cx, float cy,
                          float eye_distance, Point landmarks[5], const PoseModel& model = PoseModel());
```

(In `pose.cpp` the last two parameters are named `out` and `m`.)

**What it does.** The reverse of `estimate_pose`: builds the five landmarks of an average
face that has the given rotation, centred at `(cx, cy)` with the given eye distance in
pixels. The tests use it to create faces with a known pose and check that
`estimate_pose` returns the same angles.

**How it works.** The 3-D model points are right eye (-0.5, 0, 0), left eye (+0.5, 0, 0),
nose `(nose_x0, nose_y, nose_z)` and mouth corners `(-0.38, mouth_y, mouth_z)` and
`(+0.38, mouth_y, mouth_z)`. The mouth corner x value 0.38 is fixed in the code and is
not part of `PoseModel` (`estimate_pose` does not use the corner positions sideways).
Each point is turned in three steps: (1) yaw about the vertical axis (a positive yaw moves
the nose to +x); (2) pitch about the horizontal axis (a positive pitch, looking up, moves
the nose up, i.e. to a smaller y); (3) roll in the image plane, then z is dropped
(orthographic camera) and the result is scaled by `eye_distance` and shifted to `(cx, cy)`.

**Used by.** `tests/test_detect_units.cpp` (line 29). Not used inside `src/`.

---

## `tracker.h` and `tracker.cpp`

The tracker follows faces from frame to frame and gives each a track ID. The rest of the
system uses the ID to remember "this is the same person as in the previous frame", for
example to avoid verifying the same face again and again. It looks only at box position
(not at what the face looks like).

| Function | Purpose |
|---|---|
| `Candidate` | a possible pairing of one track with one detection (private struct) |
| `predicted_box` | where a track's box is expected to be in the new frame (private helper) |
| `FaceTracker::update` | match the new detections to the tracks, create and delete tracks |
| `FaceTracker::tracks` | read access to all live tracks |
| `FaceTracker::reset` | forget everything |

`FaceTracker` holds `tracks_` (the live tracks) and `next_id_` (the ID of the next new
track, starting at 1).

### `Candidate`

```cpp
struct Candidate {
    float overlap;
    int track;
    int detection;
};
```

**What it does.** One possible pairing: the IoU `overlap` between the predicted box of
track number `track` and the box of detection number `detection` (both are positions in
their lists, not IDs). Used only inside `update`.

### `predicted_box`

```cpp
Rect predicted_box(const Track& t)
```

**What it does.** Returns where the track's box should be in the newest frame: the last
known box moved by the track's speed. Size is not changed.

**How it works.** `frames = missed_frames + 1` (a track that was not seen for 2 frames has
moved for 3 frame-steps). Then `x += vx * frames` and `y += vy * frames`. This is what
lets a fast mover be followed: in `tests/test_detect_units.cpp` a 90 pixel wide face that
jumps 40 pixels per frame keeps one ID.

**Used by.** `FaceTracker::update`.

### `FaceTracker::update`

```cpp
void update(const std::vector<Face>& detections);
```

**What it does.** Gives the tracker the faces detected in the newest frame. Afterwards
every detection belongs to a track: either an existing one with the same ID as before, or a
new one. Tracks that were not seen for too long are removed. For each track
`updated == true` means that it was seen in this frame.

**How it works.**
1. **Score all pairs.** For each (track, detection) pair compute the IoU between the
   track's predicted box and the detection box. Pairs with IoU of at least
   `FA_TRACK_MATCH_IOU` (0.3) become `Candidate`s.
2. **Greedy matching.** Sort the candidates by IoU, best first. Go through them; skip a
   pair if its track or its detection is already taken. Otherwise match them and update the
   track: new speed, new face, `missed_frames = 0`, `updated = true`, `age_frames + 1`. The
   speed is the movement of the box CENTRE since the last stored box divided by
   `missed_frames + 1`, mixed half-and-half with the old speed (`v = 0.5 * v + 0.5 * new`),
   which smooths out jitter. The whole `Face` of the track is replaced by the detection
   (so the track carries the latest box, score, landmarks and pose).
3. **Unmatched tracks.** `missed_frames + 1`, `updated = false`, `age_frames + 1`. Then
   every track with `missed_frames > FA_TRACK_MAX_MISSED_FRAMES` (15) is erased. That is,
   a track is deleted at its 16th missed frame in a row, so it survives exactly
   `FA_TRACK_MAX_MISSED_FRAMES` missed frames (this is also how the comments in `tracker.h`
   and `config.h` put it).
4. **Unmatched detections.** Each starts a new track with the next ID (`next_id_++`),
   `updated = true`, `age_frames = 1`, speed 0.

Limits: matching uses only box overlap and is greedy, so two faces whose boxes cross
each other can in principle swap IDs; there is no appearance check. A missing track keeps its last box and speed
while it is waiting; the speed is not reduced.

**Used by.** `AuthPipeline::process` (`src/pipeline/auth_pipeline.cpp`, line 414);
`tests/test_detect_units.cpp` (lines 121 to 155).

### `FaceTracker::tracks`

```cpp
const std::vector<Track>& tracks() const { return tracks_; }
```

**What it does.** Returns all live tracks (read only). A track with `updated == true` was
seen in the newest frame; the order is the creation order of the surviving tracks, with new
tracks at the end.

**Used by.** `AuthPipeline::process` (`auth_pipeline.cpp`, lines 420 and 435); the pipeline
also offers it to callers as `AuthPipeline::tracker()` (`auth_pipeline.h`, line 169);
`tests/test_detect_units.cpp`.

### `FaceTracker::reset`

```cpp
void reset();
```

**What it does.** Deletes all tracks and starts the IDs again at 1.

**Used by.** `AuthPipeline::reset_tracking` (`auth_pipeline.cpp`, line 247);
`tests/test_detect_units.cpp` (lines 135 and 147).

---

## Detection at different camera picture sizes

The engine works with any picture size (the default camera size is 1920 x 1080, see
`camera.md`). What changes with the size is how many pixels a face has in the picture
that the detector really looks at, so how small a face can be and still be found.

* **Deep detectors.** `letterbox` shrinks the picture by `640 / (longer side)` (or the
  small network's side for small pictures, see `DeepDetector::detect`), so a face of
  width `w` camera pixels is `w * 640 / longer side` pixels wide in the network input.
  Example (calculated): an 80 pixel face is 80 x 640 / 1920 = 27 pixels wide in a
  1920 x 1080 picture and 80 x 640 / 3840 = 13 pixels wide in a 3840 x 2160 one. How
  small a face the network still finds is a property of the network and has only been
  **measured** (table below), not derived: YuNet copes with about 13 pixels, SCRFD
  needs roughly 17 to 27.
* **Haar detector.** Limited by its working width, see `haar_work_width`: the smallest
  face is about 96 camera pixels up to 1920 wide and grows above that (128 px at 2560,
  192 px at 3840).
* **The 80 pixel minimum** for a stored crop (`FA_MIN_CROP_FACE_PIXELS`, see `common.md`)
  is in camera pixels, so it does not change with the picture size.

Measured with `eval_detect <profile> --frame WxH` on this PC (share of the generated
faces found, per face width in the camera picture; the time is the median of the detection
alone):

| Picture | Detector | 50 px | 80 px | 110 px | 160 px | 250 px | 400 px | Time |
|---|---|---|---|---|---|---|---|---|
| 1920x1080 | YuNet | 100% | 100% | 100% | 100% | 100% | 100% | about 6 ms |
| 1920x1080 | SCRFD (ncnn), fitted rectangle 640x384 (`dev_noncommercial.ini`) | 85% | 100% | 100% | 100% | 100% | 100% | about 9.4 ms |
| 1920x1080 | SCRFD (ncnn), 640x640 square (`dev_noncommercial_square.ini`) | 90% | 100% | 100% | 100% | 100% | 100% | about 17.6 ms |
| 1920x1080 | Haar, after the change | 0% | 90% | 90% | 100% | 100% | 100% | about 8.7 ms |
| 1920x1080 | Haar, before (fixed work width 320) | not noted | 0% | 85% | 90% | 100% | 100% | lower (the image was shrunk more) |
| 3840x2160 | YuNet | 15% | 100% | 100% | 100% | 100% | 100% | 6.3 ms |
| 3840x2160 | SCRFD (ncnn), 640x640 square (measured before the fitted rectangle; not repeated with it) | 0% | 40% | 100% | 100% | 100% | 100% | 19 ms |
| 3840x2160 | Haar | 0% | 0% | 0% | 90% | 95% | 100% | about 8 ms |

The fitted rectangle and the square give the same result from 80 px up; at 50 px the rectangle found 85% and the
square 90%, which is one portrait of 20 (below the 80 px minimum crop width), and the time per frame fell from about
17.6 to 9.4 ms (`eval_detect` at 1920x1080, 20 portraits, `build\eval_detect_fit_vs_square.log`). At 1280x1080 nothing
changed for Haar (80 px: 95%). The speed limit of 60 ms (face detection at 1280x1080) is met by
a wide margin at 1920x1080: the benchmark rows (`bench_detect`) give YuNet 6.0 ms and Haar 8.7 ms (an earlier run) and for
SCRFD on ncnn 11.5 ms with the fitted rectangle and 17.9 ms with the square (`build\bench_dev_fitted.log`; median, detection
only). Conclusions: at the default 1920x1080 all three detectors find the
80 pixel faces that a stored crop needs; at 3840x2160 YuNet still does, SCRFD (40%) and Haar
(0%) do not, so **a 4K camera should still be asked for 1920x1080**, which is the default
(`pick_camera_mode` chooses the closest format; `camera_check --modes` shows what the camera
offers). The same figures are in `RESULTS.md`.

---

## Settings that change the behaviour

All numbers are macros with a default. Change them by editing `src/common/config.h` (or the
top of `src/detect/detector_haar.cpp` for the Haar speed settings) or pass
`/DNAME=value` to the compiler, for example `set FA_EXTRA=/DFA_POSE_YAW_TOL_DEG=3.0f` before
running `build.bat` (see `docs/ARCHITECTURE.md`). Defaults are quoted exactly as written
in the code.

### Detection

| Macro | Default | File | Effect |
|---|---|---|---|
| `FA_DETECT_INPUT_SIZE` | `640` | `config.h` | side of the square image given to the MAIN deep network (letterbox size); for SCRFD with `detector.layout = chw` it is the long side of the fitted rectangle (`letterbox_fit`). Also decides the grid sizes (`size / stride`); the comment of the original SCRFD conversion (`dev_noncommercial_square.ini`) says its input is fixed to 640x640, so change it only with a network that accepts it (`det_dyn.param` accepts any multiple of 32, but only 640 was tested). The optional small network does not use this macro: its side is the profile key `detector.small_size` |
| `FA_DETECT_SCORE_THRESHOLD` | `0.6f` | `config.h` | only the DEFAULT of `ModelProfile::detector_score` (`profile.h`). The deep decoders use `profile_.detector_score`, which a profile file can override with `detector.score` (`permissive.ini`: 0.6, `dev_noncommercial.ini`: 0.5). The Haar detector does not use it |
| `FA_DETECT_NMS_IOU` | `0.3f` | `config.h` | two boxes overlapping by more than this IoU are treated as the same face (deep detectors only; Haar has its own grouping) |
| `FA_MIN_FACE_PIXELS` | `40` | `config.h` | faces smaller than this in the camera image are ignored. Deep: checked on box width and height after mapping back. Haar: only sets the smallest size of the full scan, in working pixels (`FA_MIN_FACE_PIXELS * ratio`, but not below the 24 pixel cascade window), so with a 1280 px camera image and `FA_HAAR_WORK_WIDTH` 320 the real minimum is 96 camera pixels, and with a 1920 px image (working width 480, see `haar_work_width`) it is again 96 |
| `FA_MAX_FACES` | `32` | `config.h` | at most this many faces are returned by one `detect` call (both detector families) |
| `FA_REFINE_FACE_WIDTH` | `320` | `config.h` | `refine_face` enlarges the face to about this many pixels wide before detecting again (never shrinks) |

### Tracking

| Macro | Default | File | Effect |
|---|---|---|---|
| `FA_TRACK_MATCH_IOU` | `0.3f` | `config.h` | a detection joins a track when the IoU with the predicted box is at least this |
| `FA_TRACK_MAX_MISSED_FRAMES` | `15` | `config.h` | a track is deleted when its missed-frame count is greater than this (so at the 16th miss in a row) |

### Head pose gate

| Macro | Default | File | Effect |
|---|---|---|---|
| `FA_POSE_PITCH_TOL_DEG` | `5.0f` | `config.h` | largest `|pitch|` that `pose_within_tolerance` accepts |
| `FA_POSE_YAW_TOL_DEG` | `5.0f` | `config.h` | largest `|yaw|` accepted |
| `FA_POSE_ROLL_TOL_DEG` | `8.0f` | `config.h` | largest `|roll|` accepted |

### Classical (Haar) detector

| Macro | Default | File | Effect |
|---|---|---|---|
| `FA_HAAR_WORK_WIDTH` | `320` | `detector_haar.cpp` | the camera image is shrunk to this width before scanning (for a picture up to `FA_HAAR_REFERENCE_WIDTH` wide); smaller is faster but misses small faces |
| `FA_HAAR_REFERENCE_WIDTH` | `1280` | `detector_haar.cpp` | a picture wider than this gets a proportionally wider working image (`FA_HAAR_WORK_WIDTH * width / FA_HAAR_REFERENCE_WIDTH`), so the smallest visible face stays about 96 camera pixels (`haar_work_width`) |
| `FA_HAAR_MAX_WORK_WIDTH` | `480` | `detector_haar.cpp` | the working width never exceeds this (reached at 1920 pixels wide); above it the smallest face grows again (128 px at 2560, 192 px at 3840) |
| `FA_HAAR_FULL_SCAN_INTERVAL` | `8` | `detector_haar.cpp` | every this many frames the whole image is scanned for new faces |
| `FA_HAAR_SCALE_STEP` | `1.25f` | `detector_haar.cpp` | size ratio between two scan levels of the face search (the eye search uses a fixed 1.15) |

### Used around this engine by the pipeline (not inside `src/detect/`)

| Macro | Default | Effect |
|---|---|---|
| `FA_MIN_CROP_FACE_PIXELS` | `80` | `AuthPipeline` never stores a crop of a face narrower than this (camera pixels) |
| `FA_POSE_CHECK_INTERVAL_MS` | `100` | a track waiting for an acceptable pose is checked with `refine_face` (run by the pose detector) at most once per this many milliseconds |
| `FA_CROP_STORE_INTERVAL_MS` | `1000` | an unverified track's crop is refreshed at most once per this many milliseconds |

### Profile keys that select and tune the detector (`models/*.ini`, read by `load_profile`)

| Key | Effect |
|---|---|
| `detector.type` | `yunet`, `scrfd` or `haar`: chooses the detector family and the pose model |
| `detector.backend` | `onnx` or `ncnn` (deep detectors) |
| `detector.model` | the `.onnx` file, or the `.param` file for ncnn (the `.bin` name is derived from it) |
| `detector.input`, `detector.outputs` | ncnn only: names of the input and output blobs |
| `detector.layout` | `flat` (default) or `chw`; SCRFD only. `flat`: the nine reshaped outputs of the original conversion, network input a 640x640 square. `chw`: the nine raw outputs of the last convolutions (`65,85,105,66,86,106,67,87,107` for `det_dyn.param`), network input the smallest multiple-of-32 rectangle (`letterbox_fit`): 640x384 for 1920x1080, about 40% less work than the square for 16:9 at the same scale. `load_profile` refuses `chw` for a profile that is not SCRFD or does not have nine `detector.outputs`. `dev_noncommercial.ini` uses `chw` with `det_dyn.param`, `dev_noncommercial_square.ini` the default |
| `detector.score` | score threshold of the deep decoders (see `FA_DETECT_SCORE_THRESHOLD`) |
| `detector.model_small` | optional faster network for small camera images (default: empty = not used). Only loaded with the `onnx` backend; the comment in `profile.h` says YuNet on ONNX Runtime only. `models/permissive.ini` sets `models/permissive/yunet_320.onnx` |
| `detector.small_size` | side of the small network's input square in pixels (default `320`; `permissive.ini` also sets 320). It should match the input size the small model was made for (the code does not check this) |
| `detector.small_max_side` | the small network is used when the longer side of the camera image is at most this many pixels and the caller did not ask for `accurate = true` (default `640`; `permissive.ini` also sets 640) |
| `detector.haar_face`, `detector.haar_eye` | cascade XML files of the classical detector; without the eye file faces come without landmarks and pose |
| `pose.detector_profile` | optional path of ANOTHER profile file whose `detector.*` keys build the detector that does the pose check (`refine_face`) instead of this profile's own (see `create_pose_detector`). Empty (default) = the profile's own detector does it. `models/dev_noncommercial.ini`, `dev_noncommercial_square.ini` and `dev_noncommercial_onnx.ini` set `models/permissive.ini` (YuNet measures the pose, SCRFD finds the faces); `permissive.ini` and `classical.ini` set none. The named profile may not name another one |
| `runtime.threads` | number of CPU threads for a network and for the full Haar scan (also for the pose detector: the thread count of the profile that names it is used) |

### Fixed numbers in the code (not macros)

These are written directly in the source and can only be changed by editing it.

| Where | Number | Meaning |
|---|---|---|
| `refine_face` | 8 pixels | smallest box and smallest cut-out |
| `refine_face` | 0.5 | margin of half a box width/height on each side |
| `refine_face` | 1.01 | a computed zoom up to this is replaced by exactly 1.0 (no resize) |
| `refine_face` | 0.3 | smallest IoU to accept a found face (both passes) |
| `refine_face` | 3.0 degrees | smallest roll for which the rotation compensation runs |
| `haar_work_width` | 1 | the proportion is never below 1 (a picture up to `FA_HAAR_REFERENCE_WIDTH` wide gets exactly `FA_HAAR_WORK_WIDTH`) |
| `HaarDetector::detect` | 4 / 3 | `min_neighbors` of the full scan / of the follow scan |
| `HaarDetector::detect` | 0.3, 0.75, 1.35 | follow-scan window margin and face size range relative to the old box |
| `HaarDetector::detect` | 12 | neighbours that give the score 1.0 |
| `HaarDetector::find_eyes_and_pose` | 0.15 to 0.60 of face height; 0.15 to 0.45 of face width; 1.15; 2; 4 px; 0.6; 0.10 | eye search band, eye size range, eye scale step, eye `min_neighbors`, smallest accepted eye distance, `head_depth` factor, expected eye height offset |
| `HaarCascade::detect` | 25 (variance), 0.2, 0.1, 1.1 | flat-window limit, `similar` tolerance, "inside a bigger box" margin and size ratio |
| `FaceTracker::update` | 0.5 / 0.5 | mix of old and new speed |
| `pose.cpp` | 1e-3 | limits below which a division is avoided |
| `synthesize_landmarks` | 0.38 | mouth corner x position in eye distances |

Deep detection rejects a call with false and an empty face list if any output tensor contains NaN/Inf or decoded/mapped face coordinates overflow. Checks run before suppression and clipping, for YuNet and flat/CHW SCRFD, so invalid values cannot become fabricated detections.
