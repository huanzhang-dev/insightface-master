// frame_source.cpp - camera and recorded-frame sources. See frame_source.h.
#include "frame_source.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include "fs_compat.h"
#include <iostream>

#include "image_io.h"

namespace fa {
namespace {

// Release a COM interface pointer (safe to call with nullptr).
template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// ===========================================================================
// What a camera offers
// ===========================================================================

// The pixel format of a media type as text: "MJPG", "YUY2", "NV12" ... (the four letters in the type's GUID),
// or the name of a plain RGB format, or "?".
std::string format_name(const GUID& subtype) {
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32)) return "RGB32";
    if (IsEqualGUID(subtype, MFVideoFormat_RGB24)) return "RGB24";
    if (IsEqualGUID(subtype, MFVideoFormat_ARGB32)) return "ARGB32";
    char letters[5] = {0, 0, 0, 0, 0};
    std::memcpy(letters, &subtype.Data1, 4);
    for (int i = 0; i < 4; ++i)
        if (letters[i] < 32 || letters[i] > 126) return "?";
    return letters;
}

// One native media type of a camera: what it offers (info) and the media type itself, which the reader
// needs to switch that mode on. The caller releases `type`.
struct NativeMode {
    CameraMode info;
    IMFMediaType* type = nullptr;
};

// Ask the reader for every native media type of its first video stream.
void collect_native_modes(IMFSourceReader* reader, std::vector<NativeMode>& modes) {
    for (DWORD i = 0; i < 1024; ++i) {  // (a real camera lists a few dozen; the limit only stops a faulty driver)
        IMFMediaType* type = nullptr;
        if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type)) || !type) break;  // MF_E_NO_MORE_TYPES ends the list
        UINT32 w = 0, h = 0, numerator = 0, denominator = 0;
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
        // a camera may offer a range of frame rates in one type: take the fastest of it
        if (FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE_RANGE_MAX, &numerator, &denominator)) || denominator == 0)
            MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator);
        GUID subtype = GUID_NULL;
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        NativeMode mode;
        mode.info = CameraMode(static_cast<int>(w), static_cast<int>(h), denominator ? static_cast<double>(numerator) / denominator : 0.0,
                               format_name(subtype));
        mode.type = type;
        modes.push_back(mode);
    }
}

// Open camera number `index` and make a source reader for it that converts any camera format (YUY2, MJPG ...)
// to plain RGB. Media Foundation must be started. Returns null (after printing the reason) on failure.
IMFSourceReader* open_camera_reader(int index) {
    IMFAttributes* search = nullptr;
    MFCreateAttributes(&search, 1);
    search->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(search, &devices, &count);
    release(search);
    if (FAILED(hr) || index < 0 || static_cast<UINT32>(index) >= count) {
        std::cerr << "camera: device " << index << " not found (" << (FAILED(hr) ? 0 : count) << " cameras)\n";
        if (SUCCEEDED(hr)) {
            for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
            CoTaskMemFree(devices);
        }
        return nullptr;
    }
    IMFMediaSource* source = nullptr;
    hr = devices[index]->ActivateObject(IID_PPV_ARGS(&source));
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (FAILED(hr)) return nullptr;

    IMFAttributes* reader_attributes = nullptr;
    MFCreateAttributes(&reader_attributes, 3);
    reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    reader_attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);                  // decoding on the CPU, not through the GPU (DXVA)
    reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);      // no hardware (GPU) decoder or video processor
    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(source, reader_attributes, &reader);
    release(reader_attributes);
    release(source);
    return SUCCEEDED(hr) ? reader : nullptr;
}

// ===========================================================================
// A camera or a video file through the Media Foundation "source reader"
//
// Both give a stream of pictures; the source reader converts whatever the
// camera or the file contains (YUY2, MJPG, H.264...) to plain RGB for us. One
// class handles both, so a video file exercises exactly the same conversion and
// orientation code as a live camera does.
// ===========================================================================
class CameraSource : public FrameSource {
public:
    ~CameraSource() override {
        release(reader_);
        MFShutdown();
        CoUninitialize();
    }

