// eval_detect.cpp - how well does each detector find faces of different sizes?
//
//   eval_detect profile1.ini [profile2.ini ...] [--people N] [--frame WxH]   (default frame 1920x1080)
//
// For every face width the tool builds frames of the chosen size (default 1920x1080, Full HD) (generated scene, portraits
// as actors, known face boxes) and reports for each detector:
//   found      share of faces found with box overlap (IoU) >= 0.5
//   IoU        mean overlap of the found boxes with the true boxes
//   false/frame  detections with box overlap (IoU) below 0.5 to the true face, per frame
//   ms         median detection time per frame
// The Haar detector follows faces between full scans, so the same frame is shown 12
// times in a row to include both kinds of scans in the timing.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fs_compat.h"
#include <string>
#include <vector>

#include "detector.h"
#include "image_io.h"
#include "scene.h"
#include "timer.h"

namespace fs = fa::fs;
using namespace fa;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int people = 20, frame_w = FA_CAMERA_DEFAULT_WIDTH, frame_h = FA_CAMERA_DEFAULT_HEIGHT;
    std::vector<std::string> profiles;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--people") && i + 1 < argc) people = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frame") && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &frame_w, &frame_h);
        else profiles.push_back(argv[i]);
    }
    if (profiles.empty()) { std::printf("usage: eval_detect profile.ini ... [--people N] [--frame WxH]\n"); return 2; }

    // portraits and their face boxes (found once with the permissive YuNet detector)
    ModelProfile ref_profile;
    load_profile("models/permissive.ini", ".", ref_profile);
    auto reference = create_detector(ref_profile);
    if (!reference) { std::printf("cannot create the reference detector (models/permissive.ini)\n"); return 1; }
    std::vector<Actor> base;
    for (const auto& dir : fs::directory_iterator("data/portraits")) {
        if (static_cast<int>(base.size()) >= people) break;
        Image portrait;
        std::vector<Face> found;
        if (!fs::is_directory(dir.path()) || !load_image((dir.path() / "117th.jpg").string(), portrait)) continue;
        if (!reference->detect(portrait, found) || found.empty()) continue;
        base.push_back(make_actor(dir.path().filename().string(), portrait, found[0].box));
    }
    std::printf("%zu portraits as actors; frames %dx%d; one face per frame (centre), 12 identical frames per portrait\n", base.size(), frame_w, frame_h);

    for (const std::string& path : profiles) {
        ModelProfile profile;
        if (!load_profile(path, ".", profile)) continue;
        auto detector = create_detector(profile);
        if (!detector) continue;
        std::printf("\n=== %s : %s ===\n", path.c_str(), detector->name());
        std::printf("  face width | found | mean IoU | false/frame | median ms | p95 ms\n");
        for (float width : {50.0f, 80.0f, 110.0f, 160.0f, 250.0f, 400.0f}) {
            int found_count = 0, total = 0, false_count = 0, frames = 0;
            double iou_sum = 0;
            TimingStats time;
            for (Actor actor : base) {
                actor.path = {{0, frame_w / 2.0f, frame_h / 2.0f, width, 0.0f}};
                Image frame;
                std::vector<TruthFace> truth;
                render_frame(frame_w, frame_h, 0, 0, {actor}, frame, truth);
                {   // A frame without faces first: it clears what the Haar detector remembers from the
                    // previous portrait, so that the first call below is a genuine full scan.
                    Image blank;
                    std::vector<TruthFace> none;
                    std::vector<Face> ignore;
                    render_frame(frame_w, frame_h, 0, 0, {}, blank, none);
                    detector->detect(blank, ignore);
                }
                for (int rep = 0; rep < 12; ++rep) {
                    std::vector<Face> faces;
                    const double a = now_ms();
                    detector->detect(frame, faces);
                    time.add(now_ms() - a);
                    ++frames;
                    if (rep > 0) continue;  // accuracy only on the first (full-scan) call of each portrait
                    ++total;
                    bool hit = false;
                    for (const Face& f : faces) {
                        const float v = iou(f.box, truth[0].box);
                        if (v >= 0.5f) { hit = true; iou_sum += v; }
                        else ++false_count;
                    }
                    if (hit) ++found_count;
                }
            }
            std::printf("  %9.0f | %4.0f%% | %8.2f | %11.2f | %9.1f | %6.1f\n", width, 100.0 * found_count / total,
                        found_count ? iou_sum / found_count : 0.0, static_cast<double>(false_count) / total, time.median(), time.p95());
        }
    }
    return 0;
}
