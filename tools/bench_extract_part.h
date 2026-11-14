// bench_extract_part.h - the speed measurement of the feature extraction engine.
//
// Limit (on a 4th-generation Intel Core i5): 150 ms per face, ideally 50 ms.
#ifndef FA_BENCH_EXTRACT_PART_H
#define FA_BENCH_EXTRACT_PART_H

#include <cstdio>
#include <vector>

#include "bench_common.h"
#include "bench_frames.h"
#include "detector.h"
#include "extractor.h"
#include "timer.h"

// Time of cutting out one face, aligning it and running the network (make_face_crop + extract), median
// of `frames` runs after one warm-up run. `detector` is only used to find the face and its landmarks
// in the test frame (1920x1080, the default camera size, one portrait).
static void bench_extraction(fa::FaceDetector& detector, fa::FeatureExtractor& extractor, int frames) {
    using namespace fa;
    Image frame;
    if (!make_frame(detector, 1920, 1080, 1, frame)) { std::printf("    (no usable test frame)\n"); return; }
    std::vector<Face> faces;
    detector.detect(frame, faces);
    if (faces.empty() || !faces[0].has_landmarks) { std::printf("    (the detector found no face with landmarks in the test frame: nothing to time)\n"); return; }
    FaceCrop crop;
    Feature f;
    TimingStats t;
    make_face_crop(frame, faces[0], crop);
    extractor.extract(crop, f);  // warm-up
    for (int k = 0; k < frames; ++k) {
        double a = now_ms();
        make_face_crop(frame, faces[0], crop);
        extractor.extract(crop, f);
        t.add(now_ms() - a);
    }
    row("extract 1 face (crop+align+net)", t, 150);
    // the "ideal" limit
    std::printf("      (ideal limit 50 ms: this PC %s, est. i5 %s)\n", t.median() <= 50 ? "PASS" : "FAIL",
                t.median() * g_factor <= 50 ? "PASS" : "FAIL");
}

#endif  // FA_BENCH_EXTRACT_PART_H
