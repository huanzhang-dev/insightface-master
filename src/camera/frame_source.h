// frame_source.h - where the images come from.
//
// The authentication pipeline does not care whether a frame comes from a real
// camera or from a recorded video, as long as it can ask "give me the next
// frame". Two sources exist:
//   * a live camera (Windows Media Foundation)          -> open_camera_source()
//   * a video file (same reader as the camera)          -> open_video_file_source()
//   * a folder of numbered picture files (a recording)  -> open_sequence_source()
//     tools/gen_video writes its generated "person walks toward the camera" video in this
//     form. No test opens a real camera; test_pipeline renders its frames in memory and
//     test_common plays a small folder through this source.
#ifndef FA_FRAME_SOURCE_H
#define FA_FRAME_SOURCE_H

#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace fa {

class FrameSource {
public:
    virtual ~FrameSource() {}

    // Get the next frame (BGR). `time_ms` is its capture time in milliseconds
    // since the first frame (the first frame of a camera has time 0). Returns false at the end of a recording or on camera failure.
    virtual bool next(Image& frame, double& time_ms) = 0;

    // The size of the frames this source delivers, when it is known before the first frame (a camera or
    // a video file knows it once opened). Returns false for sources that do not know it (a folder of pictures).
    virtual bool frame_size(int& width, int& height) const { (void)width; (void)height; return false; }
};

// One picture format that a camera offers: size, frame rate and pixel format. Cameras offer many
// (for example 640x480, 1280x720 and 1920x1080, each as MJPG and as YUY2).
struct CameraMode {
    int width = 0, height = 0;
    double fps = 0;          // frames per second the camera promises in this mode (0 = not reported)
    std::string format;      // "MJPG", "YUY2", "NV12", "RGB32" ... ("?" when the camera does not say)
    CameraMode() {}
    CameraMode(int width_, int height_, double fps_, const std::string& format_)
        : width(width_), height(height_), fps(fps_), format(format_) {}
};

// Names of the cameras that Windows reports (index 0, 1, ...). Does NOT open any camera.
std::vector<std::string> list_cameras();

// The picture formats camera `index` offers, in the order the camera lists them (empty when the camera
// cannot be opened). The camera is opened to ask it, but its picture stream is not started.
std::vector<CameraMode> list_camera_modes(int index);

// Which of `modes` suits a request for width x height best: its index, or -1 when `modes` has no usable
// entry. A request of 0 x 0 means the default size (FA_CAMERA_DEFAULT_WIDTH x FA_CAMERA_DEFAULT_HEIGHT).
// The rule, in this order:
//   1. modes that need a video decoder (H264, HEVC, also called H265 or AVC1) are ignored when the camera offers anything else;
//   2. modes slower than FA_CAMERA_MIN_FPS frames per second are ignored when the camera offers faster ones;
//   3. the mode whose size is closest to the request wins; "closest" counts the relative difference of the
//      width plus that of the height (|ln(w / wanted_w)| + |ln(h / wanted_h)|), so a mode of the wrong
//      shape loses against one of the right shape;
//   4. a tie goes to the higher frame rate, then to the larger picture, then to the earlier entry.
// The camera never has to offer the exact size: any size works, the pipeline copes with every picture size.
int pick_camera_mode(const std::vector<CameraMode>& modes, int width, int height);

// Open camera number `index` and ask for about `width` x `height` pixels (the default is 1920x1080). The
// camera's own modes are listed and the one that suits best is switched on (pick_camera_mode), so
// the camera itself delivers that size; if the camera lists no usable mode the size is requested directly,
// and as a last resort the camera's default is used. The frames you receive tell you the real size
// (FrameSource::frame_size tells it before the first frame). `chosen`, when given, receives the camera mode
// that was switched on (all zero when the camera had to be used with its default).
std::unique_ptr<FrameSource> open_camera_source(int index, int width = FA_CAMERA_DEFAULT_WIDTH,
                                                int height = FA_CAMERA_DEFAULT_HEIGHT, CameraMode* chosen = nullptr);

// Play a video file (.mp4, .avi, .wmv ...) with the same Media Foundation reader and
// RGB conversion that the camera uses. `time_ms` of a frame is its time in the file,
// counted from the first frame. Returns nullptr when the file cannot be opened.
std::unique_ptr<FrameSource> open_video_file_source(const std::string& path);

// Play the picture files (.jpg .jpeg .png .bmp) of `folder` in file-name order at
// `fps` frames per second (frame k gets the time k * 1000 / fps).
std::unique_ptr<FrameSource> open_sequence_source(const std::string& folder, double fps);

}  // namespace fa

#endif  // FA_FRAME_SOURCE_H
