# Data types, options, and contracts

> Current Unicode update: `Options::ocrLanguages` defaults to eng+chi_sim+kor+jpn. `TextSymbol` contains an owned UTF-8 code-point value and bool number. `Reading::characterConfidence` is indexed by Unicode scalar, not byte; Tesseract repeats the line mean. `Pipeline` owns an optional unique_ptr<MultilingualOcr>. See [Unicode reference](unicode-and-multilingual.md).

[Documentation index](README.md)

Definitions are in [Car.hpp](../include/Car.hpp) and [Inference.hpp](../include/Inference.hpp). These structs have no handwritten member functions unless listed in the function index; ordinary implicit constructors/copies are compiler-generated.

## Reading

| Field | Meaning |
| --- | --- |
| `text` | Recognized UTF-8 text; empty means no accepted reading. |
| `confidence` | Mean character score, initially zero; not a calibrated probability. |
| `characterConfidence` | One score per character for recognizer output; track consensus does not retain this array. |

`letters(text)` and `numbers(text)` produce independent fields without modifying the complete plate. Numeric-only recognition rejects a reading containing a letter; it is different from simply extracting its numeric substring.

## Detection

| Field | Meaning |
| --- | --- |
| `box` | `Rect2f` vehicle bounds in full-frame pixels. |
| `category` | COCO category; default 2. Allowed detector categories are 2/3/5/7. |
| `confidence` | Vehicle objectness ? class score. |
| `plate` | Optional `Reading` for this observation. |
| `plateBox` | Full-frame integer plate ROI when a reading is selected. |

Heuristic candidate rectangles are vehicle-relative until `Pipeline::detect` converts them. Empty/no-reading plate bounds must not be treated as a detected plate.

## Track

| Field | Meaning |
| --- | --- |
| `id` | Monotonic ID assigned by one `Tracker`; not globally persistent across process restarts. |
| `box` | Current predicted/corrected image rectangle. |
| `filter` | Eight float states: center x/y, width/height, and their velocities; four measurements. |
| `lastSeen` | Timestamp of last matched detection, in seconds. |
| `hits` | Observation count; at least two required for display/CSV. |
| `category`, `confidence` | Voted category and most recent detection confidence. |
| `typeVotes` | Accumulated vehicle confidences by class. |
| `plateVotes`, `plateCounts` | Accumulated plate scores and observation counts by text. |
| `plate` | Winning text and its mean observation score. |
| `plateStable` | Winning text has at least two observations; not proof of correctness. |
| `trail` | Up to 30 center points for drawing. |

`predict` expires tracks after more than two seconds without observations. IDs are not reused within a tracker instance. No persistence database, cross-camera matching, or appearance-based long-term ReID is implemented. Track state is main-thread-owned; `tracks()` is a borrowed const reference, not a synchronized snapshot.

## Options

| Field | Struct default | Effect |
| --- | --- | --- |
| `source` | `"0"` | One digit 0?9 selects a camera; other strings are file/stream input. |
| `models` | `"models"` | Directory for the three required ONNX files. |
| `output` | `"output"` | Output folder; runner tries nonrecursive creation. |
| `record` | empty | Optional annotated MJPEG AVI path. |
| `headless` | false | Disable GUI; preview scheduling/recording can still occur. |
| `offline` | false | Synchronous inference and unpaced file processing. |
| `numbersOnly` | false | Reject recognition containing letters. |
| `opencl` | false | Use OpenCV OpenCL backend target. |
| `directml` | false | Use ONNX Runtime DirectML instead of OpenCV inference. |
| `maxFrames` | 0 | Zero means no explicit frame-count limit. |
| `detectorEvery` | 1 | Submit/infer once every N captured frames. |
| `threads` | 4 | OpenCV CPU thread count. DirectML session intra-op count is separately fixed at 2. |
| `fps` | 30 | Requested camera FPS and preview scheduling limit; accepted range 1?120. |
| `gpuDevice` | -1 | Negative selects adapter automatically; nonnegative is explicit. |
| `confidence` | 0.3 | Vehicle confidence threshold; plate-model threshold is separately fixed at 0.3. |
| `maxAge` | 0.5 | Maximum accepted asynchronous result age, in seconds. |

`main` replaces the default models/output directories with executable-relative project paths before parsing overrides. The launch scripts request DirectML, although the C++ options default to CPU. Basic numeric ranges are checked by `main`; lower-level classes assume valid options. Driver support determines whether camera FPS/buffer requests are honored.

## Result

| Field | Meaning |
| --- | --- |
| `detections` | Completed inference observations. |
| `frame` | Source frame number; default -1. |
| `timestamp` | Timestamp carried from submission, not completion time. |
| `seconds` | Inference duration measured with a steady clock. |

## Ownership and concurrency

- `Pipeline` owns separate vehicle, plate, and OCR model wrappers; a single instance is not called concurrently.
- `Worker` owns its pipeline, background thread, mutex, condition variable, one pending image, one latest result, and an atomic overwrite counter. Submitted images are cloned.
- `Network::Impl` owns one ORT session and CPU memory descriptor; `runtime()` owns the shared DLL/API/environment.
- DirectML input tensors borrow a contiguous float blob for the duration of `forward`; returned GPU output is cloned before ORT memory is released.
- `Network` copy construction and assignment are explicitly deleted.
- OpenCV matrices ordinarily share reference-counted data on assignment; `clone()` is used where an independent image/tensor is required.

`Options::lighting` defaults to true. `Pipeline` owns Lighting temporal state and lightingEnabled_. Original frame geometry and pixel ownership are preserved. See [lighting reference](lighting.md).

Evidence update: Detection has plateDetected, plateSource and plateImage (owned raw source crop for selected OCR). Track has plateVisible, lastPlateSeen and plateSource. Options::allowTextCandidates defaults false. Pipeline stores allowTextCandidates_. See [evidence reference](evidence-and-tracking.md) for admission/current-history rules and state ownership.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.

Current association fixes: [active recovery, class gates, switch counting and new function descriptions](association-fixes.md).
