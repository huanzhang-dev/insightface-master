// eval_features.cpp - how good are the features? (accuracy on real portraits)
//
//   eval_features <profile.ini> [--type haar] [--recognizer lbp] [--letters n-z] [--multi-only] <portrait folder>
//
//   --letters n-z   only people whose folder name starts with a letter in the range
//   --multi-only    only people with two or more photos (used to evaluate the classical
//                   extractor, which is calibrated on the single-photo people)
//
// Uses the pipeline's steps (without its pose gate): detect the largest face,
// refine its landmarks, cut the crop, align and extract the feature.
//
// It measures
//   genuine pairs   two DIFFERENT photos of the same person
//   impostor pairs  photos of two different people
// and prints the score distributions, the false accept rate (FAR) and false reject
// rate (FRR) at several thresholds, the threshold for a given FAR, and the rank-1
// identification result when all people are registered with one photo.
//
// HONEST LIMITS: the portraits are official, straight-on, evenly lit photos taken
// within a few years; the number of people with two photos is small. Treat the
// numbers as indicative; see docs/RESULTS.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "fs_compat.h"
#include <map>
#include <vector>

#include "detector.h"
#include "extractor.h"
#include "image_io.h"
#include "image_ops.h"
#include "timer.h"

namespace fs = fa::fs;
using namespace fa;

struct Sample {
    std::string person, file;
    Feature feature;
};

