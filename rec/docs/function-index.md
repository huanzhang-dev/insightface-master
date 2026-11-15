# Function index
[Documentation index](README.md)
All 78 named implemented functions/methods, including internal helpers, inline accessors, constructors, and destructors. Two explicitly deleted ownership operations are listed separately in the same inventory. Compiler-generated special members and third-party internals are not project-defined functions.
| Symbol | Source | Description |
| --- | --- | --- |
| `car::letters` | [src/Car.cpp](../src/Car.cpp) | [Extract the Unicode Characters field through a compatibility alias.](text-and-tracking.md) |
| `car::numbers` | [src/Car.cpp](../src/Car.cpp) | [Extract the independent Numbers display/export field.](text-and-tracking.md) |
| `car::typeName` | [src/Car.cpp](../src/Car.cpp) | [Map detector category IDs to stable output labels.](text-and-tracking.md) |
| `car::iou` | [src/Car.cpp](../src/Car.cpp) | [Measure rectangular overlap for tracking and plate-candidate deduplication.](text-and-tracking.md) |
| `car::assign` | [src/Car.cpp](../src/Car.cpp) | [Solve global track-to-detection assignment with the Hungarian algorithm.](text-and-tracking.md) |
| `car::boxFromState` | [src/Car.cpp](../src/Car.cpp) | [Convert a Kalman center/size state into a top-left rectangle.](text-and-tracking.md) |
| `car::observe` | [src/Car.cpp](../src/Car.cpp) | [Apply a matched observation and accumulate class/plate evidence.](text-and-tracking.md) |
| `car::Tracker::predict` | [src/Car.cpp](../src/Car.cpp) | [Predict all track boxes for the next frame and expire old tracks.](text-and-tracking.md) |
| `car::Tracker::update` | [src/Car.cpp](../src/Car.cpp) | [Associate detections to predicted tracks and create IDs for unmatched detections.](text-and-tracking.md) |
| `car::Tracker::tracks` | [include/Car.hpp:22](../include/Car.hpp) | [Expose current tracks for drawing, logging, and tests.](text-and-tracking.md) |
| `car::glyphFeature` | [src/Car.cpp](../src/Car.cpp) | [Normalize a segmented glyph into a fixed nearest-neighbor feature.](detection-and-ocr.md) |
| `car::Recognizer::Recognizer` | [src/Car.cpp](../src/Car.cpp) | [Load the neural text model and train the supplementary per-character classifier.](detection-and-ocr.md) |
| `car::Recognizer::readGlyphs` | [src/Car.cpp](../src/Car.cpp) | [Recognize individually separated dark glyphs using the startup-trained classifier.](detection-and-ocr.md) |
| `car::Recognizer::read` | [src/Car.cpp](../src/Car.cpp) | [Combine per-character classification and neural CRNN/CTC recognition.](detection-and-ocr.md) |
| `car::Pipeline::Pipeline` | [src/Car.cpp](../src/Car.cpp) | [Construct vehicle detection, plate localization, and text recognition models.](detection-and-ocr.md) |
| `car::Pipeline::plateCandidates` | [src/Car.cpp](../src/Car.cpp) | [Find supplemental bright rectangular plate candidates when neural localization is incomplete.](detection-and-ocr.md) |
| `car::Pipeline::detect` | [src/Car.cpp](../src/Car.cpp) | [Detect/classify vehicles and attach the best available plate reading.](detection-and-ocr.md) |
| `car::Worker::Worker` | [src/Car.cpp](../src/Car.cpp) | [Create a dedicated pipeline and start asynchronous inference.](worker-and-display.md) |
| `car::Worker::~Worker` | [src/Car.cpp](../src/Car.cpp) | [Stop and join the inference thread.](worker-and-display.md) |
| `car::Worker::submit` | [src/Car.cpp](../src/Car.cpp) | [Publish the newest frame without building an unbounded backlog.](worker-and-display.md) |
| `car::Worker::take` | [src/Car.cpp](../src/Car.cpp) | [Consume the most recently completed result without waiting for a new inference.](worker-and-display.md) |
| `car::Worker::loop` | [src/Car.cpp](../src/Car.cpp) | [Run the background inference loop.](worker-and-display.md) |
| `car::Worker::dropped` | [include/Car.hpp:27](../include/Car.hpp) | [Read the pending-frame overwrite counter.](worker-and-display.md) |
| `car::draw` | [src/Car.cpp](../src/Car.cpp) | [Draw tracking IDs, vehicle classes, separate text fields, trails, and preview-rate text.](worker-and-display.md) |
| `car::Runtime::Runtime` | [src/Inference.cpp](../src/Inference.cpp) | [Initialize the process-wide ONNX Runtime C API and environment.](inference.md) |
| `car::Runtime::check` | [src/Inference.cpp](../src/Inference.cpp) | [Translate ONNX Runtime status pointers into C++ exceptions.](inference.md) |
| `car::Runtime::~Runtime` | [src/Inference.cpp](../src/Inference.cpp) | [Release the shared environment and loaded inference DLL.](inference.md) |
| `car::runtime` | [src/Inference.cpp](../src/Inference.cpp) | [Provide the lazily initialized shared runtime.](inference.md) |
| `car::gpuIndex` | [src/Inference.cpp](../src/Inference.cpp) | [Select the DirectML DXGI adapter.](inference.md) |
| `car::Network::Impl::Impl` | [src/Inference.cpp](../src/Inference.cpp) | [Create internal GPU-session bookkeeping.](inference.md) |
| `car::Network::Impl::~Impl` | [src/Inference.cpp](../src/Inference.cpp) | [Release resources owned by one GPU network.](inference.md) |
| `car::Network::Network` | [src/Inference.cpp](../src/Inference.cpp) | [Load one ONNX model using the selected backend.](inference.md) |
| `car::Network::~Network` | [src/Inference.cpp](../src/Inference.cpp) | [Destroy one inference wrapper.](inference.md) |
| `car::Network::forward` | [src/Inference.cpp](../src/Inference.cpp) | [Run the model and return its first output tensor.](inference.md) |
| `car::Network::Network(copy)` | [include/Inference.hpp:12](../include/Inference.hpp) | [Prevent copying an owner of inference session resources.](inference.md) |
| `car::Network::operator=` | [include/Inference.hpp:12](../include/Inference.hpp) | [Prevent copy-assignment of inference session ownership.](inference.md) |
| `car::run` | [src/main.cpp](../src/main.cpp) | [Execute camera/video capture, inference, tracking, preview, export, and performance reporting.](application.md) |
| `main` | [src/main.cpp](../src/main.cpp) | [Resolve default project paths, parse CLI options, and dispatch application actions.](application.md) |
| `car::renderTextLine` | [src/TestVideo.cpp](../src/TestVideo.cpp) | [Render printed glyphs for synthetic plates and classifier training.](test-and-video-functions.md) |
| `car::plate` | [src/TestVideo.cpp](../src/TestVideo.cpp) | [Place an artificial license plate into a generated frame.](test-and-video-functions.md) |
| `car::generateVideos` | [src/TestVideo.cpp](../src/TestVideo.cpp) | [Generate repeatable vehicle videos, plate fixtures, snapshots, and ground-truth CSV files.](test-and-video-functions.md) |
| `car::require` | [src/Tests.cpp](../src/Tests.cpp) | [Fail an executable functional check with a useful message.](test-and-video-functions.md) |
| `car::runTests` | [src/Tests.cpp](../src/Tests.cpp) | [Run the executable self-tests.](test-and-video-functions.md) |

