# Function reference

Every project-defined function is described below, including private media helpers, inline accessors, test helpers and special members. Native API groups are in EXTERNAL_APIS.md. Header-only value types use standard member-wise ownership/copy behavior; Image and Tensor own their byte/float vectors. Copying an Image copies its pixels, unlike reference-counted shared image arrays.

## Model.cpp

| Function | Contract and behavior |
|---|---|
| `Runtime::check(status)` | Consume a nullable ORT status; release it and throw its message on failure. |
| `Runtime::Runtime / ~Runtime` | Load the application-local neural DLL, obtain the versioned C API and create the shared environment; release and unload on destruction or initialization failure. |
| `runtime()` | Return the thread-safe function-local Runtime singleton; initialization errors propagate. |
| `Model::Impl::Impl / ~Impl` | Bind the shared runtime and release owned session/memory handles, including partial initialization. |
| `Model::Model(path,directml)` | Convert UTF-8 model path to UTF-16, create a CPU or adapter-0 DirectML session, cache input/output names and allocate a CPU memory descriptor. Errors throw and release intermediate handles. |
| `Model::~Model` | Destroy implementation/session through unique ownership. |
| `Model::forward(tensor)` | Validate positive bounded shape and matching float storage, execute the model, return deep-copied float Tensor outputs. Output rank/type/size errors throw; native values/metadata are released on every path. |
| `Model copy constructor / assignment` | Deleted; session ownership cannot be copied. |

## Image.cpp

| Function | Contract and behavior |
|---|---|
| `byte(x)` | Clamp and round a pixel value to unsigned eight-bit storage. |
| `intersection(a,b)` | Return the clipped intersection rectangle; non-overlap has zero area. |
| `distance(a,b)` | Return Euclidean distance between two floating-point points. |
| `Image::Image(w,h,gray)` | Validate dimensions/maximum pixel count, allocate four-byte-aligned BGR rows and fill storage. Invalid sizes throw. |
| `samplePixel(image,x,y,out)` | Bilinearly sample BGR into three doubles; out-of-bounds or nonfinite coordinates return black. |
| `resizeImage(image,width,height)` | Return bilinearly resized owned pixels. Matching dimensions skip interpolation and return an owned value copy. Empty input/invalid output size throws. |
| `cropImage(image,roi)` | Copy an in-bounds rectangle into an owned image; invalid/out-of-bounds crops throw. |
| `grayAt(image,x,y)` | Compute rounded BGR luminance at an internally validated coordinate. |
| `grayscale(image)` | Return three-channel grayscale, preserving dimensions. Empty input returns empty. |
| `contrastImage(image)` | Apply native 8x8 tile clipped-histogram equalization and interpolate tile mappings. Only model input uses enhanced pixels; raw quality is separate. |
| `dimImage(image,scale,offset)` | Return a clamped affine brightness transform for stress tests. |
| `rotateImage(image,degrees)` | Return same-sized bilinearly rotated pixels, black outside the source. Used for in-plane stress tests, not physical 3D pose evidence. |
| `blurImage(image,radius)` | Compute a native box blur from integral sums. Negative/oversized radius throws. |
| `fromY16(input,width,height,strideBytes,black,white)` | Map unsigned 16-bit pixels to fixed-range grayscale BGR; validate pointer, stride and levels. The caller must supply a sufficiently large buffer. |
| `fillRect(image,rect,b,g,r)` | Clip and fill a BGR rectangle in place. |
| `drawRect(image,rect,b,g,r)` | Draw clipped two-pixel rectangle edges. |
| `drawPoint(image,point,b,g,r)` | Draw a small clipped point; ignore nonfinite/out-of-image coordinates. |
| `paste(destination,source,x,y)` | Copy source pixels into a fully in-bounds destination region; invalid placement throws. |
| `quality(image,roi)` | Return raw grayscale mean, standard deviation, Laplacian variance, clipped fraction and dark fraction for a clipped interior region. Empty interior returns zero statistics. |
| `Image::empty / row const and mutable (Image.hpp)` | Test owned storage and expose a row pointer. Internal callers must keep row indices in bounds. |
| `Point constructors / Rect constructors / Rect::area (Image.hpp)` | Initialize geometric value types and return rectangle area. Internal image limits keep valid rectangle products bounded. |