    // Open a video file (.mp4, .avi, .wmv ...). The picture size is the size stored in the file.
    bool open_file(const std::string& path) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(MFStartup(MF_VERSION))) return false;
        IMFAttributes* reader_attributes = nullptr;
        MFCreateAttributes(&reader_attributes, 3);
        reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        reader_attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);              // decoding on the CPU, not through the GPU (DXVA)
        reader_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);   // no hardware (GPU) decoder or video processor
        const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);
        HRESULT hr = MFCreateSourceReaderFromURL(wide.c_str(), reader_attributes, &reader_);
        release(reader_attributes);
        if (FAILED(hr)) {
            std::cerr << "video: cannot open " << path << " (HRESULT 0x" << std::hex << static_cast<unsigned>(hr) << std::dec << ")\n";
            return false;
        }
        return finish_setup(0, 0);
    }

    // Open camera `index`. `chosen` (may be null) receives the camera mode that was switched on.
    bool open(int index, int width, int height, CameraMode* chosen) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(MFStartup(MF_VERSION))) return false;
        if (chosen) *chosen = CameraMode();

        // 1. the camera, behind a source reader that converts any camera format (YUY2, MJPG...) to plain RGB
        reader_ = open_camera_reader(index);
        if (!reader_) return false;

        // 2. ask the camera what it offers and switch on the mode that suits the wanted size best, so that
        //    the CAMERA delivers about that size (a reader asked for a size the camera does not have fails)
        std::vector<NativeMode> modes;
        collect_native_modes(reader_, modes);
        std::vector<CameraMode> offered;
        for (const NativeMode& m : modes) offered.push_back(m.info);
        const int pick = pick_camera_mode(offered, width, height);
        bool switched_on = false;
        if (pick >= 0) switched_on = SUCCEEDED(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, modes[pick].type));
        for (NativeMode& m : modes) release(m.type);
        if (switched_on && finish_setup(offered[pick].width, offered[pick].height)) {
            if (chosen) *chosen = offered[pick];
            return true;
        }
        // 3. the camera listed nothing usable: ask for the size directly, and finally for its default size
        return finish_setup(width, height);
    }

    bool frame_size(int& width, int& height) const override {
        width = width_;
        height = height_;
        return width_ > 0 && height_ > 0;
    }

    // Steps shared by camera and file: ask for RGB32 pixels and find out the real size and orientation.
    bool finish_setup(int width, int height) {
        // 3. ask for RGB32 pixels at the wanted size (fall back to the default size)
        if (!set_output_type(width, height) && !set_output_type(0, 0)) return false;

        // 4. read back what we really got
        IMFMediaType* type = nullptr;
        if (FAILED(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) return false;
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
        LONG stride = 0;
        UINT32 raw_stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride))) stride = static_cast<LONG>(raw_stride);
        release(type);
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        bottom_up_ = stride < 0;  // a negative stride means the first row in memory is the bottom row
        return width_ > 0 && height_ > 0;
    }

    bool next(Image& frame, double& time_ms) override {
        for (int attempt = 0; attempt < 100; ++attempt) {
            DWORD stream = 0, flags = 0;
            LONGLONG timestamp = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &timestamp, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                release(sample);
                return false;
            }
            if (!sample) continue;  // "stream tick" with no picture: ask again

            IMFMediaBuffer* buffer = nullptr;
            bool ok = false;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                    if (length >= static_cast<DWORD>(width_) * height_ * 4) {
                        copy_bgrx_to_bgr(data, frame);
                        ok = true;
                    }
                    buffer->Unlock();  // always unlock what was locked, also when the buffer is too short
                }
                release(buffer);
            }
            release(sample);
            if (!ok) return false;
            if (first_timestamp_ < 0) first_timestamp_ = timestamp;
            time_ms = (timestamp - first_timestamp_) / 10000.0;  // Media Foundation counts in 100 ns
            return true;
        }
        return false;
    }

private:
    bool set_output_type(int width, int height) {
        IMFMediaType* type = nullptr;
        MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (width > 0 && height > 0) MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
        HRESULT hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
        release(type);
        return SUCCEEDED(hr);
    }

    // The camera gives 4 bytes per pixel (B, G, R, unused). We keep 3.
    void copy_bgrx_to_bgr(const BYTE* data, Image& frame) const {
        frame.create(width_, height_, 3);
        for (int y = 0; y < height_; ++y) {
            const BYTE* src = data + static_cast<size_t>(bottom_up_ ? height_ - 1 - y : y) * width_ * 4;
            uint8_t* dst = frame.row(y);
            for (int x = 0; x < width_; ++x, src += 4, dst += 3) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            }
        }
    }

    IMFSourceReader* reader_ = nullptr;
    int width_ = 0, height_ = 0;
    bool bottom_up_ = false;
    LONGLONG first_timestamp_ = -1;
};

