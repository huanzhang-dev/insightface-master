// eval_footage.cpp - evaluate the engines one by one on recorded footage (a generated 1920x1080 video).
//
//   eval_footage <profile.ini> <footage folder> [--fillers N] [--every K] [--fps N]
//
// The footage folder is what tools/gen_video writes: video.mp4 (or numbered frames), truth.csv (the exact
// face box of every person in every frame) and scenario.txt (who is registered). The video is played through
// the same Media Foundation reader a camera uses, so the frames are what a camera of that size would deliver.
// For every frame the engines are run one after the other and timed on their own:
//
//   DETECTION + TRACKING  the detector finds the faces, the tracker follows them. Checked against truth.csv:
//                         share of the true faces found (box overlap IoU >= 0.5), by face width; mean IoU;
//                         detections that match no true face, per frame; whether every person keeps ONE track ID
//                         (track IDs per person, ID changes); time per frame (detector, tracker).
//   FEATURE EXTRACTION    for every K-th frame and every found face that is wide enough (FA_MIN_CROP_FACE_PIXELS)
//                         the pipeline's own steps run: second look with exact landmarks (refine_face, with the
//                         profile's pose detector), pose gate, crop, align, extract. Reported: how many faces pass
//                         the pose gate, and the time of each step.
//   MATCHING              the features are compared with the registered people (the people of the scenario who
//                         are marked registered, enrolled from their portrait photo, plus N fillers who are not in
//                         the video), M faces of a frame against all N in one M:N call. Reported: similarity of the
//                         registered people to themselves and the highest similarity of strangers, how often the
//                         right person is found first (rank 1), how often a stranger would be accepted at the
//                         profile's threshold, and the time of the call.
//
// This is the engine-by-engine view; tools/check_video plays the same footage through the complete system.
// Nothing here uses a camera.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fs_compat.h"
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "config.h"
#include "detector.h"
#include "extractor.h"
#include "feature_store.h"
#include "frame_source.h"
#include "image_io.h"
#include "pose.h"
#include "profile.h"
#include "timer.h"
#include "tracker.h"

namespace fs = fa::fs;
using namespace fa;

struct TruthRow {
    std::string label;
    Rect box;
    bool visible = false;
};

static std::vector<std::string> split_line(const std::string& line, char sep) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) parts.push_back(item);
    return parts;
}

