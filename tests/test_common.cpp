// test_common.cpp - checks the foundation layer: image operations, picture files
// and loading a model with each of the two network backends.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fs_compat.h"
#include <fstream>

#include "draw_text.h"
#include "frame_source.h"
#include "video_writer.h"
#include "image_io.h"
#include "image_ops.h"
#include "inference.h"
#include "profile.h"
#include "scene.h"
#include "cpu_only_check.h"
#include "timer.h"

static int g_failures = 0;
#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; } \
        else std::printf("ok:   %s\n", msg);                     \
    } while (0)

int main() {
    using namespace fa;
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // show each line immediately, even if the program crashes

    // --- a test picture: a horizontal gradient, 1280 x 1080 ---
    Image big;
    big.create(1280, 1080, 3);
    for (int y = 0; y < big.height; ++y)
        for (int x = 0; x < big.width; ++x) {
            uint8_t* p = big.row(y) + x * 3;
            p[0] = static_cast<uint8_t>(x * 255 / (big.width - 1));  // B
            p[1] = static_cast<uint8_t>(y * 255 / (big.height - 1)); // G
            p[2] = 128;                                              // R
        }

    // --- resize: the gradient must stay a gradient ---
    Image small;
    resize_bilinear(big, small, 640, 540);
    CHECK(small.width == 640 && small.height == 540, "resize gives the requested size");
    CHECK(std::abs(small.row(270)[320 * 3 + 0] - 127) <= 2, "resize keeps the blue gradient (middle ~127)");

    // --- letterbox 1280x1080 -> 640x640 ---
    Image boxed;
    float scale; int pad_x, pad_y;
    letterbox(big, 640, boxed, scale, pad_x, pad_y);
    CHECK(boxed.width == 640 && boxed.height == 640, "letterbox output is 640x640");
    CHECK(std::fabs(scale - 0.5f) < 1e-6f && pad_x == 0 && pad_y == 50, "letterbox scale 0.5, pad_y 50");

    // letterbox_fit: the smallest rectangle (both sides a multiple of 32) instead of the square
    {
        struct Case { int w, h, expect_w, expect_h; const char* label; };
        const Case cases[] = {{1920, 1080, 640, 384, "1920x1080 -> 640x384"}, {1280, 1080, 640, 544, "1280x1080 -> 640x544"}, {1280, 720, 640, 384, "1280x720 -> 640x384"},
                              {640, 480, 640, 480, "640x480 -> 640x480"}, {640, 640, 640, 640, "640x640 -> 640x640 (a square stays a square)"},
                              {480, 640, 480, 640, "480x640 (portrait) -> 480x640"}, {2560, 1440, 640, 384, "2560x1440 -> 640x384"}};
        bool sizes_ok = true;
        for (const Case& c : cases) {
            Image src, fitted;
            src.create(c.w, c.h, 3);
            float s;
            int px, py;
            letterbox_fit(src, 640, 32, fitted, s, px, py);
            const bool ok = fitted.width == c.expect_w && fitted.height == c.expect_h && fitted.width % 32 == 0 && fitted.height % 32 == 0;
            if (!ok) std::printf("      letterbox_fit %s: got %dx%d\n", c.label, fitted.width, fitted.height);
            sizes_ok = sizes_ok && ok;
        }
        CHECK(sizes_ok, "letterbox_fit gives the smallest multiple-of-32 rectangle for a long side of 640 (1920x1080 -> 640x384, 1280x1080 -> 640x544, a square stays 640x640)");
        // a marker pixel must land where scale and padding say, and map back to where it was
        Image src, fitted;
        src.create(1920, 1080, 3);
        std::memset(src.data.data(), 0, src.data.size());
        for (int y = 540; y < 560; ++y)
            for (int x = 960; x < 980; ++x) { src.row(y)[x * 3 + 0] = 255; src.row(y)[x * 3 + 1] = 255; src.row(y)[x * 3 + 2] = 255; }
        float s;
        int px, py;
        letterbox_fit(src, 640, 32, fitted, s, px, py);
        const int tx = static_cast<int>((970 * s) + px), ty = static_cast<int>((550 * s) + py);  // where the marker's middle should be
        CHECK(std::fabs(s - 640.0f / 1920.0f) < 1e-6f && px == 0 && py == 12 && fitted.row(ty)[tx * 3 + 1] > 128 && fitted.row(10)[100 * 3 + 1] == 114,
              "letterbox_fit: scale 1/3, padding 12 rows above and below, a marker lands where scale and padding say, the padding is gray (114)");
    }

    // --- resize speed (the 1280x1080 -> 640x640 step of every frame) ---
    TimingStats t;
    for (int i = 0; i < 50; ++i) {
        double a = now_ms();
        letterbox(big, 640, boxed, scale, pad_x, pad_y);
        t.add(now_ms() - a);
    }
    std::printf("      letterbox 1280x1080 -> 640x640: median %.2f ms\n", t.median());

    // --- similarity transform: rotate 5 points by 30 degrees, scale 2, shift (10, 20) ---
    Point from[5] = {{10, 10}, {50, 12}, {30, 30}, {15, 50}, {45, 52}}, to[5];
    const float ang = 30.0f * 3.14159265f / 180.0f, s = 2.0f;
    for (int i = 0; i < 5; ++i) {
        to[i].x = s * (std::cos(ang) * from[i].x - std::sin(ang) * from[i].y) + 10;
        to[i].y = s * (std::sin(ang) * from[i].x + std::cos(ang) * from[i].y) + 20;
    }
    float m[6], inv[6];
    estimate_similarity(from, to, m);
    CHECK(std::fabs(m[0] - s * std::cos(ang)) < 1e-3f && std::fabs(m[3] - s * std::sin(ang)) < 1e-3f,
          "estimate_similarity recovers rotation and scale");
    invert_similarity(m, inv);
    float x = m[0] * 7 + m[1] * -3 + m[2], y = m[3] * 7 + m[4] * -3 + m[5];  // (7,-3) forward ...
    float bx = inv[0] * x + inv[1] * y + inv[2], by = inv[3] * x + inv[4] * y + inv[5];  // ... and back
    CHECK(std::fabs(bx - 7) < 1e-3f && std::fabs(by + 3) < 1e-3f, "invert_similarity undoes the transform");

    // --- picture files: write a PNG, read it back ---
    CHECK(save_image("build\\test_common.png", small), "save_image writes a PNG");
    Image loaded;
    CHECK(load_image("build\\test_common.png", loaded) && loaded.width == 640 && loaded.height == 540 &&
              loaded.data == small.data,
          "load_image reads back exactly the same pixels");

    // --- recorded frames: a folder of pictures played as a video ---
    {
        fa::fs::remove_all("build/seq");
        fa::fs::create_directories("build/seq");
        bool wrote = true;
        for (int i = 0; i < 3; ++i) {
            Image f = small;
            f.row(0)[0] = static_cast<uint8_t>(10 * (i + 1));  // marks the frame number
            char name[64];
            std::snprintf(name, sizeof(name), "build/seq/frame_%04d.png", i);
            wrote = wrote && save_image(name, f);
        }
        auto source = open_sequence_source("build/seq", 10.0);
        CHECK(wrote && source != nullptr, "open_sequence_source opens a folder of pictures");
        if (source) {
            Image f;
            double t = -1;
            bool ok = true;
            double times[3] = {-1, -1, -1};
            for (int i = 0; i < 3; ++i) {
                ok = ok && source->next(f, t) && f.width == 640 && f.row(0)[0] == 10 * (i + 1);
                times[i] = t;
            }
            CHECK(ok && times[0] == 0.0 && times[1] == 100.0 && times[2] == 200.0, "frames come in file order with times 0, 100, 200 ms at 10 fps");
            CHECK(!source->next(f, t), "the source reports the end of the recording");
        }
        CHECK(open_sequence_source("build/no_such_folder", 10.0) == nullptr, "a missing folder gives no source");
    }

    // --- video file: write an MP4, read it back (size, frame count, times, right way up) ---
    {
        // Every frame: red top half, blue bottom half (to see a flipped picture), and a white bar
        // whose position tells the frame number (to see frames in the right order).
        const int w = 320, h = 240, count = 20;
        VideoWriter writer;
        const bool opened = writer.open("build/test_video.mp4", w, h, 10.0);
        CHECK(opened, "VideoWriter opens an .mp4 file");
        bool written = opened;
        for (int i = 0; opened && i < count; ++i) {
            Image f;
            f.create(w, h, 3);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t* p = f.row(y) + x * 3;
                    const bool bar = x >= 10 + i * 12 && x < 22 + i * 12 && y > 100 && y < 140;
                    p[0] = bar ? 255 : (y < h / 2 ? 0 : 255);    // B
                    p[1] = bar ? 255 : 0;                         // G
                    p[2] = bar ? 255 : (y < h / 2 ? 255 : 0);    // R
                }
            written = written && writer.write(f);
        }
        CHECK(written && writer.close() && writer.frames_written() == count, "20 frames written and the file finished");

        auto video = open_video_file_source("build/test_video.mp4");
        CHECK(video != nullptr, "open_video_file_source opens the written file");
        if (video) {
            Image f;
            double t = 0, first_time = -1, last_time = 0;
            int frames = 0;
            bool right_way_up = true, size_ok = true, bars_in_order = true;
            int previous_bar = -1;
            while (video->next(f, t)) {
                if (first_time < 0) first_time = t;
                last_time = t;
                size_ok = size_ok && f.width == w && f.height == h;
                // top half must be red-ish, bottom half blue-ish (a flipped picture would be the opposite)
                const uint8_t* top = f.row(40) + 5 * 3;
                const uint8_t* bottom = f.row(200) + 5 * 3;
                right_way_up = right_way_up && top[2] > 150 && top[0] < 100 && bottom[0] > 150 && bottom[2] < 100;
                // find the white bar in row 120: its x position grows with the frame number
                int bar = -1;
                for (int x = 0; x < w; ++x) { const uint8_t* p = f.row(120) + x * 3; if (p[0] > 200 && p[1] > 200 && p[2] > 200) { bar = x; break; } }
                bars_in_order = bars_in_order && bar > previous_bar;
                previous_bar = bar;
                ++frames;
            }
            std::printf("      read back %d frames, first at %.0f ms, last at %.0f ms\n", frames, first_time, last_time);
            CHECK(frames == count && size_ok, "all 20 frames come back at the right size");
            CHECK(right_way_up, "the pictures come back the right way up (red on top, blue below)");
            CHECK(bars_in_order, "the frames come back in the order they were written");
            CHECK(first_time == 0.0 && std::fabs(last_time - 1900.0) < 60.0, "the time stamps are 0 ms ... about 1900 ms at 10 fps");
        }
        CHECK(open_video_file_source("build/no_such_video.mp4") == nullptr, "a missing video file gives no source");
    }

    // --- text drawing writes visible pixels ---
    {
        Image f;
        f.create(200, 60, 3);
        draw_text(f, 10, 10, "ID 7", 0, 255, 0, 22);
        int green = 0;
        for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x) { const uint8_t* p = f.row(y) + x * 3; if (p[1] > 200 && p[0] < 60 && p[2] < 60) ++green; }
        CHECK(green > 30, "draw_text puts green letters into the picture");
    }

    // --- which camera mode suits a requested picture size (no camera is opened: made-up mode lists) ---
    {
        // a typical webcam: three sizes as MJPG (fast), the same as uncompressed YUY2 (slow at the large sizes)
        std::vector<CameraMode> webcam;
        webcam.push_back(CameraMode(640, 480, 30, "MJPG"));    // 0
        webcam.push_back(CameraMode(1280, 720, 30, "MJPG"));   // 1
        webcam.push_back(CameraMode(1920, 1080, 30, "MJPG"));  // 2
        webcam.push_back(CameraMode(640, 480, 30, "YUY2"));    // 3
        webcam.push_back(CameraMode(1280, 720, 10, "YUY2"));   // 4
        webcam.push_back(CameraMode(1920, 1080, 5, "YUY2"));   // 5
        CHECK(pick_camera_mode(webcam, 1920, 1080) == 2, "camera mode: 1920x1080 gives the 30 fps mode, not the 5 fps one");
        CHECK(pick_camera_mode(webcam, 0, 0) == 2 && FA_CAMERA_DEFAULT_WIDTH == 1920 && FA_CAMERA_DEFAULT_HEIGHT == 1080,
              "camera mode: no wish at all means the default size 1920x1080");
        CHECK(pick_camera_mode(webcam, 1280, 720) == 1, "camera mode: 1280x720 gives the 30 fps mode, not the 10 fps one");
        CHECK(pick_camera_mode(webcam, 640, 480) == 0, "camera mode: a tie goes to the earlier entry (MJPG before YUY2 at 640x480)");
        CHECK(pick_camera_mode(webcam, 1280, 1080) == 2, "camera mode: 1280x1080 (not offered) gives the closest, 1920x1080");

        std::vector<CameraMode> small;  // the camera has nothing as large as the wish
        small.push_back(CameraMode(640, 480, 30, "YUY2"));
        small.push_back(CameraMode(1280, 720, 30, "MJPG"));
        CHECK(pick_camera_mode(small, 1920, 1080) == 1, "camera mode: no 1920x1080 on offer gives the largest, 1280x720");

        std::vector<CameraMode> uhd;  // a 4K camera: the larger picture is NOT taken just because it is larger
        uhd.push_back(CameraMode(3840, 2160, 30, "MJPG"));
        uhd.push_back(CameraMode(1920, 1080, 30, "MJPG"));
        CHECK(pick_camera_mode(uhd, 1920, 1080) == 1, "camera mode: with 4K on offer 1920x1080 is still 1920x1080");
        CHECK(pick_camera_mode(uhd, 3840, 2160) == 0, "camera mode: 4K can be asked for");

        std::vector<CameraMode> four_three;  // a camera with 4:3 pictures only: the closest SHAPE and size wins
        four_three.push_back(CameraMode(640, 480, 30, "MJPG"));
        four_three.push_back(CameraMode(1024, 768, 30, "MJPG"));
        four_three.push_back(CameraMode(1600, 1200, 30, "MJPG"));
        CHECK(pick_camera_mode(four_three, 1920, 1080) == 2, "camera mode: a 4:3 camera asked for 1920x1080 gives 1600x1200");
        // "closest" is the RELATIVE difference: 800 is 1.25 times 640 but 1024 is 1.28 times 800
        CHECK(pick_camera_mode(four_three, 800, 600) == 0, "camera mode: 800x600 gives 640x480 (relatively closer than 1024x768)");
        CHECK(pick_camera_mode(four_three, 960, 720) == 1, "camera mode: 960x720 gives 1024x768");

        std::vector<CameraMode> slow;  // everything is slow: slow modes are used after all
        slow.push_back(CameraMode(640, 480, 10, "YUY2"));
        slow.push_back(CameraMode(1920, 1080, 5, "YUY2"));
        CHECK(pick_camera_mode(slow, 1920, 1080) == 1, "camera mode: when every mode is slow the closest size is used");

        std::vector<CameraMode> coded;  // a mode that needs a video decoder is avoided when there is another
        coded.push_back(CameraMode(1920, 1080, 30, "H264"));
        coded.push_back(CameraMode(1280, 720, 30, "MJPG"));
        CHECK(pick_camera_mode(coded, 1920, 1080) == 1, "camera mode: H264 is avoided when another format is on offer");
        coded.erase(coded.begin() + 1);
        CHECK(pick_camera_mode(coded, 1920, 1080) == 0, "camera mode: H264 is used when it is all there is");

        std::vector<CameraMode> same;  // same size twice: the faster one
        same.push_back(CameraMode(1280, 720, 24, "NV12"));
        same.push_back(CameraMode(1280, 720, 30, "MJPG"));
        CHECK(pick_camera_mode(same, 1280, 720) == 1, "camera mode: the same size twice gives the faster mode");

        std::vector<CameraMode> unreported;  // a frame rate of 0 means "not reported" and counts as fast enough
        unreported.push_back(CameraMode(1920, 1080, 0, "MJPG"));
        unreported.push_back(CameraMode(1280, 720, 30, "MJPG"));
        CHECK(pick_camera_mode(unreported, 1920, 1080) == 0, "camera mode: an unreported frame rate is not held against a mode");

        std::vector<CameraMode> none;
        CHECK(pick_camera_mode(none, 1920, 1080) == -1, "camera mode: a camera without modes gives -1");
        none.push_back(CameraMode(0, 0, 30, "MJPG"));
        CHECK(pick_camera_mode(none, 1920, 1080) == -1, "camera mode: a mode without a size is not usable");
    }

    // --- the camera look of the generated footage (tools/gen_video --camera-look): each effect is really there ---
    {
        Actor actor;  // a textured sprite that moves fast to the right (400 pixels per second)
        actor.label = "t";
        actor.sprite.create(280, 340, 3);
        for (int y = 0; y < 340; ++y)
            for (int x = 0; x < 280; ++x)
                for (int c = 0; c < 3; ++c)
                    actor.sprite.row(y)[x * 3 + c] = static_cast<uint8_t>(128 + 90 * std::sin(x * 0.45f + c) * std::sin(y * 0.4f));
        actor.face_in_sprite = Rect(90, 80, 100, 120);
        actor.path.push_back(PathPoint(0, 300, 400, 120, 0));
        actor.path.push_back(PathPoint(1000, 700, 400, 120, 0));
        Actor still = actor;  // the same, standing still
        still.path.clear();
        still.path.push_back(PathPoint(0, 500, 400, 120, 0));
        still.path.push_back(PathPoint(1000, 500, 400, 120, 0));

        auto quiet = [] {  // a look with every effect switched off
            CameraLook look;
            look.clutter = false; look.noise_sigma = 0; look.blur_sigma = 0; look.motion_blur = 0;
            look.vignette = 0; look.light_slope = 0; look.exposure_drift = 0; look.warm = 0;
            return look;
        };
        auto mean_of = [](const Image& f, int x0, int y0, int x1, int y1, int ch) {
            double s = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) s += f.row(y)[x * 3 + ch];
            return s / ((x1 - x0) * (y1 - y0));
        };
        auto sharpness = [](const Image& f, int x0, int y0, int x1, int y1) {  // total horizontal change: smaller = softer
            double s = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x + 1 < x1; ++x) s += std::abs(static_cast<int>(f.row(y)[(x + 1) * 3 + 1]) - f.row(y)[x * 3 + 1]);
            return s;
        };
        auto same_image = [](const Image& a, const Image& b) {
            if (a.width != b.width || a.height != b.height) return false;
            for (int y = 0; y < a.height; ++y)
                if (std::memcmp(a.row(y), b.row(y), static_cast<size_t>(a.width) * 3) != 0) return false;
            return true;
        };

        const int W = 960, H = 540;
        Image plain, shown, other;
        std::vector<TruthFace> truth_plain, truth_look;
        std::vector<Actor> one(1, actor);
        CameraLook full;  // the default look: everything on
        render_frame(W, H, 500, 3, one, plain, truth_plain);
        render_frame(W, H, 500, 3, one, shown, truth_look, &full);
        CHECK(truth_plain.size() == 1 && truth_look.size() == 1 && truth_plain[0].visible && truth_look[0].visible &&
                  truth_plain[0].box.x == truth_look[0].box.x && truth_plain[0].box.y == truth_look[0].box.y &&
                  truth_plain[0].box.w == truth_look[0].box.w && truth_plain[0].box.h == truth_look[0].box.h,
              "camera look: the ground-truth face box is exactly the same with and without the look");
        render_frame(W, H, 500, 3, one, other, truth_look, &full);
        CHECK(same_image(shown, other), "camera look: the same frame index gives exactly the same picture (deterministic)");
        render_frame(W, H, 500, 4, one, other, truth_look, &full);
        CHECK(!same_image(shown, other), "camera look: another frame index gives another noise");
        CameraLook other_room = full;
        other_room.seed = 99;
        render_frame(W, H, 500, 3, one, other, truth_look, &other_room);
        CHECK(!same_image(shown, other), "camera look: another seed gives another room");

        // noise: two frames of the same empty background differ by about the noise (without the look: only the plain +-3 levels)
        {
            CameraLook noisy = quiet();
            noisy.noise_sigma = 6.0f;
            Image n1, n2, p1, p2;
            std::vector<TruthFace> t;
            render_frame(W, H, 0, 1, std::vector<Actor>(), n1, t, &noisy);
            render_frame(W, H, 0, 2, std::vector<Actor>(), n2, t, &noisy);
            render_frame(W, H, 0, 1, std::vector<Actor>(), p1, t);
            render_frame(W, H, 0, 2, std::vector<Actor>(), p2, t);
            auto diff_std = [](const Image& a, const Image& b) {
                double s = 0, s2 = 0;
                int n = 0;
                for (int y = 20; y < 50; ++y)
                    for (int x = 50; x < 150; ++x, ++n) {
                        const double d = static_cast<double>(a.row(y)[x * 3 + 1]) - b.row(y)[x * 3 + 1];
                        s += d; s2 += d * d;
                    }
                return std::sqrt(s2 / n - (s / n) * (s / n));
            };
            const double with_look = diff_std(n1, n2), without = diff_std(p1, p2);
            std::printf("      camera look, noise sigma 6: picture-to-picture difference %.1f levels (plain scene: %.1f)\n", with_look, without);
            CHECK(with_look > 6.0 && without < 4.0, "camera look: the sensor noise is really there (a plain frame differs by about 2.4 levels, a noisy one by about 9)");
        }
        // light: the left darker than the right, darker corners, a warm cast
        {
            CameraLook slope = quiet();
            slope.light_slope = 0.2f;
            Image s, v, w, p;
            std::vector<TruthFace> t;
            render_frame(W, H, 0, 1, std::vector<Actor>(), s, t, &slope);
            render_frame(W, H, 0, 1, std::vector<Actor>(), p, t);
            CHECK(mean_of(s, 0, 250, 60, 290, 1) < 0.9 * mean_of(s, 900, 250, 960, 290, 1) && mean_of(p, 0, 250, 60, 290, 1) > 0.97 * mean_of(p, 900, 250, 960, 290, 1),
                  "camera look: with uneven light the left edge is darker than the right (the plain scene is even)");
            CameraLook vig = quiet();
            vig.vignette = 0.25f;
            render_frame(W, H, 0, 1, std::vector<Actor>(), v, t, &vig);
            CHECK(mean_of(v, 0, 0, 40, 40, 1) < 0.95 * mean_of(v, 460, 0, 500, 40, 1) && mean_of(p, 0, 0, 40, 40, 1) > 0.97 * mean_of(p, 460, 0, 500, 40, 1),
                  "camera look: the corners are darker than the middle of the top edge (vignette)");
            CameraLook warm = quiet();
            warm.warm = 0.06f;
            render_frame(W, H, 0, 1, std::vector<Actor>(), w, t, &warm);
            const double warm_ratio = mean_of(w, 100, 200, 400, 300, 2) / mean_of(w, 100, 200, 400, 300, 0);
            const double plain_ratio = mean_of(p, 100, 200, 400, 300, 2) / mean_of(p, 100, 200, 400, 300, 0);
            CHECK(warm_ratio > 1.08 * plain_ratio, "camera look: the colour cast is warm (more red against blue than in the plain scene)");
        }
        // lens softness and motion blur: the pasted person gets softer; a person who stands still gets no motion blur
        {
            CameraLook soft = quiet();
            soft.blur_sigma = 0.9f;
            CameraLook moving = quiet();
            moving.motion_blur = 0.4f;
            std::vector<Actor> fast(1, actor), idle(1, still);
            std::vector<TruthFace> t;
            Image base_fast, soft_fast, motion_fast, base_still, motion_still;
            render_frame(W, H, 300, 5, fast, base_fast, t, &quiet());
            render_frame(W, H, 300, 5, fast, soft_fast, t, &soft);
            render_frame(W, H, 300, 5, fast, motion_fast, t, &moving);
            render_frame(W, H, 300, 5, idle, base_still, t, &quiet());
            render_frame(W, H, 300, 5, idle, motion_still, t, &moving);
            const double s0 = sharpness(base_fast, 200, 330, 700, 480), s1 = sharpness(soft_fast, 200, 330, 700, 480), s2 = sharpness(motion_fast, 200, 330, 700, 480);
            std::printf("      camera look, detail of the moving person: sharp %.0f, lens softness %.0f, motion blur %.0f\n", s0, s1, s2);
            CHECK(s1 < 0.9 * s0, "camera look: the lens softness reduces the fine detail of the picture");
            CHECK(s2 < 0.7 * s0, "camera look: a person who moves fast is smeared (motion blur)");
            CHECK(same_image(base_still, motion_still), "camera look: a person who stands still is not smeared");
        }
        // the room: strong edges (furniture, window, door, notice board) that the plain scene does not have
        {
            CameraLook room = quiet();
            room.clutter = true;
            Image r, p;
            std::vector<TruthFace> t;
            render_frame(W, H, 0, 1, std::vector<Actor>(), r, t, &room);
            render_frame(W, H, 0, 1, std::vector<Actor>(), p, t);
            auto strong_edges = [](const Image& f) {
                int n = 0;
                for (int y = 0; y < f.height; ++y)
                    for (int x = 0; x + 1 < f.width; ++x)
                        if (std::abs(static_cast<int>(f.row(y)[(x + 1) * 3 + 1]) - f.row(y)[x * 3 + 1]) > 40) ++n;
                return n;
            };
            std::printf("      camera look, strong edges: room %d, plain scene %d\n", strong_edges(r), strong_edges(p));
            CHECK(strong_edges(r) > 500 && strong_edges(p) == 0, "camera look: the room has strong edges (furniture, window, door, notice board), the plain scene none");
            Image r2;
            render_frame(W, H, 0, 2, std::vector<Actor>(), r2, t, &room);
            double d = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W * 3; ++x) d += std::abs(static_cast<int>(r.row(y)[x]) - r2.row(y)[x]);
            CHECK(d / (static_cast<double>(H) * W * 3) < 4.0, "camera look: the room is the same in every frame (only the small background noise differs)");
        }
    }

    // --- model profile files: checks of numbers, byte order mark, names with spaces ---
    {
        auto write = [](const char* path, const char* text, bool bom) {
            std::ofstream f(path, std::ios::binary);
            if (bom) f << "\xEF\xBB\xBF";
            f << text;
        };
        ModelProfile p;
        write("build/p_ok.ini", "detector.type = yunet\ndetector.outputs = out0, out1 ,out2\nrecognizer.dim = 128\n", true);
        CHECK(load_profile("build/p_ok.ini", ".", p), "a profile that starts with a byte order mark is read");
        CHECK(p.detector_outputs.size() == 3 && p.detector_outputs[1] == "out1" && p.detector_outputs[2] == "out2",
              "output names with spaces around the commas are trimmed");
        // Each case uses a FRESH profile (a value left over from an earlier case must not decide the
        // result) and is paired with the same text corrected, which must be accepted: that shows the
        // rejection comes from the one wrong value and not from something else.
        auto loads = [&](const char* text) {
            write("build/p_case.ini", text, false);
            ModelProfile fresh;
            return load_profile("build/p_case.ini", ".", fresh);
        };
        CHECK(!loads("detector.model_small = models/x.onnx\ndetector.small_size = 0\n") &&
                  loads("detector.model_small = models/x.onnx\ndetector.small_size = 320\n"),
              "a small_size of 0 is rejected (320 is accepted)");
        CHECK(!loads("detector.model_small = models/x.onnx\ndetector.small_size = 100\n"),
              "a small_size that is not a multiple of 32 is rejected");
        CHECK(!loads("runtime.threads = abc\n") && loads("runtime.threads = 4\n"),
              "a non-numeric thread count is rejected (4 is accepted)");
        CHECK(!loads("detector.backend = onxx\n") && loads("detector.backend = ncnn\n") && loads("detector.backend = onnx\n"),
              "a misspelled backend is rejected ('ncnn' and 'onnx' are accepted)");
        CHECK(!loads("recognizer.dim = 0\n") && loads("recognizer.dim = 512\n"), "a feature length of 0 is rejected (512 is accepted)");
        const std::string nine = "detector.outputs = 65,85,105,66,86,106,67,87,107\n";
        CHECK(!loads("detector.layout = chw\ndetector.type = scrfd\n") && loads(("detector.layout = chw\ndetector.type = scrfd\n" + nine).c_str()),
              "detector.layout = chw needs nine detector.outputs (nine are accepted)");
        CHECK(!loads(("detector.layout = chw\ndetector.type = yunet\n" + nine).c_str()) && loads(("detector.layout = chw\ndetector.type = scrfd\n" + nine).c_str()),
              "detector.layout = chw is only for SCRFD (YuNet is rejected)");
        CHECK(!loads("detector.layout = round\n") && loads("detector.layout = flat\n"), "a misspelled detector.layout is rejected ('flat' is accepted)");
    }

    // --- the two network backends can load their models ---
    auto yunet = create_onnx_net("models/permissive/yunet.onnx", 2);
    CHECK(yunet != nullptr, "ONNX Runtime loads yunet.onnx");
    auto scrfd = create_ncnn_net("models/dev_noncommercial/det.param", "models/dev_noncommercial/det.bin", "in0",
                                 {"out0", "out1", "out2", "out3", "out4", "out5", "out6", "out7", "out8"}, 2);
    CHECK(scrfd != nullptr, "ncnn loads the converted SCRFD detector");

    if (yunet) {
        std::vector<Tensor> out;
        InputPrep prep;  // YuNet takes BGR 0..255 without normalization
        CHECK(yunet->run(boxed, prep, out) && out.size() == 12 && out[0].data.size() == 6400,
              "yunet runs and returns 12 outputs (6400 anchors at stride 8)");
    }
    if (scrfd) {
        std::vector<Tensor> out;
        InputPrep prep;
        prep.mean[0] = prep.mean[1] = prep.mean[2] = 127.5f;
        prep.norm[0] = prep.norm[1] = prep.norm[2] = 1.0f / 128.0f;
        prep.swap_rb = true;
        CHECK(scrfd->run(boxed, prep, out) && out.size() == 9 && out[0].data.size() == 12800,
              "ncnn SCRFD runs and returns 9 outputs (12800 anchors at stride 8)");
    }

    // --- SCRFD with scaling upsampling layers (models/dev_noncommercial/det_dyn.param): any input size, same numbers ---
    {
        const std::vector<std::string> raw_names = {"65", "85", "105", "66", "86", "106", "67", "87", "107"};  // the last convolutions of the strides 8, 16, 32
        auto dyn = create_ncnn_net("models/dev_noncommercial/det_dyn.param", "models/dev_noncommercial/det_dyn.bin", "in0", raw_names, 2);
        CHECK(dyn != nullptr, "ncnn loads the SCRFD model whose upsampling layers scale instead of using fixed sizes (det_dyn)");
        if (dyn && scrfd) {
            InputPrep prep;
            prep.mean[0] = prep.mean[1] = prep.mean[2] = 127.5f;
            prep.norm[0] = prep.norm[1] = prep.norm[2] = 1.0f / 128.0f;
            prep.swap_rb = true;
            std::vector<Tensor> flat, raw;
            const bool ran = scrfd->run(boxed, prep, flat) && dyn->run(boxed, prep, raw);
            double worst = 0;
            for (int level = 0; ran && level < 3; ++level) {
                const int cells = (640 / (8 << level)) * (640 / (8 << level));
                for (int kind = 0; kind < 3; ++kind) {  // 0 = score (1 number per anchor), 1 = box (4), 2 = landmarks (10)
                    const int per = kind == 0 ? 1 : kind == 1 ? 4 : 10;
                    const std::vector<float>& f = flat[kind * 3 + level].data;
                    const std::vector<float>& r = raw[kind * 3 + level].data;
                    for (int cell = 0; cell < cells; ++cell)
                        for (int a = 0; a < 2; ++a)
                            for (int k = 0; k < per; ++k) {
                                float v = r[static_cast<size_t>(a * per + k) * cells + cell];  // channel a * per + k of the raw output
                                if (kind == 0) v = 1.0f / (1.0f + std::exp(-v));               // the flat score is after the sigmoid
                                worst = std::max(worst, static_cast<double>(std::fabs(f[static_cast<size_t>(cell * 2 + a) * per + k] - v)));
                            }
                }
            }
            std::printf("      det_dyn against det at 640x640: largest difference over all scores, boxes and landmarks %.2g\n", worst);
            CHECK(ran && worst < 1e-4, "at 640x640 the raw outputs of det_dyn are exactly the flat outputs of the original model (scores after the sigmoid, boxes, landmarks)");

            Image rect;  // 640x384, the shape of a 1920x1080 picture shrunk to a long side of 640 and rounded up to a multiple of 32
            rect.create(640, 384, 3);
            for (int y = 0; y < 384; ++y) std::memcpy(rect.row(y), boxed.row(y + 128), 640 * 3);
            std::vector<Tensor> r2;
            const bool ok = dyn->run(rect, prep, r2) && r2.size() == 9;
            CHECK(ok && r2[0].data.size() == 2 * 48 * 80 && r2[1].data.size() == 2 * 24 * 40 && r2[2].data.size() == 2 * 12 * 20 &&
                      r2[3].data.size() == 8 * 48 * 80 && r2[6].data.size() == 20 * 48 * 80 && r2[8].data.size() == 20 * 12 * 20,
                  "det_dyn runs on a 640x384 picture and gives 48x80, 24x40 and 12x20 cells at the strides 8, 16 and 32 (the square model cannot)");
        }
    }

    // --- everything runs on the CPU: no GPU anywhere ---
    CHECK(yunet && yunet->runs_on_cpu() && scrfd && scrfd->runs_on_cpu(), "both networks (ONNX Runtime and ncnn) report that they run on the CPU");
    {
        const std::vector<std::string> providers = onnxruntime_providers();
        std::string list;
        for (const std::string& p : providers) list += (list.empty() ? "" : ", ") + p;
        std::printf("      ONNX Runtime offers these execution providers: %s\n", list.c_str());
        // The CPU provider, and at most the Azure provider: that one sends a network's input to a remote service and only
        // works if a program appends it to a session (nothing here does); it is no GPU. Any other provider (CUDA, TensorRT,
        // DirectML, OpenVINO, ...) would be an accelerator and fails this check.
        bool cpu = false, only_allowed = true;
        for (const std::string& p : providers) {
            if (p == "CPUExecutionProvider") cpu = true;
            else if (p != "AzureExecutionProvider") only_allowed = false;
        }
        CHECK(cpu && only_allowed, "the ONNX Runtime library in use offers no GPU or accelerator provider (CPU, and the never-used remote Azure provider, only)");
        const std::vector<std::string> gpu = loaded_gpu_libraries();
        std::string loaded;
        for (const std::string& g : gpu) loaded += " " + g;
        const std::vector<std::string> graphics = loaded_graphics_libraries();
        std::string info;
        for (const std::string& g : graphics) info += " " + g;
        std::printf("      GPU libraries loaded in this process:%s; Direct3D 11 / DXGI libraries (informational):%s\n", gpu.empty() ? " none" : loaded.c_str(), graphics.empty() ? " none" : info.c_str());
        CHECK(gpu.empty(), "after both networks have run, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
