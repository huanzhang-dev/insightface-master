// bench_detect_part.h - the speed measurements of the face detection engine.
//
// Limits (on a 4th-generation Intel Core i5): face detection + tracking <= 60 ms at 1280x1080 and
// <= 25 ms at 640x640. Only the detection is timed here; the tracker needs less than 0.01 ms (see the
// "track" column of test_pipeline).
#ifndef FA_BENCH_DETECT_PART_H
#define FA_BENCH_DETECT_PART_H

#include <cstdio>
#include <vector>

#include "bench_common.h"
#include "bench_frames.h"
#include "detector.h"
#include "timer.h"

// Detection time of one face at 1280x1080 (limit 60 ms) and at 640x640 (limit 25 ms), and of three
// faces at 1280x1080 and 1920x1080, median of `frames` runs after one warm-up run each. Two more camera sizes are timed
// against the same 60 ms limit (the requirement names only the two sizes above; the limit is applied here by
// analogy): 1920x1080, the default camera size, and 1280x720, a size that cameras commonly deliver.
static void bench_detection(fa::FaceDetector& detector, int frames) {
    using namespace fa;
    struct Size { int w, h; double limit; const char* label; };
    for (const Size& s : {Size{1280, 1080, 60, "detect 1280x1080, 1 face"}, Size{640, 640, 25, "detect  640x640, 1 face"},
                          Size{1920, 1080, 60, "detect 1920x1080, 1 face"}, Size{1280, 720, 60, "detect 1280x720, 1 face"}}) {
        Image frame;
        if (!make_frame(detector, s.w, s.h, 1, frame)) { std::printf("    (no usable test frame)\n"); continue; }
        std::vector<Face> faces;
        detector.detect(frame, faces);  // warm-up
        TimingStats t;
        for (int k = 0; k < frames; ++k) { double a = now_ms(); detector.detect(frame, faces); t.add(now_ms() - a); }
        row(s.label, t, s.limit);
    }
    for (int width : {1280, 1920}) {  // three faces at 1280x1080 and at 1920x1080 (the default camera size)
        Image frame;
        if (make_frame(detector, width, 1080, 3, frame)) {
            std::vector<Face> faces;
            detector.detect(frame, faces);
            TimingStats t;
            for (int k = 0; k < frames; ++k) { double a = now_ms(); detector.detect(frame, faces); t.add(now_ms() - a); }
            char label[64];
            std::snprintf(label, sizeof(label), "detect %dx1080, 3 faces (found %zu)", width, faces.size());
            row(label, t, 60);
        }
    }
}

// Time of the pose check ("refinement", refine_face) of one face at 1280x1080. `detector` finds the
// face; `pose_detector` runs the refinement (null = the same detector does it, as in profiles
// without pose.detector_profile).
static void bench_pose_check(fa::FaceDetector& detector, fa::FaceDetector* pose_detector, int frames) {
    using namespace fa;
    Image frame;
    if (!make_frame(detector, 1280, 1080, 1, frame)) { std::printf("    (no usable test frame)\n"); return; }
    std::vector<Face> faces;
    detector.detect(frame, faces);
    if (faces.empty()) { std::printf("    (the detector found no face in the test frame: no pose check to time)\n"); return; }
    Face refined;
    TimingStats t;
    FaceDetector& refiner = pose_detector ? *pose_detector : detector;
    for (int k = 0; k < frames; ++k) { double a = now_ms(); refine_face(refiner, frame, faces[0], refined); t.add(now_ms() - a); }
    row(pose_detector ? "pose refinement of 1 face (other detector)" : "pose refinement of 1 face", t, 0);
}

#endif  // FA_BENCH_DETECT_PART_H