static float dot(const Feature& a, const Feature& b) {
    float s = 0;
    for (int i = 0; i < a.dim(); ++i) s += a.values[i] * b.values[i];
    return s;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) {
        std::printf("usage: eval_features <profile.ini> [--type T] [--recognizer R] [--letters a-z] [--multi-only] <portrait folder>\n");
        return 2;
    }
    ModelProfile profile;
    if (!load_profile(argv[1], ".", profile)) return 2;
    std::string folder;
    char first_letter = 0, last_letter = 127;  // --letters n-z : evaluate only people whose folder starts in this range
    bool multi_only = false;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--type") && i + 1 < argc) { profile.detector_type = argv[++i]; profile.pose_detector_profile.clear(); }
        else if (!std::strcmp(argv[i], "--recognizer") && i + 1 < argc) profile.recognizer_type = argv[++i];
        else if (!std::strcmp(argv[i], "--letters") && i + 1 < argc) { ++i; first_letter = argv[i][0]; last_letter = argv[i][2]; }
        else if (!std::strcmp(argv[i], "--multi-only")) multi_only = true;  // only people with 2+ photos (not used for calibration)
        else folder = argv[i];
    }
    auto detector = create_detector(profile);
    auto pose_detector = create_pose_detector(profile);  // does the refinement, as in the pipeline
    auto extractor = create_extractor(profile);
    if (!detector || !pose_detector || !extractor) return 1;
    std::printf("detector %s, extractor %s (%d numbers)\n", detector->name(), extractor->name(), extractor->dim());
    if (!profile.pose_detector_profile.empty()) std::printf("pose check / alignment landmarks from: %s\n", pose_detector->name());

    // ---- features of every photo ----
    std::vector<Sample> samples;
    TimingStats extract_time;
    int no_face = 0;
    for (const auto& dir : fs::directory_iterator(folder)) {
        if (!fs::is_directory(dir.path())) continue;
        const char initial = dir.path().filename().string()[0];
        if (initial < first_letter || initial > last_letter) continue;
        if (multi_only) {
            int photos = 0;
            for (const auto& f : fs::directory_iterator(dir.path())) if (f.path().extension() == ".jpg") ++photos;
            if (photos < 2) continue;
        }
        for (const auto& file : fs::directory_iterator(dir.path())) {
            if (file.path().extension() != ".jpg") continue;
            Image image;
            std::vector<Face> faces;
            if (!load_image(file.path().string(), image) || !detector->detect(image, faces) || faces.empty()) { ++no_face; continue; }
            const Face* best = &faces[0];
            for (const Face& f : faces) if (f.box.area() > best->box.area()) best = &f;
            Face refined = *best;
            if (!refine_face(*pose_detector, image, *best, refined)) refined = *best;  // fall back to the plain landmarks
            if (!refined.has_landmarks) { ++no_face; continue; }
            FaceCrop crop;
            Sample s;
            s.person = dir.path().filename().string();
            s.file = file.path().filename().string();
            double a = now_ms();
            if (!make_face_crop(image, refined, crop) || !extractor->extract(crop, s.feature)) { ++no_face; continue; }
            extract_time.add(now_ms() - a);
            samples.push_back(std::move(s));
        }
    }
    if (samples.size() < 2) { std::printf("need at least 2 usable photos\n"); return 1; }
    std::map<std::string, std::vector<int>> by_person;
    for (size_t i = 0; i < samples.size(); ++i) by_person[samples[i].person].push_back(static_cast<int>(i));
    int people_two = 0;
    for (auto& kv : by_person) if (kv.second.size() >= 2) ++people_two;
    std::printf("photos used %zu (no usable face in %d), people %zu, people with 2+ photos %d\n", samples.size(), no_face,
                by_person.size(), people_two);
    std::printf("crop + align + extract: median %.1f ms  p95 %.1f ms\n", extract_time.median(), extract_time.p95());

    // ---- score lists ----
    std::vector<float> genuine, impostor;
    struct Pair { float score; int a, b; };
    std::vector<Pair> top_impostors;  // the highest-scoring "different people": possible labelling mistakes
    for (size_t i = 0; i < samples.size(); ++i)
        for (size_t j = i + 1; j < samples.size(); ++j) {
            const float s = dot(samples[i].feature, samples[j].feature);
            if (samples[i].person == samples[j].person) genuine.push_back(s);
            else {
                impostor.push_back(s);
                if (s >= 0.45f) top_impostors.push_back({s, static_cast<int>(i), static_cast<int>(j)});
            }
        }
    std::sort(top_impostors.begin(), top_impostors.end(), [](const Pair& x, const Pair& y) { return x.score > y.score; });
    if (!top_impostors.empty()) {
        std::printf("\nhighest-scoring pairs of DIFFERENT named people (score >= 0.45; the same person under two names?):\n");
        for (size_t k = 0; k < top_impostors.size() && k < 15; ++k)
            std::printf("   %.3f  %s/%s  <->  %s/%s\n", top_impostors[k].score, samples[top_impostors[k].a].person.c_str(),
                        samples[top_impostors[k].a].file.c_str(), samples[top_impostors[k].b].person.c_str(),
                        samples[top_impostors[k].b].file.c_str());
    }
    std::sort(genuine.begin(), genuine.end());
    std::sort(impostor.begin(), impostor.end());
    if (impostor.empty()) { std::printf("need photos of at least 2 different people\n"); return 1; }
    auto mean = [](const std::vector<float>& v) { double s = 0; for (float x : v) s += x; return v.empty() ? 0.0 : s / v.size(); };
    std::printf("\ngenuine pairs  (different photos, same person): %zu   mean %.3f  min %.3f  median %.3f\n", genuine.size(),
                mean(genuine), genuine.empty() ? 0.f : genuine.front(), genuine.empty() ? 0.f : genuine[genuine.size() / 2]);
    std::printf("impostor pairs (different people):              %zu   mean %.3f  median %.3f  p99 %.3f  p99.9 %.3f  max %.3f\n",
                impostor.size(), mean(impostor), impostor[impostor.size() / 2], impostor[impostor.size() * 99 / 100],
                impostor[impostor.size() * 999 / 1000], impostor.back());

    // ---- FAR / FRR per threshold ----
    std::printf("\nthreshold | impostor pairs accepted (FAR) | genuine pairs rejected (FRR)\n");
    std::vector<float> thresholds = {0.20f, 0.30f, 0.363f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f};
    thresholds.push_back(profile.match_threshold);
    std::sort(thresholds.begin(), thresholds.end());
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end(), [](float a, float b) { return std::fabs(a - b) < 1e-6f; }),
                     thresholds.end());  // the profile's threshold may equal a listed one: print that row once
    for (float t : thresholds) {
        const size_t acc = impostor.end() - std::lower_bound(impostor.begin(), impostor.end(), t);
        const size_t rej = std::lower_bound(genuine.begin(), genuine.end(), t) - genuine.begin();
        std::printf("   %.3f%s | %6zu / %zu = %.4f%%        | %3zu / %zu = %.1f%%\n", t, std::fabs(t - profile.match_threshold) < 1e-6 ? "*" : " ",
                    acc, impostor.size(), 100.0 * acc / impostor.size(), rej, genuine.size(),
                    genuine.empty() ? 0.0 : 100.0 * rej / genuine.size());
    }
    std::printf("   (* = threshold of the profile)\n");
    // threshold just above the highest impostor score: FAR 0 on this data
    const float t_zero = impostor.back() + 1e-4f;
    const size_t rej0 = std::lower_bound(genuine.begin(), genuine.end(), t_zero) - genuine.begin();
    std::printf("   threshold above every impostor score (FAR 0 on these %zu pairs): %.3f -> FRR %zu / %zu = %.1f%%\n", impostor.size(),
                t_zero, rej0, genuine.size(), genuine.empty() ? 0.0 : 100.0 * rej0 / genuine.size());
    std::printf("   (a FAR of 1 in 10,000 needs ~100,000 impostor pairs to measure; with %zu pairs the data can only bound it)\n",
                impostor.size());

    // ---- 1:N identification: everybody registered with ONE photo, probes = the other photos ----
    std::vector<int> gallery;  // sample index of the first photo of each person
    for (auto& kv : by_person) gallery.push_back(kv.second[0]);
    int probes = 0, rank1 = 0, rank1_above = 0;
    for (auto& kv : by_person)
        for (size_t k = 1; k < kv.second.size(); ++k) {
            ++probes;
            float best = -2;
            int best_person = -1;
            for (int g : gallery) {
                const float s = dot(samples[kv.second[k]].feature, samples[g].feature);
                if (s > best) { best = s; best_person = g; }
            }
            if (samples[best_person].person == kv.first) {
                ++rank1;
                if (best >= profile.match_threshold) ++rank1_above;
            }
        }
    std::printf("\n1:N identification with %zu people registered (one photo each), %d probes (another photo of a registered person):\n",
                gallery.size(), probes);
    std::printf("   rank-1 correct: %d / %d;  correct AND above the profile threshold %.3f: %d / %d\n", rank1, probes,
                profile.match_threshold, rank1_above, probes);

    // 1:N false accepts: each person's first photo against a gallery WITHOUT that person
    std::printf("   1:N false accepts (a stranger searched among the other %zu people):\n", gallery.size() - 1);
    std::vector<float> fa_thresholds = {0.363f, 0.45f, 0.50f, 0.55f, 0.60f, profile.match_threshold};
    std::sort(fa_thresholds.begin(), fa_thresholds.end());
    fa_thresholds.erase(std::unique(fa_thresholds.begin(), fa_thresholds.end(), [](float a, float b) { return std::fabs(a - b) < 1e-6f; }),
                        fa_thresholds.end());
    for (float t : fa_thresholds) {
        int fa_count = 0;
        for (int p : gallery) {
            float best = -2;
            for (int g : gallery)
                if (samples[g].person != samples[p].person) best = std::max(best, dot(samples[p].feature, samples[g].feature));
            if (best >= t) ++fa_count;
        }
        std::printf("      threshold %.3f%s: %d of %zu strangers accepted (%.1f%%)\n", t,
                    std::fabs(t - profile.match_threshold) < 1e-6 ? "*" : "", fa_count, gallery.size(), 100.0 * fa_count / gallery.size());
    }
    return 0;
}
