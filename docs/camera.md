# Camera engine (`src/camera`)

**What this is for.** The authentication pipeline works on one picture at a time. A picture taken from a video is called a *frame*. This engine hides where the frames come from behind one very small interface, `FrameSource`, which has a single method: "give me the next frame". Three sources exist. A real camera and a video file are both read through Windows Media Foundation (the camera and video part of Windows) by the same class, so a video file exercises exactly the same conversion and orientation code as a live camera. The third source plays a folder of numbered picture files as if it were a recording. The engine also contains the opposite tool, `VideoWriter`, which writes pictures into an H.264 `.mp4` video file; it is used to create the generated test video and the annotated result video of `check_video`. The camera picture can have **any size** (the default is 1920 x 1080): when a camera is opened the engine asks the camera which picture formats it offers and switches on the one that suits the wanted size best (see "Camera picture sizes" below). Nothing in the automatic tests opens a real camera: the only program that does is `tools/camera_check.cpp`, and only when it is started with `--modes` or `--capture`. The rule that chooses the camera format, `pick_camera_mode`, needs no camera: `tests/test_common.cpp` checks it with made-up lists of formats (18 checks). The folder source is exercised by `tests/test_common.cpp` (three small pictures in `build/seq`) and played by `tools/check_video.cpp`; the video writer and the video-file source are exercised together by `tests/test_common.cpp` (it writes a 20-frame MP4 and reads it back: size, orientation, order and time stamps), by `tools/gen_video.cpp` (it writes an MP4, reads the first frame back and compares it with the original) and by `tools/check_video.cpp`. `tests/test_pipeline.cpp` does not use a frame source: it draws its frames in memory with `render_frame` (see `testdata.md`) and hands them directly to the pipeline. **Everything here runs on the CPU:** the Media Foundation objects of this engine (the camera reader, the video-file reader and the sink writer) are created with hardware transforms switched off, and the two readers also with DXVA (video decoding through the graphics card) switched off, so decoding and encoding are done by software; one thing still shows on a GPU: while `VideoWriter` encodes, the process has an idle Direct3D device (the `3d` engine at 0.0% utilization, no video engine), which was measured with the Windows GPU counters and is explained in `RESULTS.md` ("All engines on the CPU"). The functions concerned are `open_camera_reader`, `CameraSource::open_file` and `VideoWriter::open` below.

The source files are `src/camera/frame_source.h` and `src/camera/frame_source.cpp` (reading frames: the interface and the Windows-specific work) and `src/camera/video_writer.h` and `src/camera/video_writer.cpp` (writing a video file). The terms used in them:

| Term | Meaning |
|---|---|
| BGR | Colour order of every `Image` in this project: blue byte, green byte, red byte for each pixel (3 bytes per pixel). |
| RGB32 / BGRX | A Windows picture format with 4 bytes per pixel: blue, green, red and one unused byte. |
| Stride | Number of bytes from the start of one picture row to the start of the next row. |
| Bottom-up / top-down | Windows bitmaps are often stored with the bottom row of the picture first ("bottom-up"). Media Foundation announces this with a negative stride. "Top-down" means the top row comes first, as in every `Image` of this project. |
| COM | The old Windows technology Media Foundation is built on. A COM object is used through a pointer, and when you are finished you must call `Release()` on that pointer, otherwise memory is lost. Most COM calls return an `HRESULT` number; the macros `SUCCEEDED(hr)` and `FAILED(hr)` test it. |
| Source reader | A Media Foundation helper object that reads (and converts) the pictures of one camera or one video file, one by one. |
| Camera mode | One picture format that a camera offers: a size, a frame rate and a pixel format, for example 1280x720 at 30 frames per second as MJPG. A camera offers a dozen or more. Media Foundation calls it a *native media type*; the project's own type for it is `CameraMode`. |
| MJPG, YUY2, NV12 | Pixel formats that cameras send. MJPG is a JPEG picture per frame, YUY2 and NV12 are uncompressed YUV formats. The source reader converts all of them to RGB32, so the rest of the project never sees them. |
| H264, HEVC | Compressed video formats that need a video decoder. `pick_camera_mode` avoids them when the camera offers anything else. |
| Sink writer | The Media Foundation helper object that does the opposite: it takes pictures one by one and writes them into a video file. |
| Hardware transform | A Media Foundation processing step (a decoder, an encoder or a colour converter / video processor) that runs on special hardware, usually the GPU, instead of in software on the CPU. `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` tells a source reader or a sink writer not to use any. |
| DXVA | DirectX Video Acceleration: decoding video on the graphics card. `MF_SOURCE_READER_DISABLE_DXVA = TRUE` tells a source reader to decode in software. |
| H.264 / MP4 | H.264 is a video compression method; MP4 is the kind of file that holds such a video. Windows brings an H.264 encoder and decoder, so the project needs no extra library. |
| 100 ns unit | Media Foundation counts time in steps of 100 nanoseconds; 10,000 of them make 1 millisecond. |

## Camera picture sizes

**Any picture size works.** Nothing in the system is written for one fixed size. The default is **1920 x 1080** (`FA_CAMERA_DEFAULT_WIDTH` and `FA_CAMERA_DEFAULT_HEIGHT` in `config.h`, see `common.md`); it is the size that `open_camera_source` asks for when the caller names none. The generated test videos default to the same size, 1920 x 1080 (`gen_video` uses `FA_CAMERA_DEFAULT_WIDTH` and `FA_CAMERA_DEFAULT_HEIGHT`), and can be made at any size with `gen_video --size WxH` (see `tools.md`).

**What the code does when it opens a camera** (`open_camera_source(index, width, height)`):

