# Text And Tracking

> Current Unicode update: `letters` now delegates to Unicode `characters`, and `numbers` uses Unicode N* categories. Track plate length gates count Unicode scalars instead of UTF-8 bytes. See [Unicode reference](unicode-and-multilingual.md) for classification and encoding details.

[Documentation index](README.md)

Text separation, assignment, Kalman prediction, and temporal consensus.

## `car::letters`

```cpp
std::string letters(const std::string& text)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Compatibility alias that extracts the independent Unicode Characters field.

**Inputs:** `text`: valid UTF-8.

**Returns / output:** A new UTF-8 string containing non-number, non-whitespace code points, in original order.

**Behavior, ownership, and failure:** Does not uppercase lowercase input or transliterate Unicode. Keeps repetitions. `AB012C` becomes `ABC`; `0123` becomes empty. No state or I/O; malformed UTF-8 throws.

## `car::numbers`

```cpp
std::string numbers(const std::string& text)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Extract the independent Numbers display/export field.

**Inputs:** `text`: valid UTF-8.

**Returns / output:** A new UTF-8 string containing Unicode General_Category N* code points, in original order.

**Behavior, ownership, and failure:** Leading zeros and repeated digits are preserved. Does not convert the result to an integer. `AB001C` becomes `001`. No state or I/O; malformed UTF-8 throws.

## `car::typeName`

```cpp
std::string typeName(int category)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Map detector category IDs to stable output labels.

**Inputs:** `category`: COCO class index.

**Returns / output:** `2`: `passenger_car`; `3`: `motorcycle`; `5`: `bus`; `7`: `truck`; otherwise `unknown`.

**Behavior, ownership, and failure:** Used in overlays, CSV/JSONL, tests, and generated ground truth. The label is a model category, not an independently verified vehicle registration class.

## `car::iou`

```cpp
static float iou(const cv::Rect2f& a, const cv::Rect2f& b)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Measure rectangular overlap for tracking and plate-candidate deduplication.

**Inputs:** Two axis-aligned pixel rectangles with nonnegative dimensions.

**Returns / output:** Intersection area divided by `max(1, area(a)+area(b)-intersection)`.

**Behavior, ownership, and failure:** Internal helper. The denominator floor prevents division by zero; tiny rectangles therefore do not have mathematically exact IoU. No mutation.

## `car::assign`

```cpp
std::vector<int> assign(const std::vector<std::vector<double>>& costs)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Solve global track-to-detection assignment with the Hungarian algorithm.

**Inputs:** A rectangular, finite cost matrix: rows are tracks and columns are detections. Smaller costs are preferred.

**Returns / output:** One zero-based column per row, or `-1` for a dummy/unassigned column.

**Behavior, ownership, and failure:** Pads to a square matrix with dummy cost `0.8`. An empty row set returns empty; zero detection columns return unassigned rows. Does not apply the tracking acceptance gate: `Tracker::update` applies it. Ragged/nonfinite input is not validated. Approximately O(n^3) time for n=max(rows,columns).

## `car::boxFromState`

```cpp
static cv::Rect2f boxFromState(const cv::Mat& state)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Convert a Kalman center/size state into a top-left rectangle.

**Inputs:** Float column state with at least `[center_x,center_y,width,height]`.

**Returns / output:** `cv::Rect2f(center_x-width/2, center_y-height/2, width, height)`.

**Behavior, ownership, and failure:** Internal helper. Floors width and height at one pixel. Does not clip to frame boundaries or modify the state.

## `car::observe`

```cpp
static void observe(Track& track, const Detection& detection, double now)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Apply a matched observation and accumulate class/plate evidence.

**Inputs:** Mutable initialized track; matched detection; timestamp in seconds.

**Returns / output:** No return value; updates Kalman state, box, last-seen time, hit count, confidence, class votes, and plate consensus.

**Behavior, ownership, and failure:** Uses centre/size Kalman measurements and confidence-weighted class votes. Plate admission requires region/provenance evidence, 4 to 12 Unicode scalars and score >=0.50. Repeats at distinct timestamps establish consensus: three below mean score 0.75, otherwise two. Missing or differing current evidence clears current visibility; historical consensus remains explicitly labeled. At most 32 hypotheses are retained. See [evidence reference](evidence-and-tracking.md).

## `car::Tracker::predict`

```cpp
void Tracker::predict(double now)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Predict all track boxes for the next frame and expire old tracks.

**Inputs:** `now`: monotonically increasing seconds, consistent with update timestamps.

**Returns / output:** Mutates the tracker; no return value.

**Behavior, ownership, and failure:** First timestep is 1/30 s; later timesteps have a 0.001 s floor. Updates four position/size velocity transitions, runs prediction, and stores up to 30 trail centers. Removes tracks unseen for more than two seconds. Call at each accepted observation timestamp before applying those detections; project for display without advancing authoritative state. Not synchronized for concurrent callers.

## `car::Tracker::update`

```cpp
void Tracker::update(const std::vector<Detection>& detections, double now)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Associate detections to predicted tracks and create IDs for unmatched detections.

**Inputs:** Detections in the same pixel coordinate system; current seconds timestamp.

**Returns / output:** Mutates track state and allocates new monotonically increasing IDs.

**Behavior, ownership, and failure:** Gates pairs using IoU >0.05 or normalized center distance <0.7, computes overlap/distance cost, and accepts assignments below 0.95. Matched tracks call `observe`. Unmatched detections get an eight-state/four-measurement Kalman filter. No immediate deletion of unmatched tracks; expiry is handled by `predict`. An empty detection vector is supported. No cross-camera or long-occlusion ReID.

## `car::Tracker::tracks`

```cpp
const std::vector<Track>& Tracker::tracks() const
```

**Source:** [include/Car.hpp](../include/Car.hpp).

Expose current tracks for drawing, logging, and tests.

**Inputs:** No arguments.

**Returns / output:** A const reference to the owned track vector; no copy.

**Behavior, ownership, and failure:** Inline accessor. The reference must not outlive the tracker. Updates, insertion, or erasure can invalidate element references/iterators. There is no lock; use on the owning/main thread.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.

Current association fixes: [active recovery, class gates, switch counting and new function descriptions](association-fixes.md).
