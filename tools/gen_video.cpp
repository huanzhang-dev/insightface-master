// gen_video.cpp - generate "people walking toward the camera" video (requirement 4.2).
//
//   gen_video <output folder> [seconds=8] [fps=15] [people=2] [first portrait index=0] [registered=1] [--any-pose] [--size WxH] [--camera-look] [--bitrate K]
//
// --camera-look makes the footage look like a real camera in a real room (a SIMULATION, no camera is used): a cluttered
// room (furniture, window, door, a notice board with text-like lines), uneven light with darker corners, slow exposure
// changes, a warm colour cast, a slightly soft lens, motion blur (not in these slow walks: less than 2 pixels per frame, see scene.h), sensor noise, and a harder
// compression (--bitrate K sets the H.264 bitrate in kbit/s; default 4000 with --camera-look, 8000 without). The exact
// face boxes in truth.csv stay valid. See CameraLook in src/testdata/scene.h.
//
// By default only people with a comfortable head pose are used (see below); --any-pose uses everybody.
// --size WxH sets the picture size of the frames (default 1920x1080, the default camera size, Full HD; both
// numbers must be even, at least 64): the same scene is laid out for any camera size, for example
// --size 1280x720 or 640x480. The people walk the same way and their faces are as wide as in a
// 1280x1080 picture (scaled with the picture height, so 1920x1080 and 1280x1080 show the same faces).
// With many people side by side the faces grow less (at most 1/1.95 of the room each person has), so that
// nobody's face is covered by a neighbour: 1 to 2 people reach 340 px, 3 people 328 px, 5 people 197 px.
//
// Writes into the output folder
//   frame_0000.jpg ...  the numbered frames (default 1920x1080)
//   video.mp4           the same frames as a real H.264 video file
//   truth.csv           the exact face box of every person in every frame
//   scenario.txt        one line per person: "person <label> registered|stranger"
//                       (the first `registered` people are meant to be registered, the rest are
//                       strangers; tools/check_video registers them before playing the video)
// Portraits come from data/portraits (see scripts/fetch_portraits.py). Nothing here uses a camera.
//
// It also checks the MP4: it reads the first frame back through the Media Foundation reader (the
// code path a camera uses) and compares it with the original frame and with the original turned
// upside down. If the upside-down version were closer, camera pictures would arrive flipped.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fs_compat.h"
#include <fstream>
#include <string>
#include <vector>

#include "detector.h"
#include "frame_source.h"
#include "image_io.h"
#include "scene.h"
#include "video_writer.h"

namespace fs = fa::fs;
using namespace fa;

// Mean absolute difference of two pictures of the same size (0 = identical, 255 = opposite).
static double mean_abs_diff(const Image& a, const Image& b, bool flip_b) {
    if (a.width != b.width || a.height != b.height || a.channels != b.channels) return -1;
    double sum = 0;
    for (int y = 0; y < a.height; ++y) {
        const uint8_t* p = a.row(y);
        const uint8_t* q = b.row(flip_b ? b.height - 1 - y : y);
        for (int i = 0; i < a.width * a.channels; ++i) sum += std::abs(static_cast<int>(p[i]) - q[i]);
    }
    return sum / (static_cast<double>(a.width) * a.height * a.channels);
}