## WindowsMedia.cpp

| Function | Contract and behavior |
|---|---|
| `checkHR(hr,operation)` | Throw a runtime error containing operation and HRESULT when a COM/media call fails. |
| `wide(value)` | Strictly convert UTF-8 text/path to UTF-16; invalid UTF-8 throws. |
| `Platform::Platform / ~Platform` | Initialize COM MTA and Media Foundation on the calling thread; shut down on scope exit. If MF initialization fails, undo COM initialization. |
| `factory()` | Create a WIC imaging factory under COM; return a reference-counted handle. |
| `decodeStream(stream)` | Read the first WIC image frame, validate size through Image allocation, convert to BGR24 and copy owned pixels. |
| `loadImage(path)` | Open a Unicode filename through WIC and return decoded image pixels. |
| `decodeImage(bytes)` | Decode nonempty bounded encoded bytes using a WIC memory stream. |
| `encodeJPEG(image)` | Encode owned BGR as quality-0.9 JPEG in an auto-freed COM memory stream; reject empty input, format changes or incomplete reads. |
| `saveImage(path,image)` | Write JPEG-encoded bytes to the selected path and report file failures. This helper always writes JPEG; callers use .jpg names. |
| `text(image,x,y,value,rgb,size)` | Render UTF-16 text with GDI on a temporary top-down BGR DIB, copy pixels back and release GDI objects. Empty image is ignored; allocation failure throws. |
| `read32(stream)` | Read a little-endian 32-bit AVI field; truncated input throws. |
| `put32(stream,value) / put16(stream,value)` | Write little-endian integer fields; enclosing writer checks file state. |
| `four(name)` | Convert an internal four-character constant to a little-endian chunk/subtype identifier. |
| `VideoWriter::Impl::id / position / patch` | Write a chunk identifier, return current output offset, or patch an earlier size/count while preserving append position. |
| `VideoWriter::Impl::Impl(path,w,h,rate)` | Create a single-stream MJPEG AVI header with placeholder counts and sizes; invalid dimensions/rate or file failure throws. |
| `VideoWriter::VideoWriter / ~VideoWriter` | Own the AVI muxer and attempt finalization during destruction. Explicit close is used where errors must propagate; destructor suppresses exceptions. |
| `VideoWriter::write(image)` | Encode one JPEG frame, append padded AVI chunk and index entry, enforce dimensions/state and a 2 GB safety limit. File failures throw. |
| `VideoWriter::close()` | Append index, patch header/list/root sizes and frame counts, flush and close once. Explicit close reports write failures. |
| `VideoSource::Impl::scan(end,movi,depth)` | Scan bounded RIFF/LIST chunks recursively, obtain frame rate and index 00dc JPEG frames without loading all compressed frames. Reject excessive nesting, oversized/truncated chunks. |
| `VideoSource::Impl::~Impl` | Shut down the activated media source before COM handles release. |
| `VideoSource::VideoSource(source,width,height,fps,rawIR,fourcc,black,white)` | Open native JPEG AVI input or create Media Foundation camera/file reader. Camera mode selection minimizes size/rate distance; raw mode filters exposed subtype. Negotiate RGB32 for normal decoding, or full-range YUY2 for MJPEG to preserve brightness. Unsupported/missing sources or formats throw. |
| `VideoSource::~VideoSource` | Destroy reader/source/index state and close the owned input stream. |
| `VideoSource::read(image)` | Decode next indexed JPEG or synchronous MF sample into BGR. Respect stride and buffer length; normalize Y16/Y8. Return false on EOF. Type change, decoder/read errors or unsupported layouts throw. |
| `VideoSource::fps / camera` | Return selected/reported source rate and whether the source is a camera index. |
| `windowProc(hwnd,message,w,l)` | Handle close and key events; otherwise call DefWindowProc. Shared preview state belongs to the main thread. |
| `preview(image)` | Create/display a native preview, pump Windows messages and return the latest key. Draw scaled BGR with StretchDIBits. Window creation failure throws. |
| `closePreview()` | Destroy the preview window if present. |

## Vision.cpp

