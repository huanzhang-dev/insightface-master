# Persistent vehicle IDs and review videos

Assigned IDs are immutable within a tracking session. The tracker now retains lost identities in a session archive instead of deleting their identity after two seconds without detections. Matched returning vehicles recover the same ID; IDs are never reassigned by a decoded plate string. Supported model road-vehicle classes are bicycle, passenger car, motorcycle, bus and truck (COCO 1/2/3/5/7). Other wheeled categories need appropriate detection models; this is not a universal detector for every wheeled object.

## Association and recovery

Vehicle appearance is extracted from the original full vehicle crop. observe stores the last observed box and updates appearance with an 85/15 moving average. Active matching combines motion/overlap with appearance; highly incompatible available descriptors (cosine <0.65) are gated out.

After more than two seconds without a vehicle observation, predict moves a track into archived_ and marks its plate noncurrent. The record keeps its ID and history throughout the session. Before new IDs are created, unmatched active tracks receive a conservative appearance-supported recovery pass. On remaining unmatched detections, archive recovery requires the same voted vehicle class and cosine >=0.93, size ratio between 0.4 and 2.5, and spatial distance below a gap-dependent gate capped at eight vehicle widths/heights. Hungarian assignment runs only on plausible candidates and rejects near-tied choices. Successful recovery resets motion at the current detection and increments reacquisitions while preserving ID. A detection without a compatible descriptor never justifies taking an old identity. New IDs are allocated only after active and archived matching fail.

The algorithm uses colour/spatial appearance features, not a trained vehicle ReID embedding. Similar-looking vehicles, drastic viewpoint/lighting changes, crowding and long full occlusions can still cause association errors. A lost track is explicitly lost; no bounding box or current plate is invented while it cannot be observed. The same physical object cannot be guaranteed identifiable after arbitrary disappearance or indistinguishable appearances. The archive lasts for the current process/session, is not persisted across application restarts, and grows with the number of identities. No identity-age eviction is currently applied.

## Review artifacts

- `output/tracking_review/tracking_review.avi`: 12 seconds, 30 FPS, 1920x720; original photographs on the left and actual engine output on the right.
- `output/tracking_review/tracking_review_slow.avi`: the same 360 frames at 15 FPS for detailed half-speed playback (24 seconds).
- `output/tracking_review/tracking_review_input.avi`: the unannotated 960x540 input.
- `output/tracking_review/tracking_audit.csv`: frame/source time, physical fixture label, first assigned ID, currently visible track ID, state, controlled gap, observation time, recovery count and plate status. Fixture labels are evaluation metadata and are not tracker inputs.
- `output/tracking_review/tracking_review.json`: continuity outcome, IDs seen, ID changes, measured observations and recovery counts.
- `output/real_traffic_tracking.avi`: all 377 frames of the real public traffic sample, annotated by the engine at the original clip frame rate. This is real traffic, but no ground-truth identity annotations are available; it is for visual inspection, not a quantified zero-switch claim.
- `output/real_traffic_tracking/tracks.csv`: detailed ID/observation/recovery log for that real traffic video.

The controlled test intentionally suppresses car detections for four seconds, from 3.0 through just before 7.0. It does not replace detections with ground-truth boxes or force a desired ID. All three controlled vehicles retain one ID with zero ID changes; car ID #2 is recovered after the gap. This is a reproducible composite-photograph test, not a physical camera occlusion benchmark.

Recreate with `Car.exe --tracking-review output/tracking_review`. `tools/validate_outputs.ps1` builds both toolsets, runs current tests, enforces a single ID for each known positive composite vehicle, and regenerates/verifies the controlled four-second dropout review. The current report is `tests/verification.json`.

## Existing behavior updates

Detection::appearance and Track::appearance hold the 128-value features. Track adds lastObservedBox, reacquisitions and observationAge; the projected view updates age from source observation time. Tracker owns active and archived vectors. predict now archives instead of forgetting lost IDs, update attempts recovery before allocating a new ID, and project hides stale boxes while retaining archive records. Pipeline::detect computes descriptors after vehicle NMS. typeName and the detector filter now include bicycles. draw uses a deterministic colour per ID and shows detected/coasting alongside plate status. CSV appends reacquisitions. Tracking remains independent from plate recognition and continues when plates are absent or unreadable.

