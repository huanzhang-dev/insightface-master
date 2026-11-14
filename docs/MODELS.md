# Models, libraries and test data: what is used and under which licence

This page lists everything that is not our own code. **Licences were checked
against the source repositories on 2026-11-11.** A licence on a model file does
not automatically cover the data the model was trained on; that is noted where
it matters. Have your own legal review before shipping a product.

## Neural network models

| Role | Model | File | Licence | Runtime | Features |
|---|---|---|---|---|---|
| Face detector (default) | **YuNet** (libfacedetection, OpenCV Zoo `face_detection_yunet_2023mar`) | `models/permissive/yunet.onnx` | MIT (repository LICENSE) | ONNX Runtime | 5 landmarks |
| Face recognizer (default) | **SFace** (MobileFaceNet trained with the SFace loss, OpenCV Zoo `face_recognition_sface_2021dec`) | `models/permissive/sface.onnx` | Apache-2.0 (repository LICENSE) | ONNX Runtime | **128 numbers** |
| Face detector (development) | **SCRFD det_500m** (InsightFace `buffalo_sc` pack) | `models/dev_noncommercial/det.param` + `det.bin` (ncnn, fixed 640x640 input), `det_dyn.param` + `det_dyn.bin` (ncnn, any size that is a multiple of 32), `scrfd_det_500m.onnx` | **non-commercial research only** | ncnn / ONNX Runtime | 5 landmarks |
| Face recognizer (development) | **ArcFace w600k_mbf** (InsightFace `buffalo_sc` pack) | `models/dev_noncommercial/rec.*`, `arcface_w600k_mbf.onnx` | **non-commercial research only** | ncnn / ONNX Runtime | **512 numbers** |

The `dev_noncommercial` models are in the project because they meet the
256..512-number requirement (see `RESULTS.md`). **They must be replaced by a
permissively licensed 512-number model before any commercial use.** The
profile files `models/dev_noncommercial.ini` and `models/dev_noncommercial_square.ini` carry this warning too.

**The files of the SCRFD detector (`models/dev_noncommercial/`).** `det.param` and `det.bin` are the ncnn conversion of
`scrfd_det_500m.onnx`; its two upsampling layers have a fixed output size (`3=40 4=40` and `3=80 4=80` on the two `Interp`
lines), so the network accepts only a 640x640 picture. `det_dyn.param` is a copy of `det.param` in which those two lines say
"twice as large" instead (`1=2.0 2=2.0`) and nothing else differs (checked with a line-by-line comparison of the two
files); `det_dyn.bin` is a byte-for-byte copy of `det.bin` (the same weights, checked with a file comparison). The result is
the same network for any input whose sides are multiples of 32: at 640x640 the raw outputs of `det_dyn` equal the flat
outputs of `det` (largest difference 3.7e-9, `test_common`), and at 640x384 it gives the 48x80, 24x40 and 12x20 grids. The
nine flat outputs `out0` to `out8` are still in `det_dyn.param`, but their `Reshape` layers keep the fixed 80x80 sizes (read from
the file), so they are right for 640x640 only: the profile that uses `det_dyn` reads the nine raw outputs of the last
convolutions (blobs 65, 85, 105, 66, 86, 106, 67, 87, 107) with `detector.layout = chw` (`docs/common.md`, `docs/detect.md`).
Both pairs of files come from the same non-commercial model and carry the same licence restriction. The weights were not
retrained or changed.

**The development profiles also load YuNet.** `dev_noncommercial.ini`, `dev_noncommercial_square.ini` and
`dev_noncommercial_onnx.ini` contain `pose.detector_profile = models/permissive.ini`: SCRFD
finds the faces, but the head pose (pitch, yaw, roll for the pose gate) is measured by the
detector of that other profile, YuNet (`models/permissive/yunet.onnx`, **MIT**), because
SCRFD's landmarks give a much noisier pitch. So with these three profiles the programs load
the YuNet model in addition to SCRFD and ArcFace. This adds no new licence restriction to
the YuNet file (it stays MIT), but a product built from the development profiles still has
the non-commercial SCRFD and ArcFace models in it. In worker mode the worker thread holds
only the YuNet pose detector, not a second SCRFD. `permissive.ini` and `classical.ini` name
no pose detector.

**Training data.** YuNet was trained on WIDER Face, SFace and the InsightFace
models on large face datasets (MS1M / WebFace class). Those datasets have their
own terms, which the model licences above do not remove. This has not been
analysed here.

## Classical (non-neural) components

| Role | Component | Licence |
|---|---|---|
| Face and eye detection | Haar cascades `haar_frontalface.xml`, `haar_eye.xml` from OpenCV (`models/permissive/`) | Intel License Agreement for Open Source Computer Vision Library (BSD-style, quoted in the file header) |
| Feature extraction | LBP histograms, our own code (`extractor_lbp.cpp`) | project code |
| Head pose | formulas on the 5 landmarks, our own code (`pose.cpp`); the landmarks come from the profile's pose detector (YuNet for the development profiles) | project code |

## Libraries (in `third_party/`)

