CAR - CMAKE PROJECT WITH PRESERVED MODELS AND DEPENDENCIES

Keep this entire directory together when transferring it to another PC:
  src/, include/       Application, inference, preprocessing and OCR source.
  models/              ONNX weights, multilingual tessdata and model metadata.
  third_party/         OpenCV 4.5.5 SDK and archive; ONNX Runtime/DirectML and
                       Tesseract/Leptonica SDKs, runtime DLLs and packages.
  cmake/               Dependency versions, download URLs, hashes and notices.
  docs/, licenses/     Application documentation and dependency licenses.

Generated projects, executables, objects and build caches must remain outside
this directory. CMake rejects builds in this directory or its descendants.
Application C++ source has not been changed by this packaging work.

ON ANOTHER WINDOWS PC
Install Visual Studio 2015 Update 3 or Visual Studio 2019 Desktop C++ with an
x64 compiler and Windows SDK. Use CMake 3.20 (or a newer version that supports
your VS generator) and the matching VC++ runtime. GPU mode needs compatible
Windows/DirectML hardware and drivers. OCR self-tests also require Windows
fonts used by the application. These system prerequisites are not bundled.

Copy the entire project, including models/ and third_party/, to C:/src/car.
No dependency downloads are needed when using the preserved SDKs and models.
CMake defaults CAR_DEPS_ROOT and CAR_MODELS_DIR to the copied project directory
and its models/ directory, and discovers the bundled OpenCV SDK automatically.

VS2019 x64:
  cmake -S C:/src/car -B C:/build/car-v142 -G "Visual Studio 16 2019" -A x64 -T v142
  cmake --build C:/build/car-v142 --config Release --parallel
  ctest --test-dir C:/build/car-v142 -C Release --output-on-failure

VS2015 x64:
  cmake -S C:/src/car -B C:/build/car-v140 -G "Visual Studio 14 2015" -A x64 -T v140
  cmake --build C:/build/car-v140 --config Release --parallel
  ctest --test-dir C:/build/car-v140 -C Release --output-on-failure

For an existing build cache, recreate the external build directory or explicitly
set -DCAR_DEPS_ROOT=C:/src/car and -DOpenCV_DIR=C:/src/car/third_party/opencv/build/x64/vc14/lib
and clear any stale ONNXRUNTIME_INCLUDE_DIR cache value. The commands above are
intended for fresh build directories.

RUN WITH EXPLICIT MODEL/OUTPUT PATHS
  C:/build/car-v142/bin/Release/Car.exe --source 0 --directml --models C:/src/car/models --output C:/run/car/output
  C:/build/car-v142/bin/Release/Car.exe --source 0 --cpu --models C:/src/car/models --output C:/run/car/output
  C:/build/car-v142/bin/Release/Car.exe --generate-test C:/run/car/testdata --models C:/src/car/models

RESTORE OR VERIFY DEPENDENCIES
  powershell -ExecutionPolicy Bypass -File C:/src/car/tools/setup.ps1
By default setup verifies/downloads packages and models into this project.
-Destination C:/deps/car remains available for an alternative dependency root.
Downloads require network access; tar.exe extracts only the OpenCV SDK without
launching the self-extracting installer GUI. Current Windows 10/11 includes tar.

CMake copies the required runtime DLLs into the external executable directory.
Override CAR_MODELS_DIR, CAR_DEPS_ROOT, OpenCV_DIR or CAR_OPENCV_RUNTIME_DIR as
needed. The full Car application targets x64; the earlier x86 compilation
check covered only OpenCV core/imgproc.

CTest runs the existing 55 application self-test checks. It does not establish
camera accuracy, FPS guarantees or the legacy video/export verification result.
tools/validate_outputs.ps1 runs the CMake build and these CTest self-tests.
Historical .sln/bin/Release instructions in docs/ refer to the old build layout;
use this README for the current CMake workflow.
