# Authentication pipeline (`src/pipeline`)

**What this is for.** The pipeline is the complete facial-authentication system: it takes one camera frame at a time, finds the faces, follows them from frame to frame, decides when a face is good enough to look at, computes a *feature* (a list of numbers that describes how the face looks) and compares it with the registered people. It contains almost no image or network code of its own: it connects the detection engine, the tracker, the head-pose gate, the feature extractor, the matcher and the feature store, and adds the rules about *when* each of them runs. One call to `AuthPipeline::process` handles one frame. The same class also registers, deletes and renames people, using the same pieces. The **card information is always identified first**: when RF cards have been read (`begin_card_check`), the cards are looked up in the database, the features stored with them are retrieved, and the faces of the following frames are compared only with those features; a card that is not in the database is a failed comparison (and there is no fallback to a search among everybody). The expensive part of a frame (the pose check, the crop and the feature extraction) can run in one of **two modes**: inside `process` (the default, "synchronous") or on a second thread, a *worker* (`init(profile, path, true)`), so that every frame costs only the detection, the tracking and the matching. The pose check (the zoomed-in "second look") is done by the profile's *pose detector*: normally the main detector itself, but a profile can name another one (`pose.detector_profile`; the SCRFD profiles let YuNet do it), see `AuthPipeline::pose_detector`. The main detector still finds the faces in whole frames and in the photos used for registration. The files are `src/pipeline/auth_pipeline.h` (data types and the class declaration) and `src/pipeline/auth_pipeline.cpp` (the methods, the unit of work and the worker). In this repository `process` is called by `tests/test_pipeline.cpp` and by `tools/check_video.cpp`; there is no finished camera application, and the tests never open a camera (see `camera.md`).

Words used below.

