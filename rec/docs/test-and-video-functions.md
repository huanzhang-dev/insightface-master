# Test And Video Functions

> Current Unicode update: `renderTextLine` now validates UTF-8 and uses CreateFontW/DrawTextW. `runTests` now performs 55 checks, adding Unicode/supplementary digit handling, malformed UTF-8, CSV/JSON escaping, Chinese, Korean, Japanese and Cyrillic OCR. It writes a Chinese plate fixture. The earlier 12-check baseline remains historical evidence.

[Documentation index](README.md)

Executable checks and test-media creation. Synthetic-font training also uses renderTextLine during normal recognition startup.

## `car::renderTextLine`

```cpp
cv::Mat renderTextLine(const std::string& text, int width, int height, const std::string& font="Arial", bool bold=true)
```

**Source:** [src/TestVideo.cpp](../src/TestVideo.cpp).

Render printed glyphs for synthetic plates and classifier training.

**Inputs:** Text, positive bitmap dimensions, installed font name, and weight choice.

**Returns / output:** An owned BGR `cv::Mat` with centered black text on a white background.

**Behavior, ownership, and failure:** Uses a top-down 32-bit Windows DIB and GDI. Font height is 75% of bitmap height. Converts BGRA to BGR, then restores/deletes GDI objects on normal completion. Throws when initial bitmap/DC allocation fails. Installed-font substitution and Windows rasterization can affect generated pixels. Used by both test helpers and production recognizer startup.

## `car::plate`

```cpp
static void plate(cv::Mat& image, const cv::Rect& rectangle, const std::string& text)
```

**Source:** [src/TestVideo.cpp](../src/TestVideo.cpp).

Place an artificial license plate into a generated frame.

**Inputs:** Mutable BGR image; positive rectangle fully inside it; plate string.

**Returns / output:** No return value; replaces that ROI with rendered text and a one-pixel dark border.

**Behavior, ownership, and failure:** Internal video-generation helper. Calls `renderTextLine`, copies into the frame ROI, and draws its boundary. Invalid ROI or rendering failures can throw. Does not recognize text.

## `car::generateVideos`

```cpp
int generateVideos(const std::string& directory, const std::string& models="models")
```

**Source:** [src/TestVideo.cpp](../src/TestVideo.cpp).

Generate repeatable vehicle videos, plate fixtures, snapshots, and ground-truth CSV files.

**Inputs:** Existing parent of the output directory; model directory. Composite generation also needs `bus_photo.jpg`, `truck_modern.jpg`, and usable `traffic_real.mp4` in the test directory.

**Returns / output:** Returns 0 on completion; writes generated media/labels.

**Behavior, ownership, and failure:** Tries to create the directory. Writes 960?540 daylight, occlusion, and night clips, each 150 frames at 25 FPS; saves frame-50 snapshots and CSV labels. Occludes the first vehicle on frames 60?89 in the occlusion clip. Writes large mixed/numeric plate images. If bus/truck photos exist, constructs a CPU pipeline, seeks traffic frames 100/120/90/150, and extracts a sufficiently large car detection. If successful, writes a 180-frame 30 FPS photographic composite, its frame-60 snapshot, car crop, and ground truth. Fixed modern-truck source crop requires the supplied photograph dimensions. Composite generation is silently skipped when optional inputs/car extraction are unavailable. Model/inference/ROI/video-writer errors can throw. Files with the generated names are overwritten; output stream/image-write success is not checked everywhere. Generated results are not a real-world OCR accuracy benchmark.

## `car::require`

```cpp
static void require(bool condition, const char* message)
```

**Source:** [src/Tests.cpp](../src/Tests.cpp).

Fail an executable functional check with a useful message.

**Inputs:** Condition and null-terminated failure message.

**Returns / output:** Returns normally when true; throws `std::runtime_error(message)` when false.

**Behavior, ownership, and failure:** Internal assertion helper; independent of the C/C++ `assert` macro, so checks remain active in Release builds.

## `car::runTests`

```cpp
int runTests(const std::string& models)
```

**Source:** [src/Tests.cpp](../src/Tests.cpp).

Run the executable self-tests.

**Inputs:** Model directory; Windows fonts/GDI available.

**Returns / output:** Returns 0 and prints `PASS 55 checks`, or throws at the first failed check.

**Behavior, ownership, and failure:** Checks separate mixed/numeric fields, non-greedy assignment, ID persistence under reordered detections, repeated plate consensus, short occlusion, expiry, nonreused IDs, mixed/numeric OCR fixtures, small-plate OCR, a blank-scene detector result, Unicode scalar/category handling, whitespace removal, invalid UTF-8 rejection, export escaping, and Chinese/Korean/Japanese/Cyrillic OCR fixtures. Uses a CPU recognizer/pipeline. Loads actual models but opens no camera and creates no benchmark footage. Writes testdata/plate_unicode.png as a rendered Chinese plate fixture. Does not measure unrestricted camera accuracy or automatically validate an earlier build.

`runTests` also writes `testdata/plate_kor.png` and `testdata/plate_jpn.png` and checks their complete recognized strings with the Korean fast and Japanese best models, respectively. The complete suite now contains 55 checks.

runTests calls runLightingTests before the previous OCR/tracking checks. The full suite now has 55 checks. See [lighting reference](lighting.md).

Current behavior: [plate evidence and tracking fixes](evidence-and-tracking.md) define timestamp-aware updates, current/history labeling, safe overlays, evidence crops, test-only heuristics and 55-check validation.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.

Current association fixes: [active recovery, class gates, switch counting and new function descriptions](association-fixes.md).
