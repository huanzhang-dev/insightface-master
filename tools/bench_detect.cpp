// bench_detect.cpp - speed of the face detection engine alone (no extractor, no pipeline).
//
//   bench_detect [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...
//
// Requirement (on a 4th-generation Intel Core i5): <= 60 ms at 1280x1080 and <= 25 ms at 640x640.
// Prints the detection time of 1 face at both sizes and of 3 faces, and the time of the pose check
// (refine_face) of one face. Needs data/portraits for the test pictures. For each profile the detector
// and, if the profile names one (pose.detector_profile), the pose detector are created.
// THIS COMPUTER IS NOT THE TARGET PC: see bench_common.h for what "est. i5" means.
#include <cstdio>
#include <memory>
#include <string>

#include "bench_common.h"
#include "bench_detect_part.h"

using namespace fa;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BenchOptions options;
    if (!parse_bench_options(argc, argv, "bench_detect", true, options)) return 2;

    for (const std::string& path : options.profiles) {
        ModelProfile profile;
        if (!load_profile(path, ".", profile)) continue;
        if (options.threads > 0) profile.threads = options.threads;
        std::unique_ptr<FaceDetector> detector = create_detector(profile);
        if (!detector) continue;
        std::unique_ptr<FaceDetector> pose_detector;  // stays empty when the detector does the pose check itself
        if (!profile.pose_detector_profile.empty() && !(pose_detector = create_pose_detector(profile))) continue;
        std::printf("\n=== %s: detector %s, %d thread(s) ===\n", path.c_str(), detector->name(), profile.threads);

        bench_detection(*detector, options.frames);
        bench_pose_check(*detector, pose_detector.get(), options.frames);
    }
    return 0;
}
