# Dependencies

The implementation uses Windows APIs and local C++ code for all media/image operations. The previous image-library headers, libraries, DLLs, codec DLLs, reference scripts, SDK license copy and linked build outputs were removed from this project. Other projects and their SDK installations were not changed.

| Component | Version/source | Use |
|---|---|---|
| Windows Imaging Component (WIC) | Windows system component | PNG/JPEG decode and JPEG encode. |
| Media Foundation | Windows system component | Camera discovery, format selection and file/stream fallback decoding. |
| Win32/GDI | Windows system component | Preview window, text, pixel display and optional warning sound. |
| ONNX Runtime | Microsoft.ML.OnnxRuntime.DirectML 1.20.1 | Default CPU neural inference via C ABI. |
| DirectML | Microsoft.AI.DirectML 1.15.2 | Optional adapter-0 inference. |
| BlazeFace / Face Mesh ONNX weights | [Yakhyo MediaPipe ONNX models](https://github.com/yakhyo/mediapipe-face-mesh-onnx/releases/tag/weights) | Face detection and 468 facial landmarks. |
| Canonical facial geometry | [Google MediaPipe](https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_geometry/data/canonical_face_model.obj) | Six canonical vertices used in the native pose solver. |
| NASA astronaut fixture | [scikit-image astronaut.png](https://github.com/scikit-image/scikit-image/blob/v0.19.3/skimage/data/astronaut.png) | Public-domain Eileen Collins photo for image tests. |

Bundled model and runtime hashes are in `cmake/assets-manifest.json`. `tools/setup.ps1` verifies both; `-RestoreModels` can restore missing URL-listed assets after checking hashes. No Python installation is required to build or run. Runtime headers and DLLs remain under `Z:\sleep-data\third_party\onnxruntime`; model weights remain under `Z:\sleep-data\models`.

Licenses preserved: `third_party/onnxruntime_LICENSE` (MIT), `DirectML_LICENSE.txt` (Microsoft package terms), and `reference_LICENSE` (Apache 2.0 for the converted model project). Canonical MediaPipe geometry is Apache 2.0. Reference Python scripts are not shipped or executed by this implementation.

Official runtime package sources:

- [ONNX Runtime DirectML 1.20.1](https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.20.1/microsoft.ml.onnxruntime.directml.1.20.1.nupkg): `build/native/include`, `runtimes/win-x64/native/onnxruntime.dll`.
- [DirectML 1.15.2](https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.2/microsoft.ai.directml.1.15.2.nupkg): `bin/x64-win/DirectML.dll`.

Media Foundation and WIC require the relevant Windows media components; Windows N/Server installations may need those features installed. A physical NIR camera must expose a compatible Media Foundation video device. Native Y16/Y8 modes depend on the camera driver. DirectML needs a compatible DX12 adapter/driver; CPU is the default and does not require a discrete GPU.

MJPEG capture/fallback decoding requests full-range YUY2 and converts it explicitly to BGR, avoiding the observed RGB32 range expansion in the Windows decoder path. Color-range metadata follows [Microsoft Media Foundation guidance](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-mt-video-nominal-range-attribute). Independent native-decoder regression checks source frame intensities as well as count/dimensions.
