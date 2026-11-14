// test_feature.cpp - verifies the feature extraction engine on its own.
//
//   test_feature [profile.ini]        (default models/permissive.ini)
//
// The extractor needs a face cut out and aligned by its landmarks, so the first portraits of
// data/portraits are run through the profile's detector once, only to get those landmarks; after that
// only the extractor is under test. (Accuracy over many people is measured by eval_features; this
// program checks that the engine behaves, on 8 people.)
//
// Checks: the extractor loads and has the length the profile says; every feature has that length, is
// finite and has length 1; the same crop gives the same feature twice; a crop without landmarks or
// without pixels is refused (not a crash); the features of different people are less alike than the
// profile's match threshold; and, for the deep extractors, a brightened copy of a face is still alike
// (similarity at least 0.8) and still identified as that person among the 8.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "detector.h"
#include "extractor.h"
#include "fs_compat.h"
#include "image_io.h"
#include "timer.h"
#include "cpu_only_check.h"

static int g_failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }   \
        else std::printf("ok:   %s\n", msg);                              \
    } while (0)

namespace fs = fa::fs;
using namespace fa;

static float dot(const Feature& a, const Feature& b) {
    float s = 0;
    for (size_t i = 0; i < a.values.size() && i < b.values.size(); ++i) s += a.values[i] * b.values[i];
    return s;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string profile_path = argc > 1 ? argv[1] : "models/permissive.ini";
    ModelProfile profile;
    if (!load_profile(profile_path, ".", profile)) return 2;
    auto extractor = create_extractor(profile);
    auto detector = create_detector(profile);
    if (!extractor || !detector) return 2;
    std::printf("profile %s: extractor %s (%d numbers), threshold %.3f, landmarks from %s\n", profile_path.c_str(), extractor->name(),
                extractor->dim(), profile.match_threshold, detector->name());
    const bool deep = profile.recognizer_type != "lbp";
    CHECK(extractor->dim() == profile.feature_dim, "the extractor has the feature length written in the profile");
    CHECK(extractor->runs_on_cpu() && detector->runs_on_cpu(), "the extractor and the detector run on the CPU (no GPU is used)");

    // ---------------------------------------------------------------- 8 people: aligned crops
    std::vector<FaceCrop> crops;
    std::vector<std::string> names;
    for (const auto& dir : fs::directory_iterator("data/portraits")) {
        if (crops.size() >= 8) break;
        Image image;
        std::vector<Face> faces;
        if (!fs::is_directory(dir.path()) || !load_image((dir.path() / "117th.jpg").string(), image)) continue;
        if (!detector->detect(image, faces) || faces.empty() || !faces[0].has_landmarks) continue;
        FaceCrop crop;
        if (!make_face_crop(image, faces[0], crop)) continue;
        crops.push_back(std::move(crop));
        names.push_back(dir.path().filename().string());
    }
    std::printf("portraits used: %zu\n", crops.size());
    CHECK(crops.size() >= 4, "enough portraits with a detected face (>= 4)");
    if (crops.size() < 4) return 1;

    // ---------------------------------------------------------------- the features
    std::vector<Feature> features(crops.size());
    int extracted = 0, right_length = 0, finite = 0, unit = 0, repeatable = 0;
    TimingStats extract_time;
    for (size_t i = 0; i < crops.size(); ++i) {
        const double a = now_ms();
        const bool ok = extractor->extract(crops[i], features[i]);
        extract_time.add(now_ms() - a);
        if (!ok) continue;
        ++extracted;
        const Feature& f = features[i];
        if (f.dim() == extractor->dim()) ++right_length;
        double norm = 0;
        bool all_finite = true;
        for (float v : f.values) { norm += static_cast<double>(v) * v; if (!std::isfinite(v)) all_finite = false; }
        if (all_finite) ++finite;
        if (std::fabs(std::sqrt(norm) - 1.0) < 1e-3) ++unit;
        Feature again;
        double worst = 1e9;
        if (extractor->extract(crops[i], again) && again.dim() == f.dim()) {
            worst = 0;
            for (int k = 0; k < f.dim(); ++k) worst = std::fmax(worst, std::fabs(f.values[k] - again.values[k]));
        }
        if (worst < 1e-6) ++repeatable;
    }
    const int n = static_cast<int>(crops.size());
    std::printf("time of extract(): median %.1f ms\n", extract_time.median());
    CHECK(extracted == n, "every crop gives a feature");
    CHECK(right_length == n, "every feature has the length of the extractor");
    CHECK(finite == n, "every number of every feature is finite");
    CHECK(unit == n, "every feature has length 1");
    CHECK(repeatable == n, "the same crop gives the same feature twice");

    // ---------------------------------------------------------------- bad input
    {
        Feature f;
        FaceCrop empty;
        CHECK(!extractor->extract(empty, f), "a crop without pixels is refused");
        FaceCrop blind = crops[0];
        blind.face.has_landmarks = false;
        CHECK(!extractor->extract(blind, f), "a crop without landmarks is refused");
    }

    // ---------------------------------------------------------------- different people
    float most_alike = -2;
    std::string pair;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const float s = dot(features[i], features[j]);
            if (s > most_alike) { most_alike = s; pair = names[i] + " / " + names[j]; }
        }
    std::printf("most alike pair of different people: %.3f (%s)\n", most_alike, pair.c_str());
    CHECK(most_alike < profile.match_threshold, "different people are less alike than the match threshold");

    // ---------------------------------------------------------------- a changed picture of the same person
    if (deep) {
        float least_alike = 2;
        int identified = 0;
        for (int i = 0; i < n; ++i) {
            FaceCrop bright = crops[i];  // 15% brighter (every pixel, clipped at 255)
            for (auto& v : bright.image.data) v = static_cast<uint8_t>(std::fmin(255.0f, v * 1.15f));
            Feature f;
            if (!extractor->extract(bright, f)) continue;
            least_alike = std::fmin(least_alike, dot(f, features[i]));
            int best = 0;
            for (int j = 1; j < n; ++j)
                if (dot(f, features[j]) > dot(f, features[best])) best = j;
            if (best == i) ++identified;
        }
        std::printf("brightened copies: least alike to the original %.3f, identified %d of %d\n", least_alike, identified, n);
        CHECK(least_alike >= 0.8f, "a brightened copy of a face stays alike (similarity >= 0.8)");
        CHECK(identified == n, "a brightened copy is identified as the right person among the people");
    } else {
        std::printf("(classical extractor: the brightness check is only made for the deep extractors)\n");
    }

    CHECK(loaded_gpu_libraries().empty(), "after the extractor has run, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process");
    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