The current runTests total is 47. Earlier timeout-restart descriptions are superseded: expiration of an active box no longer deletes the stored identity. Fresh controlled validation now requires the same ID after recovery, rather than accepting a new ID merely because a timeout elapsed.

## New function descriptions

### `car::appearanceFeature`

```cpp
std::vector<float> appearanceFeature(const cv::Mat& image)
```

Source: [src/Car.cpp](../src/Car.cpp). Accepts an 8-bit BGR vehicle crop; empty input returns empty. Resizes to 32x32 and combines a normalized 16-hue by 4-saturation histogram (64 values, weight 0.8) with an L2-normalized 8x8 grayscale image (64 values, weight 0.6). Returns 128 floats. No text decoding, plate ID, truth labels, or external ReID network is used; OpenCV errors propagate for invalid inputs.

### `car::appearanceSimilarity`

```cpp
static double appearanceSimilarity(const std::vector<float>& a,const std::vector<float>& b)
```

Source: [src/Car.cpp](../src/Car.cpp). Computes cosine similarity between equal-length nonzero descriptors. Empty, mismatched or zero descriptors return -1, meaning unavailable. No allocations, state changes or side effects. Similarity is heuristic, not a calibrated identity probability.

### `car::resetMotion`

```cpp
static void resetMotion(Track& track,const cv::Rect2f& box)
```

Source: [src/Car.cpp](../src/Car.cpp). Reinitializes an existing track centre/size state, zeroes velocity and resets pre/post covariance to 10 at a newly reacquired detection. Preserves the assigned ID, appearance, type/plate history and trail. Requires an initialized Kalman filter and valid box; no ID is allocated.

### `car::Tracker::archivedTracks`

```cpp
const std::vector<Track>& archivedTracks() const
```

Source: [include/Car.hpp](../include/Car.hpp). Returns a const reference to session-retained lost identities. No allocation or mutation. Lifetime is tied to the tracker; later updates may invalidate element references. An archived identity does not imply current visibility.

### `car::trackingStatus`

```cpp
std::string trackingStatus(const Track& track)
```

Source: [src/Car.cpp](../src/Car.cpp). Uses observationAge from a projected track: detected at age <=0.25 seconds, coasting at <=2 seconds, lost beyond that. No side effects. The age is based on vehicle observations, independent of plate visibility.

### `car::reviewOverlap`

```cpp
static double reviewOverlap(const cv::Rect2f& a,const cv::Rect2f& b)
```

Source: [src/TrackingReview.cpp](../src/TrackingReview.cpp). Returns rectangle intersection-over-union, guarding the denominator at one pixel. Used only to evaluate known composite placements and suppress car detections during the explicitly labeled test gap. Does not assign IDs or supply model detections.

### `car::trackingReview`

```cpp
int trackingReview(const std::string& directory,const std::string& models)
```

Source: [src/TrackingReview.cpp](../src/TrackingReview.cpp). Creates 360 composite frames from original car/bus/truck photographs without inserted plate labels. Runs the actual DirectML pipeline every six frames; suppresses car detections from source seconds 3 to 7 solely to stress identity continuity. Tracker input never includes truth IDs or injected detections. Writes 30 FPS side-by-side tracking_review.avi, 15 FPS half-speed tracking_review_slow.avi, original input AVI, CSV audit, three screenshots and JSON report. Known placements are used only for evaluation and dropout suppression. Returns 0 only if each vehicle has one unique assigned ID, sufficient measured observations, and car recovery occurred; returns 1 with a failed report otherwise. Missing photographs or unwritable files throw. Output parent directory must exist; no camera is opened.

### `car::runIdentityTests`

```cpp
static int runIdentityTests()
```

Source: [src/Tests.cpp](../src/Tests.cpp). Runs seven checks: supported road-vehicle names, descriptor repeatability, lost-state archiving without ghost active boxes, recovery of the original IDs despite reversed detections, repeated 17-second observation-gap recovery, rejection of incompatible appearance, and archive retention without claiming visibility. Returns the passed count, or require throws. Combined self-test count is 47.


[Association corrections](association-fixes.md) define the precise active/archive gates, ambiguity handling, class compatibility and transition-based switch counter. These supersede the earlier archive-only recovery sequence.
