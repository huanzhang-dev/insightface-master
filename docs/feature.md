# Feature extraction engine (`src/feature/`)

## What this is for

The feature extraction engine turns a small picture of a face into a **feature**: a list of numbers (128, 256 or 512 of them, depending on the model) that describes how that face looks. Its input is a `FaceCrop` (a piece of the camera image around one face, plus five landmark points: both eyes, the nose tip and both mouth corners, which come from the detection engine). Its output is a `Feature` whose numbers are scaled so that the list has length 1; that scaling is what lets the matching engine compare two faces with a plain dot product. If anything is wrong (no landmarks, wrong picture size, the network fails) the engine returns `false` and writes no usable feature. Everything is hidden behind one small interface, `FeatureExtractor`, which has two families of implementations: **deep** (the SFace network gives 128 numbers, the ArcFace network gives 512 numbers) and **classical** (LBP texture histograms reduced to 256 numbers, no neural network). The model profile (`models/*.ini`) chooses the family, so no code change is needed to switch. In the pipeline this is step 4, "Extract" (see `ARCHITECTURE.md`); the result goes to the matching engine (`match.md`) or, when a person is being registered, into the feature store (`store.md`). Measurements of accuracy and speed are in `RESULTS.md`; no numbers are quoted here.

### Words used in this document

| Word | Meaning |
|---|---|
| Feature (also "embedding") | A list of numbers that describes one face. Same person: similar lists. Different people: different lists. Type `Feature` in `types.h`. |
| Length 1 (unit length) | The length of a list is the square root of the sum of its squares. "Length 1" means that sum is 1. Two such lists have a cosine similarity equal to their dot product (see `match.md`). |
| Landmarks | Five points on a face: right eye, left eye, nose tip, right mouth corner, left mouth corner. "Right eye" is the eye on the right side of the person, which is on the left side of the picture (`types.h`). |
| Crop (`FaceCrop`) | The part of the camera image around one face, with the landmarks moved into the crop's own coordinates. |
| Alignment | Rotating, scaling and shifting the crop so that the five landmarks land on fixed positions inside a 112 x 112 picture. Every extractor needs this so that eyes, nose and mouth are always in the same place. |
| Backend | The library that runs a neural network: `ncnn` or ONNX Runtime (`onnx`). |
| BGR | The order in which `Image` stores the colours of a pixel: Blue, Green, Red. |

### The path of one face through this engine

