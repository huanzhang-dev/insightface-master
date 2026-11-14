// video_writer.cpp - see video_writer.h.
//
// How it works: Media Foundation's "sink writer" takes pictures one by one (each with a
// time stamp) and gives them to the Windows H.264 encoder, which writes the .mp4 file.
// We tell it that our pictures are uncompressed RGB32 (4 bytes per pixel, top row first);
// it converts them to the format the encoder needs.
#include "video_writer.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>

#include <cstring>
#include <iostream>

namespace fa {

struct VideoWriter::Impl {
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    bool started = false;
};

namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void report(const char* what, HRESULT hr) {
    std::cerr << "video writer: " << what << " failed (HRESULT 0x" << std::hex << static_cast<unsigned>(hr) << std::dec << ")\n";
}

}  // namespace

VideoWriter::~VideoWriter() { close(); }

bool VideoWriter::open(const std::string& path, int width, int height, double fps, int bitrate_kbps) {
    close();
    if (width <= 0 || height <= 0 || width % 2 || height % 2 || fps <= 0) {
        std::cerr << "video writer: width and height must be even and positive, fps positive\n";
        return false;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION))) return false;

    impl_ = new Impl();
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);

    // The encoding runs in software on the CPU and never on a GPU: hardware transforms (a GPU or Quick Sync encoder, or
    // a GPU video processor for the colour conversion) are switched off. This states the intent explicitly; it did NOT
    // change what the Windows GPU counters show: while the encoder runs, the process still has idle "3d" engine
    // instances at 0.0% (Media Foundation opens a Direct3D device for its video path), but no video-encode,
    // video-processing, compute or copy engine is used (see docs/RESULTS.md, "All engines on the CPU").
    IMFAttributes* attributes = nullptr;
    MFCreateAttributes(&attributes, 1);
    if (attributes) attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    HRESULT hr = MFCreateSinkWriterFromURL(wide.c_str(), nullptr, attributes, &impl_->writer);
    release(attributes);
    if (FAILED(hr)) { report("MFCreateSinkWriterFromURL", hr); close(); return false; }

    // frame rate as a fraction: fps = numerator / 1000
    const UINT32 rate_num = static_cast<UINT32>(fps * 1000 + 0.5), rate_den = 1000;

    // what goes INTO the file: H.264
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate_kbps) * 1000);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, rate_num, rate_den);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = impl_->writer->AddStream(out, &impl_->stream);
    release(out);
    if (FAILED(hr)) { report("AddStream (H.264 encoder)", hr); close(); return false; }

    // what we GIVE the writer: uncompressed RGB32, rows stored from the top (positive stride)
    IMFMediaType* in = nullptr;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4));
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, rate_num, rate_den);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = impl_->writer->SetInputMediaType(impl_->stream, in, nullptr);
    release(in);
    if (FAILED(hr)) { report("SetInputMediaType", hr); close(); return false; }

    hr = impl_->writer->BeginWriting();
    if (FAILED(hr)) { report("BeginWriting", hr); close(); return false; }
    impl_->started = true;
    width_ = width;
    height_ = height;
    fps_ = fps;
    frames_ = 0;
    return true;
}

bool VideoWriter::write(const Image& frame) {
    if (!impl_ || !impl_->started || frame.width != width_ || frame.height != height_ || frame.channels != 3) return false;

    const DWORD bytes = static_cast<DWORD>(width_) * height_ * 4;
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, &buffer))) return false;
    BYTE* data = nullptr;
    if (FAILED(buffer->Lock(&data, nullptr, nullptr))) { release(buffer); return false; }
    for (int y = 0; y < height_; ++y) {  // BGR (3 bytes) -> BGRX (4 bytes), top row first
        const uint8_t* src = frame.row(y);
        BYTE* dst = data + static_cast<size_t>(y) * width_ * 4;
        for (int x = 0; x < width_; ++x, src += 3, dst += 4) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
        }
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);

    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);
    const LONGLONG duration = static_cast<LONGLONG>(10000000.0 / fps_ + 0.5);  // Media Foundation counts in 100 ns
    sample->SetSampleTime(static_cast<LONGLONG>(frames_) * duration);
    sample->SetSampleDuration(duration);
    const HRESULT hr = impl_->writer->WriteSample(impl_->stream, sample);
    release(sample);
    release(buffer);
    if (FAILED(hr)) { report("WriteSample", hr); return false; }
    ++frames_;
    return true;
}

bool VideoWriter::close() {
    bool ok = true;
    if (impl_) {
        if (impl_->writer && impl_->started) ok = SUCCEEDED(impl_->writer->Finalize());
        release(impl_->writer);
        delete impl_;
        impl_ = nullptr;
        MFShutdown();
        CoUninitialize();
    }
    return ok;
}

}  // namespace fa