// ===========================================================================
// A folder of numbered pictures
// ===========================================================================
class SequenceSource : public FrameSource {
public:
    bool open(const std::string& folder, double fps) {
        fps_ = fps > 0 ? fps : 30.0;
        namespace fs = fa::fs;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder, error)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") files_.push_back(entry.path().string());
        }
        if (error || files_.empty()) return false;
        std::sort(files_.begin(), files_.end());
        return true;
    }

    bool next(Image& frame, double& time_ms) override {
        if (index_ >= files_.size()) return false;
        if (!load_image(files_[index_], frame)) return false;
        time_ms = index_ * 1000.0 / fps_;
        ++index_;
        return true;
    }

private:
    std::vector<std::string> files_;
    size_t index_ = 0;
    double fps_ = 30;
};

}  // namespace

std::vector<std::string> list_cameras() {
    std::vector<std::string> names;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(MFStartup(MF_VERSION))) {
        IMFAttributes* search = nullptr;
        MFCreateAttributes(&search, 1);
        search->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (SUCCEEDED(MFEnumDeviceSources(search, &devices, &count))) {
            for (UINT32 i = 0; i < count; ++i) {
                WCHAR* name = nullptr;
                UINT32 length = 0;
                std::string text = "(unnamed camera)";
                if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                    text.assign(n > 0 ? n - 1 : 0, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, name, -1, &text[0], n, nullptr, nullptr);
                    CoTaskMemFree(name);
                }
                names.push_back(text);
                devices[i]->Release();
            }
            CoTaskMemFree(devices);
        }
        release(search);
        MFShutdown();
    }
    CoUninitialize();
    return names;
}

std::vector<CameraMode> list_camera_modes(int index) {
    std::vector<CameraMode> result;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(MFStartup(MF_VERSION))) {
        IMFSourceReader* reader = open_camera_reader(index);
        if (reader) {
            std::vector<NativeMode> modes;
            collect_native_modes(reader, modes);
            for (NativeMode& m : modes) {
                result.push_back(m.info);
                release(m.type);
            }
            release(reader);
        }
        MFShutdown();
    }
    CoUninitialize();
    return result;
}

int pick_camera_mode(const std::vector<CameraMode>& modes, int width, int height) {
    if (width <= 0 || height <= 0) {
        width = FA_CAMERA_DEFAULT_WIDTH;
        height = FA_CAMERA_DEFAULT_HEIGHT;
    }
    std::vector<int> candidates;  // indices into `modes`
    for (size_t i = 0; i < modes.size(); ++i)
        if (modes[i].width > 0 && modes[i].height > 0) candidates.push_back(static_cast<int>(i));
    if (candidates.empty()) return -1;

    // 1. modes that need a video decoder are only used when there is nothing else
    std::vector<int> narrowed;
    for (int i : candidates) {
        const std::string& f = modes[i].format;
        if (f != "H264" && f != "HEVC" && f != "H265" && f != "AVC1") narrowed.push_back(i);
    }
    if (!narrowed.empty()) candidates.swap(narrowed);

    // 2. slow modes are only used when there is nothing faster (an unreported frame rate counts as fast enough)
    narrowed.clear();
    for (int i : candidates)
        if (modes[i].fps <= 0 || modes[i].fps >= FA_CAMERA_MIN_FPS) narrowed.push_back(i);
    if (!narrowed.empty()) candidates.swap(narrowed);

    // 3. and 4. the closest size; a tie goes to the faster mode, then to the larger picture, then to the earlier entry
    int best = -1;
    double best_score = 0;
    for (int i : candidates) {
        const CameraMode& m = modes[i];
        const double score = std::fabs(std::log(static_cast<double>(m.width) / width)) + std::fabs(std::log(static_cast<double>(m.height) / height));
        bool better = best < 0 || score < best_score - 1e-9;
        if (!better && std::fabs(score - best_score) <= 1e-9) {
            const CameraMode& b = modes[best];
            better = m.fps > b.fps + 1e-6 || (std::fabs(m.fps - b.fps) <= 1e-6 && static_cast<long long>(m.width) * m.height > static_cast<long long>(b.width) * b.height);
        }
        if (better) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

std::unique_ptr<FrameSource> open_camera_source(int index, int width, int height, CameraMode* chosen) {
    auto source = std::make_unique<CameraSource>();
    if (!source->open(index, width, height, chosen)) return nullptr;
    return source;
}

std::unique_ptr<FrameSource> open_video_file_source(const std::string& path) {
    auto source = std::make_unique<CameraSource>();
    if (!source->open_file(path)) return nullptr;
    return source;
}

std::unique_ptr<FrameSource> open_sequence_source(const std::string& folder, double fps) {
    auto source = std::make_unique<SequenceSource>();
    if (!source->open(folder, fps)) return nullptr;
    return source;
}

}  // namespace fa
