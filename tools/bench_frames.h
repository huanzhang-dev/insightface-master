// bench_frames.h - the test picture of the detection and extraction benchmarks.
#ifndef FA_BENCH_FRAMES_H
#define FA_BENCH_FRAMES_H

#include <algorithm>
#include <vector>

#include "detector.h"
#include "fs_compat.h"
#include "image_io.h"
#include "scene.h"

// A frame with `faces` people at the given size (portraits from data/portraits). One face is
// placed in the middle and is 30% of the shorter side wide; several faces stand side by side and
// are 20% wide. Returns false when data/portraits does not hold enough portraits in which the
// detector finds a face (the detector is used to find the face box that the scene needs).
static bool make_frame(fa::FaceDetector& detector, int width, int height, int faces, fa::Image& frame) {
    namespace fs = fa::fs;
    using namespace fa;
    std::vector<Actor> actors;
    float x = width * (faces == 1 ? 0.5f : 0.28f);
    const float face_w = std::min(width, height) * (faces == 1 ? 0.30f : 0.20f);
    for (const auto& dir : fs::directory_iterator("data/portraits")) {
        if (static_cast<int>(actors.size()) >= faces) break;
        Image portrait;
        std::vector<Face> found;
        if (!fs::is_directory(dir.path()) || !load_image((dir.path() / "117th.jpg").string(), portrait)) continue;
        if (!detector.detect(portrait, found) || found.empty()) continue;
        Actor a = make_actor(dir.path().filename().string(), portrait, found[0].box);
        a.path = {{0, x, height * 0.5f, face_w, 0.0f}};
        actors.push_back(a);
        x += width * 0.30f;
    }
    if (static_cast<int>(actors.size()) < faces) return false;
    std::vector<TruthFace> truth;
    render_frame(width, height, 0, 0, actors, frame, truth);
    return true;
}

#endif  // FA_BENCH_FRAMES_H
