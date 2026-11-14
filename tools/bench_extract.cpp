// bench_extract.cpp - speed of the feature extraction engine alone (no pipeline).
//
//   bench_extract [--threads N] [--frames K] [--i5-factor F] profile1.ini profile2.ini ...
//
// Requirement (on a 4th-generation Intel Core i5): <= 150 ms per face, ideally <= 50 ms.
// Prints the time of make_face_crop + extract (cut out, align, network) for one face. The profile's
// detector is created only to find the face and its landmarks in the test picture (data/portraits).
// THIS COMPUTER IS NOT THE TARGET PC: see bench_common.h for what "est. i5" means.
#include <cstdio>
#include <memory>
#include <string>

#include "bench_common.h"
#include "bench_extract_part.h"

using namespace fa;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BenchOptions options;
    if (!parse_bench_options(argc, argv, "bench_extract", true, options)) return 2;

    for (const std::string& path : options.profiles) {
        ModelProfile profile;
        if (!load_profile(path, ".", profile)) continue;
        if (options.threads > 0) profile.threads = options.threads;
        std::unique_ptr<FaceDetector> detector = create_detector(profile);
        std::unique_ptr<FeatureExtractor> extractor = create_extractor(profile);
        if (!detector || !extractor) continue;
        std::printf("\n=== %s: extractor %s (%d numbers), %d thread(s) ===\n", path.c_str(), extractor->name(), extractor->dim(), profile.threads);

        bench_extraction(*detector, *extractor, options.frames);
    }
    return 0;
}
