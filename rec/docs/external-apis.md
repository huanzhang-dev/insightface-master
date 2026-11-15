# External APIs and callbacks

> Current Unicode update: Additional Unicode Windows and Tesseract C API calls are described in [Unicode reference](unicode-and-multilingual.md). CreateFontW/DrawTextW supersede their ANSI counterparts.

[Documentation index](README.md)

These APIs are directly used by the first-party C++ sources. Overloaded constructors and container/geometry operators are grouped by purpose. Project-defined functions have their full contracts in the [function index](function-index.md); this document does not claim to enumerate internal calls made inside vendor libraries.

The pinned dependency versions are OpenCV 4.5.5, ONNX Runtime DirectML 1.12.1, and Microsoft DirectML 1.9.0. Local declarations for ORT are in [onnxruntime_c_api.h](../third_party/onnxruntime/build/native/include/onnxruntime_c_api.h); artifact hashes and origins are in [manifest.json](../models/manifest.json).

## OpenCV image and matrix APIs

| Function / operation | Use in this project |

| --- | --- |

| `cv::Mat, cv::Mat_<float>` | Allocate/wrap image, state, measurement, feature, and tensor storage; `Mat_` comma initialization fills small float matrices. |

| `cv::Mat::zeros` | Create zero-filled Kalman measurement matrices or empty glyph features. |

| `cv::Mat::empty, channels, size, total` | Check image availability/layout and validate model-output element counts. |

| `cv::Mat::at<T>, ptr<T>` | Access Kalman entries and contiguous model-output rows/timesteps. |

| `cv::Mat::clone` | Deep-copy submitted frames, crops, or inference output before source buffers disappear. |

| `cv::Mat::copyTo` | Copy resized frames into letterbox ROIs and rendered plates/photos into generated scenes. |

| `cv::Mat::reshape` | View model outputs as rows or flatten glyph canvases without changing element order. |

| `cv::Mat::convertTo` | Convert features to normalized float and darken synthetic night images. |

| `cv::Mat::push_back` | Append training feature rows and numeric class labels. |

| `cv::Mat::setTo` | Reset a generated plate board to white. |

| `cv::Mat::release` | Release the pending image reference after handing it to the inference loop. |

| `cv::Mat::operator()(Rect)` | Create an ROI view; rectangles must lie inside the image. |

| `cv::Point, cv::Size, cv::Scalar, cv::Rect, cv::Rect2f` | Construct points, dimensions, fill colors, and integer/float rectangles. |

| `cv::Rect_::area, contains, size, tl` | Measure areas, associate plate centers, size resized photos, and convert ROI coordinate origins. |

| `cv::Rect_ operators &, &=, +, -` | Intersect/clip rectangles and translate between full-frame and vehicle-relative coordinates. |

| `cv::resize` | Scale images for model inputs, glyph features, and generated photo scenes. |

| `cv::cvtColor` | Convert BGR to grayscale and GDI BGRA bitmaps to BGR. |

| `cv::threshold` | Create fixed-threshold bright-plate masks or inverse-Otsu glyph masks. |

| `cv::findContours` | Locate candidate plate regions and separated glyph components. |

| `cv::boundingRect` | Convert contours/nonzero glyph points to axis-aligned bounds. |

| `cv::findNonZero` | Locate glyph foreground pixels before feature normalization. |

| `cv::dilate, cv::erode` | Generate thicker/thinner glyph training variants. |

| `cv::setIdentity` | Initialize Kalman transition/noise/covariance matrices. |

| `cv::rectangle, cv::line, cv::putText` | Draw boxes, separate text fields, trails, procedural vehicles, road markings, and labels. |

| `cv::imread, cv::imwrite` | Load photo assets and save plate fixtures/crops/inspection snapshots. |

| `cv::imshow, cv::waitKey, cv::destroyAllWindows` | Present preview frames, process Esc/q input, and close GUI windows. |

| `cv::setNumThreads` | Configure OpenCV CPU threading for application/test runs. |

## OpenCV models, tracking, and video APIs

| Function / operation | Use in this project |

| --- | --- |

| `cv::KalmanFilter::init` | Allocate the eight-state/four-measurement vehicle filter. |

| `cv::KalmanFilter::predict, correct` | Propagate motion and apply matched box measurements. |

| `cv::dnn::readNetFromONNX` | Load CPU/OpenCL networks from the configured model files. |

| `cv::dnn::Net::setPreferableBackend, setPreferableTarget` | Choose the OpenCV backend with CPU or OpenCL execution. |

