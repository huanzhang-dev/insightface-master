// camera_check.cpp - list the cameras Windows reports, show what a camera offers, and optionally grab frames.
//
//   camera_check                              only LISTS the camera names (no camera is opened)
//   camera_check --modes [index] [WxH]        lists the picture formats camera `index` (default 0) offers: size, frame
//                                             rate and pixel format, and says which one would be switched on for the
//                                             wanted size WxH (default 1920x1080). The camera is asked, but its
//                                             picture stream is not started.
//   camera_check --capture N [index] [WxH]    opens camera `index` (default 0) asking for about WxH (default 1920x1080),
//                                             says which camera mode was switched on, reads N frames, prints their
//                                             size and the speed, and writes the last frame to build\camera_check.png
//
// The tests never open a camera. This tool opens one ONLY when you pass --modes or --capture.
// The camera does not have to offer the wanted size: the closest mode is used (pick_camera_mode in frame_source.h).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "frame_source.h"
#include "image_io.h"
#include "timer.h"

namespace {
// "1920x1080" -> 1920, 1080
bool parse_size(const char* text, int& width, int& height) {
    return std::sscanf(text, "%dx%d", &width, &height) == 2 && width > 0 && height > 0;
}

// The optional arguments after --modes / --capture N: a camera number and/or a size WxH, in any order.
void read_options(int argc, char** argv, int first, int& index, int& width, int& height) {
    for (int i = first; i < argc; ++i) {
        int w = 0, h = 0;
        if (parse_size(argv[i], w, h)) { width = w; height = h; }
        else index = std::atoi(argv[i]);
    }
}
}  // namespace

int main(int argc, char** argv) {
    using namespace fa;
    const auto names = list_cameras();
    std::printf("%zu camera(s) reported by Windows:\n", names.size());
    for (size_t i = 0; i < names.size(); ++i) std::printf("  [%zu] %s\n", i, names[i].c_str());

    int index = 0, width = FA_CAMERA_DEFAULT_WIDTH, height = FA_CAMERA_DEFAULT_HEIGHT;

    if (argc >= 2 && !std::strcmp(argv[1], "--modes")) {
        read_options(argc, argv, 2, index, width, height);
        const std::vector<CameraMode> modes = list_camera_modes(index);
        if (modes.empty()) { std::printf("camera %d offers no picture format (or cannot be opened)\n", index); return 1; }
        const int pick = pick_camera_mode(modes, width, height);
        std::printf("camera %d offers %zu picture formats:\n", index, modes.size());
        for (size_t i = 0; i < modes.size(); ++i)
            std::printf("  %5dx%-5d %6.1f fps  %-6s%s\n", modes[i].width, modes[i].height, modes[i].fps, modes[i].format.c_str(),
                        static_cast<int>(i) == pick ? "   <- chosen" : "");
        if (pick >= 0) std::printf("for a request of %dx%d the camera mode %dx%d at %.1f fps (%s) is switched on\n", width, height,
                                   modes[pick].width, modes[pick].height, modes[pick].fps, modes[pick].format.c_str());
        return 0;
    }

    if (argc >= 3 && !std::strcmp(argv[1], "--capture")) {
        const int frames = std::atoi(argv[2]);
        read_options(argc, argv, 3, index, width, height);
        CameraMode chosen;
        auto camera = open_camera_source(index, width, height, &chosen);
        if (!camera) { std::printf("cannot open camera %d\n", index); return 1; }
        int real_w = 0, real_h = 0;
        camera->frame_size(real_w, real_h);
        if (chosen.width > 0)
            std::printf("asked for %dx%d; camera mode switched on: %dx%d at %.1f fps (%s); the frames will be %dx%d\n", width, height,
                        chosen.width, chosen.height, chosen.fps, chosen.format.c_str(), real_w, real_h);
        else
            std::printf("asked for %dx%d; the camera listed no usable mode, its default is used; the frames will be %dx%d\n", width, height, real_w, real_h);
        Image frame;
        double t = 0, first = -1, last = 0;
        const double start = now_ms();
        int got = 0;
        for (int i = 0; i < frames && camera->next(frame, t); ++i) {
            if (first < 0) first = t;
            last = t;
            ++got;
        }
        const double wall = now_ms() - start;
        std::printf("got %d frames of %dx%d in %.0f ms (%.1f fps; camera clock %.1f fps)\n", got, frame.width, frame.height, wall,
                    got * 1000.0 / wall, got > 1 ? (got - 1) * 1000.0 / (last - first) : 0.0);
        if (got) save_image("build\\camera_check.png", frame);
    }
    return 0;
}