| Term | Meaning |
|---|---|
| Track | One face that is followed over many frames. The tracker gives it a number, the *track ID*, that stays the same while the face is followed. A track that is lost for more than `FA_TRACK_MAX_MISSED_FRAMES` (default `15`) frames is deleted; if the person comes back later he gets a new ID. |
| Verified | A track is *verified* once its face has been matched to a registered person. Verified tracks are only followed from then on, with these exceptions: a verified track that was **not found for `FA_REVERIFY_AFTER_MISSED_FRAMES` frames in a row (default `3`) or more and is then found again** loses its verification, because another person may have stepped into the same spot and received the same track ID; `remove_person` withdraws it for the removed person; `rename_or_refresh` with a new photo withdraws it for that person; `begin_card_check` withdraws it for every track (an earlier verification proves nothing for the card check). |
| Missed run | The number of frames in a row in which a track was not found (`TrackState::missed_run`). It counts up for every frame in which the tracker still has the track but did not find the face, and is set to 0 whenever the face is found. |
| Pose, pitch, yaw, roll | Head rotation in degrees: pitch = nodding (up/down), yaw = turning left/right, roll = tilting toward a shoulder. The tolerances are `FA_POSE_PITCH_TOL_DEG` (default `5.0f`), `FA_POSE_YAW_TOL_DEG` (default `5.0f`) and `FA_POSE_ROLL_TOL_DEG` (default `8.0f`). |
| Gate | The check "is the head turned too much?". A face that fails the gate is not stored. |
| Refinement | A second, zoomed-in look at one face (`refine_face` in `detector.h`). It gives more exact landmarks and therefore a more exact pose than the first, coarse detection of the whole frame. |
| Pose detector | The detector that runs the refinement (and so measures the head pose). Normally it is the profile's own detector; a profile can name another one with `pose.detector_profile` (the shipped SCRFD profiles name YuNet, because the SCRFD landmarks gave a noisy pitch). See `AuthPipeline::pose_detector` and `create_pose_detector` (`detect.md`). |
| Crop | The part of the camera image around one face, stored together with its landmarks (`FaceCrop` in `types.h`). |
| Feature | The list of numbers computed from a crop (128, 256 or 512 numbers depending on the profile), scaled to length 1. |
| M:N | **M** = the number of currently detected faces (their features are the "queries"), **N** = the number of faces registered in the database. All M query features are compared with all N registered features in a single call of the matcher (M x N dot products, M results). M and N are independent of each other: M = 1 against N = 10,000 is the normal case, M may be larger than N, and either may be 0. N can also be a **subset** of the database: the features that RF cards stand for (see RF card). |
| RF card, `card_id` | The number of a person's RF card, stored in the database record next to the face feature (`Record::card_id`; empty = no card; unique). **Card first:** the card information is always identified first. `begin_card_check` looks every card up in the database and retrieves the feature stored with it; from then on the M faces of each frame are compared only with those N retrieved features, and with nothing else (N, the number of cards, can be smaller than, equal to or larger than M). A face is verified as the owner of the card whose feature it is most like, when the similarity reaches the threshold. So a registered person who shows somebody else's card is **not** verified, and a stranger is not either. A card that is not in the database is a **failed comparison**: it is reported as `found = false`, nothing is compared for it, and there is never a fallback to a search among all registered people while the check is active. |
| Card check | The state between `begin_card_check` and `end_card_check` (`card_check_active()`). In it `match_results` calls `FeatureStore::match_cards` instead of the matcher's `match`, and every `FrameResult` lists one `CardStatus` per card. |
| Database, write-through | The registered people are kept in a file that the feature store manages (`FeatureStore::open`, see `store.md`), in one of two formats that the user chooses: **CSV** (the default) or **SQLite** (`init`'s `db_format`). Every registration, deletion and change is written to the file before the call returns, and is undone when the file cannot be written. The pipeline therefore never saves by itself. |
| Score | Cosine similarity of two features (their dot product, because both have length 1). Higher means more alike. |
| Job | One unit of work for the pose check: "check the pose of this face and, if it is acceptable, store a crop and compute its feature". It carries a **copy** of the part of the camera image around the face (`GateJob`), so the camera frame can be reused at once. Its answer is a `GateResult`. |
| Thread, worker | A thread is a second line of execution inside the program that runs at the same time as the main one. The *worker* is the thread of the pipeline that runs the jobs. |
| Queue, in flight | The waiting line of jobs for the worker. A job is *in flight* while it waits in the queue or is running. |
| Mutex, condition variable | Tools for threads that share data. A mutex ("mutual exclusion") lets only one thread at a time touch the shared data; a condition variable lets a thread sleep until another thread says "something has changed". |
| Generation | A counter (`generation_`) that `reset_tracking` raises. Every job remembers the number it was made under; a result with an older number belongs to tracks that no longer exist and is thrown away. |

## src/pipeline/auth_pipeline.h

The header defines the three result types (`FaceStatus`, `CardStatus`, `FrameResult`), forward-declares the job types (`GateJob`, `GateResult`, defined in the .cpp), and defines the class `AuthPipeline` with its private per-track memory (`TrackState`) and the private worker (`Worker`, defined in the .cpp). Nine small functions are inline here; the larger methods are only declared and are documented in the `auth_pipeline.cpp` section below.

| Function | Purpose |
|---|---|
| `FaceStatus` (struct) | What happened to one face in one frame (for display, logging and tests). |
| `CardStatus` (struct) | What one RF card of a card-first check did in one frame. |
| `FrameResult` (struct) | Everything `process` reports about one frame: the faces, the cards, the time of each stage and the counters. |
| `GateJob`, `GateResult` (structs) | Forward declarations only; documented in the `auth_pipeline.cpp` section. |
| `AuthPipeline` (class) | The whole system; owns detector, an optional separate pose detector, extractor, tracker, store, per-track memory and the optional worker. |
| `AuthPipeline::TrackState` (private struct) | What the pipeline remembers about each track. |
| `AuthPipeline::Worker` (private struct) | Declared here, defined and documented in the `auth_pipeline.cpp` section. |
| `AuthPipeline::uses_worker` | Tells whether the pipeline runs in worker mode. |
| `AuthPipeline::card_check_active` | Tells whether a card-first check is active (`begin_card_check` was called and `end_card_check` was not). |
| `AuthPipeline::store` | Access to the registered-persons store. |
| `AuthPipeline::tracker` | Read-only access to the tracker. |
| `AuthPipeline::detector` | Access to the main detector (whole-frame detection, enrolment from photos). |
| `AuthPipeline::pose_detector` | Access to the detector that does the pose check (`refine_face`): the profile's `pose.detector_profile`, or the main detector when the profile names none. |
| `AuthPipeline::extractor` | Access to the feature extractor. |
| `AuthPipeline::match_threshold` | Read the similarity needed for a match. |
| `AuthPipeline::set_match_threshold` | Change that similarity. |
| `AuthPipeline::AuthPipeline`, `~AuthPipeline`, deleted copy, `init`, `runs_on_cpu`, `process`, `flush`, `enroll_track`, `enroll_image`, `extract_from_image`, `remove_person`, `rename_or_refresh`, `begin_card_check`, `end_card_check`, `reset_tracking`, `apply_result_state`, `match_results`, `fill_card_status` | Declared here; documented in the `auth_pipeline.cpp` section. |

### `FaceStatus` (struct)

```cpp
struct FaceStatus {
    int track_id = 0;
    Rect box;
    Pose pose;
    bool pose_ok = false;
    bool crop_stored = false;
    double crop_time_ms = 0;
    bool verified = false;
    std::string person_id;
    std::string person_name;
    float score = 0;
};
```

**What it does.** Describes one face in one frame. `FrameResult::faces` holds one `FaceStatus` for every track that was seen in this frame.

| Field | Meaning |
|---|---|
| `track_id` | ID of the track (same face = same ID from frame to frame). |
| `box` | Face box in camera-image pixels: the box of this frame's detection. |
| `pose` | Head rotation. If the track has ever been pose-checked, this is the refined pose of the **latest check result** (which can be several frames old); otherwise it is the pose that the detector reported for the whole frame (`tr.face.pose`). The "ever checked" test looks at `last_check_ms`, which is set when a job is made. Two consequences read from the code: when `process` resets that time because `time_ms` ran backwards, or because the worker queue was full, the status shows the detector's pose again until the next check; and **in worker mode** the pose shows zeros (`valid == false`) from the frame in which the first job of the track is queued until its result arrives, because the refined pose is only known then. |
| `pose_ok` | Result of the latest pose check result of this track (not necessarily made in this frame): `true` when the refined pose was inside the tolerances. It is `false` if no result has arrived yet, if the refinement failed, or if the pose was outside. For a verified track it keeps the value of the last check made before verification. |
| `crop_stored` | `true` only in the frame in which a new crop was stored for this track. In worker mode that is the frame in which the result of the job **arrived**. |
| `crop_time_ms` | The capture time of that crop: the time of the frame the crop was cut from. In synchronous mode it equals the current time; in worker mode it is earlier (the frame in which the job was made). Only meaningful when `crop_stored` is `true`; otherwise 0. |
| `verified` | `true` once this track has been matched with a registered person (or registered with `enroll_track`). |
| `person_id`, `person_name` | Who the track was verified as; empty strings until verified. |
| `score` | Similarity of the latest match attempt of this track; `0` before the first attempt. It keeps its value after verification. |

**How it works.** It is a plain struct filled by `AuthPipeline::process`: the gate step fills `pose`, `pose_ok`, and the verified/person/score fields; `match_results` sets `crop_stored` and `crop_time_ms` for the results applied in this frame and updates `verified`, `person_id`, `person_name` and `score` after the matching. (There is no card field: which card a face belongs to is told by `FrameResult::cards`, see `CardStatus`. During a card check `score` is the similarity of the face to the nearest owner among the cards, or `-2` when no card was found.)

**Used by.** Created in `AuthPipeline::process`; read in `tests/test_pipeline.cpp` (`run_video`, which uses `crop_time_ms` for the crop spacing rule) and in `tools/check_video.cpp` (`main`, to annotate the video and to compare with the truth).

### `CardStatus` (struct)

```cpp
struct CardStatus {
    std::string card_id;
    bool found = false;
    std::string person_id;
    std::string person_name;
    bool matched = false;
    int track_id = -1;
    float score = kNoScore;
};
```

**What it does.** The state of one RF card of a card-first check (`begin_card_check`) in one frame. While a check is active, `FrameResult::cards` holds one `CardStatus` per card, in the order in which the cards were given (N entries); otherwise it is empty.

| Field | Meaning |
|---|---|
| `card_id` | The card number, as given to `begin_card_check`. |
| `found` | The card is in the database. `false` means that the comparison for this card FAILED: nothing is compared for it. |
| `person_id`, `person_name` | The card's owner, as the database says now; empty when the card is not found. |
| `matched` | In this frame a face that is verified as the owner is in the picture. |
| `track_id` | That face's track ID (`-1` when `matched` is `false`). If two faces were both verified as the same owner, the first one in `FrameResult::faces` is reported. |
| `score` | The highest similarity any face has had to the owner since `begin_card_check` (`kNoScore`, `-2`, until a face has been compared with him). It never goes down while the check is active. |

**How it works.** A plain struct filled by `AuthPipeline::fill_card_status` at the end of every `process` call: `found`, `person_id` and `person_name` come from a fresh look-up of the card in the database (so a card whose owner was deleted during the check turns into `found = false` in the next frame), `matched` and `track_id` from the faces of this frame, and `score` from the pipeline's `card_best_` list that `match_results` keeps up to date.

**Used by.** Created in `AuthPipeline::fill_card_status`; read in `tests/test_pipeline.cpp` (`run_video`, which collects it per card in a `CardLog`, and video 6).

### `FrameResult` (struct)

```cpp
struct FrameResult {
    std::vector<FaceStatus> faces;
    std::vector<CardStatus> cards;
    double detect_ms = 0, track_ms = 0, match_ms = 0, total_ms = 0;
    double gate_ms = 0;
    double extract_ms = 0;
    int extractions = 0;
    int matched_queries = 0;
    int jobs_in_flight = 0;
    double worker_ms = 0;
};
```

**What it does.** The answer of one `process` call.

| Field | Meaning |
|---|---|
| `faces` | One `FaceStatus` per track that was **seen in this frame**. Tracks that exist but were not found in this frame are not listed. |
| `cards` | While a card-first check is active (`begin_card_check`): one `CardStatus` per card (N entries, in the order given). Empty otherwise. |
| `detect_ms` | Time of step 1 (detection), in milliseconds. |
| `track_ms` | Time of step 2 (the tracker update), in milliseconds. |
| `match_ms` | Time of the last step (`match_results`: the M:N call and the bookkeeping that follows it, then `fill_card_status`), in milliseconds. |
| `total_ms` | Time of the whole `process` call, including everything that has no field of its own (clearing dead tracks, taking over worker results, copying the region of a job, queuing the job). |
| `gate_ms` | **Synchronous mode only:** time of the pose check and crop (the job without its feature extraction), run inside `process`. It does not include the copying of the image region for the job. Always 0 in worker mode. |
| `extract_ms` | **Synchronous mode only:** time of the feature extraction, run inside `process`. Always 0 in worker mode. (Before the worker mode existed this was the time of step 4 in every case; now it only describes the synchronous mode.) |
| `extractions` | How many features were computed (a feature that fails to compute is not counted). In worker mode: how many arrived in this frame. |
| `matched_queries` | How many features (M, the new features of the currently detected faces) went into the matcher in this frame: all of them in **one** call, either `match` against all N registered people or, during a card check, `FeatureStore::match_cards` against the N features of the cards. It stays 0 when there was nothing to match (and, without a card check, when the store is empty). |
| `jobs_in_flight` | **Worker mode only:** the number of jobs waiting or running after this frame (0 in synchronous mode). |
| `worker_ms` | **Worker mode only:** the work time (pose check plus extraction, measured on the worker thread) of the results that were taken over in this frame. It is not part of `total_ms`, because it was spent on the other thread. |

**How it works.** Times are taken with `now_ms()` from `timer.h` (a steady clock in milliseconds). `process` starts every call by assigning a fresh `FrameResult()`, so all fields start at 0 and an empty face list. In synchronous mode `detect_ms + track_ms + gate_ms + extract_ms + match_ms` is smaller than `total_ms` only by the small remainder named under `total_ms`.

**Used by.** Filled by `AuthPipeline::process`; read in `tests/test_pipeline.cpp` (`run_video`, `print_timing`, which collects the stage times, the largest `faces.size()` and `matched_queries` and the `cards`; the lifecycle test reads `jobs_in_flight`) and in `tools/check_video.cpp` (`main`: `total_ms`, `gate_ms`, `extract_ms`, `jobs_in_flight`).

### `AuthPipeline` (class)

```cpp
class AuthPipeline {
public:  /* AuthPipeline, ~AuthPipeline, init, uses_worker, process, flush, enroll_*, remove_person, ... */
private: /* TrackState, Worker, apply_result_state, match_results, detector_, pose_detector_, extractor_, ... */
};
```

**What it does.** Owns every part of the system and exposes the operations: `init` once, `process` for every frame, and the registration functions. Create one object, call `init`, then call `process` for every frame. The object cannot be copied. **Two modes:**

| | Synchronous (default) | Worker thread (`init(..., async_worker = true)`) |
|---|---|---|
| Where the pose check and the extraction run | inside `process()`, with the pipeline's pose detector (`pose_detector()`) and extractor | on a second thread with its own pose detector and extractor |
| Cost of one frame | a frame in which a crop is due takes longer (an extra detector run plus a feature extraction); other frames are cheaper | detection, tracking and matching only, about the same in every frame |
| When a result is visible | when `process()` returns | in a LATER frame (the header comment says usually 2 to 10 frames later); `flush()` waits for it |
| Where it is documented | `process` | `Worker`, `process` step 3a, `flush` |

Private members:

| Member | Meaning |
|---|---|
| `detector_` | `std::unique_ptr<FaceDetector>`, created by `create_detector(profile)`. The main detector: it finds the faces in whole frames (step 1) and in the photos of `extract_from_image`. Used by the main thread only. |
| `pose_detector_` | `std::unique_ptr<FaceDetector>`, created by `create_pose_detector(profile)` in `init` **only when the profile names one** (`pose.detector_profile`); **null otherwise**, and then `detector_` also does the pose check. Used by the main thread only (the worker has its own, see `Worker`). It is only used for the refinement (`refine_face`), never to find faces in a whole frame. |
| `extractor_` | `std::unique_ptr<FeatureExtractor>`, created by `create_extractor(profile)`. Used by the main thread only. |
| `tracker_` | The `FaceTracker` that gives the track IDs. |
| `store_` | The `FeatureStore` of registered persons (and its matcher). `init` opens it as a managed database: its file (CSV or SQLite) is locked against a second program and every change is written to it at once (see `store.md`). The path of that file is `store_.path()` and its format `store_.format()`; the pipeline has no path or format member of its own. |
| `threshold_` | Similarity needed for "verified". Initialised to `0.5f` in the header (comment: "replaced by the profile's recognizer.threshold in init()"); `init` always replaces it by the profile's `match_threshold`, so `0.5f` is only seen if `init` was never called. |
| `card_check_` | `bool`: a card-first check is active (`begin_card_check` was called and `end_card_check` was not). Starts `false`; `init` sets it `false` again. `card_check_active()` returns it. |
| `active_cards_` | `std::vector<std::string>`: the cards of the active check, in the order given (including cards that are not in the database). Empty when no check is active. |
| `card_best_` | `std::vector<float>`: for each card of the check the highest similarity any face has had to its owner since the check began (`kNoScore` until a face was compared). It is what `CardStatus::score` reports. |
| `states_` | `std::map<int, TrackState>`: the memory of every live track, by track ID. |
| `faces_` | The detections of the current frame; kept as a member only so its memory is reused from frame to frame. |
| `generation_` | Counter raised by `reset_tracking`; jobs carry the number they were made under. Starts at 0. |
| `worker_` | `std::unique_ptr<Worker>`: the worker thread and its data; **null in synchronous mode** (this is what `uses_worker` tests). |

**How it works.** The main thread owns all pipeline data (`states_`, `store_`, `tracker_` ...); the worker thread never touches them: it only works on the copies carried by the jobs and on its own pose detector and extractor, and hands its results back through a list protected by a mutex. The `AuthPipeline` object itself is still to be used from one thread (the caller's).

**Used by.** `tests/test_pipeline.cpp` (`main`, in both modes, plus a second pipeline in the worker lifecycle test), `tools/check_video.cpp` (`main`) and `tools/bench.cpp` (`main`, which uses it only as a convenient holder of a ready detector, pose detector and extractor and never calls `process`; the single-engine programs `bench_detect`, `bench_extract` and `bench_match` and the test `test_feature` do not use the pipeline at all).

### `AuthPipeline::TrackState` (private struct)

```cpp
struct TrackState {
    bool verified = false;
    std::string person_id, person_name;
    float last_score = 0;
    int missed_run = 0;
    bool has_crop = false;
    bool job_pending = false;
    double last_crop_ms = -1e9;
    double last_check_ms = -1e9;
    bool last_pose_ok = false;
    Pose last_pose;
    FaceCrop crop;
};
```

**What it does.** The pipeline's memory about one track. One entry is created the first time a track is seen in a frame and erased when the tracker no longer has that track.

| Field | Meaning |
|---|---|
| `verified` | The track has been matched to a registered person. Once `true`, the track is not cropped, extracted or matched again until it loses the verification: in `process` when the track was lost for `FA_REVERIFY_AFTER_MISSED_FRAMES` (3) frames or more and is found again, in `remove_person`, in `rename_or_refresh` when a new photo is given, or in `begin_card_check` (for every track). |
| `person_id`, `person_name` | Who the track was verified as. |
| `last_score` | Score of the latest match attempt. It is set to 0 when `process` drops the verification of a track that was lost. |
| `missed_run` | Frames in a row in which this track was not found. `process` counts it up for every frame in which the track exists but was not updated and sets it to 0 when the face is found again. It decides whether a verified track keeps its verification when it is found again. |
| `has_crop` | A crop is stored (it may already have been extracted and matched). `enroll_track` needs this. |
| `job_pending` | Worker mode: a job for this track is queued or running. While it is `true` no second job is made for the track. Set when the job is queued, cleared when its result is taken over. Never set in synchronous mode. |
| `last_crop_ms` | Time of the last stored crop: the capture time of the frame it was cut from. `-1e9` (about a million seconds in the past) means "never", so the first crop is due at once. |
| `last_check_ms` | Time of the last pose check (the second, zoomed-in look); set when a job is made. `-1e9` means "never checked". |
| `last_pose_ok` | Result of the latest pose check that has been taken over. |
| `last_pose` | The refined pose measured by the latest check that has been taken over. |
| `crop` | The stored face crop (one per track; a newer crop replaces it). |

**How it works.** It is a plain struct. `process` reads and writes it during the gate and result steps; `apply_result_state`, `enroll_track`, `remove_person`, `rename_or_refresh` and `reset_tracking` also touch `states_`. It is only used on the main thread.

**Used by.** Only inside `auth_pipeline.cpp` (through `states_`).

### `AuthPipeline::uses_worker`

```cpp
bool uses_worker() const;
```

**What it does.** Returns `true` when the pipeline was initialised with `async_worker = true` and therefore has a worker thread; `false` in synchronous mode (and before `init`).

**How it works.** Returns `worker_ != nullptr`.

**Used by.** No caller in `src/`, `tools/` or `tests/`.

### `AuthPipeline::card_check_active`

```cpp
bool card_check_active() const { return card_check_; }
```

**What it does.** Returns `true` while a card-first check is active: after `begin_card_check` (also when some of its cards were not found, but not when the list was empty) and until `end_card_check`. `init` makes it `false` again.

**How it works.** A one-line getter on the member `card_check_`.

**Used by.** `tests/test_pipeline.cpp` (`main`, video 6: after the check has ended the pipeline is back to the normal search among all people).

### `AuthPipeline::store`

```cpp
FeatureStore& store();
```

**What it does.** Gives access to the store of registered persons, for reading (`count()`, `records()`, `find()`) and, if the caller wants, for changing it directly. The store is open (see `init`), so changes made through `store()` directly are written to the database file just like the pipeline's own, and are undone when the write fails. What they do not do is touch the tracks: registering a person this way does not mark any track as verified, and removing a person this way does not un-verify the tracks that were verified as him. Use `enroll_track` / `enroll_image` and `remove_person` for that. `store().set_card_id(...)` gives a registered person an RF card later (a card can also be given at registration, see `enroll_track`).

**How it works.** Returns a reference to the member `store_`.

**Used by.** `tests/test_pipeline.cpp` (`main`: counting people, checking a name, giving two registered people an RF card, re-reading the database file) and `tools/check_video.cpp` (`main`: counting the registered people for its report).

### `AuthPipeline::tracker`

```cpp
const FaceTracker& tracker() const;
```

**What it does.** Read-only access to the tracker, for example to list all live tracks.

**How it works.** Returns a constant reference to `tracker_`.

**Used by.** `tests/test_pipeline.cpp` (`main`, in the worker lifecycle test: after `reset_tracking` the tracker must hold no track).

### `AuthPipeline::detector`

```cpp
FaceDetector& detector();
```

**What it does.** Gives access to the main face detector that the pipeline built from the profile (for example to time it or to run it on a portrait). It is the detector of step 1 (whole frames) and of the registration from photos (`extract_from_image`). In worker mode this is the main thread's detector, not the worker's. It is **not** necessarily the detector that measures the head pose: for that use `pose_detector()`.

**How it works.** Returns `*detector_`. Only valid after a successful `init`.

**Used by.** `tools/bench.cpp` (`main`, to hand it to `bench_detection`, `bench_extraction` and `bench_pose_check` (in `tools/bench_detect_part.h` and `bench_extract_part.h`), and to tell whether `pose_detector()` is a different detector); `tests/test_pipeline.cpp` (`main`, to print the name, to detect the faces in the portraits and in the scene of video 4, and in the check that the pose detector is separate exactly when the profile names one); `tools/check_video.cpp` (`main`, to print the name).

### `AuthPipeline::pose_detector`

```cpp
FaceDetector& pose_detector();
```

**What it does.** Gives access to the detector that does the **pose check** (step 3, the refinement `refine_face`): the separate pose detector when the profile names one with `pose.detector_profile` (the shipped SCRFD profiles name YuNet), otherwise the main detector itself. Code that calls `refine_face` the way the pipeline does (tests, tools) must use this one, not `detector()`; with a SCRFD profile the pose measured by `refine_face(pipeline.detector(), ...)` would be the noisier SCRFD pose that the setting exists to avoid. The detector that `pose_detector()` returns must be used by one thread at a time, like every detector.

**How it works.** Returns `*pose_detector_` when that pointer is not null, else `*detector_` (a one-line inline function in the header). Only valid after a successful `init`. In worker mode this is the main thread's pose detector (used by the synchronous gate and by the callers); the worker has its own (see `Worker`).

**Used by.** `AuthPipeline::process` (synchronous mode: `run_gate_job(pose_detector(), *extractor_, job)`); `tools/bench.cpp` (`main`, which hands it to `bench_pose_check`, the row "pose refinement of 1 face"); `tests/test_pipeline.cpp` (`main`: prints `pose check by: <name>`, the check that a separate pose detector exists exactly when the profile names one, and the two `refine_face` calls: the pose of the portraits and the pose in the scene of video 4).

### `AuthPipeline::extractor`

```cpp
FeatureExtractor& extractor();
```

**What it does.** Gives access to the feature extractor of the pipeline (the main thread's, not the worker's).

**How it works.** Returns `*extractor_`. Only valid after a successful `init`.

**Used by.** `tools/bench.cpp` (`main`, which hands it to `bench_extraction` to time `extract`); `tests/test_pipeline.cpp` and `tools/check_video.cpp` (`main`, to print name and feature length).

### `AuthPipeline::match_threshold`

```cpp
float match_threshold() const;
```

**What it does.** Returns the similarity a match needs to reach before the track is marked verified (`score >= threshold`).

**How it works.** Returns `threshold_`, which `init` took from the profile (`recognizer.threshold` in the `.ini` file; the shipped profiles use 0.55 for `permissive.ini`, 0.50 for `classical.ini` and 0.40 for `dev_noncommercial.ini`, `dev_noncommercial_square.ini` and `dev_noncommercial_onnx.ini`; the built-in default of `ModelProfile` when the key is missing is `0.363f`).

**Used by.** `tests/test_pipeline.cpp` and `tools/check_video.cpp` (`main`, printed in the first line).

### `AuthPipeline::set_match_threshold`

```cpp
void set_match_threshold(float t);
```

**What it does.** Changes the similarity needed for a match. It affects only matches made after the call; tracks that are already verified stay verified.

**How it works.** Assigns `threshold_ = t`. No range check.

**Used by.** No caller in `src/`, `tools/` or `tests/`.

## src/pipeline/auth_pipeline.cpp

Includes `image_ops.h` (for `crop`), `pose.h` (for `pose_within_tolerance`), `timer.h` (for `now_ms`) and the C++ thread tools (`<thread>`, `<mutex>`, `<condition_variable>`, `<deque>`). All code belongs to namespace `fa`. The file has four parts: the unit of work (`GateJob`, `GateResult`, `run_gate_job`, `make_job`), the worker (`AuthPipeline::Worker`), the pipeline methods and the registration methods.

| Function | Purpose |
|---|---|
| `GateJob` (struct) | The input of one unit of work: a copy of the image region around a face plus the face. |
| `GateResult` (struct) | The output of one unit of work: pose, crop and feature. |
| `run_gate_job` | Do one unit of work (refine pose, crop, extract) with a given pose detector and extractor. |
| `make_job` | Copy the region of the frame around a tracked face into a `GateJob`. |
| `AuthPipeline::Worker` (struct) | The worker thread with its own pose detector and extractor, its job queue and its result list. |
| `Worker::~Worker` | Stop and join the thread. |
| `Worker::start` | Start the thread. |
| `Worker::shutdown` | Ask the thread to stop and wait for it. |
| `Worker::enqueue` | Add a job to the queue (refused when the queue is full). |
| `Worker::take_finished` | Move the finished results out (without waiting). |
| `Worker::in_flight` | Number of jobs waiting or running. |
| `Worker::wait_idle` | Wait until the worker has nothing left to do. |
| `Worker::loop` | The body of the thread: take a job, run it, store the result. |
| `AuthPipeline::AuthPipeline`, `AuthPipeline::~AuthPipeline` | Constructor and destructor (defined here because `Worker` is defined here). |
| `AuthPipeline::init` | Build detector (and, if the profile names one, a pose detector) and extractor from a profile, open the database (a CSV or a SQLite file, as the user chooses), optionally start the worker. |
| `AuthPipeline::runs_on_cpu` | True when every engine of the pipeline (detector, pose detector, extractor, and in worker mode the worker's own pose detector and extractor) runs on the CPU. |
| `AuthPipeline::reset_tracking` | Forget all tracks (start of a new video) and raise the generation. |
| `AuthPipeline::apply_result_state` | Take over what a job found into the track's state. |
| `AuthPipeline::match_results` | Match the features of all applied results (one M:N call against all registered people, or, during a card check, one call against the features that the cards stand for) and mark verified tracks. |
| `AuthPipeline::fill_card_status` | Fill `FrameResult::cards` for the current frame (card check only). |
| `AuthPipeline::process` | Handle one frame: detect, track, take over worker results, gate (job), match. |
| `AuthPipeline::flush` | Worker mode: wait for the worker and apply all results. |
| `AuthPipeline::enroll_track` | Register the person shown by a track, using the track's stored crop. |
| `AuthPipeline::extract_from_image` | Compute the feature of the largest face in a still picture. |
| `AuthPipeline::enroll_image` | Register the largest face of a still picture. |
| `AuthPipeline::remove_person` | Delete a registered person and un-verify the tracks that were matched to him. |
| `AuthPipeline::rename_or_refresh` | Change a person's name and/or replace his feature by one from a new photo. |
| `AuthPipeline::begin_card_check` | RF cards were read: look them up in the database FIRST, then compare the faces of the following frames only with the features stored for them. |
| `AuthPipeline::end_card_check` | Leave card-first mode: faces are searched among all registered people again. |

### `GateJob` (struct)

```cpp
struct GateJob {
    int track_id = 0;
    int generation = 0;
    double time_ms = 0;
    Image region;
    Face coarse;
};
```

**What it does.** Everything one unit of work needs, copied out of the camera frame so that the frame can be reused at once (the worker may take many frames of time).

| Field | Meaning |
|---|---|
| `track_id` | The track the job belongs to. |
| `generation` | The pipeline's `generation_` when the job was made. |
| `time_ms` | Capture time of the frame the job was made from. |
| `region` | The part of the camera image around the face: the face box enlarged by one box width / height on every side, clipped to the frame. |
| `coarse` | The face as the detector found it. Its box and landmarks are given in the coordinates of `region`, not of the camera image. |

**How it works.** A plain struct (declared in the header, defined here); filled by `make_job`.

**Used by.** `make_job`, `run_gate_job`, `Worker` (queue), `AuthPipeline::process`.

### `GateResult` (struct)

```cpp
struct GateResult {
    int track_id = 0;
    int generation = 0;
    double time_ms = 0;
    bool refined = false;
    Pose pose;
    bool pose_ok = false;
    bool has_crop = false;
    FaceCrop crop;
    bool has_feature = false;
    Feature feature;
    double refine_ms = 0, extract_ms = 0;
    bool applied = false;
};
```

**What it does.** What a job found.

| Field | Meaning |
|---|---|
| `track_id`, `generation`, `time_ms` | Copied from the job. |
| `refined` | The second look found the face again (`refine_face` succeeded). |
| `pose` | The exact head rotation (valid when `refined`). |
| `pose_ok` | The pose is inside the tolerance (`pose_within_tolerance`). |
| `has_crop` | A crop was cut out (needs `pose_ok`). |
| `crop` | The crop (landmarks in crop coordinates, as made by `make_face_crop`). |
| `has_feature` | The feature of that crop was computed. |
| `feature` | The feature. |
| `refine_ms`, `extract_ms` | How long the pose check (including cutting the crop) and the feature extraction took. |
| `applied` | Set by `apply_result_state` when the result was taken over into the track state; `match_results` ignores results that were not applied. |

**How it works.** A plain struct (declared in the header, defined here); filled by `run_gate_job`.

**Used by.** `run_gate_job`, `Worker` (result list), `apply_result_state`, `match_results`, `process`, `flush`.

### `run_gate_job`

```cpp
GateResult run_gate_job(FaceDetector& pose_detector, FeatureExtractor& extractor, const GateJob& job);
```

**What it does.** The unit of work of steps "gate" and "extract": check the head pose of the face of `job` with the second, zoomed-in look; if the pose is inside the tolerances, cut out a crop and compute its feature. It uses the pose detector and extractor that are passed in, which **must not be used by another thread at the same time**. The first parameter is called `pose_detector` because the function only uses it for `refine_face`; it never detects faces in a whole frame. It changes no pipeline data: it only reads the job and returns the result, so it can run on any thread. It lives in the anonymous namespace.

**How it works.**
1. Copies `track_id`, `generation` and `time_ms` into the result and starts a timer.
2. `refine_face(pose_detector, job.region, job.coarse, refined)`: the zoomed-in second look (on the job's region, not on the whole frame). If it succeeds: `refined = true`, the pose is taken from the refined face, `pose_ok = pose_within_tolerance(pose)`; if the pose is acceptable and `make_face_crop(job.region, refined, r.crop)` succeeds, `has_crop = true`.
3. `refine_ms` is the time of step 2.
4. If a crop exists, `extractor.extract(r.crop, r.feature)` computes the feature (`has_feature` is its result) and `extract_ms` is its time.
The same function runs inline in synchronous mode and on the worker thread in worker mode, so both modes behave the same way.

**Used by.** `Worker::loop` (worker mode, with the worker's own pose detector) and `AuthPipeline::process` (synchronous mode, with `pose_detector()`).

### `make_job`

```cpp
bool make_job(const Image& frame, const Track& track, double time_ms, int generation, GateJob& job);
```

**What it does.** Copies the part of the camera frame around a tracked face into a `GateJob`. Returns `false` (no job) when the region would be smaller than 8 pixels in width or height (a face practically outside the frame). It lives in the anonymous namespace.

**How it works.** The region is the face box (`track.face.box`) enlarged by one box width to the left and right and one box height up and down (rounded outward) and clipped to the frame; the reason (from the code comment): the pose check needs the box plus half a box of margin and the crop needs 30 percent. The pixels are copied with `crop` (from `image_ops.h`). The job gets the track id, the generation, the frame time and a copy of the track's face whose box and landmarks are moved into the coordinates of the region.

**Used by.** `AuthPipeline::process`.

### `AuthPipeline::Worker` (struct)

```cpp
struct AuthPipeline::Worker {
    std::unique_ptr<FaceDetector> pose_detector;
    std::unique_ptr<FeatureExtractor> extractor;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<GateJob> jobs;
    std::vector<GateResult> done;
    bool stop = false;
    bool busy = false;
};
```

**What it does.** The worker: a thread that takes jobs from a queue, runs them and puts the results into a list that the main thread collects. It has its **own** pose detector and extractor, because the main thread's objects keep internal buffers and are not thread-safe. The worker never detects faces in whole frames (the main thread did that and sent the face with the job); it only runs the refinement `refine_face`. So the member is called `pose_detector` (not `detector`), and it holds **only what the pose check needs**: with a profile that names a pose detector (the SCRFD profiles) the worker holds just a YuNet instance and **no SCRFD network at all**; with a profile that names none it holds a second instance of the profile's own detector.

| Member | Meaning |
|---|---|
| `pose_detector`, `extractor` | The worker's own networks (created in `init` with `FA_WORKER_NETWORK_THREADS` CPU threads each). `pose_detector` comes from `create_pose_detector(worker_profile)`, so it is the profile's pose detector (or a copy of the profile's own detector when the profile names none). |
| `thread` | The worker thread. |
| `mutex` | Protects everything below it: whoever reads or writes `jobs`, `done`, `stop` or `busy` holds it. |
| `wake` | Condition variable: "there is a job" or "stop". The worker sleeps on it while the queue is empty. |
| `idle` | Condition variable: "the worker has nothing left to do". `wait_idle` sleeps on it. |
| `jobs` | The queue of waiting jobs (`std::deque`). |
| `done` | The finished results, waiting to be collected. |
| `stop` | Set to ask the thread to finish. |
| `busy` | `true` while a job is running. |

**How it works.** The main thread calls `enqueue` and `take_finished`; the thread runs `loop`. The shared data is only touched while holding `mutex`; the slow part, `run_gate_job`, runs without the lock so that the main thread is never blocked for long.

**Used by.** `AuthPipeline` (member `worker_`), created in `init`.

### `Worker::~Worker`

```cpp
~Worker();
```

**What it does.** Stops and joins the thread when the worker is destroyed.

**How it works.** Calls `shutdown()`.

**Used by.** Runs when `worker_` is reset (`init`) or when the `AuthPipeline` is destroyed.

### `Worker::start`

```cpp
void start();
```

**What it does.** Starts the thread, which runs `loop`.

**How it works.** `thread = std::thread([this] { loop(); })`.

**Used by.** `AuthPipeline::init`.

### `Worker::shutdown`

```cpp
void shutdown();
```

**What it does.** Asks the thread to stop and waits until it has ended. A job that is running is finished first; jobs that are still queued are dropped.

**How it works.** Sets `stop` while holding the mutex, wakes the thread (`wake.notify_all()`) and `join`s it if it is joinable. Calling it twice is harmless.

**Used by.** `Worker::~Worker`.

### `Worker::enqueue`

```cpp
bool enqueue(GateJob&& job);
```

**What it does.** Adds a job to the queue and wakes the worker. Returns `false` (and does nothing) when `FA_WORKER_QUEUE_SIZE` (default `4`) jobs are already waiting; the job that is running is not counted. The caller (the gate) then tries again in the next frame.

**How it works.** Under the mutex: checks `jobs.size()`, moves the job into the queue; then `wake.notify_one()`.

**Used by.** `AuthPipeline::process`.

### `Worker::take_finished`

```cpp
void take_finished(std::vector<GateResult>& out);
```

**What it does.** Moves all finished results into `out` and empties the result list. It does not wait for running jobs.

**How it works.** Under the mutex: moves every element of `done` to the end of `out`, then `done.clear()`.

**Used by.** `AuthPipeline::process` (step 3a) and `AuthPipeline::flush`.

### `Worker::in_flight`

```cpp
int in_flight();
```

**What it does.** Returns the number of jobs that are waiting or running right now (queue size plus 1 if a job is running).

**How it works.** Under the mutex: `jobs.size() + (busy ? 1 : 0)`.

**Used by.** `AuthPipeline::process` (fills `FrameResult::jobs_in_flight`).

### `Worker::wait_idle`

```cpp
void wait_idle();
```

**What it does.** Blocks the calling thread until the queue is empty and no job is running.

**How it works.** Waits on the condition variable `idle` until `jobs.empty() && !busy`.

**Used by.** `AuthPipeline::flush`.

### `Worker::loop`

```cpp
void loop();
```

**What it does.** The body of the worker thread. It repeats: sleep until there is a job or a stop request; take the job; run it; store the result; and signal "idle" when the queue is empty. When `stop` is set it ends; queued jobs that were not started are dropped.

**How it works.** It holds the mutex except while it sleeps and while it runs a job. It waits on `wake` (which releases the mutex while sleeping) for `stop || !jobs.empty()`; on `stop` it leaves the loop. Otherwise it moves the first job out of the queue, sets `busy = true`, **unlocks**, runs `run_gate_job(*pose_detector, *extractor, job)`, **locks** again, appends the result to `done`, sets `busy = false` and, if the queue is empty, calls `idle.notify_all()`. After the loop it sets `busy = false` and notifies `idle` once more, so that nobody waits for a worker that has ended.

**Used by.** `Worker::start` (as the thread function).

### `AuthPipeline::AuthPipeline`, `AuthPipeline::~AuthPipeline`

```cpp
AuthPipeline();
~AuthPipeline();
AuthPipeline(const AuthPipeline&) = delete;
AuthPipeline& operator=(const AuthPipeline&) = delete;
```

**What it does.** Constructor and destructor of the pipeline. The destructor stops the worker thread, if there is one. Copying is deleted: a pipeline owns networks and a running thread.

**How it works.** Both are `= default`, but written in the .cpp file, because `std::unique_ptr<Worker>` needs the complete type `Worker` where it is destroyed. C++ destroys the members in the reverse order of their declaration, so `worker_` (declared last) goes first: `~Worker` joins the thread before the detector, the pose detector, the extractor and the store are destroyed.

**Used by.** Where an `AuthPipeline` variable is declared or goes out of scope: `tests/test_pipeline.cpp` (`main`, twice), `tools/bench.cpp` (`main`), `tools/check_video.cpp` (`main`).

### `AuthPipeline::init`

```cpp
bool init(const ModelProfile& profile, const std::string& store_path, bool async_worker = false,
          DatabaseFormat db_format = DatabaseFormat::Auto);
```

**What it does.** Prepares the pipeline. It creates the detector and the extractor that the profile asks for, and, when the profile names one with `pose.detector_profile`, a separate pose detector (for the shipped SCRFD profiles: a YuNet detector). It takes the match threshold from the profile and opens the database of registered persons: the file `store_path`, as a managed database (`FeatureStore::open`, see `store.md`), in the format the **user chooses** with `db_format`: `DatabaseFormat::Csv` (the default format of the project) or `DatabaseFormat::Sqlite`; with `DatabaseFormat::Auto` (the default of the parameter) the format follows the file: an existing file is recognised by its content, a new file by its extension (`.db`, `.sqlite`, `.sqlite3` = SQLite, anything else = CSV). An explicit choice that does not fit an existing file is refused: a SQLite file is never read as CSV or the other way round (`tools/db_convert` converts a database). The file is locked against a second program, and from now on every registration, deletion and change is written to it at once (and undone when the write fails). A **missing** file is fine: it is **created at once**, empty (so a folder that cannot be written is noticed here, not at the first registration). An **empty** path means "no file": the registered persons then live in memory only and nothing is written anywhere. With `async_worker = true` (default `false`) it also creates a second pose detector, a second extractor and the worker thread (worker mode). Returns `false` if a model cannot be created (also one of the worker's, and also the pose detector: for example when the named pose profile cannot be read), if the database cannot be opened (the file is damaged, is of the other format than the one asked for, cannot be created, or another program has the database open), or if the stored features have a different length than the extractor produces (the message says "wrong profile?"; the lock is released again). Reasons are printed to `std::cerr` (by `create_detector`, `create_pose_detector`, `create_extractor`, or by `init` itself, with the prefix `pipeline: `).

**How it works.**
1. `worker_.reset()` and `pose_detector_.reset()`: an old worker and an old pose detector (from an earlier `init`) are stopped and released, so a profile without a pose detector does not keep the previous one.
2. `create_detector(profile)` and `create_extractor(profile)`; if either returns `nullptr`, return `false`.
3. If `profile.pose_detector_profile` is not empty: `pose_detector_ = create_pose_detector(profile)`; if that returns `nullptr`, return `false`. (With an empty key `pose_detector_` stays null and `pose_detector()` returns the main detector, so no second network is loaded.)
4. Set `threshold_ = profile.match_threshold`.
5. Forget any card check of an earlier `init`: `card_check_ = false`, `active_cards_` and `card_best_` cleared.
6. If `store_path` is not empty: `store_.open(store_path, db_format)` (takes the lock, reads the file or creates it empty, binds the store to it). On failure print `pipeline: ` and `store_.last_error()` to `std::cerr` and return `false`. If the path is empty, `store_.close()` is called instead: the store is bound to no file.
7. If the store is not empty, compare the length of the first record's feature with `extractor_->dim()`. Only the first record is compared (the store itself refuses to mix lengths when records are added or opened). `FeatureStore::open` also refuses a database whose features are not finite numbers of length 1, which is reported by step 6. If the length does not fit, print `pipeline: the database holds features of length <n> but the extractor makes <m> (wrong profile?)`, call `store_.close()` to release the lock again, and return `false`.
8. If `async_worker`: a copy of the profile with `threads = FA_WORKER_NETWORK_THREADS` (default `1`) is made, the worker gets its own pose detector (`create_pose_detector(worker_profile)`) and extractor from it (the main thread's objects are not thread-safe), and if both exist the thread is started and kept in `worker_`; if one cannot be created the function closes the database again (the lock is released) and returns `false`. The worker does not get a copy of the main detector: it never detects faces in whole frames, so with a SCRFD profile it holds YuNet only. The worker's networks use only 1 CPU thread by default so that the other core stays free for the detection in the main thread.
9. Return `true`.

**Used by.** `tests/test_pipeline.cpp` (`main`, with a fresh `build\test_pipeline.csv` or, with `--sqlite`, `build\test_pipeline.db` (the format is passed explicitly: `DatabaseFormat::Sqlite` or `DatabaseFormat::Csv`); with `--async` the names get `_async` and `async_worker = true`; the worker lifecycle test also creates a second pipeline with `build\test_pipeline_async2.csv` or `.db`); `tools/bench.cpp` (`main`, with `build\bench_store.csv`, synchronous); `tools/check_video.cpp` (`main`, with `build\check_video.csv`; `async_worker` is its `--async` option).

### `AuthPipeline::runs_on_cpu`

```cpp
bool runs_on_cpu() const;
```

**What it does.** Answers "does every engine of this pipeline run on the CPU?". It takes no input and changes nothing. It returns `true` when the main detector, the pose detector (if the profile names a separate one), the extractor and, in worker mode, the worker's own pose detector and extractor all run on the CPU. **This is always the case:** the project uses no GPU (the target PC has none). The matcher is AVX2 code on the CPU and is not asked; the classical engines (Haar, LBP) are plain C++. The function exists so that "all engines run on the CPU" is a statement that a test and the tools can check, not a comment. It returns `false` before `init` or after a failed `init` (there is no detector or extractor then), and `false` in worker mode if a worker engine is missing.

**How it works.** `cpu = detector_ && extractor_ && detector_->runs_on_cpu() && extractor_->runs_on_cpu()`; if `pose_detector_` exists its `runs_on_cpu()` is added with "and"; if `worker_` exists, `worker_->pose_detector` and `worker_->extractor` must exist and answer `true` too (the worker's networks are separate objects from the main thread's, so they are asked separately). Each engine passes the question on: `FaceDetector::runs_on_cpu` and `FeatureExtractor::runs_on_cpu`, which for the deep engines ask their networks (`Net::runs_on_cpu`). The answer says what the code asked for; that no GPU library is loaded into the process is checked by the tests from the outside (`tests/cpu_only_check.h`).

**Used by.** `tests/test_pipeline.cpp` (`main`, at the very end, after all videos and the worker lifecycle test: the check `every engine of the pipeline (...) runs on the CPU`), `tools/check_video.cpp` (`main`, the line `all engines run on the CPU: yes (no GPU is used)`).

### `AuthPipeline::reset_tracking`

```cpp
void reset_tracking();
```

**What it does.** Forgets all tracks and everything the pipeline remembered about them (stored crops, verified flags), and also what the detector remembers from earlier frames. Track IDs start again at 1. The registered persons and the match threshold are not changed. Call it at the start of a new video or camera session. After it, `enroll_track` can no longer use the old crops. In worker mode, jobs that were already made for the old tracks may still be running; their results are thrown away when they arrive.

**How it works.** `tracker_.reset()`, `states_.clear()`, `++generation_` and `detector_->reset()`. The raised generation is what makes old results recognisable (`apply_result_state` compares it); a queued job of the old generation is still run by the worker, only its result is ignored. `FaceDetector::reset()` is a virtual function whose default does nothing; only a detector that follows faces from frame to frame has something to forget. The Haar detector overrides it (it clears its remembered face boxes and counters, so that the next detection is a full scan again); the YuNet and SCRFD detectors keep the default. Only the main detector (`detector_`) is reset; the code does not reset a separate pose detector or the worker's pose detector (the refinement works on a cut-out of one frame and the YuNet and SCRFD detectors have nothing to forget anyway).

**Used by.** `tests/test_pipeline.cpp` (`run_video`, at the start of every video, and the worker lifecycle test).

### `AuthPipeline::apply_result_state`

```cpp
bool apply_result_state(GateResult& result);
```

**What it does.** Takes over what a job found into the state of its track. Returns `false` and ignores the result when it was made before the last `reset_tracking` or when the track no longer exists; returns `true` and marks the result as `applied` otherwise.

**How it works.**
1. If `result.generation != generation_` return `false` (the result belongs to tracks from before `reset_tracking`).
2. Find the track's state in `states_`; if there is none (the track is gone) return `false`.
3. `job_pending = false`; `last_pose_ok = refined && pose_ok`; if the second look succeeded `last_pose` is set to the measured pose.
4. If a crop was made: the state's `crop` becomes a copy of the result's crop (with the track id and `time_ms` = the capture time of the frame the job was made from), `has_crop = true` and `last_crop_ms` = that capture time. The crop is stored even if the track has become verified in the meantime.
5. `applied = true`; return `true`.

**Used by.** `AuthPipeline::process` (step 3a for worker results; also right after an inline job in synchronous mode) and `AuthPipeline::flush`.

### `AuthPipeline::match_results`

```cpp
void match_results(std::vector<GateResult>& results, FrameResult* out);
```

**What it does.** Compares the M features of all **applied** results (the currently detected faces that got a new feature) with the registered people and marks the tracks that reach the threshold as verified. It makes **one** matcher call, however many faces there are, and which one depends on whether a card check is active:
* **No card check:** `store_.matcher().match(...)`, the M features against all N registered people (**one M:N call**). A track is verified when its best match exists (`index >= 0`) and its `score >= threshold_`.
* **Card check** (`begin_card_check`): `store_.match_cards(...)`. The cards are looked up in the database FIRST, and the M features are compared only with the N features that were retrieved for them (one M:N call on a subset of the database, see `FeatureStore::match_cards` and `FeatureMatcher::score_matrix`). A track is verified as the **owner of the card whose feature its face is most like**, when that similarity reaches `threshold_`. A card that is not in the database is compared with nothing, and nothing falls back to a search among all registered people.

`out` may be `nullptr`; if it is given, it receives the counters and the per-face status of the frame.

**How it works.**
1. For every applied result: if `out` is given and the result has a crop, the matching `FaceStatus` gets `crop_stored = true` and `crop_time_ms` = the capture time of the crop; if the result has a feature, `out->extractions` grows by 1. A feature is put on the list of queries only if the track is **not verified** (a verified track needs no new check; its crop was still stored). The list holds the M features (`queries`) and the track of each (`query_track`).
2. A small local function, a lambda named `publish`, copies the new state of the tracks (verified, person id, name, `last_score` as `score`) into the `FaceStatus` list of the frame, if `out` is given.
3. **During a card check:** if there are no queries, return. Otherwise `store_.match_cards(queries, active_cards_, compared)`: ONE call for all M faces against the cards of the check (the return value is not used: the `found` flag of every card in `compared.cards` carries the information). `out->matched_queries` is M. For every query the track's `last_score` is the best similarity of the face to any found card (`-2`, `kNoScore`, when no card was found); if the face has a best card (`best_card >= 0`) and the score is at or above `threshold_`, the track becomes **verified** with the id and name of that card's owner (the record `compared.cards[best_card].record` of the store). Then for every card `card_best_[c]` is raised to the best similarity that any face had to its owner in this call (that is what `CardStatus::score` reports), `publish` is called and the function returns. The faces are not compared with anything else, so a registered person who shows somebody else's card gets a low score and stays unverified, even though a search among all people would have found him.
4. **Without a card check:** if there are no queries or the store is empty, return (no matching, `matched_queries` stays 0). Otherwise `store_.matcher().match(queries, matches)`: ONE M:N call for all of them against all N registered people (the registered table is read once). `out->matched_queries` is M. For every query the best score becomes the track's `last_score`; if the best match exists (`index >= 0`) and `score >= threshold_`, the track becomes **verified** with the person's id and name taken from the store record. The second-best score returned by the matcher is not used. Then `publish`.

**Used by.** `AuthPipeline::process` (with the frame's result) and `AuthPipeline::flush` (with `nullptr`).

### `AuthPipeline::fill_card_status`

```cpp
void fill_card_status(FrameResult& result) const;
```

**What it does.** Private. Fills `result.cards` for the current frame: one `CardStatus` for every card of the active card check, in the order given, or nothing when no check is active. It is `const`: it reads the pipeline and writes only into `result`.

**How it works.** `result.cards.clear()`; if no card check is active, return. For every card `c` of `active_cards_`: `card_id` is the card as given; `score` is `card_best_[c]`; the card is looked up again with `store_.find_by_card` (every frame, because the database may have changed in the meantime: a card whose owner was deleted becomes `found = false` from the next frame on, and a card that was given to somebody becomes found). If an owner exists: `found = true`, `person_id` and `person_name` are his, and the faces of `result.faces` are searched for the first one that is `verified` and whose `person_id` is the owner's: then `matched = true` and `track_id` is that face's track. The entry is appended.

**Used by.** `AuthPipeline::process` (after `match_results`, so the statuses of the faces are up to date).

### `AuthPipeline::process`

```cpp
bool process(const Image& frame, double time_ms, FrameResult& result);
```

**What it does.** Handles one camera frame. `frame` is a BGR image of any size, `time_ms` is its capture time in milliseconds (any zero point; it should not go backwards, because all interval rules compare it with earlier times; if it does go back for a track, the timers of that track are reset, see step 3b). `result` is overwritten with the outcome. Returns `false` only when the **detector** itself fails (nothing is tracked or stored in that case and `result` is empty); in every other case it returns `true`, even if a crop or a feature could not be computed. In worker mode the call does **not** wait for the worker: results of earlier frames are picked up, new jobs are queued.

The settings that control the frame (all in `src/common/config.h`; defaults quoted exactly):

| Setting | Default | Role in `process` |
|---|---|---|
| `FA_MIN_CROP_FACE_PIXELS` | `80` | A face narrower than this many pixels (width of the tracker's box in the camera image) is never cropped. |
| `FA_POSE_CHECK_INTERVAL_MS` | `100` | A track that is waiting for an acceptable pose is checked at most once per this many milliseconds. |
| `FA_CROP_STORE_INTERVAL_MS` | `1000` | An unverified track gets at most one new crop per this many milliseconds (the older crop is overwritten). The time of the last crop is the capture time of the frame it was cut from. |
| `FA_POSE_PITCH_TOL_DEG` / `FA_POSE_YAW_TOL_DEG` / `FA_POSE_ROLL_TOL_DEG` | `5.0f` / `5.0f` / `8.0f` | The pose gate: `pose_within_tolerance` needs a valid pose with `abs(pitch) <= 5`, `abs(yaw) <= 5`, `abs(roll) <= 8` degrees (the limits themselves are accepted). |
| `FA_REFINE_FACE_WIDTH` | `320` | The zoom of the second look: the face is enlarged to about this many pixels wide (never shrunk). |
| `FA_TRACK_MAX_MISSED_FRAMES` | `15` | A track not seen for more than this many frames is removed by the tracker. |
| `FA_REVERIFY_AFTER_MISSED_FRAMES` | `3` | A **verified** track that was not found in this many frames in a row (or more) loses its verification when it is found again (during a card check it is then matched against the cards again). A shorter gap, for example one missed detection, keeps it. |
| `FA_WORKER_NETWORK_THREADS` | `1` | Worker mode: CPU threads of each of the worker's own networks (used in `init`). |
| `FA_WORKER_QUEUE_SIZE` | `4` | Worker mode: at most this many jobs wait for the worker; when the queue is full the track simply tries again in the next frame. |
| profile `match_threshold` | per profile | Score needed to mark a track verified. |

**How it works.** The steps of every frame, in order (the same five steps as in `ARCHITECTURE.md`, with step 3 split in two):

- **Step 1, detect.** `detector_->detect(frame, faces_)` finds all faces in the whole frame (the detector resizes the image itself and returns boxes and landmarks in camera-image pixels). This is always the main detector, never the pose detector. If this call returns `false`, `process` returns `false` at once.
- **Step 2, track.** `tracker_.update(faces_)` matches the detections to the existing tracks and starts new tracks. Afterwards `process` erases the memory (`states_` entry) of every track that the tracker no longer has.
- **Step 3a, results of finished jobs (worker mode only).** `worker_->take_finished(results)` collects the jobs that finished since the last frame, and `apply_result_state` takes each over into its track (results of an older generation or of dead tracks are ignored). `result.worker_ms` adds up their work time. This is done *before* the gate, so that the gate below already knows about a new crop and about the job no longer being pending.
- **Step 3b, gate: pose check and crop for unverified tracks.** This step loops over all tracks that the tracker has. A track that was **not** seen in this frame (`updated == false`) only gets its `missed_run` counter increased by 1; it is otherwise skipped and gets no `FaceStatus`. For a track that **was** seen in this frame the following is done, in this order:
   * **Re-verification rule.** If the track is verified and its `missed_run` is at least `FA_REVERIFY_AFTER_MISSED_FRAMES` (3), the verification is dropped: `verified` becomes `false`, the person id and name are cleared, `last_score` is set to 0 and `last_crop_ms` is reset to `-1e9` (so a crop is due at once). The reason (from the code comment): a different person may have stepped into the same spot and received the same track ID, and verification is a statement about one continuously followed face. The tracker itself keeps the track (and its ID) for up to `FA_TRACK_MAX_MISSED_FRAMES` (15) missed frames, so a gap of 3 to 15 frames gives the same ID but a new verification; a gap of 1 or 2 frames keeps the verification; a longer gap gives a new track anyway. The stored crop and the last pose-check time are not changed by this rule.
   * After this rule `missed_run` is set to 0.
   * **Time going backwards.** If `time_ms` is smaller than the track's `last_crop_ms` or `last_check_ms` (for example because a new recording starts without `reset_tracking`), that timer is set back to `-1e9`, so the track does not have to wait for the old time to come round again before a crop is stored.
   * A `TrackState` is created when it does not exist yet, and a `FaceStatus` is prepared.
   * **Only unverified tracks are cropped.** A crop is *due* when all three conditions hold: the track is not verified; the width of the track's box is at least `FA_MIN_CROP_FACE_PIXELS` (80); and at least `FA_CROP_STORE_INTERVAL_MS` (1000 ms) have passed since the last stored crop (a track that never had a crop is due at once).
   * **Pose check at most every 100 ms, one job at a time.** If a crop is due, **no job of this track is pending** and at least `FA_POSE_CHECK_INTERVAL_MS` (100 ms) have passed since the last pose check, a job is made with `make_job` (a copy of the region around the face; if the region is too small nothing is done). `last_check_ms = time_ms` is stored. The job does the second, zoomed-in look (`refine_face`: the face is cut out with a margin, enlarged to about 320 pixels wide and detected again, a second time when the measured roll is 3 degrees or more, see `detector.cpp`; the pose is computed from the refined landmarks with the pose model for `<detector type>-refined`; the detector that runs this look is the **pose detector**, which is YuNet for the SCRFD profiles, so for them the model is `yunet-refined`), checks the **refined pose gate** (`pose_within_tolerance`), and if the pose is accepted cuts the crop and computes its feature (`run_gate_job`). Because the condition is tested once per frame, the real time between two checks is rounded up to a whole number of frames (at 15 frames per second, about 133 ms).
   * **Synchronous mode:** the job runs right here with the pipeline's pose detector (`pose_detector()`) and extractor. `result.gate_ms` receives its time without the extraction, `result.extract_ms` the time of the extraction; the result is taken over at once (`apply_result_state`) and kept for the matching step.
   * **Worker mode:** the job is handed to the worker with `enqueue`; if that succeeds the track is `job_pending`. If the queue is full, `last_check_ms` is set back to `-1e9`, so the track tries again in the next frame. The answer arrives in step 3a of a later frame.
   * **Crop stored once per second, overwriting.** When a result with a crop is applied, the track's previous crop is **overwritten**, `has_crop` is set and `last_crop_ms` becomes the capture time of the frame the job was made from; the next crop can come no earlier than 1000 ms after that frame. If the refinement failed or the pose is outside the tolerances, no crop is stored and the track is simply checked again at the next frame that is at least 100 ms later.
   * The `FaceStatus` gets the most exact pose known (see `FaceStatus`), `pose_ok`, and the verified/person/score fields from the `TrackState`.
- **Steps 4 and 5, extract and match once.** The feature extraction (step 4) is part of the job, so what is left here is the matching. The features of the results that were applied in this frame (made inline in synchronous mode, or arrived from the worker) are passed to `match_results`: it sets `crop_stored` / `crop_time_ms` in the statuses, counts `result.extractions`, and makes **one matcher call** for all of them: normally ONE M:N call (M features against all N registered people; the registered table is read once, which is what makes the M:N matcher fast); while a card check is active (`begin_card_check`) ONE `FeatureStore::match_cards` call instead (the M features against the N features that the cards stand for, and nothing else). A score at or above `threshold_` marks the track verified. Then `fill_card_status` fills `result.cards` (card check only). `result.match_ms` is the time of these two steps, `result.jobs_in_flight` (worker mode) is read from the worker, and `result.total_ms` is the time of the whole `process`.

   **Verified tracks no longer cropped, extracted or matched.** Because `verified` is part of the "crop is due" test, a verified track makes no new job from the next frame on: no pose check, no crop, no extraction, no matching. It is only tracked (the tracker keeps its ID alive), until it loses the verification (re-verification rule, `remove_person`, or `rename_or_refresh` with a new photo). In worker mode a result that arrives for a track that has meanwhile become verified still stores its crop, but its feature is not matched. If the tracker loses a track for more than 15 frames and the person returns, he is a new track and goes through the steps again.

Example with the defaults (an illustration of the rules, not a measurement), synchronous mode: a new face 100 pixels wide appears at 0 ms. It is wide enough and has no crop yet, so at once a job is made. If the head is tilted 12 degrees, the gate refuses it; the next check is made at the first frame at or after 100 ms. When a check finally passes, at time T, the crop is stored, its feature is computed and matched in the same frame. If the score is below the threshold, the track stays unverified and the next crop is possible at T + 1000 ms (it replaces the old one). If the score reaches the threshold, nothing further is stored, extracted or matched for that track. Now the verified person leaves the picture for 7 frames and somebody else steps into the same spot: the tracker keeps the track because 7 is not more than 15 missed frames, but 7 is at least 3, so when a face is found again the verification is dropped, a crop is due at once, and the newcomer has to be recognised on his own face (`tests/test_pipeline.cpp`, video 4, tests exactly this). In worker mode the same happens, except that the result of the job made at time T arrives a few frames later and the crop time is T.

**Used by.** `tests/test_pipeline.cpp` (`run_video`, once per generated frame, plus one extra frame after the rename and two frames in the worker lifecycle test) and `tools/check_video.cpp` (`main`, once per frame of the video). Nothing else calls it.

### `AuthPipeline::flush`

```cpp
void flush();
```

**What it does.** Worker mode: waits until the worker has finished everything that is queued and applies all results, so that a stored crop or a verification is visible at once (it is used by the tests and before registering a person). In synchronous mode it does nothing. The effect shows in the track states; no `FrameResult` is filled, so a crop that arrives this way is not reported as `crop_stored` in any frame.

**How it works.** `worker_->wait_idle()` (blocks until no job is waiting or running), `take_finished`, `apply_result_state` for each result, then `match_results(results, nullptr)`, so features that arrive this way are matched (and can verify tracks) like in `process`, also against the cards while a card check is active.

**Used by.** `AuthPipeline::enroll_track`, `tools/check_video.cpp` (`main`, once after the last frame so the last results are applied) and `tests/test_pipeline.cpp` (`main`, worker lifecycle test).

### `AuthPipeline::enroll_track`

```cpp
bool enroll_track(int track_id, const std::string& person_id, const std::string& name, const std::string& card_id = std::string());
```

**What it does.** Registers the person who is currently shown by track `track_id` (for example "this stranger is now employee E007"). It uses the face crop that the pipeline has stored for that track, adds the feature to the store under `person_id` and `name` (and, when `card_id` is not empty, with that RF card number: the card must not belong to somebody else already), and marks the track as verified as that person. The store writes the database file as part of the registration (write-through, see `FeatureStore::add`); there is no separate save. Returns `false` if the track has no stored crop (a message is printed), if the feature cannot be computed (no message), or if the store refuses the registration: `person_id` is already registered or empty, `card_id` belongs to another person, the feature, the name or the card number is not acceptable, or the database file cannot be written (then the registration has been undone).

**How it works.** It first calls `flush()` (in worker mode this makes sure that the newest crop of the track has been taken over; it does nothing in synchronous mode). It looks up the track in `states_`; without an entry or without `has_crop` it prints `enroll: track N has no stored face crop` and fails. It then computes the feature from the stored crop with `extractor_->extract` (the main thread's extractor; the feature is not kept from earlier matching, it is computed again), builds a `Record` (with `card_id`) and calls `store_.add` (which refuses duplicate ids and duplicate cards, features of another length, features that are not finite numbers of length 1 and ids, names or card numbers with line breaks, and writes the database file; see `FeatureStore::add`). If `add` returns `false`, the reason (`store_.last_error()`) is printed on `std::cerr` with the prefix `enroll: ` and the function returns `false`: the person is neither in the memory store nor in the file (a failed write was undone by the store), and the track is not marked verified. Only after `add` has returned `true` does it set `verified`, `person_id` and `person_name` in the track's state and return `true`. So memory, file and track state cannot disagree after a failed write.

**Used by.** `tests/test_pipeline.cpp` (`main`, "register the stranger from the face crop stored for his track").

### `AuthPipeline::extract_from_image`

```cpp
bool extract_from_image(const Image& photo, Feature& feature, Face* face = nullptr);
```

**What it does.** Computes the feature of the largest face in a still picture (for example an ID portrait) without registering anything. `photo` is a BGR image. `feature` receives the result. If `face` is not `nullptr`, it receives the detected face (box and landmarks in the coordinates of `photo`, not of the crop). Returns `false` if no face is found, the largest face has no landmarks, the crop cannot be cut, or the extractor fails.

**How it works.** `detector_->detect(photo, faces)`; the face with the largest box area wins (the first one on a tie); `make_face_crop` cuts it out; `extractor_->extract` computes the feature. It always uses the main detector (`detector_`, not the pose detector) and the main thread's extractor, also in worker mode, so it must be called from the thread that calls `process`. Unlike the live path, this function does **not** use `refine_face` (it works with the landmarks of the first detection) and it applies neither the pose gate nor the minimum face width: any face the detector accepts can be registered. The detector's own limits still apply (its score threshold and, for the deep detectors, the minimum face size `FA_MIN_FACE_PIXELS`, default `40`).

**Used by.** `AuthPipeline::enroll_image` and `AuthPipeline::rename_or_refresh`. No caller passes the `face` argument.

### `AuthPipeline::enroll_image`

```cpp
bool enroll_image(const Image& photo, const std::string& person_id, const std::string& name, const std::string& card_id = std::string());
```

**What it does.** Registers a person from a still picture: finds the largest face in `photo`, computes its feature, and adds it under `person_id` and `name`, with the RF card number `card_id` when one is given (it must not belong to somebody else); the store writes the database file as part of the registration (no separate save). Returns `false` if no feature can be made (no message), if `person_id` is empty or already registered, if `card_id` belongs to another person, if the store refuses the record (for example a feature that is not a finite vector of length 1, or a name with a line break), or if the database file cannot be written (then the registration has been undone).

**How it works.** Builds a `Record` (with `card_id`), fills its feature with `extract_from_image` and calls `store_.add`. If that returns `false`, the reason is printed on `std::cerr` with the prefix `enroll: ` and the function returns `false` (as with `enroll_track`, a failed write leaves no trace: the person is not in memory either). Otherwise it returns `true`.

**Used by.** `tests/test_pipeline.cpp` (`main`, registers the first 20 portraits) and `tools/check_video.cpp` (`main`, registers the people of the scenario and the filler people from their portraits).

### `AuthPipeline::remove_person`

```cpp
bool remove_person(const std::string& person_id);
```

**What it does.** Deletes a registered person; the database file is rewritten without him before the function returns (no separate save). Returns `false` if the id is unknown, or if the database file cannot be written (then the person has **not** been deleted: the store put him back at his place). The reason is printed on `std::cerr` with the prefix `remove: `. After a successful deletion, tracks that were verified as this person are made unverified again, so the person is checked again (and no longer recognised) from the next frame on.

**How it works.** `store_.remove(person_id)`; if that returns `false`, print `remove: ` and `store_.last_error()` and return `false` without touching any track. Otherwise every `TrackState` whose `person_id` equals the removed id gets `verified = false`, empty id and name, and `last_crop_ms = -1e9` (so a new crop is due immediately; `last_score` and the stored crop are left as they are), and the function returns `true`.

**Used by.** `tests/test_pipeline.cpp` (`main`, "delete the person").

### `AuthPipeline::rename_or_refresh`

```cpp
bool rename_or_refresh(const std::string& person_id, const std::string& new_name, const Image* new_photo);
```

**What it does.** The "modify" operation. It changes the display name of a registered person, replaces the person's stored feature by the feature of a new photo, or both. An empty `new_name` keeps the old name; a `nullptr` for `new_photo` keeps the old feature. Returns `false` if a photo is given but no feature can be made from it, if the id is unknown, if the new feature has another length than the stored one or is not a finite vector of length 1, if the new name contains a line break, or if the database file cannot be written (then the old name and feature are restored). The reason is printed on `std::cerr` with the prefix `modify: `. Otherwise the database file already holds the change when the function returns `true` (no separate save). Faces that are being tracked as this person at the moment follow the change at once (see below).

**How it works.** If `new_photo` is not `nullptr`, `extract_from_image` fills `fresh`; otherwise `fresh` stays an empty `Feature`. Then `store_.update(person_id, new_name, fresh)` applies the rule "empty name or empty feature means keep the old one" and writes the database file. If it returns `false`, print `modify: ` and `store_.last_error()` and return `false`; no track is changed. After a successful update, every live track that is **verified as this person** is changed too: if `new_name` is not empty its `person_name` becomes the new name (so the next `FaceStatus` shows it at once); if a new feature was given (`fresh` is not empty) the track is un-verified (`verified = false`, empty id and name, `last_crop_ms = -1e9`), because the old verification was made with the old feature and no longer proves anything, so the track is cropped and matched again. `last_score` is not changed. The function then returns `true`. A failed write cannot leave the tracks changed while the store is not, because the tracks are only changed after `update` has succeeded.

**Used by.** `tests/test_pipeline.cpp` (`main`, only the form with `nullptr`: "modify: the name changes", followed by a check that a face that is still being tracked as this person shows the new name in the next frame). The photo form is not used by any test.

### `AuthPipeline::begin_card_check`

```cpp
bool begin_card_check(const std::vector<std::string>& card_ids);
```

**What it does.** Starts a **card-first check**. It tells the pipeline that one or more RF cards have been read (the hardware that reads them is not part of this program: the card numbers are handed to this function). The card information is **always identified first**: every card is looked up in the database and the feature stored with it is retrieved. From then on the faces detected in the frames that follow (M of them in a frame) are compared with exactly those N retrieved features and with nothing else. M and N are independent of each other: fewer faces than cards, as many, or more. The rules:
* A face is verified as the owner of the card whose feature it is most like, when the similarity reaches the threshold. So the owner who shows his own card is verified; a registered person who shows somebody else's card is **not** verified (his face is compared with the owner's feature only, not with everybody), and a stranger is not either.
* A card that is **not in the database** is a **failed comparison**: the line `card: '<card>' is not in the database: the comparison for it failed` is printed on `std::cerr`, the card is reported as `found = false` in `FrameResult::cards`, nothing is compared for it, and the function returns `false`. The other cards of the list still work. There is **never a fallback** to a search among all registered people while the check is active. If the owner of a card is deleted during the check, the card stops being found from the next frame on.
* Verifications from before are reset: a face that was verified earlier (also by the normal search) is checked again against the cards, because its old verification proves nothing for this check.
* The check stays active until `end_card_check()`. Calling `begin_card_check` again replaces the list of cards.

Returns `true` only if `card_ids` is not empty and every card was found. With an empty list it prints `card: no card given`, returns `false` and starts nothing (an active check stays as it was). With a list in which some card is unknown it returns `false`, but the check **is** active anyway, with all the cards of the list: the cards that were found can verify faces, the unknown ones are reported as not found.

**How it works.**
1. An empty list: print `card: no card given` and return `false`.
2. `flush()`: in worker mode the results that are still in flight are taken over and matched first, in the mode that was in force until now; in synchronous mode nothing happens.
3. `card_check_ = true`, `active_cards_ = card_ids`, `card_best_` set to `kNoScore` for every card.
4. `store_.match_cards(no faces, card_ids, probe)`: with no faces nothing is compared, but every card is looked up now; its return value (every card found) is the return value of this function, and a line is printed for each card that is not found. (`match_results` calls `match_cards` again for every frame with new features, so a card that appears in the database later is found then.)
5. For every `TrackState`: `verified = false`, empty person id and name, `last_score = 0` and `last_crop_ms = -1e9`, so that a crop of the face is due at once. The stored crops and the pose-check times are not touched.
6. Return the result of step 4.

**Used by.** `tests/test_pipeline.cpp` (`run_video`, before the first frame of a video when it is given a list of cards: video 6). No tool calls it; there is no finished camera application that reads an RF card.

### `AuthPipeline::end_card_check`

```cpp
void end_card_check();
```

**What it does.** Leaves card-first mode. From the next frame on, unverified faces are searched among all registered people again (`match`), and `FrameResult::cards` is empty. Faces that were verified by the check **stay verified**: only the normal rules take a verification away (a gap of `FA_REVERIFY_AFTER_MISSED_FRAMES` frames, `remove_person`, `rename_or_refresh` with a new photo, or a new `begin_card_check`). It does nothing when no check is active.

**How it works.** `card_check_ = false`, `active_cards_.clear()`, `card_best_.clear()`.

**Used by.** `tests/test_pipeline.cpp` (`run_video`, after the last frame of a video that had a card check).

## Where code and comments disagree

One small point, below. Otherwise the comments of `auth_pipeline.h` (the five steps, the exception for re-verification, the "TWO MODES" text, the comments of `FrameResult`, `FaceStatus` and `CardStatus`, the "card first" text), `ARCHITECTURE.md` and the comments in `config.h` agree with the code. The point: the comment of `begin_card_check` says that after an unknown card the check is active "with the cards that were found". The code keeps every card of the list in `active_cards_`, the unknown ones too; they are reported as `found = false` in every `FrameResult::cards` and nothing is compared for them, so the effect is the one the comment describes. Points to read carefully:

* The 1000 ms limit is for the *stored crop*; while a track is waiting for an acceptable pose, the second look (pose check, one extra detector run, two when the head is tilted by 3 degrees or more) can run again every 100 ms (rounded up to whole frames).
* `FrameResult::gate_ms` ("pose check and crop") does not include copying the image region for the job (`make_job`); that time is only inside `total_ms`.
* The header says a result arrives "usually 2 to 10 frames later" in worker mode. The code has no such limit: it depends on the speed of the machine and the length of the queue (`FA_WORKER_QUEUE_SIZE`); `RESULTS.md` has the measurements.

Not a disagreement, but worth knowing: `AuthPipeline::uses_worker()` and `AuthPipeline::set_match_threshold()` have no caller in `src/`, `tools/` or `tests/`, and no caller passes the `face` argument of `extract_from_image`. Two faces that both look like the same card's owner are both verified as him (each face is verified as the owner of the card it is most like; nothing says that a card can match only one face); `CardStatus::track_id` then names the first of them.

Changes made through `AuthPipeline::store()` advance the store revision. Before the next `process()` or `flush()`, the pipeline discards cached verification and prior worker results and resets card comparison scores. Failed database changes leave the revision and current verification unchanged. Pipeline management methods keep their existing targeted track updates.