| `cv::dnn::Net::setInput, forward` | Run CPU/OpenCL inference and return model output zero. |

| `cv::dnn::blobFromImage` | Create NCHW float blobs; handles resize, RGB channel swap, and configured scaling/mean. |

| `cv::dnn::NMSBoxes` | Suppress overlapping vehicle or plate detections; current vehicle suppression is class-agnostic. |

| `cv::ocl::haveOpenCL, setUseOpenCL` | Check availability and enable the optional OpenCL target. |

| `cv::ml::KNearest::create, setDefaultK, train, findNearest` | Construct/train the glyph classifier and predict labels with neighbor distances. |

| `cv::VideoCapture constructors, open, isOpened` | Create camera/file capture and verify it opened. |

| `cv::VideoCapture::set, get` | Request buffering/camera FPS, read native FPS, and seek reference-video frames. |

| `cv::VideoCapture::read, release` | Fetch decoded frames and close the input. |

| `cv::VideoWriter constructors, fourcc, open, isOpened` | Create MJPEG AVI encoders and validate writer startup. |

| `cv::VideoWriter::write, release` | Append annotated/generated frames and finalize output containers. |

## Windows and DXGI APIs

| Function / operation | Use in this project |

| --- | --- |

| `GetModuleFileNameA, GetModuleFileNameW` | Locate the running executable for default project paths and the app-local inference DLL. |

| `GetFullPathNameA` | Normalize the executable-relative project-root guess. |

| `LoadLibraryW, GetProcAddress, FreeLibrary` | Load ONNX Runtime, resolve C exports, and release its library handle. |

| `MultiByteToWideChar` | Convert the UTF-8 model filename to a UTF-16 Windows path for ORT. |

| `CreateDXGIFactory1` | Create a graphics-adapter enumerator for automatic GPU selection. |

| `IDXGIFactory1::EnumAdapters1` | Enumerate candidate adapters until DXGI_ERROR_NOT_FOUND. |

| `IDXGIAdapter1::GetDesc1` | Read dedicated memory and software-adapter flags. |

| `IUnknown::Release` | Release DXGI factory/adapter COM references. |

| `_mkdir` | Try to create output/test directories; this is nonrecursive. |

| `CreateCompatibleDC, CreateDIBSection` | Create a GDI drawing context and top-down pixel-backed glyph bitmap. |

| `SelectObject` | Select/restore the bitmap and font in the drawing context. |

| `FillRect, GetStockObject` | Paint a white glyph background with the stock white brush. |

| `CreateFontA` | Select font family, size, and regular/bold weight for generated text. |

| `SetTextColor, SetBkMode, DrawTextA` | Draw centered antialiased black text with transparent background handling. |

| `DeleteObject, DeleteDC` | Release generated fonts/bitmaps and drawing contexts. |

| `FAILED, RGB, __uuidof` | Macros/compiler operators, not runtime functions: check HRESULTs, compose color values, and obtain interface IDs. |

## ONNX Runtime and DirectML C APIs

| Function / operation | Use in this project |

| --- | --- |

| `OrtGetApiBase, OrtApiBase::GetApi` | Dynamically resolve the runtime base and obtain the compile-time API version table. |

| `OrtApi::CreateEnv, ReleaseEnv` | Create/release the shared inference environment. |

| `OrtApi::GetErrorMessage, ReleaseStatus` | Copy an error message and free a consumed failure status. |

| `OrtApi::CreateSessionOptions, ReleaseSessionOptions` | Allocate/free model-session configuration. |

| `OrtApi::DisableMemPattern` | Disable memory-pattern optimization as required by this DirectML configuration. |

| `OrtApi::SetSessionExecutionMode` | Set sequential execution for DirectML sessions. |

| `OrtApi::SetSessionGraphOptimizationLevel` | Enable all graph optimizations. |

| `OrtApi::SetIntraOpNumThreads` | Set the session CPU intra-op thread count to two. |

| `OrtSessionOptionsAppendExecutionProvider_DML` | Export resolved through a function pointer to append the selected DirectML adapter. |

| `OrtApi::CreateSession, ReleaseSession` | Load an ONNX model session and release it later. |

| `OrtApi::GetAllocatorWithDefaultOptions` | Obtain the allocator used for input/output name strings. |

| `OrtApi::SessionGetInputName, SessionGetOutputName` | Get names for input zero and output zero. |

| `OrtAllocator::Free` | Free name strings returned with the ORT allocator. |

