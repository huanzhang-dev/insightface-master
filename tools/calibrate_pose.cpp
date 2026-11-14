// calibrate_pose.cpp - measure the proportions of a FRONTAL face as a landmark
// detector reports them (the PoseModel constants in pose.cpp).
//
//   calibrate_pose <profile.ini> [--type T] [--refine] <folder with portraits>
//
// --refine measures the landmarks of the refinement step (refine_face), which is what the
// authentication gate uses; without it the plain detection landmarks are measured. With --refine
// the refinement is run by the profile's pose detector (pose.detector_profile; the detector itself
// when the profile names none), so the printed PoseModel line belongs to THAT detector's "-refined"
// entry in pose.cpp (e.g. yunet-refined for models/dev_noncommercial.ini). --type T measures the
// detector T alone and ignores pose.detector_profile.
// Besides the proportions it prints eye_px (the eye distance in pixels, to see the face sizes
// used), the pitch and yaw statistics, how many of these frontal faces the pose gate accepts with
// the constants now in pose.cpp, and a ready-to-paste PoseModel line with the measured means.
//
// The portraits are official, straight-on photos, so their pitch and yaw are
// close to zero ON AVERAGE. The tool prints, over all detected faces, the mean
// and standard deviation of:
//   nose_x   sideways position of the nose tip (should be ~0)
//   nose_y   nose tip below the eye line       -> PoseModel::nose_y
//   mouth_y  mouth below the eye line          -> PoseModel::mouth_y
// all in eye distances. Only the largest face of each picture is used.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "fs_compat.h"
#include <vector>

#include "detector.h"
#include "image_io.h"
#include "pose.h"

namespace {
void stats(const char* name, std::vector<float>& v) {
    if (v.empty()) return;
    double mean = 0, var = 0;
    for (float x : v) mean += x;
    mean /= v.size();
    for (float x : v) var += (x - mean) * (x - mean);
    std::sort(v.begin(), v.end());
    std::printf("  %-8s mean %+.4f  median %+.4f  std %.4f  (n=%zu)\n", name, mean, v[v.size() / 2],
                std::sqrt(var / v.size()), v.size());
}
}  // namespace

int main(int argc, char** argv) {
    using namespace fa;
    if (argc < 3) { std::printf("usage: calibrate_pose <profile.ini> [--type T] [--refine] <folder>\n"); return 2; }
    ModelProfile profile;
    if (!load_profile(argv[1], ".", profile)) return 2;
    std::string folder;
    bool refine = false;  // measure the landmarks of the refinement step instead of the plain detection
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--type") && i + 1 < argc) { profile.detector_type = argv[++i]; profile.pose_detector_profile.clear(); }
        else if (!std::strcmp(argv[i], "--refine")) refine = true;
        else folder = argv[i];
    }
    auto detector = create_detector(profile);
    if (!detector) return 1;
    // The refinement step is run by the profile's pose detector (pose.detector_profile), which is the
    // detector itself when the profile names none; the plain landmarks always come from `detector`.
    std::unique_ptr<FaceDetector> pose_detector = refine ? create_pose_detector(profile) : nullptr;
    if (refine && !pose_detector) return 1;
    std::printf("detector: %s", detector->name());
    if (refine) std::printf("  (landmarks of the refinement step, made by %s)", pose_detector->name());
    std::printf("\n");

    std::vector<float> nx, ny, my, eye_px, pitches, yaws;
    int pose_n = 0, ok_pitch = 0, ok_yaw = 0, ok_roll = 0, ok_all = 0;
    for (auto& e : fa::fs::recursive_directory_iterator(folder)) {
        if (e.path().extension() != ".jpg") continue;
        Image image;
        std::vector<Face> faces;
        if (!load_image(e.path().string(), image) || !detector->detect(image, faces) || faces.empty()) continue;
        const Face* best = &faces[0];
        for (const Face& f : faces) if (f.box.area() > best->box.area()) best = &f;
        Face refined;
        if (refine) {
            if (!refine_face(*pose_detector, image, *best, refined)) continue;
            best = &refined;
        }
        float x, y, m;
        if (!landmark_geometry(*best, x, y, m)) continue;
        nx.push_back(x); ny.push_back(y); my.push_back(m);
        if (best->pose.valid) {  // how many of these (frontal!) faces would the gate accept with the constants now in pose.cpp?
            ++pose_n;
            if (std::fabs(best->pose.pitch) <= FA_POSE_PITCH_TOL_DEG) ++ok_pitch;
            if (std::fabs(best->pose.yaw) <= FA_POSE_YAW_TOL_DEG) ++ok_yaw;
            if (std::fabs(best->pose.roll) <= FA_POSE_ROLL_TOL_DEG) ++ok_roll;
            if (pose_within_tolerance(best->pose)) ++ok_all;
            pitches.push_back(best->pose.pitch);
            yaws.push_back(best->pose.yaw);
        }
        const float dx = best->landmarks[1].x - best->landmarks[0].x, dy = best->landmarks[1].y - best->landmarks[0].y;
        eye_px.push_back(std::sqrt(dx * dx + dy * dy));
    }
    stats("nose_x", nx);
    stats("nose_y", ny);
    stats("mouth_y", my);
    stats("eye_px", eye_px);
    stats("pitch deg", pitches);
    stats("yaw deg", yaws);
    if (pose_n > 0) {
        // These are straight-on portraits, so a perfect pose estimate would accept (nearly) all of them.
        std::printf("  with the constants now in pose.cpp the gate (pitch %.0f, yaw %.0f, roll %.0f degrees) accepts %d of %d faces (%.0f%%): "
                    "pitch ok %d, yaw ok %d, roll ok %d\n",
                    FA_POSE_PITCH_TOL_DEG, FA_POSE_YAW_TOL_DEG, FA_POSE_ROLL_TOL_DEG, ok_all, pose_n, 100.0 * ok_all / pose_n, ok_pitch, ok_yaw,
                    ok_roll);
    }
    if (!ny.empty()) {
        auto mean = [](const std::vector<float>& v) { double s = 0; for (float a : v) s += a; return s / v.size(); };
        std::printf("  mean values for PoseModel{nose_y, mouth_y, nose_z, mouth_z, nose_x0}:  {%.3ff, %.3ff, 0.45f, 0.0f, %.3ff}\n",
                    mean(ny), mean(my), mean(nx));
    }
    return 0;
}
