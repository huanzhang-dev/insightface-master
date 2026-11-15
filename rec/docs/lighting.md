# Lighting correction and night identification

Lighting correction is enabled by default. Use `--lighting auto` or `--lighting off` for controlled comparison. This is software preprocessing for inference; it does not change camera exposure controls, camera driver settings or the original preview/recorded pixels. Overlays remain on the original camera image. Live preview FPS and AI updates per second are measured independently.

## Processing path

`Pipeline::detect` creates a shadow-preserving corrected view. In sufficiently dark scenes it also creates a linear exposure view and runs the vehicle detector on both, combining candidates with the existing NMS. The plate detector uses the final exposure view; heuristic candidates use it only when --allow-text-candidates is explicitly enabled, while OCR receives normalized crops from the original image. Coordinates remain in original image pixels. The heuristic plate threshold uses Otsu for dark vehicle crops and a fixed bright threshold otherwise. Existing vehicle categories, tracking, Unicode character/number separation, and plate consensus remain in effect.

The pipeline owns temporal exposure state through `Lighting`; one worker serializes calls. Bounded preprocessing and a second vehicle pass only at night limit compute cost. There is no generative enhancement or invented plate text. Near-black and flat-region guards avoid deliberately amplifying absent detail, but OCR can still misread noisy images and temporal consensus is not proof of correctness.

## Test materials and limits

Generate test clips with `Car.exe --generate-lighting testdata` after creating `composite_plates.avi`. `lighting_night.avi` and `lighting_uneven.avi` are transformations of photographic composites with artificial Latin plates. They provide reproducible exposure/noise stress tests, not real-night footage. Unicode script checks separately render and darken clean CJK/Latin text. Reports: `tests/lighting_comparison.json`; annotated night playback: `output/night_preview15.avi` and `output/night_preview30.avi`.

Completely unlit targets, clipped highlights/headlight glare, motion blur and sensor saturation may destroy detail that software cannot recover. Actual nighttime performance requires testing the target camera/site and may require low-light sensors, controlled illumination or infrared support. The current models have not been retrained on a labeled night-driving dataset. No guarantee is made for all lighting conditions or unrestricted real camera plates.

## Functions

### `car::percentile`

```cpp
static int percentile(const cv::Mat& gray,double fraction)
```

Source: [src/Lighting.cpp](../src/Lighting.cpp). Computes an 8-bit grayscale histogram and returns the requested cumulative quantile. Input must be nonempty CV_8UC1 with a fraction in (0,1]; used internally on frame probes and plate images. Reads strided rows without modifying input; returns an intensity 0..255.

### `car::Lighting::apply`

```cpp
cv::Mat apply(const cv::Mat& frame)
```

Source: [src/Lighting.cpp](../src/Lighting.cpp). Accepts CV_8UC3 BGR and returns an inference view with identical geometry. Empty input is returned unchanged; invalid type throws invalid_argument. Samples luminance at 160x90, rejects near-black frames with 95th percentile <=5, and applies bounded gamma when median <50. Gamma is smoothed 75/25 across ordinary changes and reset when the change exceeds 0.35. Uneven-light correction triggers at 15th percentile <25, 95th >100 and median <65. Normal daylight passes through. A gamma LUT, mild dark-scene Gaussian smoothing, and CLAHE blended 35% on Lab luminance improve exposure/contrast while retaining colour. Input pixels are never modified; returned Mat may share the original storage on passthrough. State belongs to one pipeline and calls must be serialized. It cannot recover information absent from the sensor.

### `car::nightExposureView`

```cpp
cv::Mat nightExposureView(const cv::Mat& frame)
```

Source: [src/Lighting.cpp](../src/Lighting.cpp). Accepts an empty or 8-bit BGR frame and returns an optional second inference image. Uses median <50 and 95th percentile >5 to select night scenes; otherwise returns an empty Mat. Applies gain clamped to 1..7 with a two-level black offset. Output retains dimensions and does not alter the source. The second detector pass uses this view; detections from both exposures are merged by existing NMS. No extra detector pass occurs in normal daylight.

### `car::normalizePlateLighting`

```cpp
cv::Mat normalizePlateLighting(const cv::Mat& patch)
```

Source: [src/Lighting.cpp](../src/Lighting.cpp). Accepts empty or 8-bit BGR/grayscale plate crops and returns grayscale. Converts the original camera crop rather than the enhanced full-frame view, stretches 5th..95th percentile contrast, and avoids amplification when that range is <12. Slow background flattening is used only when low >45 and high >160. Returns owned normalized pixels (except the empty case); rejects unsupported pixel depth. Does not synthesize characters. Called before either multilingual or Latin OCR when lighting correction is enabled.

### `car::generateLightingVideos`

```cpp
int generateLightingVideos(const std::string& directory)
```

Source: [src/Lighting.cpp](../src/Lighting.cpp). Reads an existing photographic composite_plates.avi in the supplied existing directory. Writes lighting_night.avi and lighting_uneven.avi at 30 FPS with matching frame geometry, plus frame-60 JPEG samples. Night exposure uses 0.14*pixel+2; uneven exposure varies from 0.12 to about 0.97 across the image. Adds reproducible Gaussian noise with sigma 1.2 and RNG seed 20261115. Clips retain the artificial source plates and are clearly labeled simulated. Returns 0; missing source or unwritable video throws runtime_error. No camera is opened.

### `car::runLightingTests`

```cpp
static int runLightingTests(const std::string& models)
```

Source: [src/Tests.cpp](../src/Tests.cpp). Runs nine regression checks: black-frame passthrough, daylight passthrough, dark enhancement without modifying input, secondary-exposure gating, flat-plate noise guard, and exact dark Chinese/Korean/Japanese/Latin OCR strings after normalization. Uses installed fonts and pinned language models. Returns the number of passed checks; require throws on first failure. Called by runTests, bringing the entire suite to 55 checks. Printed/model-generated fixtures are not physical-camera accuracy measurements.

## External API additions

OpenCV `LUT` applies exposure tables; `createCLAHE`/`CLAHE::apply` limit local luminance contrast; `GaussianBlur` attenuates noise or estimates slow illumination; `divide` performs plate background flattening. `cvtColor`, `split`, `merge`, `addWeighted`, `resize`, `convertTo`, and `threshold` provide colour-space processing and adaptive binarization. `RNG::fill` produces deterministic test noise. Standard `log`, `pow`, and `ceil` calculate exposure curves/quantiles. All processing uses the pinned OpenCV 4.5.5 dependency.

## Recorded comparison

Both v140 and v142 pass 30 self-tests. On the six-second simulated night clip, enabling correction yields at least 136 stable correct track rows for each of the three artificial plate/type pairs. With correction off there are zero exact stable correct plate/type rows in that clip. Daylight stable plate counts are unchanged. Uneven lighting improves the bus and truck plate reads, but the heavily shadowed car is still missed; this is an unresolved limitation.

Headless real-time night runs schedule 14.97 and 29.92 preview FPS at the selected 15/30 targets, with approximately 4.2 AI updates per second, zero stale results, and inference p95 around 280?290 ms. The queue drops pending frames to bound latency. These measurements apply to this machine and this simulated clip with default combined CJK/Latin OCR, not all cameras or scenes. The recorded annotated night AVI files can be played to review the actual output.

Earlier comparison results on this page are historical synthetic-text measurements. Current evidence-gated behavior and fresh validation are described in [evidence reference](evidence-and-tracking.md).