| Function | Contract and behavior |
|---|---|
| `cameraPixels(raw,settings)` | Return grayscale BGR in infrared mode, otherwise an owned image value copy. |
| `eyeRatio(points,indices)` | Compute six-point eye aspect ratio. Missing mesh or tiny width returns zero; fixed internal indices must be valid. |
| `overlap(a,b)` | Return guarded rectangle intersection-over-union for suppression and continuity. |
| `eyeObservable(image,points,indices)` | Gate one raw eye patch by size/bounds/brightness/contrast/detail and a dark-lens heuristic. True is not a guarantee that every occlusion was recognized. |
| `project(xyz,pose,width,height)` | Project a canonical 3D vertex with Euler rotation/translation and approximate intrinsics; behind-camera depth returns a large residual sentinel. |
| `solve6(matrix,output)` | Solve a six-variable augmented linear system using pivoted Gaussian elimination; return false for a singular system. |
| `poseCost(image,pose,width,height)` | Return squared projection residual sum for the six canonical observations. |
| `estimatePose(landmarks,width,height,face,pitch,yaw,roll)` | Fit canonical geometry with damped numerical-Jacobian least squares; write Euler angles and return validity gated by finite values, reprojection error, depth and angles. Not a calibrated camera-angle measurement. |
| `poseFixture(pitch,yaw,roll,width,height)` | Generate exact synthetic landmark observations from known pose for numerical regression tests only. |
| `inputTensor(image,size,scale,offset)` | Build owned RGB NCHW float data from a correctly sized BGR crop using model-specific normalization. |
| `Vision::Vision(settings)` | Load both neural sessions and generate 896 detector anchors; model/provider failures throw. |
| `Vision::analyze(image,timestamp)` | Detect/suppress boxes, require one supported face/ROI, gate raw detail, rotate/crop/normalize, infer mesh, calculate EAR/MAR/pose and visibility. Return Features with explicit unknown reason for unusable input; unexpected tensors/ROI errors throw. Update coarse box/time subject continuity. |
| `analyze sort / toSource / inside lambdas` | Sort confidence descending; map crop coordinates back to source; check landmark image bounds. All are local deterministic helpers. |

## Temporal.cpp

| Function | Contract and behavior |
|---|---|
| `validateSettings(settings)` | Reject nonfinite or out-of-range thresholds, dimensions/FPS, unsupported eyewear and invalid FOURCC length. |
| `Temporal::Temporal(settings)` | Copy validated settings and initialize state. |
| `Temporal::reset()` | Clear history, calibration, event timestamps and alert latch; restore the configured EAR threshold. |
| `Temporal::update(feature)` | Require monotonic source time, integrate only close valid observations, update calibration/closure/coverage/closure fraction/yawn/nod events and return Decision. Subject change resets history; unknown view never counts as closed and cannot clear a prior alert. |

## Application.cpp

| Function | Contract and behavior |
|---|---|
| `Worker::Worker / ~Worker` | Construct Vision and start one inference thread; destruction requests stop, wakes it and joins before releasing models. |
| `Worker::submit(image,time,index)` | Copy newest pixels under mutex, replace pending work and increment overwritten-frame counter; notify worker. |
| `Worker::take(result)` | Consume newest completed result if available; propagate background errors as exceptions. |
| `Worker::loop()` | Wait for pending work/stop, infer outside the lock and publish source-coupled output/timing. Capture std::exception for main-thread propagation. |
| `Worker::dropped() const (Sleep.hpp)` | Return atomic overwritten-pending-frame count. |
| `Worker condition-variable predicate` | Wake only for stop or a nonempty pending image. |
| `jsonString(text)` | Quote JSON text and escape quotation, backslash and controls. Other bytes are assumed valid UTF-8. |
| `overlay(image,feature,decision,fps)` | Draw source-face geometry/status in place; empty/tiny images are handled. |
| `dashboard(current,analysis,decision,fps,age)` | Return the 1280x640 current/source side-by-side dashboard with timing and evidence metrics. |
| `run(settings)` | Open capture, manage async or offline inference, reject stale measurements, update temporal state, present/log/record and optionally sound warnings. Rotate recordings each minute. Return 0 normal or 2 camera EOF/loss/empty source; exceptions become exit 1 in main. Writes selected output files. |

