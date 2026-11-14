// detect_image.cpp - run a face detector on picture files and print / draw the result.
//
//   detect_image <profile.ini> [--type yunet|scrfd|haar] [--draw outfolder] image1 image2 ...
//   detect_image <profile.ini> --folder data\portraits   (all .jpg files below the folder)
//
// --type overrides detector.type of the profile, so one profile can test all
// detectors (for example --type haar uses the classical detector).
#include <cstdio>
#include <cstring>
#include "fs_compat.h"
#include <string>
#include <vector>

#include "detector.h"
#include "image_io.h"
#include "image_ops.h"
#include "timer.h"

int main(int argc, char** argv) {
    using namespace fa;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) {
        std::printf("usage: detect_image <profile.ini> [--type T] [--draw dir] [--folder dir | images...]\n");
        return 2;
    }
    ModelProfile profile;
    if (!load_profile(argv[1], ".", profile)) return 2;

    std::vector<std::string> images;
    std::string draw_dir;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--type") && i + 1 < argc) profile.detector_type = argv[++i];
        else if (!std::strcmp(argv[i], "--draw") && i + 1 < argc) draw_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--folder") && i + 1 < argc) {
            for (auto& e : fa::fs::recursive_directory_iterator(argv[++i]))
                if (e.path().extension() == ".jpg") images.push_back(e.path().string());
        } else images.push_back(argv[i]);
    }
    auto detector = create_detector(profile);
    if (!detector) return 1;
    if (!draw_dir.empty()) fa::fs::create_directories(draw_dir);
    std::printf("detector: %s\n", detector->name());

    int with_face = 0, total = 0, faces_total = 0;
    TimingStats timing;
    for (const std::string& path : images) {
        Image image;
        if (!load_image(path, image)) { std::printf("cannot read %s\n", path.c_str()); continue; }
        std::vector<Face> faces;
        detector->detect(image, faces);  // first call also warms up the network
        double a = now_ms();
        detector->detect(image, faces);
        const double ms = now_ms() - a;
        timing.add(ms);
        ++total;
        if (!faces.empty()) ++with_face;
        faces_total += static_cast<int>(faces.size());
        std::printf("%-55s %4dx%-4d %zu face(s) %6.1f ms", path.c_str(), image.width, image.height, faces.size(), ms);
        for (const Face& f : faces)
            std::printf("  [score %.2f box %.0f,%.0f %.0fx%.0f pose p%+.1f y%+.1f r%+.1f%s]", f.score, f.box.x, f.box.y, f.box.w,
                        f.box.h, f.pose.pitch, f.pose.yaw, f.pose.roll, f.has_landmarks ? "" : " nolm");
        std::printf("\n");
        if (!draw_dir.empty()) {
            for (const Face& f : faces) {
                draw_rect(image, f.box, 0, 255, 0, 3);
                if (f.has_landmarks)
                    for (int k = 0; k < 5; ++k) draw_dot(image, f.landmarks[k], 4, k < 2 ? 255 : 0, 0, k < 2 ? 0 : 255);
            }
            std::string name = fa::fs::path(path).parent_path().filename().string() + "_" +
                               fa::fs::path(path).stem().string() + ".jpg";
            save_image(draw_dir + "\\" + name, image);
        }
    }
    std::printf("\nimages with a face: %d / %d   faces: %d   detect time median %.1f ms  p95 %.1f ms  max %.1f ms\n",
                with_face, total, faces_total, timing.median(), timing.p95(), timing.max());
    return 0;
}
