# Detection And Ocr

> Current Unicode update: `Recognizer` below is the optional Latin backend. `Pipeline` now owns a `MultilingualOcr` by default and dispatches to it unless `--ocr-languages latin` is selected. Accepted plate lengths count Unicode scalars. See [Unicode reference](unicode-and-multilingual.md).

[Documentation index](README.md)

Vehicle/plate detection and neural or per-character recognition. All rectangles use image pixels unless stated otherwise.

## `car::glyphFeature`

```cpp
static cv::Mat glyphFeature(const cv::Mat& binary)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Normalize a segmented glyph into a fixed nearest-neighbor feature.

**Inputs:** A single-channel binary image with foreground glyph pixels nonzero.

**Returns / output:** A 1?768 `CV_32F` row, scaled to [0,1].

**Behavior, ownership, and failure:** Finds the foreground bounds, fits them inside 20?28 pixels, centers them in a 24-column ?32-row canvas, and flattens it. Empty foreground produces a zero vector. Does not alter input. Used identically for generated training samples and observed glyphs.

## `car::Recognizer::Recognizer`

```cpp
Recognizer(const std::string& path, bool numbersOnly=false, bool opencl=false, bool directml=false, int device=-1)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Load the neural text model and train the supplementary per-character classifier.

**Inputs:** ONNX path; numeric-only policy; backend flags; DirectML adapter index (`-1` means automatic).

**Returns / output:** A ready recognizer owning a `Network` and `cv::ml::KNearest`.

**Behavior, ownership, and failure:** Generates 4,320 synthetic glyph samples: 36 classes ?4 fonts ?2 weights ?5 heights ?3 morphology variants. Fonts are Arial, Consolas, Tahoma, Verdana; heights 24/32/40/48/64. Trains k=3 on `glyphFeature`. Performs model loading and Windows GDI work; can throw on missing models, backend failure, or image/ML errors. It does not train on real plate data or save learned samples to disk.

## `car::Recognizer::readGlyphs`

```cpp
Reading Recognizer::readGlyphs(const cv::Mat& gray)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Recognize individually separated dark glyphs using the startup-trained classifier.

**Inputs:** Single-channel grayscale plate crop.

**Returns / output:** A `Reading`, or an empty reading if segmentation/classification fails.

**Behavior, ownership, and failure:** Private helper. Uses inverse Otsu thresholding and external contours. Accepts 4?12 components, each height >35% and <95% of crop height, width >=2 and <30% of crop width. Sorts left-to-right; predicts each glyph with k=3. Rejects if nearest normalized squared distance >0.065. Character score is max(0,1-2*sqrt(distance)); plate score is the character mean. Numeric-only mode rejects the whole reading on any letter. Scores are similarity heuristics, not calibrated probabilities.

## `car::Recognizer::read`

```cpp
Reading Recognizer::read(const cv::Mat& patch)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Combine per-character classification and neural CRNN/CTC recognition.

**Inputs:** A clear, nonempty BGR or grayscale plate crop.

**Returns / output:** Full uppercase alphanumeric text, mean confidence, and one confidence per emitted character.

**Behavior, ownership, and failure:** Empty input returns empty. Tries segmented glyphs first and returns immediately if their confidence >0.9. Otherwise converts to grayscale, forms a 100?32 blob using (pixel-127.5)/127.5, runs CRNN, and requires shape [time,1,37]. CTC decoding removes blank index 0 and adjacent repeat labels while preserving repeats separated by blanks. Applies softmax-based character scores. Finally prefers a glyph reading if >0.8 and higher-confidence than CRNN. Numeric-only mode rejects a decoded letter rather than silently stripping it. Model/shape errors throw. A single recognizer is not safe for simultaneous inference calls.

## `car::Pipeline::Pipeline`

```cpp
Pipeline(const Options& options)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Construct vehicle detection, plate localization, and text recognition models.

**Inputs:** Model directory containing `yolox.onnx`, `plate.onnx`, `ocr_latin.onnx`; backend and recognition options.

**Returns / output:** A pipeline owning all model instances.

**Behavior, ownership, and failure:** Loads all three models immediately. Uses configured vehicle confidence and forwards backend/device/numeric-only settings. Missing artifacts or unsupported backends throw. Model instances are not shared between workers.

## `car::Pipeline::plateCandidates`

```cpp
std::vector<cv::Rect> Pipeline::plateCandidates(const cv::Mat& vehicle)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Find supplemental bright rectangular plate candidates when neural localization is incomplete.

**Inputs:** A BGR vehicle ROI.

**Returns / output:** Up to four trimmed rectangles in vehicle-relative coordinates.

**Behavior, ownership, and failure:** Returns empty for images smaller than 40?30. Thresholds grayscale at 160; considers all contours. Requires width >=35, height >=10, aspect ratio between 2.0 and 6.8, area <35% of vehicle area, and top y >20% of vehicle height. Trims borders, clips to ROI, sorts by descending area. This is a heuristic, not another neural plate detector; it favors bright single-line plates.

## `car::Pipeline::detect`

```cpp
std::vector<Detection> Pipeline::detect(const cv::Mat& image)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Detect/classify vehicles and attach the best available plate reading.

**Inputs:** A nonempty BGR video frame; called by the synchronous runner or worker thread.

**Returns / output:** At most 30 confidence-sorted `Detection` objects with full-frame boxes and optional readings/plate boxes.

**Behavior, ownership, and failure:** Letterboxes to 640?640 at the top-left with fill 114. YOLOX takes raw RGB values and must return 8,400?85 values; decodes strides 8/16/32, keeps classes 2/3/5/7, thresholds objectness?class score, clips boxes, and runs class-agnostic NMS at 0.45. When vehicles exist, runs the plate model on RGB/255 and requires [1,N,6]; plate confidence is >=0.3 and NMS is 0.45. Associates plate centers with vehicle ROIs. For the first eight vehicles, supplements neural crops with heuristic candidates, removes IoU >0.7 duplicates, and tries up to five crops. Chooses the highest-confidence reading of length 4?12 containing at least one digit. Remaining vehicles can be tracked without OCR. Shape/inference errors throw; no internal concurrency protection.

Vehicle detection now merges two exposure views in dark scenes; plate candidates use adaptive thresholding on dark vehicle crops, and OCR crops are normalized from original pixels. See [lighting reference](lighting.md).

Current behavior: [plate evidence and tracking fixes](evidence-and-tracking.md) define timestamp-aware updates, current/history labeling, safe overlays, evidence crops, test-only heuristics and 55-check validation.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.