1. The camera is opened behind a source reader and asked for all the picture formats it offers (`collect_native_modes`). Each one is a size, a frame rate and a pixel format.
2. `pick_camera_mode` chooses the best format for the wanted size. The rule is in the section of that function. In short: avoid H264 and HEVC, avoid formats slower than `FA_CAMERA_MIN_FPS` (15 frames per second) when faster ones exist, then take the format whose size is closest to the wanted one (the relative difference counts, so a picture of the wrong shape loses).
3. The camera itself is switched to that format (`SetCurrentMediaType` with the camera's own media type). The reader is then asked for RGB32 pixels at that size and converts whatever pixel format the camera sends. The camera delivers the size; this code does not scale the pictures. A reader that is asked for a size the camera does not have fails, which is why the camera is switched first.
4. If the camera lists no usable format, or the chosen one is refused, the wanted size is requested directly; if that is refused too, the camera's own default size is used.

The real size is known before the first frame (`FrameSource::frame_size`) and is the size of every frame that `next` returns. The caller can also ask which format was switched on (the `chosen` parameter of `open_camera_source`; all zero means the fallback). **What a camera offers** is shown by `camera_check --modes` (`tools.md`, `TESTING.md`). Example, measured on the development PC: the "Integrated Camera" offers 16 formats, the largest is 1280x720 (NV12 at 30 fps and YUY2 at 10 fps). For the default request of 1920x1080 `pick_camera_mode` chooses 1280x720 NV12 at 30 fps (the 10 fps format is too slow) and the frames are 1280 x 720.

**What the rest of the system does with a different size.**

* **Deep detectors** (YuNet, SCRFD) shrink the picture with `letterbox` into the 640 x 640 network square, whatever its size, and map the faces back to camera pixels. (SCRFD in `dev_noncommercial.ini` uses `letterbox_fit` instead: the smallest rectangle with sides that are multiples of 32, 640 x 384 for 1920 x 1080, 640 x 544 for 1280 x 1080, 640 x 480 for 4:3, still 640 x 640 for a square picture. The shrink factor below is the same, so the faces have the same size in the network input and what is found hardly changes (`detect.md`), and the network does 40% less work for 16:9.) The shrink factor is 640 / (longer side), so a face of width `w` becomes `w * 640 / longer side` pixels in the network input, and the smallest face that is found depends on the detector. Measured with `eval_detect --frame WxH` (share of faces found): at 1920x1080 YuNet finds 100% of the faces from 50 px (17 px in the network input) and SCRFD 90% at 50 px (square; 85% with the fitted rectangle, one portrait of 20) and 100% from 80 px; at 3840x2160 YuNet finds 15% at 50 px and 100% from 80 px (13 px in the network input), SCRFD 0% at 50 px, 40% at 80 px and 100% from 110 px. So at 4K the 80 pixel minimum crop width is still found by YuNet but not reliably by SCRFD, and a 4K camera should still be asked for 1920x1080 (the default). The full table is in `detect.md` ("Detection at different camera picture sizes").
* **The Haar detector** shrinks the gray picture to a working width that depends on the picture width (`haar_work_width` in `detector_haar.cpp`, see `detect.md`), so the smallest face it sees stays about 96 camera pixels up to 1920 wide; above that the working width is capped at 480 and the smallest face grows again (128 px at 2560 wide, 192 px at 3840 wide). Measured at 1920x1080 it finds 90% of the faces at 80 px and 100% from 160 px; at 3840x2160 it finds 0% up to 110 px and 90% at 160 px.
* **The tracker** works on the overlap (IoU) of boxes and has no pixel constants, so it does not depend on the picture size.
* **Face crops** are the face box plus 30 percent on every side (`make_face_crop`), so they are proportional to the face, not to the picture.
* **The 80 pixel minimum.** A face must be at least `FA_MIN_CROP_FACE_PIXELS` = 80 camera pixels wide before a crop is stored for recognition. This number is fixed in pixels, so a smaller picture needs people closer to the camera: if the camera shows the same view, a face that is 80 pixels wide in a 1920 wide picture is about 53 pixels in a 1280 wide one and 27 in a 640 wide one (calculated, proportional to the width).
* **Time per frame** grows with the picture (the conversion, the shrinking, the detector); measured detection times at 1920x1080 and 1280x720 are printed by `bench` and `bench_detect`.

`test_pipeline` (video 5) plays the same walk at five sizes (640x480 to 2560x1440, with 1920x1080 in the middle) and `run_tests.bat` generates and checks a 1920x1080 and a 1280x720 video (`tools.md`).

## src/camera/frame_source.h

This header defines the interface and one struct, and declares six public functions. The six free functions are only *declared* here; they are defined (and therefore documented in detail) in the `frame_source.cpp` section below.

| Function | Purpose |
|---|---|
| `FrameSource` (class) | The interface every frame source implements. |
| `FrameSource::~FrameSource` | Virtual destructor so a source can be deleted through a base pointer. |
| `FrameSource::next` | Pure virtual: deliver the next frame and its time stamp. |
| `FrameSource::frame_size` | Virtual: the size of the frames, when it is known before the first frame (default: not known). |
| `CameraMode` (struct) | One picture format that a camera offers: size, frame rate, pixel format. |
| `CameraMode::CameraMode` | The two constructors: default (all zero) and the four-argument one. |
| `list_cameras` | Names of the cameras Windows reports (defined in `frame_source.cpp`). |
| `list_camera_modes` | The picture formats one camera offers (defined in `frame_source.cpp`). |
| `pick_camera_mode` | Which of a list of formats suits a wanted size best (defined in `frame_source.cpp`). |
| `open_camera_source` | Open one camera as a `FrameSource` (defined in `frame_source.cpp`). |
| `open_video_file_source` | Play a video file as a `FrameSource` (defined in `frame_source.cpp`). |
| `open_sequence_source` | Play a folder of pictures as a `FrameSource` (defined in `frame_source.cpp`). |

### `FrameSource` (class)

```cpp
class FrameSource {
public:
    virtual ~FrameSource() {}
    virtual bool next(Image& frame, double& time_ms) = 0;
    virtual bool frame_size(int& width, int& height) const { (void)width; (void)height; return false; }
};
```

**What it does.** An abstract base class (an "interface"): it declares what every frame source can do but contains no camera code itself. Code that consumes frames only needs a `FrameSource` pointer and does not care whether a camera, a video file or a folder of pictures is behind it. The `open_*` functions return `std::unique_ptr<FrameSource>`, a pointer that deletes the source automatically when it goes out of scope.

**How it works.** C++ virtual functions: the call `source->next(...)` runs the version of `next` that belongs to the real object (`CameraSource` or `SequenceSource`). The class has no data members.

**Used by.** Implemented by `CameraSource` (camera and video file) and `SequenceSource` (both in `frame_source.cpp`). Held through `std::unique_ptr<FrameSource>` in `tools/camera_check.cpp` (`main`), `tools/check_video.cpp` (`main`), `tools/gen_video.cpp` (`main`) and `tests/test_common.cpp` (`main`, the folder source). No other file in `src/`, `tools/` or `tests/` refers to the type.

### `FrameSource::frame_size`

```cpp
virtual bool frame_size(int& width, int& height) const { (void)width; (void)height; return false; }
```

**What it does.** Tells the size of the frames this source delivers, **before** the first frame has been read. Returns `true` and sets `width` and `height` when the source knows the size; returns `false` (and leaves both parameters untouched) when it does not. A camera and a video file know it as soon as they are open. A folder of pictures does not: every picture file may have its own size.

**How it works.** A virtual function with a default body that says "not known" (not pure virtual, so `SequenceSource` does not have to override it). The two `(void)` casts only stop the compiler from warning about unused parameters. `CameraSource::frame_size` overrides it.

**Used by.** `tools/camera_check.cpp` (`main`, with `--capture`: it prints the real picture size before it reads the first frame). No other caller.

### `CameraMode` (struct)

```cpp
struct CameraMode {
    int width = 0, height = 0;
    double fps = 0;          // frames per second the camera promises in this mode (0 = not reported)
    std::string format;      // "MJPG", "YUY2", "NV12", "RGB32" ... ("?" when the camera does not say)
    CameraMode() {}
    CameraMode(int width_, int height_, double fps_, const std::string& format_);
};
```

**What it does.** One picture format that a camera offers: the picture size in pixels, the frame rate the camera promises in that format (0 when the camera does not say) and the pixel format as four letters (`MJPG`, `YUY2`, `NV12`, `H264` ...), or `RGB32`, `RGB24`, `ARGB32`, or `?` when the format has no readable name. A camera offers many of them, for example 640x480, 1280x720 and 1920x1080, each as MJPG and as YUY2. `list_camera_modes` returns a list of them, `pick_camera_mode` chooses one, and `open_camera_source` can report the one it switched on.

**How it works.** A plain struct with default member values (0, 0, 0 and an empty text). `format_name` makes the format text from the media type's subtype GUID.

**Used by.** `list_camera_modes`, `pick_camera_mode`, `open_camera_source` (the `chosen` parameter), `CameraSource::open`, `NativeMode` and `collect_native_modes` in `frame_source.cpp`; `tools/camera_check.cpp` (`main`) and `tests/test_common.cpp` (`main`, the made-up lists for the checks "camera mode: ...").

### `CameraMode::CameraMode` (the two constructors)

```cpp
CameraMode() {}
CameraMode(int width_, int height_, double fps_, const std::string& format_);
```

**What it does.** The default constructor makes the all-zero mode (width 0, height 0, frame rate 0, empty format); this is what `open_camera_source` returns in `chosen` when the camera had to be used with its default size. The four-argument constructor makes a mode from its four values, for example `CameraMode(1920, 1080, 30, "MJPG")`.

**How it works.** The four-argument constructor copies its arguments with a member initialiser list. Both constructors are written out by hand **for Visual Studio 2015**: that compiler does not accept brace initialisation (`CameraMode{1920, 1080, 30, "MJPG"}`) of a struct whose members have default values, so the code uses `CameraMode(1920, 1080, 30, "MJPG")` everywhere (the same reason as for `Rect::Rect` and `Point::Point` in `common.md`).

**Used by.** `collect_native_modes` (the four-argument one), `CameraSource::open` (the default one, to clear `chosen`), `tools/camera_check.cpp` (`main`, a default `CameraMode chosen`) and `tests/test_common.cpp` (`main`, the four-argument one for every made-up mode).

### `FrameSource::~FrameSource`

```cpp
virtual ~FrameSource() {}
```

**What it does.** Does nothing by itself, but being `virtual` guarantees that deleting a source through a `FrameSource` pointer runs the destructor of the real class (so a camera is shut down properly).

**How it works.** Standard C++ rule: a base class that is deleted through a base pointer needs a virtual destructor, otherwise the derived part would not be cleaned up.

**Used by.** Called implicitly when a `unique_ptr<FrameSource>` in `camera_check.cpp`, `check_video.cpp`, `gen_video.cpp` or `test_common.cpp` goes out of scope; for a camera or video-file source this runs `CameraSource::~CameraSource`.

### `FrameSource::next`

```cpp
virtual bool next(Image& frame, double& time_ms) = 0;
```

**What it does.** Gets the next frame. `frame` receives a 3-channel BGR image (its old contents are replaced). `time_ms` receives the capture time in milliseconds. The function returns `true` on success and `false` at the end of a recording or video file or when the camera fails; after a `false` the contents of `frame` must not be used. The time is measured from the first frame: the first frame of a camera has time 0 (the header says so), a frame of a video file has its time in the file counted from the first frame, and for the folder source it is `frame number * 1000 / fps`.

**How it works.** It is a pure virtual function (`= 0`), so each implementation provides its own version: `CameraSource::next` and `SequenceSource::next` below.

**Used by.** Called in `tools/camera_check.cpp` (`main`, the `--capture` loop), `tools/check_video.cpp` (`main`, the play loop), `tools/gen_video.cpp` (`main`, one call to read the first frame of the MP4 back) and `tests/test_common.cpp` (`main`, the folder source). The pipeline's `AuthPipeline::process` does not call it: it receives a ready frame as a parameter, so the caller decides where frames come from.

## src/camera/frame_source.cpp

Everything in the anonymous namespace (`release`, `format_name`, `NativeMode`, `collect_native_modes`, `open_camera_reader`, `CameraSource`, `SequenceSource`) is private to this file; other code can only reach it through the public functions at the end (`list_cameras`, `list_camera_modes`, `pick_camera_mode`, `open_camera_source`, `open_video_file_source`, `open_sequence_source`). All code is in namespace `fa`. The file also calls many Windows functions (`MFStartup`, `GetNativeMediaType`, `SetCurrentMediaType`, `MFGetAttributeSize` and so on); these are Windows' own and are not documented here.

| Function | Purpose |
|---|---|
| `release` | Release a COM pointer and set it to `nullptr` (safe to call with `nullptr`). |
| `format_name` | The pixel format of a media type as text (`MJPG`, `YUY2`, `NV12` ...). |
| `NativeMode` (struct) | One camera mode (`CameraMode`) together with the media type that switches it on. |
| `collect_native_modes` | Ask a source reader for every media type its camera offers. |
| `open_camera_reader` | Open camera number `index` and make a source reader for it. |
| `CameraSource` (class) | `FrameSource` that reads a camera **or a video file** through the Media Foundation source reader. |
| `CameraSource::~CameraSource` | Release the reader and shut Media Foundation and COM down. |
| `CameraSource::open_file` | Open a video file and ask for RGB32 pictures. |
| `CameraSource::open` | Open the camera, switch on the camera mode that suits the wanted size best, then call `finish_setup`. |
| `CameraSource::frame_size` | The real size of the frames (known once the camera or file is open). |
| `CameraSource::finish_setup` | Steps shared by camera and file: ask for RGB32, read back size and stride. |
| `CameraSource::next` | Read one sample, convert it to a BGR `Image`, compute the time stamp. |
| `CameraSource::set_output_type` | Ask the reader for RGB32 pictures, optionally with a given size. |
| `CameraSource::copy_bgrx_to_bgr` | Copy 4-byte pixels into a 3-byte BGR image, flipping rows if the picture is bottom-up. |
| `SequenceSource` (class) | `FrameSource` that plays picture files from a folder. |
| `SequenceSource::open` | List and sort the picture files of a folder. |
| `SequenceSource::next` | Load the next picture file. |
| `list_cameras` | Names of all cameras Windows reports; opens none. |
| `list_camera_modes` | The picture formats of one camera; opens it but does not start its stream. |
| `pick_camera_mode` | The index of the format that suits a wanted size best (a pure function, no camera needed). |
| `open_camera_source` | Create a `CameraSource` and open it as a camera. |
| `open_video_file_source` | Create a `CameraSource` and open it as a video file. |
| `open_sequence_source` | Create a `SequenceSource` and open it. |

### `release`

```cpp
template <class T> void release(T*& p);
```

**What it does.** Calls `p->Release()` if `p` is not `nullptr` and then sets `p` to `nullptr`, so the pointer can never be released twice. It works for any COM interface type `T`.

**How it works.** A small template (one function that works for many pointer types). The pointer is passed by reference (`T*&`) so the function can reset the caller's variable.

**Used by.** `open_camera_reader`, `CameraSource::~CameraSource`, `CameraSource::open_file`, `CameraSource::open`, `CameraSource::finish_setup`, `CameraSource::next`, `CameraSource::set_output_type`, `list_cameras` and `list_camera_modes`. (`video_writer.cpp` has its own copy of this helper.)

### `format_name`

```cpp
std::string format_name(const GUID& subtype);
```

**What it does.** Turns the pixel format of a media type (its *subtype*, a 16-byte identifier called a GUID) into text. The three plain RGB formats get their names `RGB32`, `RGB24` and `ARGB32`. For every other format the text is the four letters that Windows hides in the first four bytes of the GUID: `MJPG`, `YUY2`, `NV12`, `H264` and so on. If one of those four bytes is not a printable character (below 32 or above 126) the result is `?`.

**How it works.** `IsEqualGUID` against the three RGB subtypes first. Otherwise `memcpy` copies the first four bytes of the GUID (`Data1`, a 32-bit number whose bytes are the format's four letters) into a 5-character array that ends with a 0, and each letter is tested for being printable.

**Used by.** `collect_native_modes` (for the `format` field of every `CameraMode`).

### `NativeMode` (struct)

```cpp
struct NativeMode {
    CameraMode info;
    IMFMediaType* type = nullptr;
};
```

**What it does.** One camera mode in two forms: `info`, the plain description (`CameraMode`: size, frame rate, format text) that `pick_camera_mode` works on, and `type`, the Media Foundation media type itself, which the source reader needs to switch that mode on. The media type is a COM object: **whoever receives a `NativeMode` must `release(type)`** (`CameraSource::open` and `list_camera_modes` do).

**How it works.** A plain struct. `info` and `type` always belong together, so the index that `pick_camera_mode` returns for the list of the `info` values is also the index of the right `type`.

**Used by.** `collect_native_modes`, `CameraSource::open`, `list_camera_modes`.

### `collect_native_modes`

```cpp
void collect_native_modes(IMFSourceReader* reader, std::vector<NativeMode>& modes);
```

**What it does.** Asks the source reader for every media type that the camera behind it offers for its first video stream, and appends one `NativeMode` for each to `modes`, in the order the camera lists them. For every mode it records the picture size, the frame rate and the pixel format text.

**How it works.** A loop over the index `i`: `GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type)` returns the next media type; when it fails or returns nothing (Media Foundation answers `MF_E_NO_MORE_TYPES` at the end of the list) the loop stops. The loop also stops after 1024 entries: a real camera lists a few dozen, the limit only protects against a faulty driver. For each type, `MFGetAttributeSize(MF_MT_FRAME_SIZE)` gives the size and the subtype GUID goes to `format_name`. The frame rate is `MF_MT_FRAME_RATE_RANGE_MAX` when the type has one (a camera may offer a range of rates in one type: the fastest of it is taken), otherwise `MF_MT_FRAME_RATE`, as numerator / denominator; a missing rate or a denominator of 0 gives frame rate 0 ("not reported"). The media type is **not** released here; it is stored in the `NativeMode`.

**Used by.** `CameraSource::open` and `list_camera_modes`.

### `open_camera_reader`

```cpp
IMFSourceReader* open_camera_reader(int index);
```

**What it does.** Opens camera number `index` and makes a source reader for it that converts any camera format (YUY2, MJPG ...) to plain RGB. Media Foundation must already be started by the caller. Returns the reader (the caller releases it), or `nullptr` on failure. If the index does not exist it prints `camera: device N not found (M cameras)` to `std::cerr` (M is 0 when the device list itself could not be read); other failures return `nullptr` silently.

**How it works.** It builds a search attribute list that asks only for video-capture devices (`MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID`), calls `MFEnumDeviceSources`, and checks that `index` is inside the list (on a bad index the device objects and the array are released first). `ActivateObject` turns the chosen device into an `IMFMediaSource`, the object that produces the camera's pictures. A source reader is created from it with an attribute list of **three** entries (`MFCreateAttributes(&reader_attributes, 3)`, released after the reader exists): `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = TRUE` (with this switch on, Windows converts whatever format the camera sends into plain RGB, so this project does not need any colour conversion code of its own), `MF_SOURCE_READER_DISABLE_DXVA = TRUE` (a camera that sends compressed pictures is decoded in software, not through the graphics card) and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (no hardware decoder and no hardware video processor is put into the chain). The two last entries are there to keep the camera path on the CPU; the project asks for no Direct3D device manager either. No test opens a camera, so this path was not measured with the GPU counters (the same attributes are set for video files, which were: see `open_file`). The media source is released afterwards (the reader holds its own reference). No size is set here and no stream is started: that is why `list_camera_modes` can use it just to ask the camera.

**Used by.** `CameraSource::open` and `list_camera_modes`.

### `CameraSource` (class)

```cpp
class CameraSource : public FrameSource { /* open_file, open, frame_size, finish_setup, next, ... */ };
```

**What it does.** The Media Foundation implementation of `FrameSource`. One class handles both a live camera and a video file, because both are a stream of pictures that the source reader converts to plain RGB (whatever the camera or the file contains: YUY2, MJPG, H.264 ...). It owns the source reader and remembers the real picture size, whether rows are stored bottom-up, and the time stamp of the first frame. A video file therefore goes through exactly the same conversion and orientation code as a live camera.

**How it works.** Private data: `reader_` (the source reader, a COM pointer), `width_` and `height_` (the picture size really delivered, 0 until `open_file` or `open` succeeds; `frame_size` returns them), `bottom_up_` (true when the stride is negative) and `first_timestamp_` (time stamp of the first frame in 100 ns units, -1 until a frame has been read).

**Used by.** Created only inside `open_camera_source` and `open_video_file_source`.

### `CameraSource::~CameraSource`

```cpp
~CameraSource() override;
```

**What it does.** Cleans up: releases the source reader, calls `MFShutdown()` and `CoUninitialize()`.

**How it works.** It always calls `MFShutdown` and `CoUninitialize`, even when `open_file` or `open` failed early (for example because `MFStartup` itself failed); the return values of these calls are not checked. The media source object obtained in `open` is not shut down explicitly: it is only released (the reader holds its own reference to it).

**Used by.** Runs automatically when the `unique_ptr<FrameSource>` that holds the camera or file is destroyed, or immediately in `open_camera_source` / `open_video_file_source` when `open` / `open_file` returns `false`.

### `CameraSource::open_file`

```cpp
bool open_file(const std::string& path);
```

**What it does.** Opens a video file (`.mp4`, `.avi`, `.wmv` ... anything Windows can decode) so that `next` delivers its pictures. The picture size is the size stored in the file. Returns `true` when the file is ready and `false` otherwise; on failure it prints `video: cannot open <path> (HRESULT 0x...)` to `std::cerr`.

**How it works.**
1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` starts COM (the result is not checked) and `MFStartup(MF_VERSION)` starts Media Foundation; if `MFStartup` fails the function returns `false`.
2. An attribute list of **three** entries is created (`MFCreateAttributes(&reader_attributes, 3)`): `MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = TRUE` (with this switch on, Windows converts the pictures of the file to plain RGB), `MF_SOURCE_READER_DISABLE_DXVA = TRUE` (decoding, for example of H.264, is done in software, not through the graphics card) and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE` (no hardware decoder and no hardware video processor). The same three entries are set in `open_camera_reader`. Measured with the Windows GPU counters (`RESULTS.md`, "All engines on the CPU"): `eval_footage`, which plays an MP4 through this function, had no GPU engine instance, and neither had a throw-away program that only decoded an MP4. The list is released as soon as the reader has been created.
3. The path (UTF-8 text) is converted to a wide-character string with `MultiByteToWideChar(CP_UTF8, ...)`, and `MFCreateSourceReaderFromURL` creates the source reader from it. If that fails, the message above is printed and the function returns `false`.
4. `finish_setup(0, 0)` does the rest: "0, 0" means "no size requested", so the size of the file is kept.

**Used by.** Called only by `open_video_file_source`.

### `CameraSource::open`

```cpp
bool open(int index, int width, int height, CameraMode* chosen);
```

**What it does.** Opens camera number `index` (0 is the first camera in the list that Windows reports) and makes it deliver pictures of about `width` x `height` pixels: it asks the camera which picture formats it offers and switches on the one that suits best. Returns `true` when the camera is ready to deliver frames and `false` otherwise. `chosen` may be null; otherwise it receives the camera mode that was switched on, or stays all zero when the camera had to be used with its default size (it is set to all zero as soon as Media Foundation has started; when `MFStartup` fails it is not touched). The camera may not have the wanted size: the real size is read back and stored, and the frames returned by `next` have that real size (`frame_size` tells it before the first frame). If the index does not exist `open_camera_reader` prints `camera: device N not found (M cameras)` to `std::cerr`; other failures return `false` silently.

**How it works.**
1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` starts COM (the result is not checked) and `MFStartup(MF_VERSION)` starts Media Foundation; if `MFStartup` fails the function returns `false`. Then `*chosen` is set to the all-zero `CameraMode()` (when `chosen` is not null).
2. `open_camera_reader(index)` finds the camera and makes the source reader (device search, `ActivateObject`, the three reader attributes: video processing on, DXVA off, hardware transforms off). If it returns `nullptr` the function returns `false`.
3. `collect_native_modes` lists the camera's media types. The plain `CameraMode` descriptions are copied into a vector `offered`, and `pick_camera_mode(offered, width, height)` returns the index of the best one (or -1).
4. If a mode was picked, `SetCurrentMediaType` with that mode's own media type switches the **camera** to it. Every media type obtained in step 3 is released. A reader that is asked directly for a size the camera does not have fails; this is why the camera is switched first.
5. If the switch worked, `finish_setup(offered[pick].width, offered[pick].height)` asks for RGB32 pixels at that size and reads back the real size and stride (see below). When this succeeds, `*chosen` is set to `offered[pick]` and the function returns `true`.
6. Otherwise (the camera listed no usable mode, the switch was refused, or `finish_setup` failed) the last resort is `finish_setup(width, height)`: RGB32 at the size the caller asked for and, if that is refused too, at the camera's default size. `chosen` stays all zero then.

**Used by.** Called only by `open_camera_source`.

### `CameraSource::frame_size`

```cpp
bool frame_size(int& width, int& height) const override;
```

**What it does.** The `CameraSource` version of `FrameSource::frame_size`: gives the real picture size of the camera or video file that is open. It sets `width` and `height` to the stored `width_` and `height_` and returns `true` when both are greater than 0; before a successful `open` or `open_file` they are 0 and it returns `false` (the two output values are written in both cases).

**How it works.** Copies the two members and compares them with 0. The members were set by `finish_setup` from `MF_MT_FRAME_SIZE` as the reader really delivers it, so this is the size of every frame that `next` returns, not the size that was asked for.

**Used by.** Through the `FrameSource` pointer in `tools/camera_check.cpp` (`main`, with `--capture`).

### `CameraSource::finish_setup`

```cpp
bool finish_setup(int width, int height);
```

**What it does.** The steps that camera and video file have in common, after the source reader exists: ask the reader for RGB32 pictures, read back the size and the stride that were really agreed, and remember them. Returns `true` when both sizes are greater than 0. (In the source it stands in the public part of the class, but it is only a helper of `open_file` and `open`.)

**How it works.**
1. `set_output_type(width, height)` asks for RGB32 at the wanted size. `CameraSource::open` passes the size of the camera mode it has just switched on, so the camera really has that size; in its last-resort call it passes the size the caller asked for. If that is refused, `set_output_type(0, 0)` asks for RGB32 at the default size of the camera or file. If both are refused the function returns `false`.
2. `GetCurrentMediaType` reads back what was really agreed: `MF_MT_FRAME_SIZE` gives `width_` and `height_`, and `MF_MT_DEFAULT_STRIDE` gives the stride. The stride attribute is stored as an unsigned number and converted to a signed one, so a negative stride is recognised. `bottom_up_ = (stride < 0)`. If the stride attribute is missing, the stride stays 0 and the picture is treated as top-down.
3. The function succeeds only if both sizes are greater than 0.

**Stride sign and bottom-up pictures.** The stride is how many bytes lie between the start of two neighbouring rows. For RGB pictures Windows may announce a negative stride, which means "the first row in memory is the bottom row of the picture". The code uses only the *sign* of the stride (`bottom_up_`); it does not use its size. It assumes the rows are tightly packed (each row exactly `width * 4` bytes), which is also what the length check in `next` expects. Whether a given real camera or video file reports a negative or positive stride cannot be told from the code; both cases are handled by `copy_bgrx_to_bgr`. For video files the result is checked in practice: `tools/gen_video.cpp` writes an MP4, reads its first frame back with `open_video_file_source` and compares it with the original and with the original turned upside down, to be sure that pictures arrive the right way up. No real camera was tested here.

**Used by.** `CameraSource::open_file` and `CameraSource::open`.

### `CameraSource::next`

```cpp
bool next(Image& frame, double& time_ms) override;
```

**What it does.** Waits for the next camera picture (or reads the next picture of the video file), converts it to a 3-channel BGR `Image` and computes its time in milliseconds. Returns `false` if the camera fails, the stream or file ends, the picture buffer is too small, or 100 "empty" reads happen in a row.

**How it works.**
1. It tries up to 100 times. Each try calls `reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, ...)`. This call is *synchronous*: it blocks until a sample is available (for a file: until the next picture has been decoded).
2. If the call fails or the flag `MF_SOURCE_READERF_ENDOFSTREAM` is set (the end of the video file), the function returns `false`.
3. If no sample came back (a "stream tick" that carries no picture), the loop simply tries again.
4. `ConvertToContiguousBuffer` gives one block of memory; `Lock` makes it readable. The code accepts the buffer only if its length is at least `width_ * height_ * 4` bytes; then `copy_bgrx_to_bgr` fills `frame`. After every successful `Lock` the buffer is unlocked (`Unlock` is called also when the buffer is too short) and then released. If the length is too small the function returns `false`.
5. The very first time stamp is remembered in `first_timestamp_`. `time_ms = (timestamp - first_timestamp_) / 10000.0`, because Media Foundation counts in 100 ns units. The first frame therefore has time 0, and for a video file the time is the position in the file counted from its first frame.

Playing a video file is not slowed down: `next` returns as soon as the picture is decoded, so a caller that wants real-time playback must wait for `time_ms` itself (as `check_video` does with `--realtime`).

**Used by.** Called through the `FrameSource` pointer in `tools/camera_check.cpp` (`main`, with `--capture`), `tools/check_video.cpp` (`main`, video file) and `tools/gen_video.cpp` (`main`, first frame of the MP4).

### `CameraSource::set_output_type`

```cpp
bool set_output_type(int width, int height);
```

**What it does.** Tells the source reader which picture format the program wants to receive: video, subtype RGB32, and, when `width` and `height` are both greater than 0, that frame size. With `0, 0` no size is given, which means "keep the default size". Returns `true` when the reader accepts the format.

**How it works.** It creates an `IMFMediaType`, sets `MF_MT_MAJOR_TYPE = MFMediaType_Video` and `MF_MT_SUBTYPE = MFVideoFormat_RGB32`, optionally `MF_MT_FRAME_SIZE` through `MFSetAttributeSize`, and calls `SetCurrentMediaType` on the first video stream. The media type object is released before returning.

**Used by.** `CameraSource::finish_setup` (twice at most per call: wanted size, then default size).

### `CameraSource::copy_bgrx_to_bgr`

```cpp
void copy_bgrx_to_bgr(const BYTE* data, Image& frame) const;
```

**What it does.** Converts the 4-bytes-per-pixel memory block (blue, green, red, unused) into `frame`, a 3-channel BGR image of `width_` x `height_` pixels. The fourth byte of each pixel is dropped. When `bottom_up_` is true the rows are copied in reverse order, so the resulting `Image` always has its top row first, like every other image in the project.

**How it works.** `frame.create(width_, height_, 3)` sets the size and fills the image with zeros. Then for every output row `y` the source row is `bottom_up_ ? height_ - 1 - y : y`; its start address is `data + row * width_ * 4`. Each pixel copies byte 0, 1 and 2 and skips byte 3. It is a plain double loop; no SIMD is used.

**Used by.** `CameraSource::next`.

### `SequenceSource` (class)

```cpp
class SequenceSource : public FrameSource { /* open, next */ };
```

**What it does.** The "recording" implementation of `FrameSource`: it plays the picture files of one folder in order, at a fixed frame rate that you choose (it does not look at any time stored in the files).

**How it works.** Private data: `files_` (the sorted list of file paths), `index_` (number of the next file, starts at 0) and `fps_` (frames per second, default 30).

**Used by.** Created only inside `open_sequence_source`.

### `SequenceSource::open`

```cpp
bool open(const std::string& folder, double fps);
```

**What it does.** Collects the picture files of `folder` and prepares playback at `fps` frames per second. A value of `fps` that is zero or negative is replaced by 30. Returns `false` if the folder cannot be read or contains no picture files.

**How it works.** It walks the folder with `std::filesystem::directory_iterator` (using the error-code form, so a missing folder does not throw). For each entry the extension is converted to lower case and the file is kept when it is `.jpg`, `.jpeg`, `.png` or `.bmp`. The list is sorted with `std::sort` on the full path text, which means plain character order. Number the files with leading zeros (`frame_0009`, `frame_0010`), as `tools/gen_video.cpp` does (`frame_0000.jpg`, ...), otherwise `frame_10` would come before `frame_9`. Sub-folders are not searched.

**Used by.** Called only by `open_sequence_source`.

### `SequenceSource::next`

```cpp
bool next(Image& frame, double& time_ms) override;
```

**What it does.** Loads the next picture file into `frame` (as BGR) and sets `time_ms` to `index * 1000.0 / fps`, so frame 0 has time 0, frame 1 has `1000 / fps`, and so on. Returns `false` when all files have been played or when a file cannot be read.

**How it works.** If `index_` is past the last file it returns `false`. Otherwise it calls `load_image` (from `image_io.h`, Windows Imaging Component). Only when loading works are `time_ms` set and `index_` increased; a file that cannot be read therefore stops the playback and a repeated call tries the same file again.

**Used by.** Called through the `FrameSource` pointer in `tests/test_common.cpp` (`main`), which plays three pictures and then checks that a fourth call returns `false`, and in `tools/check_video.cpp` (`main`, when the input is a folder of frames).

### `list_cameras`

```cpp
std::vector<std::string> list_cameras();
```

**What it does.** Returns the friendly names of the cameras that Windows reports, in the order of their index (the first name is camera 0, the number you pass to `open_camera_source`). It does **not** open any camera. If Media Foundation cannot be started or the list cannot be read, the result is an empty vector. A camera without a readable name is listed as `(unnamed camera)`.

**How it works.** `CoInitializeEx`, `MFStartup`, then the same device search as in `open_camera_reader` (video-capture devices only; the code is a separate copy because here every device is only asked for its name, not activated). For each device it asks for the attribute `MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME`, which Windows returns as a wide-character (UTF-16) string; `WideCharToMultiByte(CP_UTF8, ...)` converts it to a normal UTF-8 `std::string`, and the Windows-allocated string is freed with `CoTaskMemFree`. Each device object is released, then the device array is freed. `MFShutdown` (only if `MFStartup` succeeded) and `CoUninitialize` close everything again.

**Used by.** `tools/camera_check.cpp` (`main`), which prints the names; this is the call that runs when `camera_check` is started without arguments.

### `list_camera_modes`

```cpp
std::vector<CameraMode> list_camera_modes(int index);
```

**What it does.** Returns the picture formats that camera number `index` offers, in the order the camera lists them (one `CameraMode` each: size, frame rate, pixel format). The camera is opened to ask it, but its picture stream is **not** started. The result is empty when the camera cannot be opened (no such index, no permission) or offers nothing; in the first case `open_camera_reader` also prints `camera: device N not found (M cameras)` when the index does not exist.

**How it works.** `CoInitializeEx` and `MFStartup`; `open_camera_reader(index)` makes the source reader; `collect_native_modes` lists the media types; for each `NativeMode` the `info` part is appended to the result and its media type is released (`release(m.type)`); the reader is released; `MFShutdown` (only if `MFStartup` succeeded) and `CoUninitialize` close everything again. No size is set and no sample is read.

**Used by.** `tools/camera_check.cpp` (`main`, with `--modes`).

### `pick_camera_mode`

```cpp
int pick_camera_mode(const std::vector<CameraMode>& modes, int width, int height);
```

**What it does.** Chooses which of `modes` suits a request for `width` x `height` pixels best, and returns its index in `modes`, or -1 when `modes` has no usable entry (an empty list, or only entries whose width or height is 0). A request of 0 x 0 (any `width` or `height` that is 0 or negative) means the default size, `FA_CAMERA_DEFAULT_WIDTH` x `FA_CAMERA_DEFAULT_HEIGHT` (1920 x 1080). The camera never has to offer the exact size.

It is a **pure function**: it looks only at its arguments, opens no camera and uses no Media Foundation, so `tests/test_common.cpp` can test it with made-up lists.

**The rule**, in this order:

1. **Decoder formats are ignored** when the camera offers anything else. The ignored formats are `H264`, `HEVC`, `H265` and `AVC1` (compressed video that needs a decoder; the header comment names them too). If every mode is one of them, they are all kept.
2. **Slow modes are ignored** when the camera offers faster ones: a mode is slow when its frame rate is below `FA_CAMERA_MIN_FPS` (15). A frame rate of 0 means "not reported" and counts as fast enough. If every remaining mode is slow, they are all kept.
3. **The closest size wins.** The distance of a mode from the request is `|ln(w / wanted_w)| + |ln(h / wanted_h)|` (`ln` is the natural logarithm). It measures the *relative* difference of the width plus that of the height, so a picture of the wrong shape loses against one of the right shape, and a mode 1.25 times too large is as close as one 1.25 times too small.
4. **A tie** (distances equal within 1e-9) goes to the higher frame rate (difference above 1e-6), then to the larger picture (`width * height`), then to the earlier entry in the list.

**How it works.** A list of candidate indices is narrowed by step 1 and step 2 (each narrowing is skipped when it would leave nothing). Then one loop over the candidates computes the distance and keeps the best; because a later entry replaces the best only when it is strictly better, equal entries keep the earlier one.

**Worked examples**, taken from the checks in `tests/test_common.cpp` (`webcam` is a typical camera: 640x480, 1280x720 and 1920x1080 as MJPG at 30 fps, then 640x480 YUY2 at 30, 1280x720 YUY2 at 10 and 1920x1080 YUY2 at 5):

* `webcam`, request 1920x1080: the two slow YUY2 modes (10 and 5 fps) are dropped by rule 2; of the rest, 1920x1080 MJPG has distance 0. Result: entry 2, not the 5 fps one. A request of 0 x 0 gives the same entry.
* `webcam`, request 640x480: the MJPG entry 0 and the YUY2 entry 3 both have distance 0 and 30 fps and the same size; rule 4 keeps the earlier one, entry 0.
* `webcam`, request 1280x1080 (not offered): 1280x720 and 1920x1080 have the same distance, 0.405: for 1280x720 the width matches and the height is 1.5 times too small (`|ln(720/1080)| = 0.405`), for 1920x1080 the height matches and the width is 1.5 times too large (`|ln(1920/1280)| = 0.405`). The tie goes to the larger picture: entry 2, 1920x1080.
* A 4:3 camera with 640x480, 1024x768 and 1600x1200, request 1920x1080: 1600x1200 has distance 0.288, 1024x768 has 0.970. Result: 1600x1200, the closest in size and shape.
* The same camera, request 800x600: 640x480 is 1.25 times too small (distance 0.446) and 1024x768 is 1.28 times too large (0.494), so 640x480 wins; a request of 960x720 gives 1024x768 (distance 0.129).
* A 4K camera with 3840x2160 and 1920x1080, request 1920x1080: 1920x1080 (the larger picture is not taken just because it is larger); request 3840x2160: 3840x2160.
* `H264` 1920x1080 and `MJPG` 1280x720, request 1920x1080: the MJPG mode (rule 1); if the MJPG mode is removed from the list, the H264 mode is used because it is all there is.
* 640x480 YUY2 at 10 fps and 1920x1080 YUY2 at 5 fps (everything slow): rule 2 keeps both, the closest size wins, 1920x1080.
* 1920x1080 MJPG with frame rate 0 and 1280x720 MJPG at 30 fps, request 1920x1080: the first (an unreported rate is not held against a mode).

**Used by.** `CameraSource::open` (to choose the mode to switch on), `tools/camera_check.cpp` (`main`, `--modes` marks the chosen mode) and `tests/test_common.cpp` (`main`, 18 checks whose text starts with `camera mode:`).

### `open_camera_source`

```cpp
std::unique_ptr<FrameSource> open_camera_source(int index, int width = FA_CAMERA_DEFAULT_WIDTH,
                                                int height = FA_CAMERA_DEFAULT_HEIGHT, CameraMode* chosen = nullptr);
```

**What it does.** Opens camera number `index` and returns it as a `FrameSource` that delivers pictures of about `width` x `height` pixels. Without these two arguments the size is the default, 1920 x 1080. The camera does not have to offer that size: its own formats are listed, the one that suits best is switched on (`pick_camera_mode`), and the frames you receive have that real size (`FrameSource::frame_size` tells it before the first frame). If the camera lists no usable format the size is requested directly, and as a last resort the camera's default size is used. `chosen`, when it is not null, receives the camera mode that was switched on; it is all zero when the camera had to be used with its default. Returns `nullptr` when the camera cannot be opened (no such index, no permission, format refused).

**How it works.** It creates a `CameraSource` with `std::make_unique`, calls `open(index, width, height, chosen)` and returns the object only when that succeeded. The steps inside are described under `CameraSource::open` and in "Camera picture sizes" above. If `open` fails the object is destroyed on the way out, which calls `~CameraSource`.

**Used by.** `tools/camera_check.cpp` (`main`, with `--capture`, which passes the camera number, the size from the command line (default 1920x1080) and a `CameraMode` to receive the chosen mode). The default arguments are used by no caller in this project.

### `open_video_file_source`

```cpp
std::unique_ptr<FrameSource> open_video_file_source(const std::string& path);
```

**What it does.** Plays a video file (`.mp4`, `.avi`, `.wmv` ...) as a `FrameSource`, with the same Media Foundation reader and RGB conversion that the camera uses. The `time_ms` of a frame is its time in the file, counted from the first frame; the picture size is the size stored in the file. Returns `nullptr` when the file cannot be opened (a message is printed).

**How it works.** It creates a `CameraSource` with `std::make_unique`, calls `open_file(path)` and returns the object only when that succeeded.

**Used by.** `tools/check_video.cpp` (`main`, when the input is a video file), `tools/eval_footage.cpp` (`main`, when the footage folder holds `video.mp4`), `tools/gen_video.cpp` (`main`, for the orientation check of the MP4 it has just written) and `tests/test_common.cpp` (`main`, the round trip with a 320 x 240, 20-frame, 10 frames per second MP4 made of a red top half, a blue bottom half and a white bar that moves with the frame number: the test checks that all 20 frames come back at the right size, right way up, in order, with time stamps from 0 to about 1900 ms, and that a missing video file gives no source).

### `open_sequence_source`

```cpp
std::unique_ptr<FrameSource> open_sequence_source(const std::string& folder, double fps);
```

**What it does.** Returns a `FrameSource` that plays the picture files of `folder` (`.jpg .jpeg .png .bmp`, in file-name order) at `fps` frames per second; frame `k` gets the time `k * 1000 / fps`. Returns `nullptr` if the folder is missing, unreadable or has no picture files.

**How it works.** It creates a `SequenceSource`, calls `open(folder, fps)` and returns it only on success.

**Used by.** `tests/test_common.cpp` (`main`), `tools/check_video.cpp` (`main`, when the input is a folder of frames) and `tools/eval_footage.cpp` (`main`, when the footage folder holds no `video.mp4`). The test creates the folder `build/seq` with three PNG pictures `frame_0000.png` to `frame_0002.png` (copies of a 640 x 540 test picture whose first byte is set to 10, 20 and 30 as a frame marker), opens it with `open_sequence_source("build/seq", 10.0)` and makes four checks: the folder opens; the frames come in file order (width 640, correct marker) with the times exactly 0, 100 and 200 ms at 10 frames per second; the fourth `next` reports the end of the recording; and `open_sequence_source("build/no_such_folder", 10.0)` returns `nullptr`. `tools/gen_video.cpp` writes a folder of numbered frames in the form this function plays.

## src/camera/video_writer.h

The header declares one class, `VideoWriter`, that writes pictures into a video file. Its default constructor and the deleted copy functions are in the header; its one inline function is `frames_written`; the other methods are only declared here and are documented in the `video_writer.cpp` section below.

| Function | Purpose |
|---|---|
| `VideoWriter` (class) | Writes BGR pictures into an H.264 `.mp4` file. |
| `VideoWriter::VideoWriter` | Default constructor (does nothing; the file is created by `open`). |
| `VideoWriter::VideoWriter(const VideoWriter&)`, `VideoWriter::operator=` | Deleted: a writer owns an open file and cannot be copied. |
| `VideoWriter::frames_written` | How many pictures have been written since `open`. |
| `VideoWriter::~VideoWriter`, `open`, `write`, `close` | Declared here; documented in the `video_writer.cpp` section. |

### `VideoWriter` (class)

```cpp
class VideoWriter {
public:
    VideoWriter() = default;
    ~VideoWriter();
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;
    bool open(const std::string& path, int width, int height, double fps, int bitrate_kbps = 8000);
    bool write(const Image& frame);
    bool close();
    int frames_written() const { return frames_; }
private:
    struct Impl;
    Impl* impl_ = nullptr;
    int width_ = 0, height_ = 0, frames_ = 0;
    double fps_ = 0;
};
```

**What it does.** Creates an `.mp4` video file and appends pictures to it one by one. The pictures are 8-bit BGR `Image`s of one fixed size. The usual sequence is: `open`, many times `write`, then `close`. The destructor closes the file too, but only `close` tells you whether the file was finished correctly.

**How it works.** The Windows types are hidden in the private struct `Impl` (defined in `video_writer.cpp`), so that programs that include this header do not need the Windows headers. The other private data: `impl_` (pointer to the `Impl`, null while no file is open), `width_` and `height_` (the picture size given to `open`), `frames_` (pictures written so far) and `fps_` (the frame rate).

**Used by.** `tools/gen_video.cpp` (`main`, writes `video.mp4`), `tools/check_video.cpp` (`main`, writes the annotated video) and `tests/test_common.cpp` (`main`, writes a 20-frame test video).

### `VideoWriter::VideoWriter`, `VideoWriter::operator=` (constructor and deleted copy)

```cpp
VideoWriter() = default;
VideoWriter(const VideoWriter&) = delete;
VideoWriter& operator=(const VideoWriter&) = delete;
```

**What it does.** The default constructor makes a writer that has no file yet (`impl_` is null, all counters 0). Copying a writer is forbidden by `= delete`: two objects must not share one open file and its encoder.

**How it works.** Standard C++ defaulted and deleted special member functions; a copy attempt is a compile error.

**Used by.** The constructor runs where a `VideoWriter` variable is declared (`gen_video.cpp`, `check_video.cpp`).

### `VideoWriter::frames_written`

```cpp
int frames_written() const;
```

**What it does.** Returns how many pictures `write` has accepted since the last successful `open` (0 before the first). `check_video` uses it to open the writer at the first frame only.

**How it works.** Returns the member `frames_`.

**Used by.** `tools/check_video.cpp` (`main`) and `tests/test_common.cpp` (`main`, expects 20 after 20 writes).

## src/camera/video_writer.cpp

The file writes through the Media Foundation *sink writer*: it takes pictures one by one, each with a time stamp, hands them to the Windows H.264 encoder and writes the `.mp4` file. The program tells it that its own pictures are uncompressed RGB32 (4 bytes per pixel, top row first); the writer converts them to the format the encoder needs. The writer is created with hardware transforms switched off (`MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE`, see `VideoWriter::open`), so the encoder is the software one and runs on the CPU; the process still has an idle Direct3D device while it encodes.

| Function | Purpose |
|---|---|
| `VideoWriter::Impl` (struct) | The Media Foundation objects of one open file. |
| `release` | Release a COM pointer (a private copy of the helper in `frame_source.cpp`). |
| `report` | Print a failed Windows call with its `HRESULT` number. |
| `VideoWriter::~VideoWriter` | Close the file if it is still open. |
| `VideoWriter::open` | Create the file and set up the H.264 stream. |
| `VideoWriter::write` | Convert one picture and append it. |
| `VideoWriter::close` | Finish the file and release everything. |

### `VideoWriter::Impl` (struct)

```cpp
struct VideoWriter::Impl {
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    bool started = false;
};
```

**What it does.** Holds what belongs to one open video file: `writer` (the sink writer, a COM pointer), `stream` (the index of the video stream inside the writer) and `started` (true after `BeginWriting` has succeeded, so that `close` knows whether there is anything to finish).

**How it works.** A plain struct created with `new` in `open` and deleted in `close`.

**Used by.** `VideoWriter::open`, `write` and `close`.

### `release` (video_writer.cpp)

```cpp
template <class T> void release(T*& p);
```

**What it does.** The same helper as in `frame_source.cpp`: calls `Release()` on a COM pointer if it is not null and sets it to null.

**How it works.** A small template in the anonymous namespace of this file.

**Used by.** `VideoWriter::open`, `write` and `close`.

### `report`

```cpp
void report(const char* what, HRESULT hr);
```

**What it does.** Prints `video writer: <what> failed (HRESULT 0x<number>)` to `std::cerr`, so that a failed Windows call can be looked up.

**How it works.** One `std::cerr` line with the number in hexadecimal.

**Used by.** `VideoWriter::open` (for the failing step) and `VideoWriter::write` (for `WriteSample`).

### `VideoWriter::~VideoWriter`

```cpp
~VideoWriter();
```

**What it does.** Closes the file if it is still open, so a forgotten `close` does not leave a broken file.

**How it works.** Calls `close()` and ignores its result.

**Used by.** Runs automatically when a `VideoWriter` variable goes out of scope (`gen_video.cpp`, `check_video.cpp`).

### `VideoWriter::open`

```cpp
bool open(const std::string& path, int width, int height, double fps, int bitrate_kbps = 8000);
```

**What it does.** Creates the file `path` (an `.mp4`) for pictures of `width` x `height` pixels, played at `fps` frames per second. `bitrate_kbps` (default `8000`) is the target size of the compressed video in kilobits per second. Width and height must be even numbers and positive and `fps` positive; otherwise it prints `video writer: width and height must be even and positive, fps positive` and returns `false`. Returns `true` when the file is ready for `write` and `false` when a Windows call failed (the failing step is printed). A writer that is already open is closed first. The encoding is done by the software encoder on the CPU: the writer is created with hardware transforms switched off (step 3).

**How it works.**
1. `close()` (nothing happens if no file is open), then the argument check.
2. `CoInitializeEx` and `MFStartup` start COM and Media Foundation; the `Impl` is created.
3. The path (UTF-8 text) is converted to a wide-character string. An attribute list with **one** entry is made (`MFCreateAttributes(&attributes, 1)`): `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = FALSE`, which tells the writer not to use a hardware (GPU) encoder or a GPU video processor for the colour conversion; the encoding is done by the Microsoft software H.264 encoder (`mfh264enc.dll` was in the module list of a running `gen_video`). If the list cannot be created the writer is still made, without the attribute (the code checks for a null list before it sets the entry). `MFCreateSinkWriterFromURL(path, nullptr, attributes, &writer)` creates the sink writer from the path and the list, and the list is released. The file type is chosen by Windows from the extension. **What this does and does not do:** it keeps the encoder and the colour conversion off the GPU. It does **not** remove every trace of a GPU: measured with the Windows GPU counters, a process that writes an MP4 (`gen_video`, `check_video`'s annotated video) still has `3d` engine instances at 0.0% utilization while it encodes, and no video, compute or copy engine instance; the instances were the same with and without this attribute. They are an idle Direct3D device that Media Foundation opens for its video path (an inference; see `RESULTS.md`, "All engines on the CPU"). Removing even that device was not done: it would mean converting the pictures to NV12 on the CPU before they reach the encoder, which changes the colour handling of the footage.
4. The frame rate is given as a fraction: numerator `fps * 1000` rounded, denominator 1000.
5. **Output type** (what goes into the file): major type video, subtype H.264, average bit rate `bitrate_kbps * 1000`, progressive (not interlaced), the frame size, the frame rate and square pixels (aspect ratio 1:1). `AddStream` adds it and returns the stream index.
6. **Input type** (what the program gives the writer): uncompressed RGB32, progressive, the same size and rate, and `MF_MT_DEFAULT_STRIDE` = `width * 4`, a *positive* stride, which tells Windows that the rows are stored from the top. `SetInputMediaType` registers it; Windows inserts the colour conversion to the format the encoder needs.
7. `BeginWriting` starts the writing; then `started` is set and `width_`, `height_`, `fps_` are stored and `frames_` is set to 0.
If any step fails, `report` prints it, `close()` cleans up and the function returns `false`.

**Used by.** `tools/gen_video.cpp` (`main`, the size given by `--size`, 1920 x 1080 by default, at the video's frame rate, default bit rate; both numbers must be even), `tools/check_video.cpp` (`main`, size of the shown picture rounded down to even numbers) and `tests/test_common.cpp` (`main`, 320 x 240 at 10 frames per second).

### `VideoWriter::write`

```cpp
bool write(const Image& frame);
```

**What it does.** Appends one picture. `frame` must be a 3-channel BGR image of exactly the size given to `open`; the function returns `false` (and writes nothing) when no file is open, when the size differs, or when the image does not have 3 channels, or when a Windows call fails. The picture gets its time from its number: frame `n` is shown at `n` times the frame duration, so the video plays at the steady rate given to `open`, whatever the real capture times were.

**How it works.**
1. A Media Foundation memory buffer of `width * height * 4` bytes is created and locked.
2. Every pixel is copied from 3 bytes (blue, green, red) to 4 bytes (blue, green, red, 255), rows in the same order, top row first.
3. The buffer is unlocked and its length set; a sample is created that holds the buffer; its time is `frames_ * duration` and its duration is `10,000,000 / fps` rounded (Media Foundation counts in 100 ns units).
4. `WriteSample` hands the sample to the encoder; the sample and the buffer are released. On a failure `report` prints it and the function returns `false`; otherwise `frames_` grows by 1.

**Used by.** `tools/gen_video.cpp` (`main`, one call per rendered frame), `tools/check_video.cpp` (`main`, one call per annotated frame) and `tests/test_common.cpp` (`main`, 20 calls).

### `VideoWriter::close`

```cpp
bool close();
```

**What it does.** Finishes the file: after `close` the `.mp4` can be played. Returns `true` if the file was finished correctly (or if nothing was open) and `false` if finishing failed. It is safe to call twice: the second call finds nothing open and returns `true`. The destructor calls it too; call it yourself to see whether it worked.

**How it works.** If a file is open and writing has started, `Finalize()` on the sink writer writes the end of the file; its success is the return value. Then the writer is released, the `Impl` deleted, and `MFShutdown()` and `CoUninitialize()` undo what `open` started. When `open` fails halfway, `close` is also the clean-up (it does not call `Finalize` because `started` is still false).

**Used by.** `VideoWriter::open` (clean-up), `VideoWriter::~VideoWriter`, `tools/gen_video.cpp` (`main`, to see whether the MP4 was written correctly), `tools/check_video.cpp` (`main`) and `tests/test_common.cpp` (`main`, together with `frames_written()` it decides the check "20 frames written and the file finished").

## Where code and comments disagree

* `frame_source.h` says "Two sources exist:" and then lists three (camera, video file, folder of pictures). The code has three: `open_camera_source`, `open_video_file_source` and `open_sequence_source`.
* No other disagreement found. The new comments of `frame_source.h` (`frame_size`, `CameraMode`, `list_camera_modes`, `pick_camera_mode`, `open_camera_source`) and of `config.h` (camera picture size) agree with the code. The comments in the header about the camera's time zero (its first frame) and about `test_common` and `test_pipeline` agree with the code, `CameraSource::next` unlocks every buffer that it locked, and the comments of `video_writer.h` and `video_writer.cpp` (even width and height, RGB32 top-down input, H.264 in `.mp4`) agree with the code; the comments of the two reader attribute lists in `frame_source.cpp` ("decoding on the CPU, not through the GPU (DXVA)", "no hardware (GPU) decoder or video processor") also agree with it.
