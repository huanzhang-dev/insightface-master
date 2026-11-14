// detector.cpp - helpers shared by all detectors, and the create_detector factory.
#include "detector.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "image_ops.h"
#include "pose.h"

namespace fa {

float iou(const Rect& a, const Rect& b) {
    const float left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const float right = std::min(a.right(), b.right()), bottom = std::min(a.bottom(), b.bottom());
    const float inter = std::max(0.0f, right - left) * std::max(0.0f, bottom - top);
    const float uni = a.area() + b.area() - inter;
    return uni > 0 ? inter / uni : 0.0f;
}

// Non-maximum suppression: sort by score, then drop every box that overlaps an
// already kept (better) box too much.
void non_max_suppression(std::vector<Face>& faces, float iou_threshold) {
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.score > b.score; });
    std::vector<Face> kept;
    for (const Face& candidate : faces) {
        bool suppressed = false;
        for (const Face& k : kept) {
            if (iou(candidate.box, k.box) > iou_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) kept.push_back(candidate);
    }
    faces.swap(kept);
}

bool refine_face(FaceDetector& detector, const Image& frame, const Face& coarse, Face& refined) {
    if (frame.empty() || coarse.box.w < 8 || coarse.box.h < 8) return false;

    // 1. cut out the face with a margin of half a face width/height on every side
    Rect region = {coarse.box.x - 0.5f * coarse.box.w, coarse.box.y - 0.5f * coarse.box.h, 2.0f * coarse.box.w,
                   2.0f * coarse.box.h};
    const float x1 = std::max(0.0f, region.x), y1 = std::max(0.0f, region.y);
    const float x2 = std::min(static_cast<float>(frame.width), region.right());
    const float y2 = std::min(static_cast<float>(frame.height), region.bottom());
    if (x2 - x1 < 8 || y2 - y1 < 8) return false;
    region = {std::floor(x1), std::floor(y1), std::ceil(x2) - std::floor(x1), std::ceil(y2) - std::floor(y1)};
    Image cut;
    crop(frame, region, cut);

    // 2. enlarge so that the face is about FA_REFINE_FACE_WIDTH pixels wide (never shrink)
    float zoom = std::max(1.0f, static_cast<float>(FA_REFINE_FACE_WIDTH) / coarse.box.w);
    if (zoom <= 1.01f) zoom = 1.0f;  // too small a change to be worth a resize; keep the true scale
    Image big;
    if (zoom > 1.0f) resize_bilinear(cut, big, static_cast<int>(cut.width * zoom), static_cast<int>(cut.height * zoom));
    else big = cut;

    // 3. detect again; keep the face that overlaps the old one most
    std::vector<Face> found;
    if (!detector.detect(big, found, true) || found.empty()) return false;
    const Rect expected = {(coarse.box.x - region.x) * zoom, (coarse.box.y - region.y) * zoom, coarse.box.w * zoom,
                           coarse.box.h * zoom};
    const Face* best = nullptr;
    float best_iou = 0.3f;
    for (const Face& f : found) {
        const float v = iou(f.box, expected);
        if (v > best_iou) { best_iou = v; best = &f; }
    }
    if (!best) return false;

    // 3b. Rotation compensation. A landmark network reads a strongly tilted face with a
    // bias (it under-reports the tilt). So we turn the enlarged picture by the tilt we
    // measured, so that the face stands upright, and detect once more. The remaining
    // tilt is then small and is read exactly:  true tilt = turn + remaining tilt.
    Face upright_best = *best;
    if (best->pose.valid && std::fabs(best->pose.roll) >= 3.0f) {
        const float theta = best->pose.roll * 3.14159265f / 180.0f;
        const float cx = best->box.x + best->box.w / 2, cy = best->box.y + best->box.h / 2;
        const float c = std::cos(theta), s = std::sin(theta);
        // "output pixel -> input pixel": turn by +theta about the face centre
        const float to_source[6] = {c, -s, cx - c * cx + s * cy, s, c, cy - s * cx - c * cy};
        Image turned;
        warp_affine(big, to_source, turned, big.width, big.height);
        std::vector<Face> again;
        if (detector.detect(turned, again, true) && !again.empty()) {
            const Face* pick = nullptr;
            float pick_iou = 0.3f;
            for (const Face& f : again) {
                const float v = iou(f.box, best->box);
                if (v > pick_iou) { pick_iou = v; pick = &f; }
            }
            if (pick && pick->has_landmarks) {
                upright_best = *pick;
                // map the landmarks back into the untilted picture (same transform as above)
                for (int k = 0; k < 5; ++k) {
                    const float x = pick->landmarks[k].x, y = pick->landmarks[k].y;
                    upright_best.landmarks[k].x = to_source[0] * x + to_source[1] * y + to_source[2];
                    upright_best.landmarks[k].y = to_source[3] * x + to_source[4] * y + to_source[5];
                }
                upright_best.box = best->box;
            }
        }
    }
    best = &upright_best;

    // 4. back to camera-image coordinates
    refined = *best;
    auto back_x = [&](float x) { return x / zoom + region.x; };
    auto back_y = [&](float y) { return y / zoom + region.y; };
    refined.box = {back_x(best->box.x), back_y(best->box.y), best->box.w / zoom, best->box.h / zoom};
    for (int k = 0; k < 5; ++k) {
        refined.landmarks[k].x = back_x(best->landmarks[k].x);
        refined.landmarks[k].y = back_y(best->landmarks[k].y);
    }
    // The pose is computed from the final landmarks (already mapped back into the
    // untilted picture, so the roll includes the turn), with the model measured for
    // refined landmarks, which sit slightly differently from the coarse ones.
    if (std::string(detector.type()) == "haar") {
        // The Haar detector finds only the eyes; nose and mouth are placed like an average
        // face, so the landmark formulas would read yaw and pitch out of an assumption, not
        // out of the picture. Keep the detector's own yaw and pitch (from where the eyes sit
        // in the face box) and take only the roll from the eye line.
        const float dx = refined.landmarks[1].x - refined.landmarks[0].x;
        const float dy = refined.landmarks[1].y - refined.landmarks[0].y;
        refined.pose = best->pose;
        refined.pose.roll = std::atan2(dy, dx) * 180.0f / 3.14159265f;
        refined.pose.valid = best->has_landmarks;
    } else {
        refined.pose = estimate_pose(refined, pose_model_for(std::string(detector.type()) + "-refined"));
    }
    return true;
}

std::unique_ptr<FaceDetector> create_detector(const ModelProfile& profile) {
    if (profile.detector_type == "yunet" || profile.detector_type == "scrfd") return create_deep_detector(profile);
    if (profile.detector_type == "haar") return create_haar_detector(profile);
    std::cerr << "detector: unknown detector.type '" << profile.detector_type << "'\n";
    return nullptr;
}

std::unique_ptr<FaceDetector> create_pose_detector(const ModelProfile& profile) {
    if (profile.pose_detector_profile.empty()) return create_detector(profile);
    ModelProfile pose;
    // The named file's relative model paths start at the same project root as this profile's.
    if (!load_profile(profile.pose_detector_profile, profile.root, pose)) {
        std::cerr << "detector: cannot use pose.detector_profile '" << profile.pose_detector_profile << "'\n";
        return nullptr;
    }
    if (!pose.pose_detector_profile.empty()) {
        std::cerr << "detector: " << profile.pose_detector_profile << " names a pose.detector_profile itself; only one level is allowed\n";
        return nullptr;
    }
    pose.threads = profile.threads;  // the caller decides how many CPU threads the networks may use
    return create_detector(pose);
}

}  // namespace fa