## Unicode and multilingual additions

- [`car::wideText`](unicode-and-multilingual.md): Validates UTF-8 and converts it to Windows UTF-16.
- [`car::numericCodepoint`](unicode-and-multilingual.md): Binary-searches generated Unicode 17.0.0 General_Category N* ranges.
- [`car::whitespaceCodepoint`](unicode-and-multilingual.md): Recognizes Unicode White_Space code points.
- [`car::textSymbols`](unicode-and-multilingual.md): Validates UTF-8, walks complete code points, omits whitespace, and emits owned UTF-8 values plus numeric-category flags in logical source order.
- [`car::characters`](unicode-and-multilingual.md): Returns all non-whitespace, non-numeric Unicode code points, preserving spelling and order.
- [`car::numbers`](unicode-and-multilingual.md): Returns Unicode N* characters in source order.
- [`car::compactText`](unicode-and-multilingual.md): Removes Unicode whitespace from OCR output, preserving all other code points.
- [`car::textLength`](unicode-and-multilingual.md): Returns non-whitespace Unicode scalar count via textSymbols.
- [`car::jsonText`](unicode-and-multilingual.md): Returns a complete quoted JSON string: escapes quotes, backslashes and ASCII controls; preserves valid UTF-8.
- [`car::csvText`](unicode-and-multilingual.md): Returns a complete quoted CSV field, doubling embedded quotes.
- [`car::MultilingualOcr::Impl::Impl`](unicode-and-multilingual.md): Loads native x64 Leptonica and Tesseract DLLs from the executable directory, resolves the pinned C ABI, creates an engine and initializes requested language models.
- [`car::MultilingualOcr::Impl::~Impl`](unicode-and-multilingual.md): Deletes the owned Tesseract engine, then unloads Tesseract and Leptonica modules.
- [`car::MultilingualOcr::MultilingualOcr`](unicode-and-multilingual.md): Constructs the owned native implementation from tessdata path, language expression and whole-reading numeric restriction.
- [`car::MultilingualOcr::~MultilingualOcr`](unicode-and-multilingual.md): Releases the owned implementation and its engine/modules.
- [`car::MultilingualOcr::read`](unicode-and-multilingual.md): Accepts a nonempty 8-bit BGR or grayscale plate crop.
- [`car::Pipeline::~Pipeline`](unicode-and-multilingual.md): Destroys the optional multilingual engine and model members through their owners.

