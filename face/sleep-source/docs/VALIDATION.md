# Validation

Run `powershell -ExecutionPolicy Bypass -File tools/validate.ps1 -DirectML`. The script verifies model/runtime hashes, configures/builds CMake/Ninja with both v140/v142, runs CTest and 45 functional checks, inspects compiler/output PE architectures, audits binary import tables, regenerates/fully decodes videos, and checks paced 15/30 FPS playback. Evidence is in `Z:\sleep-data\tests\verification.json` and per-toolset reports.

Compiler evidence is x86 compiler PE machine 0x014c and x64 output machine 0x8664, plus an `_M_X64` compile-time probe. Host OS is Windows AMD64; this is an x86-hosted Windows x64 toolchain check, not Linux-to-Windows compilation. CMake's explicit toolchain flag is recorded but not used alone as proof.

Functional coverage: temporal blinks/closure/PERCLOS, latching/recovery, unknown coverage, sample gaps and 15/60 FPS timing equivalence, closed-start calibration, yawn/nod fusion, invalid measurements/config/time, Y16/gray conversion, JSON/overlay handling, EAR, subject reset, native pose recovery/nonfinite rejection, grayscale weights/resize/crop, actual neural face/mesh inference, opaque mode, eye mask, IR-style preprocessing, multiple faces including JPEG/grayscale input, strict ROI isolation, darkness, blur, rotation, WIC JPEG round trip, native AVI round trip plus independent Media Foundation pixel/frame decoding and malformed tensor rejection.

Demo verification decodes every generated frame: 360 photo-transform input frames, 360 actual-inference output frames, and 600 scripted temporal frames at 30 FPS. The scripted sequence expects its first sustained-closure alarm at about 10.5 seconds, no blink alarm, persistence through occlusion and open-eye recovery.

Paced playback checks achieved preview within 10% of 15/30 FPS and >=25 accepted inferences/second on this host. Dark, masked and two-face intervals must remain unknown with no eye-state claim; the open-eye photo sequence must not alarm. DirectML testing is an optional separate 90-frame offline throughput check.

No physical IR camera, real sunglasses/glasses sequence, real out-of-plane pose dataset or real drowsiness dataset was tested. Passing 45 assertions is not an accuracy percentage. Sensor-to-alarm latency and hard-condition accuracy require real labeled sequences and hardware tests.

`-UseExistingEvidence` reruns integrity, architecture/import, artifact and cleanup assertions against already completed test files without rebuilding/replaying; the report explicitly records this mode. Source files must not be newer than each binary, and per-toolset functional reports must postdate their binaries. Omit it for a fresh full run.