// The feature of the largest face of a portrait photo, the way AuthPipeline::extract_from_image makes it.
static bool portrait_feature(FaceDetector& detector, FeatureExtractor& extractor, const std::string& path, Feature& feature) {
    Image photo;
    std::vector<Face> faces;
    if (!load_image(path, photo) || !detector.detect(photo, faces) || faces.empty()) return false;
    const Face* best = &faces[0];
    for (const Face& f : faces)
        if (f.box.area() > best->box.area()) best = &f;
    FaceCrop crop;
    return best->has_landmarks && make_face_crop(photo, *best, crop) && extractor.extract(crop, feature);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) {
        std::printf("usage: eval_footage <profile.ini> <footage folder> [--fillers N] [--every K] [--fps N]\n");
        return 2;
    }
    const std::string profile_path = argv[1], folder = argv[2];
    int fillers = 20, every = 3;
    double fps = 15;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--fillers") && i + 1 < argc) fillers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) every = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) fps = std::atof(argv[++i]);
    }

    ModelProfile profile;
    if (!load_profile(profile_path, ".", profile)) return 2;
    auto detector = create_detector(profile);
    auto extractor = create_extractor(profile);
    if (!detector || !extractor) return 2;
    std::unique_ptr<FaceDetector> own_pose_detector;
    if (!profile.pose_detector_profile.empty()) own_pose_detector = create_pose_detector(profile);
    FaceDetector& pose_detector = own_pose_detector ? *own_pose_detector : *detector;
    std::printf("profile %s: detector %s, pose check by %s, extractor %s (%d numbers), threshold %.2f\n", profile_path.c_str(), detector->name(),
                pose_detector.name(), extractor->name(), extractor->dim(), profile.match_threshold);
    std::printf("all engines run on the CPU: %s\n", detector->runs_on_cpu() && pose_detector.runs_on_cpu() && extractor->runs_on_cpu() ? "yes (no GPU is used)" : "NO");

    // ---- the scenario and the truth ----
    std::map<std::string, bool> registered;  // label -> registered?
    {
        std::ifstream f(folder + "\\scenario.txt");
        if (!f) { std::printf("no scenario.txt in %s (make the footage with gen_video)\n", folder.c_str()); return 2; }
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string word, label, kind;
            ss >> word >> label >> kind;
            if (word == "person" && !label.empty()) registered[label] = (kind == "registered");
        }
    }
    std::map<int, std::vector<TruthRow>> truth;
    {
        std::ifstream f(folder + "\\truth.csv");
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            const auto p = split_line(line, ',');
            if (p.size() < 9) continue;
            TruthRow r;
            r.label = p[2];
            r.box = {static_cast<float>(std::atof(p[3].c_str())), static_cast<float>(std::atof(p[4].c_str())),
                     static_cast<float>(std::atof(p[5].c_str())), static_cast<float>(std::atof(p[6].c_str()))};
            r.visible = std::atoi(p[8].c_str()) != 0;
            truth[std::atoi(p[0].c_str())].push_back(r);
        }
        if (truth.empty()) { std::printf("no truth in %s\\truth.csv\n", folder.c_str()); return 2; }
    }

    // ---- the registered people: the scenario's registered people and N fillers who are not in the video ----
    FeatureStore store;
    std::map<std::string, int> gallery_index;  // label -> position in the gallery
    std::set<std::string> not_registrable;     // registered people whose portrait gives no feature with this profile
    int enrolled = 0, filler_count = 0;
    for (const auto& entry : registered) {
        if (!entry.second) continue;
        Record r;
        r.id = entry.first;
        r.name = entry.first;
        if (!portrait_feature(*detector, *extractor, "data/portraits/" + entry.first + "/117th.jpg", r.feature) || !store.add(r)) {
            // (the classical profile finds the landmarks of a portrait only when it finds both eyes)
            std::printf("cannot register %s: this profile finds no face with landmarks in his portrait; his faces are left out of the matching figures\n",
                        entry.first.c_str());
            not_registrable.insert(entry.first);
            continue;
        }
        gallery_index[entry.first] = store.count() - 1;
        ++enrolled;
    }
    for (const auto& dir : fs::directory_iterator("data/portraits")) {
        if (filler_count >= fillers) break;
        const std::string label = dir.path().filename().string();
        if (!fs::is_directory(dir.path()) || registered.count(label)) continue;
        Record r;
        r.id = label;
        r.name = label;
        if (portrait_feature(*detector, *extractor, (dir.path() / "117th.jpg").string(), r.feature) && store.add(r)) ++filler_count;
    }
    std::printf("people in the footage: %zu (%d registered), registered in the gallery: %d + %d fillers = N = %d\n", registered.size(), enrolled, enrolled,
                filler_count, store.count());

    // ---- the footage ----
    const std::string video = folder + "\\video.mp4";
    std::unique_ptr<FrameSource> source = fs::exists(video) ? open_video_file_source(video) : open_sequence_source(folder, fps);
    if (!source) { std::printf("cannot open the footage in %s\n", folder.c_str()); return 2; }

    // detection / tracking bookkeeping
    struct Bucket { const char* label; float lo, hi; int truth, found; };  // (no default member values: Visual Studio 2015 cannot brace-initialize those)
    Bucket buckets[] = {{"< 100 px", 0, 100, 0, 0}, {"100-199 px", 100, 200, 0, 0}, {"200-299 px", 200, 300, 0, 0}, {">= 300 px", 300, 1e9f, 0, 0}};
    int truth_faces = 0, found_faces = 0, detections = 0, false_detections = 0, frames = 0;
    double iou_sum = 0;
    TimingStats detect_ms, track_ms;
    FaceTracker tracker;
    std::map<std::string, std::set<int>> track_ids;  // actor -> every track ID that followed him
    std::map<std::string, int> last_track, id_changes;
    // extraction / matching bookkeeping
    int extract_candidates = 0, gate_passed = 0;
    TimingStats refine_ms, crop_ms, extract_ms, match_ms, batch_size;
    int genuine_total = 0, genuine_rank1 = 0, genuine_accepted = 0;
    int stranger_total = 0, stranger_accepted = 0;
    float stranger_max = -1, genuine_min = 2;
    double genuine_sum = 0;
    int image_w = 0, image_h = 0;

    Image frame;
    double t_ms = 0;
    for (int index = 0; source->next(frame, t_ms); ++index) {
        image_w = frame.width;
        image_h = frame.height;
        const auto truth_it = truth.find(index);
        ++frames;

        // 1. detection
        std::vector<Face> faces;
        double a = now_ms();
        detector->detect(frame, faces);
        detect_ms.add(now_ms() - a);
        detections += static_cast<int>(faces.size());
        // 2. tracking
        a = now_ms();
        tracker.update(faces);
        track_ms.add(now_ms() - a);

        std::vector<char> face_used(faces.size(), 0);
        std::vector<std::pair<const TruthRow*, int>> pairs;  // truth face -> index of the detection that found it
        if (truth_it != truth.end()) {
            for (const TruthRow& tr : truth_it->second) {
                if (!tr.visible) continue;
                ++truth_faces;
                Bucket* bucket = &buckets[3];
                for (Bucket& b : buckets)
                    if (tr.box.w >= b.lo && tr.box.w < b.hi) bucket = &b;
                ++bucket->truth;
                int best = -1;
                float best_iou = 0.5f;
                for (size_t k = 0; k < faces.size(); ++k) {
                    const float v = iou(faces[k].box, tr.box);
                    if (v >= best_iou) { best_iou = v; best = static_cast<int>(k); }
                }
                if (best >= 0) {
                    ++found_faces;
                    ++bucket->found;
                    iou_sum += best_iou;
                    face_used[best] = 1;
                    pairs.push_back({&tr, best});
                }
                // which track follows this person?
                int track = 0;
                float track_iou = 0.4f;
                for (const Track& t : tracker.tracks()) {
                    const float v = iou(t.face.box, tr.box);
                    if (t.updated && v >= track_iou) { track_iou = v; track = t.id; }
                }
                if (track) {
                    track_ids[tr.label].insert(track);
                    if (last_track.count(tr.label) && last_track[tr.label] != track) ++id_changes[tr.label];
                    last_track[tr.label] = track;
                }
            }
        }
        for (size_t k = 0; k < faces.size(); ++k)
            if (!face_used[k]) ++false_detections;

        // 3. extraction and 4. matching for every K-th frame: the faces that the pipeline would take a crop of
        if (index % every != 0) continue;
        std::vector<Feature> queries;
        std::vector<const TruthRow*> who;
        for (const auto& p : pairs) {
            const Face& coarse = faces[p.second];
            if (coarse.box.w < FA_MIN_CROP_FACE_PIXELS) continue;
            ++extract_candidates;
            Face refined;
            a = now_ms();
            const bool refined_ok = refine_face(pose_detector, frame, coarse, refined);
            refine_ms.add(now_ms() - a);
            if (!refined_ok || !pose_within_tolerance(refined.pose)) continue;  // the pose gate
            ++gate_passed;
            FaceCrop crop;
            Feature f;
            a = now_ms();
            const bool cropped = make_face_crop(frame, refined, crop);
            const double crop_time = now_ms() - a;
            a = now_ms();
            const bool extracted = cropped && extractor->extract(crop, f);
            if (!extracted) continue;
            crop_ms.add(crop_time);
            extract_ms.add(now_ms() - a);
            queries.push_back(std::move(f));
            who.push_back(p.first);
        }
        if (queries.empty() || store.count() == 0) continue;
        std::vector<MatchResult> results;
        a = now_ms();
        store.matcher().match(queries, results);  // M faces against all N registered people in one call
        match_ms.add(now_ms() - a);
        batch_size.add(static_cast<double>(queries.size()));
        for (size_t q = 0; q < queries.size(); ++q) {
            const std::string& label = who[q]->label;
            if (not_registrable.count(label)) continue;  // no gallery entry for him: neither a genuine nor a stranger comparison
            const bool is_registered = registered[label];
            if (is_registered) {
                const int own = gallery_index[label];
                // similarity to himself: the matcher gives only the best, so compute it from the gallery feature
                float own_score = 0;
                for (int d = 0; d < queries[q].dim(); ++d) own_score += queries[q].values[d] * store.records()[own].feature.values[d];
                ++genuine_total;
                genuine_sum += own_score;
                genuine_min = std::min(genuine_min, own_score);
                if (results[q].index == own) ++genuine_rank1;
                if (results[q].index == own && results[q].score >= profile.match_threshold) ++genuine_accepted;
            } else {
                ++stranger_total;
                stranger_max = std::max(stranger_max, results[q].score);
                if (results[q].score >= profile.match_threshold) ++stranger_accepted;
            }
        }
    }

    // ---- the report ----
    std::printf("\nfootage %s: %d frames of %dx%d, %d true faces, %zu people\n", folder.c_str(), frames, image_w, image_h, truth_faces, registered.size());
    std::printf("\nDETECTION (%s)\n", detector->name());
    std::printf("  true faces found (IoU >= 0.5)   %d / %d = %.1f%%   mean IoU of the found boxes %.3f\n", found_faces, truth_faces,
                truth_faces ? 100.0 * found_faces / truth_faces : 0.0, found_faces ? iou_sum / found_faces : 0.0);
    for (const Bucket& b : buckets)
        if (b.truth > 0) std::printf("    face width %-10s %5d / %5d = %5.1f%%\n", b.label, b.found, b.truth, 100.0 * b.found / b.truth);
    std::printf("  detections without a true face   %d in %d frames = %.3f per frame\n", false_detections, frames, frames ? static_cast<double>(false_detections) / frames : 0.0);
    std::printf("  time per frame at %dx%d          detect: median %.1f ms, p95 %.1f, max %.1f   (limit 60 ms for detection + tracking)\n", image_w, image_h,
                detect_ms.median(), detect_ms.p95(), detect_ms.max());
    std::printf("TRACKING\n");
    int max_ids = 0, total_changes = 0;
    for (const auto& e : track_ids) max_ids = std::max(max_ids, static_cast<int>(e.second.size()));
    for (const auto& e : id_changes) total_changes += e.second;
    std::printf("  track IDs per person             most: %d (1 = every person keeps one ID), ID changes in all: %d\n", max_ids, total_changes);
    std::printf("  tracker time per frame           median %.3f ms, max %.3f ms\n", track_ms.median(), track_ms.max());
    std::printf("  detection + tracking             median %.1f ms, p95 %.1f ms, max %.1f ms\n", detect_ms.median() + track_ms.median(),
                detect_ms.p95() + track_ms.p95(), detect_ms.max() + track_ms.max());
    std::printf("FEATURE EXTRACTION (%s, %d numbers; every %d. frame, faces >= %d px)\n", extractor->name(), extractor->dim(), every, FA_MIN_CROP_FACE_PIXELS);
    std::printf("  faces looked at                  %d, passed the pose gate: %d (%.0f%%)\n", extract_candidates, gate_passed,
                extract_candidates ? 100.0 * gate_passed / extract_candidates : 0.0);
    std::printf("  time per face                    pose check %.1f ms | crop+align %.2f ms | extract %.1f ms   (limit 150 ms per face for the extraction)\n",
                refine_ms.median(), crop_ms.median(), extract_ms.median());
    std::printf("MATCHING (M faces against N = %d registered people, threshold %.2f)\n", store.count(), profile.match_threshold);
    if (genuine_total > 0)
        std::printf("  registered people                %d faces: similarity to himself mean %.3f, min %.3f; right person first (rank 1) %.1f%%; accepted at the threshold %.1f%%\n",
                    genuine_total, genuine_sum / genuine_total, genuine_min, 100.0 * genuine_rank1 / genuine_total, 100.0 * genuine_accepted / genuine_total);
    if (stranger_total > 0)
        std::printf("  strangers                        %d faces: highest similarity to anybody %.3f; accepted at the threshold: %d (%.1f%%)\n", stranger_total,
                    stranger_max, stranger_accepted, 100.0 * stranger_accepted / stranger_total);
    std::printf("  M:N call                         %zu calls, M = %.1f faces on average, median %.3f ms, max %.3f ms\n", match_ms.count(), batch_size.median(),
                match_ms.median(), match_ms.max());
    return 0;
}