1. The pipeline has a face with landmarks (from the detection engine's refine step).
2. `make_face_crop` cuts the face out of the camera frame with a margin and moves the landmarks into the crop.
3. `FeatureExtractor::extract` aligns the crop to 112 x 112, turns it into numbers (deep network or LBP histograms), and scales the result to length 1 (`normalize_feature`).
4. The `Feature` goes to the matcher (to look for a registered person) or to the store (to register a person).

## src/feature/extractor.h

The public header of the engine. It declares the interface `FeatureExtractor`, the factory functions and a few helpers. Macro: `FA_EXTRACTOR_H` is the include guard (it stops the header from being read twice); it has no other role.

| Function | Purpose |
|---|---|
| `FeatureExtractor` (class) | The interface every extractor implements. |
| `~FeatureExtractor()` | Virtual destructor, so an extractor can be deleted through the interface. |
| `dim()` | Length of the features this extractor makes. |
| `extract(crop, out)` | Crop to feature (align, compute, normalize). |
| `extract_aligned(aligned, out)` | Already-aligned 112 x 112 picture to feature. |
| `name()` | Short text name of the extractor. |
| `runs_on_cpu()` | True when the extractor runs on the CPU (virtual, default `true`; every extractor of the project does). |

The following functions are only declared in this header. Each one is documented in the `.cpp` file that defines it:

| Function | Documented under |
|---|---|
| `create_extractor` | `extractor.cpp` |
| `make_face_crop` | `extractor.cpp` |
| `normalize_feature` | `extractor.cpp` |
| `create_deep_extractor` | `extractor_deep.cpp` |
| `create_lbp_extractor`, `lbp_raw_size`, `lbp_raw_histogram`, `lbp_save_calibration` | `extractor_lbp.cpp` |

### FeatureExtractor (class)

```cpp
class FeatureExtractor {
public:
    virtual ~FeatureExtractor() {}
    ...
};
```

**What it does.** An abstract base class (an "interface"): it lists what every extractor can do but contains no working code. The rest of the project only ever talks to a `FeatureExtractor`, never to the SFace/ArcFace/LBP classes directly.

**How it works.** The two real classes, `DeepExtractor` (in `extractor_deep.cpp`) and `LbpExtractor` (in `extractor_lbp.cpp`), inherit from it and fill in the four "= 0" methods. `create_extractor` decides which one to build from the profile. Because both hide behind the same four methods, the pipeline works unchanged with deep or classical models.

**Used by.** `AuthPipeline` (`src/pipeline/auth_pipeline.h`) owns one as `std::unique_ptr<FeatureExtractor> extractor_`; `tools/eval_features.cpp`, `tools/bench_extract.cpp` and `tests/test_feature.cpp` create one directly (`tools/bench.cpp` gets its through `AuthPipeline`).

### ~FeatureExtractor

```cpp
virtual ~FeatureExtractor() {}
```

**What it does.** Cleans up an extractor object.

**How it works.** It is declared `virtual`. In C++ that is what makes `delete` (here done automatically by `std::unique_ptr<FeatureExtractor>`) run the destructor of the real class (`DeepExtractor` or `LbpExtractor`) and free its network and buffers. Without `virtual` those would leak.

**Used by.** Called automatically when a `std::unique_ptr<FeatureExtractor>` is destroyed (for example `AuthPipeline::extractor_`).

### dim

```cpp
virtual int dim() const = 0;
```

**What it does.** Returns the number of values in each feature this extractor produces (for example 128, 256 or 512).

**How it works.** `DeepExtractor` returns the value of `recognizer.dim` from the profile; `LbpExtractor` returns the dimension it was built with (256 for `classical.ini`). The value is fixed once the extractor is built.

**Used by.** `AuthPipeline::init` (compares it with the length of the registered features in the database), `tools/eval_features.cpp`, `tools/bench.cpp` and `tools/bench_extract.cpp` (printing), `tests/test_pipeline.cpp` (printing) and `tests/test_feature.cpp` (printing, and the check that it equals `recognizer.dim` of the profile).

### extract

```cpp
virtual bool extract(const FaceCrop& crop, Feature& out) = 0;
```

**What it does.** Computes the feature of one stored face crop. Input: a `FaceCrop` whose image holds the face and whose `face.landmarks` are given in the crop's own coordinates (as `make_face_crop` produces them). Output: `out`, a feature of length 1 with `dim()` values. Returns `false` when the crop has no landmarks (or an empty image) or when the network fails; in that case `out` should not be used.

**How it works.** Both implementations do the same two steps: (1) `align_face` rotates, scales and shifts the crop into a 112 x 112 picture; (2) the aligned picture is handed to `extract_aligned`. The difference is what `extract_aligned` does with it (network or LBP).

**Used by.** `AuthPipeline::process` (features of the new crops), `AuthPipeline::enroll_track`, `AuthPipeline::extract_from_image`, `tools/eval_features.cpp`, `tools/bench_extract_part.h` (`bench_extraction`, line 30 and line 34; run by `bench` and `bench_extract`), `tests/test_feature.cpp` (`main`, many checks: length, finite, length 1, repeatable, bad input, brightened copies).

### extract_aligned

```cpp
virtual bool extract_aligned(const Image& aligned, Feature& out) = 0;
```

**What it does.** Same as `extract`, but for a picture that is already aligned to 112 x 112 (`FA_ALIGNED_FACE_SIZE`). Returns `false` if the picture has the wrong size (or, for the deep extractor, is not 3-channel colour) or if the network fails.

**How it works.** This is the second half of `extract`. It skips alignment, so it needs no landmarks.

**Used by.** Only `DeepExtractor::extract` and `LbpExtractor::extract` (they call it after aligning). As the header comment says, it is offered for callers that do the alignment themselves; a search of `src/`, `tools/` and `tests/` finds no such caller in the project.

### name

```cpp
virtual const char* name() const = 0;
```

**What it does.** Returns a short text that says which extractor this is, for printing in logs. Examples: `"sface/onnxruntime"`, `"arcface/ncnn"`, `"lbp/classical"`.

**How it works.** `DeepExtractor` builds the text once as `recognizer_type + "/" + backend name`; `LbpExtractor` always returns the fixed text `"lbp/classical"`.

**Used by.** `tools/eval_features.cpp`, `tools/bench.cpp`, `tools/bench_extract.cpp`, `tests/test_feature.cpp` and `tests/test_pipeline.cpp` (printing only).

### runs_on_cpu

```cpp
virtual bool runs_on_cpu() const { return true; }
```

**What it does.** Answers "does this extractor run on the CPU?": `true` when it does, `false` when it would use a GPU. **Every extractor of this project runs on the CPU** (the target PC has no GPU), so the answer is always `true`; the function makes that a checked statement. It takes no input.

**How it works.** A virtual function with a default body, not a pure virtual one. The default `true` is the right answer for the classical LBP extractor, which is plain C++ code on the CPU (`LbpExtractor` does not override it). `DeepExtractor` overrides it and asks its network (`Net::runs_on_cpu`, see `common.md`). The answer says what the code asked for; that no GPU library is loaded into the process is checked from the outside by the tests (`tests/cpu_only_check.h`).

**Used by.** `AuthPipeline::runs_on_cpu` (the extractor, and in worker mode the worker's own extractor), `tools/eval_footage.cpp` (`main`, the line `all engines run on the CPU`), `tests/test_feature.cpp` (`main`, the check that the extractor and the detector run on the CPU).

## src/feature/extractor.cpp

Helpers shared by both extractor families, and the factory.

| Function | Purpose |
|---|---|
| `normalize_feature` | Scale a feature to length 1. |
| `make_face_crop` | Cut a face (with a margin) out of a camera image and move the landmarks into the crop. |
| `create_extractor` | Factory: build the extractor named in the profile. |

### normalize_feature

```cpp
void normalize_feature(Feature& f);
```

**What it does.** Changes the feature `f` in place so that its length becomes exactly 1 (up to float rounding). A feature that is all zeros stays all zeros (the feature store refuses such a feature, because it is not a vector of length 1; see `store.md`).

**How it works.**
1. Add up the square of every value, using `double` (more digits than `float`, to avoid rounding loss).
2. If that sum is 0 or negative, stop (nothing to scale).
3. Compute `1 / sqrt(sum)` and multiply every value by it.

Example: `(3, 4)` has sum of squares 25, so each value is multiplied by 1/5 and becomes `(0.6, 0.8)`.

**Used by.** `DeepExtractor::extract_aligned` and `LbpExtractor::extract_aligned`. (`src/testdata/synthetic.cpp` has a separate function named `normalize` that does the same job for test data; it is not this function.)

### make_face_crop

```cpp
bool make_face_crop(const Image& frame, const Face& face, FaceCrop& crop);
```

**What it does.** Cuts the face out of the camera image `frame` and stores it in `crop`. The cut-out is larger than the detected box (a 30% margin on every side) so that alignment, which rotates the picture, never runs out of picture. The box and the five landmarks inside `crop.face` are moved into the crop's own coordinates. Returns `false` if the frame is empty, if the box is narrower or lower than 2 pixels, or if the part left inside the camera image is smaller than 2 pixels in either direction; returns `true` otherwise (when the cut-out image is not empty).

**How it works.**
1. Reject an empty frame or a box with `w < 2` or `h < 2`.
2. Grow the box by `0.3 * w` left and right and `0.3 * h` up and down. Example: box `x=100, y=100, w=100, h=120` becomes region `x=70, y=64, w=160, h=192`.
3. Clip the region to the camera image (`0 .. width`, `0 .. height`); if less than 2 pixels remain, return `false`.
4. Round the region outward to whole pixels (`floor` for the left/top corner, `ceil` for the right/bottom corner).
5. Copy those pixels with `fa::crop` (from `image_ops.h`).
6. Copy `face` into `crop.face` and subtract the region's left/top corner from the box and from all five landmarks. In the example a landmark at `(150, 130)` becomes `(80, 66)`.

Things to know: the function does not set `crop.track_id` or `crop.time_ms` (the caller does); it does not check `has_landmarks` (the extractor does later); and the box and landmarks are only shifted, not clipped, so near the image border they may lie partly outside the crop.

**Used by.** `AuthPipeline::process` (storing a track's crop) and `AuthPipeline::extract_from_image`, `tools/eval_features.cpp`, `tools/bench_extract_part.h` (`bench_extraction`, lines 29 and 33), `tools/calibrate_lbp.cpp`, `tests/test_feature.cpp` (`main`, to cut out the 8 portrait faces).

### create_extractor

```cpp
std::unique_ptr<FeatureExtractor> create_extractor(const ModelProfile& profile);
```

**What it does.** The factory. Reads `profile.recognizer_type` and builds the matching extractor. Returns `nullptr` (and prints `extractor: unknown recognizer.type '...'` to `std::cerr`) when the type is unknown. It also returns `nullptr` when the chosen extractor cannot be built (for example the model file is missing; the reason is printed by the code that failed).

**How it works.** A simple `if`:
- `"sface"` or `"arcface"` goes to `create_deep_extractor`;
- `"lbp"` goes to `create_lbp_extractor`;
- anything else prints the message and returns `nullptr`.

**Used by.** `AuthPipeline::init` (`src/pipeline/auth_pipeline.cpp`), `tools/eval_features.cpp`, `tools/bench_extract.cpp` (line 28) and `tests/test_feature.cpp` (line 49). `tools/bench.cpp` and `tests/test_pipeline.cpp` get their extractor through `AuthPipeline`.

## src/feature/extractor_deep.cpp

The deep-learning extractor: the SFace network (128 numbers) or the ArcFace network (512 numbers). The steps for one face crop are:

1. **Align** the crop to 112 x 112 (`align_face`).
2. **Prepare the input**: swap the colour order from BGR to RGB and, for ArcFace, shift/scale the pixel values.
3. **Run the network** (through the `Net` interface in `inference.h`, which hides ncnn and ONNX Runtime). It answers with a list of 128 or 512 numbers.
4. **Normalize** the list to length 1.

Everything except `create_deep_extractor` lives in an anonymous namespace (it is private to this file).

| Function | Purpose |
|---|---|
| `DeepExtractor` (class and constructor) | Holds the network, the input settings and reusable buffers. |
| `DeepExtractor::load` | Load the network and choose the input preparation. |
| `DeepExtractor::dim` | Feature length (from the profile). |
| `DeepExtractor::name` | Text such as `"sface/onnxruntime"`. |
| `DeepExtractor::runs_on_cpu` | True when the network runs on the CPU. |
| `DeepExtractor::extract` | Align a crop, then call `extract_aligned`. |
| `DeepExtractor::extract_aligned` | Run the network on an aligned picture and normalize the answer. |
| `create_deep_extractor` | Public factory for this class. |

### DeepExtractor (class and constructor)

```cpp
class DeepExtractor : public FeatureExtractor {
public:
    explicit DeepExtractor(const ModelProfile& profile) : profile_(profile) {}
    ...
private:
    ModelProfile profile_;
    std::unique_ptr<Net> net_;
    InputPrep prep_;
    int dim_ = 0;
    std::string name_;
    Image aligned_;
    std::vector<Tensor> outputs_;
};
```

**What it does.** The implementation of `FeatureExtractor` for SFace and ArcFace. The constructor only stores a copy of the profile; the real work (loading the network) is in `load()`, so that failure can be reported with a return value.

**How it works.** The data members are:

| Member | Role |
|---|---|
| `profile_` | Copy of the settings (model path, backend, input/output blob names, `feature_dim`, `threads`). |
| `net_` | The loaded network (`Net`, either ncnn or ONNX Runtime). |
| `prep_` | How pixels are turned into network input (`InputPrep`: `swap_rb`, `mean[3]`, `norm[3]`). |
| `dim_` | Expected feature length. |
| `name_` | Text returned by `name()`. |
| `aligned_` | The 112 x 112 picture, kept between calls so memory is not re-allocated for every face. |
| `outputs_` | The network's answer, also kept between calls. |

Because `aligned_` and `outputs_` are reused, one `DeepExtractor` should not be used from two threads at once (the code has no locking).

**Used by.** Only `create_deep_extractor`.

### DeepExtractor::load

```cpp
bool load()
```

**What it does.** Loads the network named in the profile and sets how the input is prepared. Returns `false` if the network cannot be loaded.

**How it works.**
1. If `recognizer.backend` is `"ncnn"`: the model path in the profile is the `.param` file. The matching `.bin` path is made by replacing `.param` with `.bin` in the same path (`models/dev_noncommercial/rec.param` gives `.../rec.bin`). If the path does not contain `.param`, the path is used unchanged for both, and loading will fail. Then `create_ncnn_net(model, bin, recognizer_input, {recognizer_output}, threads)` is called.
2. For any other backend text (normally `"onnx"`; the code does not check the spelling) `create_onnx_net(model, threads)` is called.
3. If no network came back, return `false`.
4. Choose the input preparation. The network sees `value = (pixel - mean) * norm`, per colour channel:
   - `arcface`: colours swapped to RGB (`swap_rb = true`), `mean = 127.5`, `norm = 1/127.5`, so pixel values 0..255 become -1..+1.
   - anything else (that means `sface`): colours swapped to RGB, `mean = 0`, `norm = 1` (the `InputPrep` defaults), so the raw values 0..255 are passed on unchanged.
5. `dim_` is set from `profile.feature_dim`, and `name_` from `recognizer_type + "/" + net_->backend()`.

**Used by.** `create_deep_extractor`.

### DeepExtractor::dim

```cpp
int dim() const override { return dim_; }
```

**What it does.** Returns `dim_`, the feature length from the profile key `recognizer.dim` (128 for `permissive.ini`, 512 for the three `dev_noncommercial` profiles).

**How it works.** A one-line getter. The value is checked against what the network really returns every time `extract_aligned` runs.

**Used by.** See `FeatureExtractor::dim`.

### DeepExtractor::name

```cpp
const char* name() const override { return name_.c_str(); }
```

**What it does.** Returns the text built in `load()` (for example `"arcface/ncnn"`).

**How it works.** A one-line getter on the stored `std::string`.

**Used by.** See `FeatureExtractor::name`.

### DeepExtractor::runs_on_cpu

```cpp
bool runs_on_cpu() const override { return net_ && net_->runs_on_cpu(); }
```

**What it does.** Returns `true` when the network of this extractor runs on the CPU. It returns `false` if there is no network (an extractor whose `load()` failed is never handed out by `create_deep_extractor`, so that case is not seen from outside). The SFace network (ONNX Runtime) and the ArcFace networks (ncnn with Vulkan switched off, or ONNX Runtime) all run on the CPU.

**How it works.** Asks `net_->runs_on_cpu()` (see `Net::runs_on_cpu` in `common.md`).

**Used by.** Through `FeatureExtractor::runs_on_cpu` (see there).

### DeepExtractor::extract

```cpp
bool extract(const FaceCrop& crop, Feature& out) override
```

**What it does.** Computes the feature of a face crop. Returns `false` if the crop image is empty or the crop's face has no landmarks (`has_landmarks` is false); otherwise returns what `extract_aligned` returns.

**How it works.**
1. Check image and landmarks.
2. `align_face(crop.image, crop.face, aligned_)` fills the member picture `aligned_` with the face rotated, scaled and shifted so the five landmarks sit on the standard 112 x 112 positions (see "Supporting code" below).
3. Call `extract_aligned(aligned_, out)`.

**Used by.** Everything that calls `FeatureExtractor::extract` (see that entry).

### DeepExtractor::extract_aligned

```cpp
bool extract_aligned(const Image& aligned, Feature& out) override
```

**What it does.** Runs the neural network on an aligned face and returns the normalized feature. Returns `false` when the picture is not exactly 112 x 112 with 3 channels, when the network call fails, when the network does not return exactly one output, or when the number of values differs from `dim_` (in that last case it also prints `extractor: network returned N numbers, profile says M` to `std::cerr`).

**How it works.**
1. Check size and channels (`FA_ALIGNED_FACE_SIZE` = 112, 3 channels).
2. `net_->run(aligned, prep_, outputs_)`. Inside the `Net` the picture is turned into the network input using `prep_`: for ONNX Runtime a planar (channel by channel) float array of shape 1 x 3 x 112 x 112, for ncnn an ncnn `Mat` with mean and norm applied by `substract_mean_normalize`. The network returns its answer as flat lists of numbers.
3. Require exactly one output list with `dim_` numbers.
4. Copy that list into `out.values` and call `normalize_feature(out)` so the length is 1.

**Used by.** `DeepExtractor::extract` only.

### create_deep_extractor

```cpp
std::unique_ptr<FeatureExtractor> create_deep_extractor(const ModelProfile& profile);
```

**What it does.** Builds a `DeepExtractor` from the profile. Returns `nullptr` and prints `extractor: cannot load <model path>` to `std::cerr` if the network cannot be loaded.

**How it works.** Creates the object, calls `load()`, and returns the object only if loading worked. It is declared in `extractor.h` so that `create_extractor` can reach it from another file.

**Used by.** `create_extractor` only.

## src/feature/extractor_lbp.cpp

The classical extractor. **LBP** stands for "Local Binary Pattern". For every pixel, compare it with its 8 neighbours and write one bit per neighbour (1 if the neighbour is at least as bright as the centre). The 8 bits make a code from 0 to 255 that describes the little texture around the pixel (flat area, edge, corner, ...). The whole recipe:

1. Align the face to 112 x 112, convert to gray, equalize the brightness.
2. Compute the LBP code of every pixel (except the 1-pixel border).
3. Keep only 59 kinds of code: the "uniform patterns" (at most 2 changes between 0 and 1 when walking around the 8 bits), plus one catch-all bin for every other code.
4. Cut the face into a 7 x 7 grid; in every cell count the codes: a 59-number histogram per cell, so 7 x 7 x 59 = 2891 numbers.
5. Divide every cell histogram by its total, take the square root of each value (this reduces the influence of very frequent codes), subtract a mean vector and multiply by 1 / spread. The mean and the spread come from the calibration file `models/lbp_calibration.bin`; without that file the mean is 0 and the spread is 1.
6. Reduce the 2891 numbers to 256 with a fixed random projection and scale to length 1.

(These are the six steps of the recipe in the source comment. In the code, `compute_raw_histogram` does steps 2 to 5 except the subtraction of the mean and the multiplication by 1 / spread; `extract_aligned` does the rest.)

The LBP extractor is plain C++ code on the CPU, with no network. It does not override `FeatureExtractor::runs_on_cpu`: the interface's default answer, `true`, is its answer.

The code's own comment is honest about the limit: this method has no training, separates people far worse than the deep networks, and "cannot reach 1 error in 10,000"; it is the classical alternative. Measurements are in `RESULTS.md`.

| Function | Purpose |
|---|---|
| `kGrid`, `kBins`, `kRawDim` (constants) | Grid size 7, bins per cell 59, raw length 2891. |
| `UniformTable` (struct and constructor) | Table: 8-bit LBP code to histogram bin 0..58. |
| `compute_raw_histogram` | Steps 2 to 5 without the calibration part of step 5: LBP codes to square-rooted grid histograms. |
| `equalize` | Histogram equalization of a gray image. |
| `LbpExtractor` (class and constructor) | The classical extractor; builds the random projection. |
| `LbpExtractor::dim` | Feature length. |
| `LbpExtractor::name` | Returns `"lbp/classical"`. |
| `LbpExtractor::load_calibration` | Read the calibration file (mean and 1/spread). |
| `LbpExtractor::extract` | Align a crop, then call `extract_aligned`. |
| `LbpExtractor::extract_aligned` | Full LBP pipeline: gray, equalize, histograms, calibrate, project, normalize. |
| `lbp_raw_size` | Returns 2891 (for the calibration tool). |
| `lbp_raw_histogram` | Public version of the histogram step (for the calibration tool). |
| `lbp_save_calibration` | Write the calibration file. |
| `create_lbp_extractor` | Public factory for this class. |

### kGrid, kBins, kRawDim (constants)

```cpp
const int kGrid = 7;       // 7 x 7 cells
const int kBins = 59;      // uniform LBP bins
const int kRawDim = kGrid * kGrid * kBins;  // 2891
```

**What it does.** Fix the shape of the histogram data: the face is cut into `kGrid x kGrid` = 49 cells, each cell has `kBins` = 59 counters, so a whole face gives `kRawDim` = 2891 numbers.

**How it works.** Plain compile-time constants in the anonymous namespace. Why 59: of the 256 possible 8-bit codes, 58 are "uniform" (at most 2 changes between 0 and 1 around the circle; this count of 58 is a mathematical fact, not written in the code), and all the other codes share one extra bin, the last one (index 58, written `kBins - 1`).

**Used by.** All functions in this file; `lbp_raw_size` exposes `kRawDim` to other files.

### UniformTable (struct and constructor)

```cpp
struct UniformTable {
    unsigned char bin[256];
    UniformTable() { ... }   // body not copied here; see the steps below
};
```

**What it does.** A lookup table: `bin[code]` is the histogram bin (0..58) for an LBP code (0..255).

**How it works.** The constructor visits the codes 0, 1, ..., 255 in order. For each code it walks around the 8 bits (bit 0 to bit 7, and from bit 7 back to bit 0) and counts how often neighbouring bits differ ("transitions"). If there are at most 2 transitions the code is uniform and gets the next free bin number (0, 1, 2, ...). Otherwise it gets the catch-all bin 58. Example: the code `00111000` (value 56) has 2 transitions, so it is uniform; the code `01100110` (value 102) has 4 transitions, so it goes to bin 58.

**Used by.** A member `table_` of `LbpExtractor` (built once per extractor) and a function-level `static const` in `lbp_raw_histogram` (built once per program run). Both are passed to `compute_raw_histogram`.

### compute_raw_histogram

```cpp
void compute_raw_histogram(const Image& gray, const UniformTable& table, std::vector<float>& raw)
```

**What it does.** Turns an equalized 112 x 112 gray picture into the 2891 numbers (7 x 7 cells x 59 bins), each cell normalized and square-rooted, without any calibration. The result is written into `raw` (resized to `kRawDim`). It shares the work between the extractor and the calibration tool, so both see exactly the same numbers.

**How it works.**
1. Set all 2891 counters to zero.
2. Visit every pixel except the 1-pixel border (the border has no complete ring of 8 neighbours). For the pixel `c`, build the code: bit 0 = top-left neighbour >= `c`, bit 1 = top, bit 2 = top-right, bit 3 = right, bit 4 = bottom-right, bit 5 = bottom, bit 6 = bottom-left, bit 7 = left. (Clockwise from top-left.)
3. Find the grid cell: `cell = (position - 1) * 7 / (width - 2)` for x and for y (integer division). For 112 x 112 that spreads the 110 x 110 inner pixels over 7 x 7 cells of 15 or 16 pixels.
4. Add 1 to the counter `[cell][table.bin[code]]`.
5. For each cell: divide every counter by the cell's total (if the total is 0, all values stay 0), then replace each value by its square root. After this, the squares of one cell's 59 values add up to 1.

The code assumes a square picture (it uses `width - 2` for both directions); the callers guarantee 112 x 112.

**Used by.** `LbpExtractor::extract_aligned` and `lbp_raw_histogram`.

### equalize

```cpp
void equalize(Image& gray)
```

**What it does.** Spreads the brightness values of a gray picture over the whole range 0..255 ("histogram equalization"), in place, so that lighting differences (a darker or brighter photo) matter less.

**How it works.**
1. Count how many pixels there are for each brightness 0..255.
2. Walk from 0 to 255, adding up the counts ("cumulative"). The new value of brightness `v` is `min(255, cumulative * 255 / total)`, where `total` is the number of pixels.
3. Replace every pixel by its new value.

The effect: brightness levels that many pixels share are pulled apart, so the result is a picture whose brightness values are spread roughly evenly over 0..255, whatever the original lighting was.

**Used by.** `LbpExtractor::extract_aligned` and `lbp_raw_histogram`.

### LbpExtractor (class and constructor)

```cpp
class LbpExtractor : public FeatureExtractor {
public:
    explicit LbpExtractor(int dim)
    ...
private:
    int dim_;
    std::vector<float> mean_, inverse_spread_;
    std::vector<float> projection_;
    std::vector<float> raw_;
    UniformTable table_;
    Image aligned_, gray_;
};
```

**What it does.** The implementation of `FeatureExtractor` for the classical method. The constructor prepares the three tables that do not depend on the face: a mean vector of zeros, an inverse-spread vector of ones (so "no calibration" until `load_calibration` replaces them), and the random projection matrix.

**How it works.** The random projection is a matrix with `dim` rows and 2891 columns (for 256: 740,096 numbers, about 3 MB). Each entry is a random number from a normal (bell-curve) distribution with mean 0 and spread 1, multiplied by `1 / sqrt(2891)`. The random generator is `std::mt19937` with the **fixed seed 20240601**, so the matrix is meant to be the same on every run: two faces are always projected with the same matrix. A caution that is general C++ knowledge and not visible in this code: the C++ standard fixes the numbers produced by `std::mt19937`, but not the exact algorithm of `std::normal_distribution`, so another compiler or standard library could make a different matrix. Treat features as comparable only when they come from the same build.

The members: `dim_` (output length), `mean_` and `inverse_spread_` (2891 values each, the calibration), `projection_` (the matrix), `raw_` (a reusable buffer for the histogram numbers), `table_` (a `UniformTable`), `aligned_` and `gray_` (reusable pictures). As with `DeepExtractor`, one object should not be used from two threads at once.

**Used by.** Only `create_lbp_extractor`.

### LbpExtractor::dim

```cpp
int dim() const override { return dim_; }
```

**What it does.** Returns the length of the features (256 for `classical.ini`).

**How it works.** A one-line getter.

**Used by.** See `FeatureExtractor::dim`.

### LbpExtractor::name

```cpp
const char* name() const override { return "lbp/classical"; }
```

**What it does.** Returns the fixed text `"lbp/classical"`.

**How it works.** A one-line function returning a string literal.

**Used by.** See `FeatureExtractor::name`.

### LbpExtractor::load_calibration

```cpp
void load_calibration(const std::string& path)
```

**What it does.** Optionally loads the calibration made by `tools/calibrate_lbp`: the mean and `1 / spread` of each of the 2891 histogram values. Why it exists (from the comment in `extractor.h`): the LBP histogram of every face looks much like that of every other face, which would make all similarities high; subtracting the average histogram and dividing by the usual spread fixes that. This is not training of a model; it only needs the average and spread over some faces.

**How it works.**
1. Open the file in binary mode. If it cannot be opened, print `extractor: <path> not found: the classical extractor runs UNCALIBRATED (make the file with tools/calibrate_lbp)` to `std::cerr` and return. The extractor then keeps mean 0 and 1/spread = 1, i.e. it works without calibration.
2. Read `2 * 2891` floats (23,128 bytes: the file has no header; first the 2891 means, then the 2891 values of 1/spread, in the computer's native float format).
3. If fewer bytes are available than that, print `extractor: <path> has the wrong size and is ignored (UNCALIBRATED)` to `std::cerr` and return, again keeping the defaults. (Only a file that is too short is detected: the code reads exactly 23,128 bytes, so extra bytes at the end of a longer file are not noticed.)
4. Otherwise copy the first half into `mean_` and the second half into `inverse_spread_`.

The messages are there because, as the code comment says, without calibration all similarities are high, so a normal threshold lets strangers in, and features stored without calibration do not match features made with it.

**Used by.** `create_lbp_extractor`, which passes `profile.root + "/models/lbp_calibration.bin"`. Reading the file is done only by this private method when the extractor is created, as the comment in `extractor.h` says; the only public calibration function that touches the file is `lbp_save_calibration` (writing).

### LbpExtractor::extract

```cpp
bool extract(const FaceCrop& crop, Feature& out) override
```

**What it does.** Computes the LBP feature of a face crop. Returns `false` if the crop image is empty or the face has no landmarks; otherwise the result of `extract_aligned`.

**How it works.** The same two steps as `DeepExtractor::extract`: `align_face` into the member `aligned_` (112 x 112), then `extract_aligned`.

**Used by.** Everything that calls `FeatureExtractor::extract`.

### LbpExtractor::extract_aligned

```cpp
bool extract_aligned(const Image& aligned, Feature& out) override
```

**What it does.** Computes the LBP feature of an aligned face. Returns `false` only if the picture is not 112 x 112; otherwise it always succeeds and writes `dim_` values of length 1 into `out`.

**How it works.**
1. `to_gray` converts to one channel (a gray input is just copied), then `equalize`.
2. `compute_raw_histogram` fills `raw_` with the 2891 square-rooted values.
3. Calibration: for every `i`, `raw[i] = (raw[i] - mean_[i]) * inverse_spread_[i]`.
4. Random projection: output value number `d` is the sum over `i` of `projection_[d][i] * raw[i]` (a matrix times vector product, 256 x 2891 multiplications).
5. `normalize_feature(out)` scales the result to length 1.

**Used by.** `LbpExtractor::extract` only.

### lbp_raw_size

```cpp
int lbp_raw_size();
```

**What it does.** Returns 2891, the number of histogram values per face.

**How it works.** Returns the constant `kRawDim`. It exists so that other files (the calibration tool) do not need to know the constant.

**Used by.** `tools/calibrate_lbp.cpp`.

### lbp_raw_histogram

```cpp
bool lbp_raw_histogram(const Image& aligned, std::vector<float>& raw);
```

**What it does.** For an aligned 112 x 112 face, computes the 2891 square-rooted histogram numbers without calibration. Returns `false` if the picture size is not 112 x 112. This is the quantity whose mean and spread the calibration tool measures.

**How it works.** `to_gray`, `equalize`, then `compute_raw_histogram`, with a function-level `static const UniformTable` (built once and reused).

**Used by.** `tools/calibrate_lbp.cpp`.

### lbp_save_calibration

```cpp
bool lbp_save_calibration(const std::string& path, const std::vector<float>& mean, const std::vector<float>& inverse_spread);
```

**What it does.** Writes a calibration file. Returns `false` if either vector does not have exactly 2891 values or if writing fails.

**How it works.** Opens `path` in binary mode (an existing file is replaced), writes the 2891 means and then the 2891 values of 1/spread as raw floats, and returns whether the stream is still healthy. It writes directly to `path` (no temporary file). The shipped file `models/lbp_calibration.bin` is 23,128 bytes, which equals 2 x 2891 x 4.

How `tools/calibrate_lbp` makes the vectors: for every face of the portraits in the chosen letter range it takes the 2891 values from `lbp_raw_histogram`, then per value computes the mean and the standard deviation (the "spread") over all faces. It stores `1 / max(spread, 0.01)` (the floor stops very small spreads from creating huge weights) and requires at least 10 faces.

**Used by.** `tools/calibrate_lbp.cpp`.

### create_lbp_extractor

```cpp
std::unique_ptr<FeatureExtractor> create_lbp_extractor(const ModelProfile& profile);
```

**What it does.** Builds an `LbpExtractor` and loads its calibration file if one exists. Never returns `nullptr` (there is no model to fail to load).

**How it works.**
1. The feature length is `profile.feature_dim` if it is between 1 and `FA_MAX_FEATURE_DIM` (512), otherwise 256. (So with `classical.ini`, `recognizer.dim = 256` gives 256.)
2. Build the `LbpExtractor` (this creates the random projection).
3. Call `load_calibration(profile.root + "/models/lbp_calibration.bin")`. `profile.root` is the second argument that was given to `load_profile` (the tools use `"."`). If the file is missing (or too short), the extractor still works, without calibration, and `load_calibration` prints a message that contains `UNCALIBRATED` to `std::cerr`.

**Used by.** `create_extractor` only.

## Supporting code from other folders

The extractors depend on a few functions that live in `src/common/`. They are listed here only so that the picture is complete; they have their own owners.

| Function | File | Purpose |
|---|---|---|
| `align_face` | `image_ops.h` / `.cpp` | Rotate/scale/shift a crop so the 5 landmarks land on the 112 x 112 template. Described below. |
| `aligned_template_112` | `image_ops.cpp` | The 5 standard landmark positions inside a 112 x 112 face. |
| `estimate_similarity` | `image_ops.cpp` | Least-squares rotation + scale + shift that maps 5 points onto 5 other points. |
| `invert_similarity` | `image_ops.cpp` | Reverse of that transform. |
| `warp_affine` | `image_ops.cpp` | Builds the output picture by looking up each pixel in the source (bilinear). |
| `to_gray`, `crop` | `image_ops.cpp` | Colour to gray; copy a rectangle out of an image (outside pixels become 0). |
| `InputPrep`, `Net::run`, `create_ncnn_net`, `create_onnx_net` | `inference.h` / `.cpp` | Run a network with ncnn or ONNX Runtime; `InputPrep` is `value = (pixel - mean[c]) * norm[c]` with optional BGR to RGB swap. |
| `load_profile`, `ModelProfile` | `profile.h` / `.cpp` | Read a `models/*.ini` file. |

### align_face

```cpp
void align_face(const Image& src, const Face& face, Image& aligned);
```

**What it does.** Produces a `FA_ALIGNED_FACE_SIZE` x `FA_ALIGNED_FACE_SIZE` (112 x 112) picture in which the five landmarks of `face` (given in the coordinates of `src`) lie on the standard positions of the ArcFace/SFace template. The result has the same number of channels as `src`. Pixels whose source position falls outside the source image stay 0 (black; the code also treats the last column and last row of the source as outside); that is why `make_face_crop` takes a margin.

**How it works.**
1. Copy the 5 landmarks of `face`.
2. `estimate_similarity(landmarks, template, to_aligned)` finds the rotation, uniform scale and shift that moves the landmarks as close as possible (least squares) to the template. The result is the matrix `[a, -b, tx, b, a, ty]`, meaning `x' = a*x - b*y + tx` and `y' = b*x + a*y + ty`.
3. `invert_similarity` reverses it, because `warp_affine` works backwards: for each output pixel it asks "where in the source does this come from?".
4. `warp_affine` fills the 112 x 112 output using bilinear interpolation (each output pixel is a weighted mix of the 4 nearest source pixels).

The template (`aligned_template_112`), in the order right eye, left eye, nose tip, right mouth corner, left mouth corner, is: `(38.2946, 51.6963)`, `(73.5318, 51.5014)`, `(56.0252, 71.7366)`, `(41.5493, 92.3655)`, `(70.7299, 92.2041)`. The landmark order of `Face` (`types.h`) is the same.

**Used by.** `DeepExtractor::extract`, `LbpExtractor::extract`, `tools/calibrate_lbp.cpp`. (`aligned_template_112` is also used by `src/detect/detector_haar.cpp`.)

## Settings that control this engine

Macros in `src/common/config.h` (change the file, or pass `/DNAME=value` to the compiler, see `ARCHITECTURE.md`):

| Macro | Default | Meaning here |
|---|---|---|
| `FA_ALIGNED_FACE_SIZE` | `112` | Side of the aligned square face. Both extractors reject other sizes. The template in `aligned_template_112` is written for 112 and the code does not rescale it, so do not change this macro without also changing the template and the model. |
| `FA_MAX_FEATURE_DIM` | `512` | Upper limit for the LBP feature length in `create_lbp_extractor`. The comment in `config.h` says it is the largest length the classical extractor and the synthetic generator (`tools/gen_features.cpp`) accept, and that the deep extractors, the matcher and the store do not check it. |

Profile entries read by the extractor (`ModelProfile` in `profile.h`, keys in `models/*.ini`):

| Key | Field | Default in `ModelProfile` | Used for |
|---|---|---|---|
| `recognizer.type` | `recognizer_type` | `"sface"` | Chooses the extractor: `sface`, `arcface` or `lbp`. |
| `recognizer.backend` | `recognizer_backend` | `"onnx"` | `ncnn` or `onnx` (deep only). |
| `recognizer.model` | `recognizer_model` | empty | Model file (`.onnx`, or `.param` for ncnn). Relative paths start at the project root. |
| `recognizer.input` | `recognizer_input` | `"in0"` | ncnn only: input blob name. |
| `recognizer.output` | `recognizer_output` | `"out0"` | ncnn only: output blob name. |
| `recognizer.dim` | `feature_dim` | `128` | Feature length (checked against the network's answer; for LBP it is the projection size). |
| `recognizer.threshold` | `match_threshold` | `0.363f` | Not used by the extractor; the pipeline uses it to accept a match (see `match.md`). |
| `runtime.threads` | `threads` | `2` | CPU threads for each network. |

The five profiles shipped in `models/`:

| Profile | `recognizer.type` | Backend | Model file | `recognizer.dim` | `recognizer.threshold` |
|---|---|---|---|---|---|
| `permissive.ini` | `sface` | `onnx` | `models/permissive/sface.onnx` | `128` | `0.55` |
| `dev_noncommercial.ini` | `arcface` | `ncnn` | `models/dev_noncommercial/rec.param` (input `in0`, output `out0`) | `512` | `0.40` |
| `dev_noncommercial_square.ini` | `arcface` | `ncnn` | `models/dev_noncommercial/rec.param` (input `in0`, output `out0`; the same recognizer as `dev_noncommercial.ini`, only its SCRFD detector differs) | `512` | `0.40` |
| `dev_noncommercial_onnx.ini` | `arcface` | `onnx` | `models/dev_noncommercial/arcface_w600k_mbf.onnx` | `512` | `0.40` |
| `classical.ini` | `lbp` | not set (default `onnx`, ignored by LBP) | none | `256` | `0.50` (the file calls it a placeholder) |

Licence notes for the models are in `MODELS.md`. Accuracy and speed measurements are in `RESULTS.md`.

LBP calibration files that exist must have the exact expected size, finite means, and finite positive inverse spreads; invalid files fail extractor creation. A missing optional calibration retains the existing uncalibrated warning/fallback. LBP extraction returns false with an empty output for a zero or non-finite projected feature, including a valid calibration that centers the input to zero.
