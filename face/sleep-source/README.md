# Sleep: source-only native Windows C++ project

`Z:\sleep` contains only maintained source, headers, CMake configuration, scripts, manifests and Markdown documentation. No binaries, model weights, external SDKs, images, videos, test reports or generated builds are stored here. No OpenCV dependency is used.

| Location | Contents |
|---|---|
| `Z:\sleep` | Source/configuration/documentation. |
| `Z:\sleep-data` | Models, neural runtime SDK, fixtures, test reports and recordings. |
| `Z:\sleep-build` | External Ninja/CMake build trees and executable/DLL outputs. |
| `Z:\sleep-transfer` | Separate source/assets ZIPs for another PC. |

C++14 / Windows x64 / Visual Studio 2015 v140 and 2019 v142. Native Windows WIC, Media Foundation and GDI handle images/camera/video; ONNX Runtime runs neural face detection/landmarks. Drowsiness decisions use quality-gated geometric and temporal cues.

Build locally with `build_vs2015.cmd` or `build_vs2019.cmd`; run `run_ir_camera.cmd`. Use [another-PC setup](docs/OTHER_PC.md) for a clean machine, or [CMake details](docs/BUILD.md) for custom paths.

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup.ps1
powershell -ExecutionPolicy Bypass -File tools\validate.ps1 -DirectML
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

`setup.ps1` verifies external assets; supply `-AssetArchive` when restoring them on another PC. Packaging includes only verified required assets, not existing recordings, test reports or downloaded package caches. Output archives and SHA-256 metadata stay outside the source tree.

The launcher lets users set camera index, target FPS and eyewear mode. Look forward with open eyes for five usable seconds to calibrate. Q/Esc quits and R resets for another subject. For known IR-opaque lenses, use opaque mode; software cannot observe eyes through material blocking the sensor wavelength.

Physical IR-camera, real eyewear and real drowsiness accuracy remain unmeasured. Review videos are photo-transform neural inference and clearly labeled scripted features, not validation of nighttime sleep-state accuracy. See [operation](docs/OPERATION.md), [architecture](docs/ARCHITECTURE.md), [all function descriptions](docs/FUNCTIONS.md), and [validation](docs/VALIDATION.md).