| Library | Version | Licence | Note |
|---|---|---|---|
| ncnn | 1.0.20261020 (`NCNN_VERSION_STRING`), VS2015 x64 build (headers and libraries copied once into `third_party/ncnn/`; this project does not use the folder it came from) | BSD-3-Clause | CPU only: Vulkan is switched off in code for every network (`use_vulkan_compute = false`). The library itself was **built with** its Vulkan code (`NCNN_VULKAN 1` in `platform.h`, hence the glslang libraries), which is never used; see "CPU only" below. It **is** built with its AVX2 and FMA kernels and chooses them at run time (`NCNN_AVX2 1`, `NCNN_FMA 1`, `NCNN_RUNTIME_CPU 1`; `NCNN_AVX512 0`), so a missing AVX2 build is not why SCRFD on ncnn is slower than on ONNX Runtime (`docs/RESULTS.md`, section 5e) |
| ONNX Runtime | **1.17.3** (pinned, see below) | MIT | CPU only: the CPU build, providers `CPUExecutionProvider` and the remote `AzureExecutionProvider` (no GPU), and no execution provider is appended in code; no GPU provider library is shipped (`onnxruntime.dll` and `onnxruntime_providers_shared.dll` only) |
| SQLite | **3.54.1** (the amalgamation, `third_party/sqlite/sqlite3.c` and `sqlite3.h`) | Public domain (SQLite's own blessing instead of a licence; no conditions) | The optional database format (CSV is the default). Downloaded from sqlite.org (`sqlite-amalgamation-3540100.zip`) and verified against the SHA3-256 value published on sqlite.org/download.html (`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`). Compiled as plain C into the project (by `build.bat` and by the Visual Studio project `fa_sqlite`), not used as a DLL. It compiles with the real Visual Studio 2015 C compiler |

**CPU only: what exactly is enforced, and how it is proven.** All engines run on the CPU (the target PC has no GPU).

* **ncnn:** `src/common/inference.cpp` sets `net_.opt.use_vulkan_compute = false` before a model is loaded, so ncnn never creates a Vulkan instance or opens a GPU, although the bundled library was built with its Vulkan code. Rebuilding ncnn with `NCNN_VULKAN=OFF` would remove that code path altogether; this was **not done**.
* **ONNX Runtime:** the bundled 1.17.3 is the CPU build. The session options set only the thread counts and the optimisation level; no execution provider is appended, so the built-in CPU provider does everything. The library lists `AzureExecutionProvider` too: that is no GPU, it sends data to a remote service and works only if a program appends it to a session, which nothing here does.
* **The matcher** is AVX2 code and **Haar and LBP** are plain C++ (no library that could use a GPU). **Media Foundation** (video decode and encode, the camera) is told not to use hardware: the source readers (video files and the camera) are created with `MF_SOURCE_READER_DISABLE_DXVA = TRUE` and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE`, the sink writer of the MP4 files with `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (`docs/camera.md`), and `src\camera` hands over no Direct3D device manager. The encoder is the Microsoft software H.264 encoder. One nuance, measured: a program that writes an MP4 (`gen_video`, the annotated video of `check_video`) has an idle Direct3D device while it encodes (the `3d` engine at 0.0% utilization, no video engine); no engine does.
* **Proof:** every network, detector, extractor and the pipeline answer `runs_on_cpu()`; `test_common` checks that ONNX Runtime offers no provider except the CPU one (and the unused Azure one); `test_common`, `test_feature` and `test_pipeline` check that no GPU library (Vulkan, Direct3D 12, CUDA and cuDNN, TensorRT, OpenCL, DirectML, a GPU ONNX Runtime provider) is loaded into the process after the engines have run; and the Windows GPU counters (`scripts\gpu_probe.ps1`) showed no GPU engine instance for the process of a complete `test_pipeline` run or of `eval_footage`, which decodes an MP4 (the programs that write an MP4 show the idle `3d` device described above, never a video, compute or copy engine). The probes are coarse (one reading about every 3 s). Details, numbers and what is not proven: `docs/TESTING.md` and `docs/RESULTS.md` (section 2, "All engines on the CPU").

**Why ONNX Runtime is pinned to 1.17.3.** Version 1.31 crashes at start on this
PC: it needs a newer Visual C++ runtime (`msvcp140.dll` 14.40+) than Windows
ships here (14.34). A 4th-generation i5 PC will be just as old or older. 1.17.3
works with the runtime that Windows already has. If you upgrade ONNX Runtime,
test on the target PC first.

## Test data

| Data | Where it comes from | Licence |
|---|---|---|
| 135+ official portrait photos of members of the US Congress | Wikimedia Commons, categories "Official portraits of members of the 116th/117th United States Congress", downloaded by `scripts/fetch_portraits.py` | Each file was kept only when Commons' licence metadata says **Public domain** (works of the US federal government). The list with source page, author and licence of every file is `data/portraits/LICENSES.csv`. |
| Synthetic features (10,000 random people) | generated by `tools/gen_features` | none (random numbers) |
| Synthetic video "person walks toward the camera" | generated by `tools/gen_video` and by the tests, from the portraits; the 1920x1080 footage of the evaluation (`data\videos\fhd_1person`, `fhd_3people`, `fhd_5people`, and the same three with a simulated camera look, `fhd_cam_1person`, `fhd_cam_3people`, `fhd_cam_5people`) is made by `scripts\make_fhd_footage.bat`. **It is generated, not recorded:** the footage of the inspection camera could not be found, and the `fhd_cam_*` videos only simulate a camera (a drawn room, simulated light, lens and noise) | derived from the portraits above |

Remarks about the portraits:

* They are photos of real, named public officials. The photos are public domain,
  but the people keep rights over how their likeness is used. They are used here
  only for local testing. **Do not publish `data/portraits` outside this project
  and do not use it to build a product database.** `data/` is listed in `.gitignore`.
* Some people appear under two different file names on Commons. `scripts/merge_portraits.py`
  merges them; eight such cases were found because the *features* of two
  differently named folders scored 0.73..0.96 against each other (different people
  never do). Without that cleaning the evaluation would count them as false accepts.
* Official portraits are evenly lit, straight-on studio photographs. They are an
  **easy** test. Real camera images will give lower same-person scores.
