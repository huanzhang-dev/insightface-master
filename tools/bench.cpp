// bench.cpp - speed of every engine against the requirements, through the complete pipeline.
//
//   bench [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...
//
// Requirements (on a 4th-generation Intel Core i5):
//   face detection + tracking   <= 60 ms at 1280x1080,  <= 25 ms at 640x640
//                               (only the detection is timed here; the tracker needs < 0.01 ms,
//                               see the "track" column of test_pipeline)
//   feature extraction          <= 150 ms (ideally <= 50 ms) per face
//   feature matching            as fast as possible (M:N)
//
// The three measurements are the same ones that the single-engine programs bench_detect, bench_extract
// and bench_match run (they are written once, in bench_detect_part.h, bench_extract_part.h and
// bench_match_part.h); this program builds the AuthPipeline from each profile and times all three.
// THIS COMPUTER IS NOT THE TARGET PC: see bench_common.h for what "est. i5" means.
#include <cstdio>
#include <string>
#include <vector>

#include "auth_pipeline.h"
#include "bench_common.h"
#include "bench_detect_part.h"
#include "bench_extract_part.h"
#include "bench_match_part.h"

using namespace fa;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BenchOptions options;
    if (!parse_bench_options(argc, argv, "bench", true, options)) return 2;

    for (const std::string& path : options.profiles) {
        ModelProfile profile;
        if (!load_profile(path, ".", profile)) continue;
        if (options.threads > 0) profile.threads = options.threads;
        AuthPipeline pipeline;
        if (!pipeline.init(profile, "build\\bench_store.csv")) continue;
        FaceDetector& detector = pipeline.detector();
        std::printf("\n=== %s: detector %s, extractor %s (%d numbers), %d thread(s) ===\n", path.c_str(), detector.name(),
                    pipeline.extractor().name(), pipeline.extractor().dim(), profile.threads);

        bench_detection(detector, options.frames);
        bench_extraction(detector, pipeline.extractor(), options.frames);
        // the pose check runs on the pipeline's pose detector, which is another detector when the profile names one
        bench_pose_check(detector, &pipeline.pose_detector() == &detector ? nullptr : &pipeline.pose_detector(), options.frames);
    }

    bench_matching();  // independent of the profile
    return 0;
}
