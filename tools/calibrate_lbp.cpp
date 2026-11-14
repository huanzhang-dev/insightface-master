// calibrate_lbp.cpp - compute the calibration file of the classical (LBP) extractor.
//
//   calibrate_lbp <profile.ini> <portrait folder> <a-m | singles> <output file>
//
// The third argument is a letter range, or the word "singles" = only people who have
// exactly one photo (so that people with two photos can be used for evaluation).
// Only people whose folder name starts with a letter in the range are used, so
// that the people used for evaluation (the other letters) never influence the
// calibration. The output holds, for each of the 2891 histogram values, the mean
// and 1/spread over all faces (see lbp_raw_histogram in extractor.h).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "fs_compat.h"
#include <vector>

#include "detector.h"
#include "extractor.h"
#include "image_io.h"
#include "image_ops.h"

namespace fs = fa::fs;
using namespace fa;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: calibrate_lbp <profile.ini> <portrait folder> <letters a-m | singles> <output file>\n");
        return 2;
    }
    ModelProfile profile;
    if (!load_profile(argv[1], ".", profile)) return 2;
    // third argument: a letter range "a-m", or "singles" = only people who have exactly one photo
    const bool singles = std::string(argv[3]) == "singles";
    const char first = singles ? 'a' : argv[3][0], last = singles ? 'z' : argv[3][2];
    auto detector = create_detector(profile);
    if (!detector) return 1;

    const int n_values = lbp_raw_size();
    std::vector<double> sum(n_values, 0.0), sum_sq(n_values, 0.0);
    int count = 0;
    for (const auto& dir : fs::directory_iterator(argv[2])) {
        if (!fs::is_directory(dir.path())) continue;
        const char initial = dir.path().filename().string()[0];
        if (initial < first || initial > last) continue;
        if (singles) {
            int photos = 0;
            for (const auto& f : fs::directory_iterator(dir.path())) if (f.path().extension() == ".jpg") ++photos;
            if (photos != 1) continue;
        }
        for (const auto& file : fs::directory_iterator(dir.path())) {
            if (file.path().extension() != ".jpg") continue;
            Image image;
            std::vector<Face> faces;
            if (!load_image(file.path().string(), image) || !detector->detect(image, faces) || faces.empty()) continue;
            const Face* best = &faces[0];
            for (const Face& f : faces) if (f.box.area() > best->box.area()) best = &f;
            Face refined = *best;
            if (!refine_face(*detector, image, *best, refined)) refined = *best;
            FaceCrop crop;
            Image aligned;
            std::vector<float> raw;
            if (!refined.has_landmarks || !make_face_crop(image, refined, crop)) continue;
            align_face(crop.image, crop.face, aligned);
            if (!lbp_raw_histogram(aligned, raw)) continue;
            for (int i = 0; i < n_values; ++i) { sum[i] += raw[i]; sum_sq[i] += static_cast<double>(raw[i]) * raw[i]; }
            ++count;
        }
    }
    if (count < 10) { std::printf("too few faces (%d)\n", count); return 1; }
    std::vector<float> mean(n_values), inverse_spread(n_values);
    for (int i = 0; i < n_values; ++i) {
        const double m = sum[i] / count;
        const double variance = std::max(0.0, sum_sq[i] / count - m * m);
        mean[i] = static_cast<float>(m);
        inverse_spread[i] = static_cast<float>(1.0 / std::max(std::sqrt(variance), 0.01));  // floor avoids huge weights
    }
    if (!lbp_save_calibration(argv[4], mean, inverse_spread)) return 1;
    std::printf("calibration from %d faces (%s) written to %s\n", count, singles ? "people with a single photo" : argv[3], argv[4]);
    return 0;
}