int main(int argc, char** argv) {
    // the options (--any-pose, --size WxH) may stand anywhere; everything else is a positional argument
    std::vector<std::string> positional;
    bool any_pose = false, camera_look = false;
    std::string portraits = "data/portraits", portrait_file = "117th.jpg";
    int bitrate_kbps = 0;  // 0 = the default of the chosen look
    int width = FA_CAMERA_DEFAULT_WIDTH, height = FA_CAMERA_DEFAULT_HEIGHT;  // picture size of the generated frames (1920x1080)
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--portraits") && i + 1 < argc) portraits = argv[++i];
        else if (!std::strcmp(argv[i], "--portrait-file") && i + 1 < argc) portrait_file = argv[++i];
        else if (!std::strcmp(argv[i], "--any-pose")) any_pose = true;
        else if (!std::strcmp(argv[i], "--camera-look")) camera_look = true;
        else if (!std::strcmp(argv[i], "--bitrate")) {
            if (i + 1 >= argc || (bitrate_kbps = std::atoi(argv[++i])) < 500) {
                std::printf("--bitrate needs the video bitrate in kbit/s, at least 500, for example --bitrate 4000\n");
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--size")) {
            if (i + 1 >= argc || std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width < 64 || height < 64 || width % 2 != 0 || height % 2 != 0) {
                std::printf("--size needs WxH with two even numbers of at least 64, for example --size 1920x1080\n");
                return 2;
            }
        } else positional.push_back(argv[i]);
    }
    if (positional.empty()) {
        std::printf("usage: gen_video <output folder> [seconds=8] [fps=15] [people=2] [first portrait index=0] [registered=1] [--any-pose] [--size WxH] [--camera-look] [--bitrate K] [--portraits DIR] [--portrait-file NAME]\n");
        return 2;
    }
    const std::string out = positional[0];
    const double seconds = positional.size() > 1 ? std::atof(positional[1].c_str()) : 8.0;
    const double fps = positional.size() > 2 ? std::atof(positional[2].c_str()) : 15.0;
    const int people = positional.size() > 3 ? std::atoi(positional[3].c_str()) : 2;
    const int first = positional.size() > 4 ? std::atoi(positional[4].c_str()) : 0;
    const int registered = positional.size() > 5 ? std::atoi(positional[5].c_str()) : 1;
    int pose_skipped = 0;
    // --camera-look: the picture looks like a real camera in a real room (room, uneven light, lens softness, motion blur,
    // sensor noise, see CameraLook in src/testdata/scene.h) and the video is compressed harder (4000 kbit/s unless --bitrate).
    CameraLook look;
    look.frame_ms = 1000.0 / fps;
    look.seed = 7 + people * 13 + first;  // another group of people gets another room
    if (bitrate_kbps == 0) bitrate_kbps = camera_look ? 4000 : 8000;
    // The scene was laid out for 1280x1080: positions scale with the picture, face widths with its height.
    const float scale_x = width / 1280.0f, scale_y = height / 1080.0f, scale_face = std::min(scale_x, scale_y);

    ModelProfile profile;
    if (!load_profile("models/permissive.ini", ".", profile)) return 2;
    auto detector = create_detector(profile);
    if (!detector) return 1;

    // The actors: walk in from the sides / the middle, face grows, head straightens.
    std::vector<Actor> actors;
    int skipped = 0;
    for (const auto& dir : fs::directory_iterator(portraits)) {
        if (static_cast<int>(actors.size()) >= people) break;
        if (!fs::is_directory(dir.path())) continue;
        Image portrait;
        std::vector<Face> found;
        if (!load_image((dir.path() / portrait_file).string(), portrait) || !detector->detect(portrait, found) || found.empty()) continue;
        // Choose people who look at the camera: the pose check refuses faces whose head is turned or
        // nodded by more than a few degrees, so a person with a natural pose near the limit would be
        // recognized very late or never. (Run with --any-pose to include everybody.)
        Face refined;
        const bool comfortable = refine_face(*detector, portrait, found[0], refined) && refined.pose.valid &&
                                 std::fabs(refined.pose.pitch) <= 3.5f && std::fabs(refined.pose.yaw) <= 3.5f && std::fabs(refined.pose.roll) <= 4.0f;
        if (!comfortable && !any_pose) { ++pose_skipped; continue; }
        if (skipped++ < first) continue;
        Actor a = make_actor(dir.path().filename().string(), portrait, found[0].box);
        const int k = static_cast<int>(actors.size());
        // The picture of a person is 2.8 face widths wide (head, hair, shoulders), so a face is covered by the
        // neighbour's picture when the two centres are closer than about 1.9 face widths. With many people
        // side by side the faces therefore stay smaller (the width at the end is at most spacing / 1.95), and
        // the start positions leave the same room at the start, so every face stays fully visible.
        const float spacing = width / static_cast<float>(people);
        const float w1 = std::min(340 * scale_face, spacing / 1.95f);
        const float w0 = std::min(90 * scale_face, w1);
        const float offset = std::min(120.0f * scale_x, std::max(0.0f, (spacing - 1.95f * w0) / 2));
        const float start_x = width * (k + 0.5f) / people + (k % 2 ? offset : -offset);
        const float end_x = width * (k + 0.5f) / people;
        a.path = approach_path(seconds * 1000, start_x, 420 * scale_y, end_x, 520 * scale_y, w0, w1,
                               (k % 2 ? -22.0f : 22.0f), seconds * 1000 * 0.35);
        a.appear_ms = k * 600.0;  // the people arrive one after the other
        actors.push_back(a);
    }
    if (actors.empty()) { std::printf("no portraits found in data/portraits\n"); return 1; }
    std::printf("chose %zu people (skipped %d whose natural head pose is outside the comfortable range for the pose check%s)\n", actors.size(),
                pose_skipped, any_pose ? "; --any-pose is on" : "");

    fs::create_directories(out);
    {
        std::ofstream scenario(out + "\\scenario.txt");
        for (size_t k = 0; k < actors.size(); ++k)
            scenario << "person " << actors[k].label << ' ' << (static_cast<int>(k) < registered ? "registered" : "stranger") << '\n';
    }
    std::ofstream truth(out + "\\truth.csv");
    truth << "frame,time_ms,person,x,y,w,h,roll_deg,visible\n";

    VideoWriter video;
    const bool have_video = video.open(out + "\\video.mp4", width, height, fps, bitrate_kbps);
    if (!have_video) std::printf("warning: no MP4 will be written (see the message above); the jpg frames are still written\n");

    Image frame, first_frame;
    std::vector<TruthFace> faces;
    int index = 0;
    for (double t = 0; t <= seconds * 1000; t += 1000.0 / fps, ++index) {
        render_frame(width, height, t, index, actors, frame, faces, camera_look ? &look : nullptr);
        if (index == 0) first_frame = frame;
        char name[64];
        std::snprintf(name, sizeof(name), "\\frame_%04d.jpg", index);
        if (!save_image(out + name, frame)) { std::printf("cannot write frames to %s\n", out.c_str()); return 1; }
        if (have_video && !video.write(frame)) { std::printf("warning: writing the MP4 failed at frame %d\n", index); }
        for (const TruthFace& f : faces)
            truth << index << ',' << t << ',' << f.label << ',' << f.box.x << ',' << f.box.y << ',' << f.box.w << ',' << f.box.h
                  << ',' << f.roll_deg << ',' << (f.visible ? 1 : 0) << '\n';
    }
    const bool closed = !have_video || video.close();
    std::printf("wrote %d frames of %zu people to %s (truth.csv, scenario.txt%s)\n", index, actors.size(), out.c_str(),
                have_video && closed ? ", video.mp4" : "");

    // ---- check the MP4: right size, right orientation ----
    if (have_video && closed) {
        auto source = open_video_file_source(out + "\\video.mp4");
        Image back;
        double t = 0;
        if (!source || !source->next(back, t)) {
            std::printf("MP4 check: FAILED to read the first frame back\n");
            return 1;
        }
        const double upright = mean_abs_diff(first_frame, back, false), flipped = mean_abs_diff(first_frame, back, true);
        std::printf("MP4 check: read back %dx%d; mean difference to the original frame %.2f, to the original turned upside down %.2f (0 = identical, 255 = opposite)\n",
                    back.width, back.height, upright, flipped);
        const bool ok = upright >= 0 && upright < 12.0 && flipped > upright * 2;
        std::printf("MP4 check: %s\n", ok ? "PASSED (right way up, compressed copy is close to the original)"
                                           : "FAILED (flipped, wrong size or too different)");
        save_image(out + "\\mp4_first_frame_readback.png", back);
        return ok ? 0 : 1;
    }
    return 0;
}