| `OrtApi::CreateCpuMemoryInfo, ReleaseMemoryInfo` | Describe host tensor memory and release that descriptor. |

| `OrtApi::CreateTensorWithDataAsOrtValue` | Wrap the existing contiguous float input blob without copying its host storage. |

| `OrtApi::Run` | Execute the selected session with one named input and one requested output. |

| `OrtApi::GetTensorTypeAndShape` | Get runtime output shape/type metadata. |

| `OrtApi::GetDimensionsCount, GetDimensions` | Retrieve tensor rank and actual dimensions for a cv::Mat wrapper. |

| `OrtApi::GetTensorMutableData` | Access output memory before cloning it into an owned OpenCV matrix. |

| `OrtApi::ReleaseTensorTypeAndShapeInfo, ReleaseValue` | Release shape metadata and temporary input/output tensor wrappers. |

## C++ standard library and language operations

| Function / operation | Use in this project |

| --- | --- |

| `std::min, std::max` | Clamp dimensions, timesteps, drawing positions, FPS, and confidence calculations. |

| `std::sqrt, std::exp, std::sin, std::isfinite` | Compute distances/scores, decode model widths/heights, animate scenes, and reject nonfinite outputs. |

| `std::sort, std::max_element, std::remove_if` | Rank candidates/confidences, choose class logits, order glyphs, and erase expired tracks. |

| `std::atoi, std::stoi, std::stof, std::stod` | Convert CLI/camera index values; string conversions can throw on invalid/out-of-range input. |

| `std::string / std::wstring constructors, substr, find_last_of, c_str, empty, size` | Build paths and output strings, isolate text characters, and pass strings to C APIs. |

| `std::vector constructors, size, empty, begin, end, data, push_back, erase, resize, operator[]` | Own detections, tracks, assignment arrays, training data, tensor dimensions, and output lists. |

| `std::map::operator[], iteration` | Accumulate class/plate votes and per-text observation counts. |

| `std::deque::push_back, pop_front, size, operator[]` | Maintain the bounded trail of vehicle center points. |

| `std::ofstream constructors and operator bool` | Open/check CSV, JSONL, ground-truth, and performance report streams; destructors close streams. |

| `std::ostringstream::str, stream operator<<, std::fixed, std::setprecision` | Format overlay labels, numeric reports, and console output. |

| `std::unique_ptr constructors, reset, operator->, destructor` | Own workers/pipelines and GPU implementation objects with deterministic destruction. |

| `std::thread constructor, join` | Start the inference member-function thread and wait for its exit. |

| `std::mutex; std::lock_guard; std::unique_lock` | Protect pending/latest frames, error text, and worker shutdown flags with RAII locking. |

| `std::condition_variable::wait, notify_one` | Sleep until a frame/stop condition arrives; wake the worker after submission or shutdown. |

| `std::atomic<int> constructor, load, operator++` | Track overwritten pending frames without a data race. |

| `std::chrono::steady_clock::now, duration<double>, duration::count, milliseconds` | Measure loop/inference time and construct preview deadlines. |

| `std::this_thread::sleep_until` | Pace nonoffline file playback to the source clock. |

| `std::runtime_error constructor, std::exception::what` | Represent runtime/test failures and report their messages. |

| `std::cout, std::cerr` | Standard streams used for help, diagnostics, model adapter information, and test results. |

| `new, delete through RAII, sizeof, casts, range-for, operators` | Language constructs used for ownership and data manipulation; not additional project-defined functions. |

## Anonymous callbacks defined by this project

| Function / operation | Use in this project |

| --- | --- |

| `Tracker::predict expiry predicate` | Captures now; returns true when now-lastSeen exceeds two seconds for std::remove_if. |

| `Recognizer::readGlyphs ordering predicate` | Orders character rectangles by ascending x for std::sort. |

| `Pipeline::plateCandidates ordering predicate` | Orders candidate rectangles by descending area. |

| `Pipeline::detect ordering predicate` | Orders surviving vehicle detections by descending confidence. |

| `Worker::loop wait predicate` | Captures this; permits wake-up when stop is set or a pending image exists. |

Night preprocessing APIs and the reproducible noise generator are documented in [lighting reference](lighting.md).

Persistent tracking uses existing OpenCV resize/cvtColor/norm, histogram accumulation and standard vector/set containers. The review writer uses VideoWriter, imwrite, rectangle/putText and source photographs. No external ReID network, plate-text identity lookup, or fabricated detection is introduced. See [function reference](persistent-tracking.md).