Unicode `numbers` replaces the earlier ASCII implementation; it is one function, documented on the Unicode page.

## Lighting additions

- [`car::percentile`](lighting.md): Grayscale histogram quantile.
- [`car::Lighting::apply`](lighting.md): Stateful shadow and local contrast correction.
- [`car::nightExposureView`](lighting.md): Optional second exposure for night detection.
- [`car::normalizePlateLighting`](lighting.md): Original-crop contrast and illumination normalization.
- [`car::generateLightingVideos`](lighting.md): Reproducible dark/uneven test video generation.
- [`car::runLightingTests`](lighting.md): Nine low-light and dark CJK/Latin regression checks.

## Evidence and review additions

- [`car::Tracker::project`](evidence-and-tracking.md): Read-only display projection from measurement-time state.
- [`car::plateStatus`](evidence-and-tracking.md): Explicit current/unconfirmed/historical status.
- [`car::runReviewTests`](evidence-and-tracking.md): Ten regressions covering review findings and evidence.
- [`car::generateEvidenceVideos`](evidence-and-tracking.md): Restore original photographs without inserted plate labels.

## Persistent tracking additions

- [`car::appearanceFeature`](persistent-tracking.md): Accepts an 8-bit BGR vehicle crop; empty input returns empty.
- [`car::appearanceSimilarity`](persistent-tracking.md): Computes cosine similarity between equal-length nonzero descriptors.
- [`car::resetMotion`](persistent-tracking.md): Reinitializes an existing track centre/size state, zeroes velocity and resets pre/post covariance to 10 at a newly reacquired detection.
- [`car::Tracker::archivedTracks`](persistent-tracking.md): Returns a const reference to session-retained lost identities.
- [`car::trackingStatus`](persistent-tracking.md): Uses observationAge from a projected track: detected at age <=0.25 seconds, coasting at <=2 seconds, lost beyond that.
- [`car::reviewOverlap`](persistent-tracking.md): Returns rectangle intersection-over-union, guarding the denominator at one pixel.
- [`car::trackingReview`](persistent-tracking.md): Creates 360 composite frames from original car/bus/truck photographs without inserted plate labels.
- [`car::runIdentityTests`](persistent-tracking.md): Runs seven checks: supported road-vehicle names, descriptor repeatability, lost-state archiving without ghost active boxes, recovery of the original IDs despite reversed detections, repeated 17-second observation-gap recovery, rejection of incompatible appearance, and archive retention without claiming visibility.

## Association review corrections

- [`car::identityRecoveryCost`](association-fixes.md): Returns a candidate recovery cost or 2.0 when disallowed.
- [`car::recoveryAssignment`](association-fixes.md): Accepts a rectangular candidate cost matrix; empty input returns empty.
- [`car::IdSwitchCounter::observe`](association-fixes.md): Updates previousId and switches for actual positive-ID transitions.
- [`car::runAssociationFixTests`](association-fixes.md): Runs eight regressions: 140-pixel movement in 0.3 seconds retains one active ID; weak appearance cannot bypass motion gating; active appearance recovery rejects car/bus mismatch; a bus cannot inherit an archived car ID with identical appearance; near-tied active identities are not guessed; absent descriptors cannot justify recovery; 1,2,1 counts two switches; gaps/repetitions do not inflate switches.