## Tests.cpp

| Function | Contract and behavior |
|---|---|
| `require(condition,message)` | Throw when a test invariant fails. |
| `measured(time,ear)` | Create explicitly scripted Features for temporal tests; no camera inference. |
| `advance(temporal,clock,seconds,ear,visible,mar,pitch,rate)` | Advance test source time and update a sequence of scripted measurements; return final decision. |
| `tests(root)` | Run 45 meaningful temporal/native codec/image/geometry/model assertions and 60 CPU inference timing iterations; save fixture JPEG and self-test report. Passing checks are not accuracy measurements. |
| `verifyVideo(path,expected,width,height)` | Decode every frame with native VideoSource; require count, dimensions and 30 FPS. |
| `ellipse(image,centre,radius,b,g,r)` | Draw sampled ellipse points for the clearly labeled scripted feature animation. |
| `demo(root,directml)` | Generate native photo-transform input/inference AVIs plus scripted temporal video/JSONL; close, reopen and verify full decoding; write demo verification report. |

## main.cpp / cmake/compiler_probe.cpp

| Function | Contract and behavior |
|---|---|
| `application main(argc,argv)` | Create COM/MF platform scope, resolve CMake-defined external data root, parse CLI, validate and dispatch run/tests/demo. Catch exceptions, print diagnostic and return 1. |
| `compiler probe main()` | Compile only for _M_X64 and eight-byte pointers; print actual MSVC version and target result. |

## Scripts and configuration functions

| Entry | Contract |
|---|---|
| `tools/setup.ps1 [-RestoreModels]` | Verify model/runtime size and SHA-256; optionally restore missing URL-listed assets through verified temporary downloads. Missing/corrupt files throw. |
| `tools/validate.ps1 [-DirectML] [-UseExistingEvidence]` | Build/test both toolsets, audit compiler/target PE machines and imports, verify cleanup, videos, scripted alarms and paced playback; write consolidated evidence. |
| `Invoke-Checked(program,args)` | Invoke a native tool and throw for nonzero exit. |
| `Assert-True(condition,message)` | Throw if evidence assertion fails. |
| `Get-PeMachine(path)` | Read a PE signature/machine from a read-only stream; validate signature and dispose stream on all paths. |
| `tools/build_cmake.cmd [v140/v142] [external_directory]` | Enter x86-hosted target-x64 MSVC environment, configure external Ninja build, compile and run CTest; propagate failure. |
| `build_vs2015.cmd / build_vs2019.cmd` | Delegate to CMake build wrapper with the selected compiler. |
| `run_ir_camera.cmd` | Prompt camera/FPS/eyewear, select external binary, enable IR/sound and record to a fresh session directory. |
| `review_demos.cmd` | Open both generated review AVIs with the configured system player. |
| `CMakeLists.txt / cmake/windows-x64.cmake` | Reject in-source builds, require Windows MSVC x64, define native runtime/media linkage, copy only neural runtime DLLs and configure tests/compiler evidence. |

Implicit default constructors/assignments/destructors for Settings, Features, Decision, Analysis, Quality, Point, Rect, Tensor, FaceCandidate and internal Segment follow their members. Deleted copy members of Model and Platform prevent resource duplication; Worker is noncopyable through its mutex/thread members. Local integral-image and pose lambdas are pure indexing/mapping helpers.

## Source-only deployment scripts

- `tools/setup.ps1 -DataRoot PATH [-AssetArchive ZIP] [-RestoreModels]`: resolve and require an external data root, extract only manifest-listed members when missing, verify sizes/SHA-256 and create external writable result folders; unsafe paths/corrupt assets throw.
- `tools/package.ps1 [-Destination PATH] [-DataRoot PATH]`: verify external assets and create separate source/assets archives plus SHA256.json outside the source tree. Existing archive names cause an error. Archive handles are disposed on every path; recordings/reports/caches are excluded from the asset archive.
- `main`: installed applications detect sibling data/models first, then use the configured external SLEEP_DATA_ROOT.
- CMake install rules copy app/DLLs, required models/fixture and license files to external bin/data/licenses and create writable result directories.
