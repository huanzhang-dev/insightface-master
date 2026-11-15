# Plate evidence, observation time, and review fixes

The composite demo deliberately draws AB1234, 789012, and TR4567 on white labels. Recognizing these labels demonstrates reading inserted text; it does not establish recognition of the original vehicle plates or real-world plate accuracy. Vehicle classes and track IDs are separate from plate text and can be inferred when no plate is readable.

## Production and synthetic behavior

By default only regions proposed by the learned plate detector are passed to OCR. White-rectangle/text heuristics are disabled. `--allow-text-candidates` explicitly restores them for synthetic demonstrations and adds a visible SYNTHETIC TEXT TEST banner; performance.json records synthetic_text_mode. The test-video launcher enables this mode, while the camera launcher does not. A detector proposal can still be a false positive: source='detector' means model evidence, not verified registration identity.

Every nonempty OCR output has an observation timestamp, full-frame plate_box, evidence_source, and evidence_image in recognition.jsonl. The PNG in output/evidence is copied from the original source frame used by inference before lighting normalization. It is not cropped from the later display frame. Inspect that crop to determine which pixels produced the reading. Failure to save evidence aborts rather than logging an unauditable read. Continuous runs accumulate evidence images alongside logs.

A track admits a plate hypothesis only when plateDetected is true, plateBox has positive area, and plateSource is detector or the explicitly enabled heuristic_test. Text length is 4 to 12 Unicode scalars and confidence must be at least 0.50. Mean confidence below 0.75 requires three matching observations; higher confidence requires two. Observation times must increase; duplicate timestamps do not count. This supports the Chinese 0.53/0.54 fixtures without accepting a single weak reading as a stable value. Scores and repeated text remain fallible. At most 32 text hypotheses are retained per track.

## Current versus historical

plateVisible indicates that the currently matched observation supports the consensus text. A detection without an eligible matching read immediately clears it. The projected view also clears it after 0.5 seconds without a supporting plate observation. plateStatus returns observed, unconfirmed, historical, or not_observed. The display shows Characters/Numbers only for observed readings; otherwise it says not currently read. It never silently displays remembered text as a current observation.

tracks.csv appends plate_status, plate_source, observed_time_s, and last_read_text to the earlier schema. The plate/characters/numbers columns are empty and stable is 0 for noncurrent or unconfirmed text. last_read_text explicitly preserves the consensus history. observed_time_s is the last supporting plate observation, not the current display time. Historical confidence/source fields describe retained evidence, not a new reading.

## Timestamp correction

The application predicts/corrects the authoritative Kalman tracker at result.timestamp. It does not label delayed measurements with the current camera time. Tracker::project then calculates the display position at the current frame timestamp using the estimated velocity without advancing the authoritative filter. Initial velocity is unknown; short-history estimates can still lag. Positions between AI updates are estimates, not new detections. Results older than maxAge remain discarded, and tracks expire after two seconds without a vehicle observation.

## Functions added

### `car::Tracker::project`

```cpp
Tracker project(double now) const;
```

Source: [src/Car.cpp](../src/Car.cpp). Input is a display timestamp at or after the latest measurement time. Returns a projected tracker for read-only drawing/export: extrapolates box centres with stored velocity, omits tracks older than two seconds, and ages plate visibility. Does not call Kalman prediction or alter the source filter. The returned object shares reference-counted filter matrices and is intended only as a read-only view; do not mutate its filters or use it as the authoritative tracker. No camera/model work occurs.

### `car::plateStatus`

```cpp
std::string plateStatus(const Track& track);
```

Source: [src/Car.cpp](../src/Car.cpp). Accepts a current or projected track and returns observed when stable and visible, unconfirmed when visible but not stable, historical when only retained text exists, or not_observed when no text is stored. No mutations or I/O. Call on a projected view to include current-age handling.

### `car::runReviewTests`

```cpp
static int runReviewTests();
```

Source: [src/Tests.cpp](../src/Tests.cpp). Runs ten regression checks for moderate-confidence Chinese consensus, rejection without plate-region evidence, single-read rejection, duplicate-time rejection, hidden-plate history, projection aging/expiry, delayed-state extrapolation without modifying the source box, safe overlays from 1x1 to 80x80, and heuristic-free production defaults. Returns the count or throws through require. No camera is opened. The full self-test suite now has 55 checks.

### `car::generateEvidenceVideos`

```cpp
int generateEvidenceVideos(const std::string& directory);
```

Source: [src/TestVideo.cpp](../src/TestVideo.cpp). Reads the existing photographic composite and source photos from an existing directory, restores the original photograph for each vehicle instead of its inserted label, and writes composite_no_labels.avi plus a JPEG sample. Returns 0; missing inputs or unwritable output throw. This negative fixture removes the inserted strings but is not a guarantee that the original photos contain no actual plate/text. The validator requires that the removed artificial strings never reappear, not that the detector emit no proposals whatsoever.

## Existing functions updated

- observe now checks spatial/provenance evidence, counts independent observation times, uses the two/three-observation confidence rule, clears visibility when appropriate, and bounds history.
- Tracker::predict operates on observation times. Its dt has a 0.001-second floor and no former 0.5-second cap; source times must be monotonic.
- Pipeline::Pipeline stores allowTextCandidates; detect uses detector proposals by default, marks provenance and retains exact raw plate crops. plateCandidates remains the test-only heuristic implementation and is not called by production defaults.
- run applies measurement times correctly, projects a read-only display view, saves evidence PNGs, exports current/history fields, and labels test mode. main parses --allow-text-candidates and --generate-evidence and rejects nonfinite confidence/max-age options.
- draw clips label panels to frame bounds and skips text on frames below 80 pixels wide or 60 pixels high; boxes/trails still use OpenCV clipping. Empty input returns immediately. This fixes the reproduced 32x32 crash.
- runTests calls runReviewTests as well as the original tracking/OCR/lighting checks. Synthetic tracker fixtures explicitly supply plate-region evidence.

## Fresh validator

`powershell -ExecutionPolicy Bypass -File tools/validate_outputs.ps1` builds both toolsets, runs both current executables, extracts the actual self-test counts, regenerates the no-label fixture, and runs new 15/30 FPS synthetic previews plus detector-only positive/negative clips. It checks current/history exports, source timestamps, Unicode symbol reconstruction, detector-only provenance and saved evidence images. It records binary/source hashes, logs, output directory and UTC start/completion times in tests/verification.json. Status is running, passed, or failed; historical files are never used to grant a new pass. Each invocation creates output/verification_<UTC stamp>.

All footage tests remain synthetic/composite, physical_camera_tested is false, and real_plate_accuracy_established is false. Earlier unicode_verification.json and lighting_comparison.json files are historical measurements from before this correction; use the fresh verification.json for current behavior.

Conflicting reads update lastPlateObservation (deduplication time) but do not refresh lastPlateSeen or plateSource for the historical consensus. An additional regression verifies that conflicting duplicate frames cannot advance historical timestamps or count as multiple votes.

tracks.csv also exports vehicle_observed_time_s (the source time of the last vehicle detection). If detections stop for more than the two-second timeout, a later detection receives a new monotonically increasing ID. Fresh validation records identity segments and permits an ID restart only after that observation timeout; overlapping/continuous observation segments with different IDs still fail. This exposes detector dropouts instead of claiming unconditional ID persistence or comparing against wall-clock display time.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.
