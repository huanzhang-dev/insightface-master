// video_writer.h - write pictures into a video file (H.264 in an .mp4), with Windows
// Media Foundation (part of Windows, no extra library).
//
// Used to create the generated test video and the annotated result video that a
// person can watch. The pictures are 8-bit BGR images of one fixed size.
#ifndef FA_VIDEO_WRITER_H
#define FA_VIDEO_WRITER_H

#include <string>

#include "types.h"

namespace fa {

class VideoWriter {
public:
    VideoWriter() = default;
    ~VideoWriter();
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    // Create `path` (an .mp4 file). Width and height must be even numbers. The video plays
    // at `fps` frames per second; `bitrate_kbps` is the target size of the compressed video.
    bool open(const std::string& path, int width, int height, double fps, int bitrate_kbps = 8000);

    // Append the next picture (must have the size given to open()). Returns false on failure.
    bool write(const Image& frame);

    // Finish the file. Called by the destructor too; call it yourself to see whether it worked.
    bool close();

    int frames_written() const { return frames_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int width_ = 0, height_ = 0, frames_ = 0;
    double fps_ = 0;
};

}  // namespace fa

#endif  // FA_VIDEO_WRITER_H
