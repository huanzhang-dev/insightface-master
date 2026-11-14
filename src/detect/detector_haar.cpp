// detector_haar.cpp - the classical (non-deep) face detector.
//
// Steps for one camera image:
//   1. Convert to gray and shrink to FA_HAAR_WORK_WIDTH pixels wide (speed; wider for wide pictures).
//   2. Run the face cascade (haar_cascade.h) to find face boxes.
//   3. Inside each face box look for the two eyes with the eye cascade.
//   4. From the eyes work out roll (exact) and approximate yaw / pitch, and
//      build the 5 landmarks the recognizers need for alignment.
//
// LIMITS (written down on purpose): a Haar cascade only fires for faces that look
// almost straight at the camera, and without a landmark network the yaw and
// pitch below are rough estimates (+-5..10 degrees). The deep detectors in
// detector_deep.cpp are the accurate path; this one is the classical alternative.
#include <algorithm>
#include <cmath>
#include <iostream>

#include "detector.h"
#include "haar_cascade.h"
#include "image_ops.h"
#include "pose.h"

// Speed settings of the classical detector (all can be changed with /D... at build time).
// The camera image is shrunk to this width before scanning. A smaller image is much
// faster but cannot see small faces: the smallest face is 24 working pixels wide, i.e.
// 24 * (camera width / working width) camera pixels (96 px for a 1280 px camera image).
#ifndef FA_HAAR_WORK_WIDTH
#define FA_HAAR_WORK_WIDTH 320
#endif
// A camera image wider than FA_HAAR_REFERENCE_WIDTH gets a proportionally wider working image (320 * width /
// 1280), so that the smallest visible face stays about 96 camera pixels instead of growing with the picture
// (it was 144 px at 1920 wide), but never wider than FA_HAAR_MAX_WORK_WIDTH, which bounds the scan time:
// above 1920 pixels the smallest face grows again (128 px at 2560, 192 px at 3840). See haar_work_width().
#ifndef FA_HAAR_REFERENCE_WIDTH
#define FA_HAAR_REFERENCE_WIDTH 1280
#endif
#ifndef FA_HAAR_MAX_WORK_WIDTH
#define FA_HAAR_MAX_WORK_WIDTH 480
#endif
// Every this many frames the whole image is scanned for NEW faces; frames in between
// only follow the known faces.
#ifndef FA_HAAR_FULL_SCAN_INTERVAL
#define FA_HAAR_FULL_SCAN_INTERVAL 8
#endif
// Size ratio between two scan levels. Larger = fewer levels = faster, but a face of an
// in-between size is matched less well.
#ifndef FA_HAAR_SCALE_STEP
#define FA_HAAR_SCALE_STEP 1.25f
#endif

namespace fa {
namespace {

const float kPi = 3.14159265358979f;

// The width a camera image of `camera_width` pixels is shrunk to before it is scanned: FA_HAAR_WORK_WIDTH
// for pictures up to FA_HAAR_REFERENCE_WIDTH wide, growing in proportion above that, at most FA_HAAR_MAX_WORK_WIDTH.
// (A picture narrower than this is not shrunk at all.)
int haar_work_width(int camera_width) {
    const double wanted = FA_HAAR_WORK_WIDTH * std::max(1.0, static_cast<double>(camera_width) / FA_HAAR_REFERENCE_WIDTH);
    return static_cast<int>(std::max<double>(FA_HAAR_WORK_WIDTH, std::min<double>(wanted, FA_HAAR_MAX_WORK_WIDTH)));
}

class HaarDetector : public FaceDetector {
public:
    bool load(const ModelProfile& profile) {
        threads_ = profile.threads;
        if (!face_.load(profile.haar_face_model)) return false;
        has_eyes_ = !profile.haar_eye_model.empty() && eyes_.load(profile.haar_eye_model);
        return true;
    }

    const char* name() const override { return "haar/classical"; }
    const char* type() const override { return "haar"; }

    // Forget the faces of the previous frames: the next call is a full scan again.
    void reset() override {
        last_boxes_.clear();
        frame_counter_ = 0;
        last_width_ = last_height_ = 0;
    }

    bool detect(const Image& frame, std::vector<Face>& faces, bool /*accurate*/ = false) override {
        faces.clear();
        if (frame.empty()) return false;

        // 1. gray + shrink
        to_gray(frame, gray_);
        const int work_width = haar_work_width(frame.width);
        const float ratio = frame.width > work_width ? static_cast<float>(work_width) / frame.width : 1.0f;
        if (ratio < 1.0f) resize_bilinear(gray_, work_, work_width, static_cast<int>(frame.height * ratio));
        else work_ = gray_;

        // 2. faces. Two modes:
        //    FULL SCAN     the whole working image, all sizes; needed to find NEW faces.
        //                  Done on the first frame, every FA_HAAR_FULL_SCAN_INTERVAL frames,
        //                  and whenever no face was known.
        //    FOLLOW SCAN   in between, only a window around each face of the previous frame
        //                  and only sizes close to its old size. A face cannot move far
        //                  between two frames, so this is much cheaper.
        ++frame_counter_;
        const bool size_changed = work_.width != last_width_ || work_.height != last_height_;
        last_width_ = work_.width;
        last_height_ = work_.height;
        const bool full_scan = last_boxes_.empty() || size_changed || frame_counter_ % FA_HAAR_FULL_SCAN_INTERVAL == 0;

        std::vector<HaarHit> hits;
        if (full_scan) {
            const int min_size = std::max(face_.window_width(), static_cast<int>(FA_MIN_FACE_PIXELS * ratio));
            face_.detect(work_, min_size, work_.width, FA_HAAR_SCALE_STEP, 4, hits, threads_);
        } else {
            for (const Rect& previous : last_boxes_) {
                // search window: the old box enlarged by 30% on every side
                Rect roi = {previous.x - 0.3f * previous.w, previous.y - 0.3f * previous.h, 1.6f * previous.w, 1.6f * previous.h};
                const float x1 = std::max(0.0f, roi.x), y1 = std::max(0.0f, roi.y);
                const float x2 = std::min(static_cast<float>(work_.width), roi.right());
                const float y2 = std::min(static_cast<float>(work_.height), roi.bottom());
                if (x2 - x1 < face_.window_width() || y2 - y1 < face_.window_height()) continue;
                roi = {std::floor(x1), std::floor(y1), std::floor(x2) - std::floor(x1), std::floor(y2) - std::floor(y1)};
                crop(work_, roi, roi_image_);
                std::vector<HaarHit> local;
                face_.detect(roi_image_, static_cast<int>(previous.w * 0.75f), static_cast<int>(previous.w * 1.35f),
                             FA_HAAR_SCALE_STEP, 3, local, 1);
                if (local.empty()) continue;
                const HaarHit* best = &local[0];
                for (const HaarHit& h : local) if (h.neighbors > best->neighbors) best = &h;
                HaarHit moved = *best;
                moved.box.x += roi.x;
                moved.box.y += roi.y;
                hits.push_back(moved);
            }
        }
        last_boxes_.clear();
        for (const HaarHit& h : hits) last_boxes_.push_back(h.box);

        // 3. one Face per hit, mapped back to the camera image
        for (const HaarHit& h : hits) {
            Face f;
            f.box = {h.box.x / ratio, h.box.y / ratio, h.box.w / ratio, h.box.h / ratio};
            f.score = std::min(1.0f, h.neighbors / 12.0f);  // more agreeing windows = more sure
            if (has_eyes_) find_eyes_and_pose(h.box, ratio, f);
            faces.push_back(f);
            if (static_cast<int>(faces.size()) >= FA_MAX_FACES) break;
        }
        return true;
    }

private:
    // Search the upper half of the face box for two eyes (one on each side).
    void find_eyes_and_pose(const Rect& box_work, float ratio, Face& f) {
        // region of interest in the working image: from 15% to 60% of the face height
        Rect roi = {box_work.x, box_work.y + 0.15f * box_work.h, box_work.w, 0.45f * box_work.h};
        // crop() rounds the corner down; do the same here first, because the eye positions found
        // inside the crop are converted back with this very corner (an unrounded corner would
        // shift both eyes by up to one working pixel and bias yaw and pitch).
        roi.x = std::floor(roi.x);
        roi.y = std::floor(roi.y);
        crop(work_, roi, eye_roi_);
        std::vector<HaarHit> eye_hits;
        const int min_eye = std::max(eyes_.window_width(), static_cast<int>(0.15f * box_work.w));
        const int max_eye = static_cast<int>(0.45f * box_work.w);
        if (max_eye < min_eye) return;
        eyes_.detect(eye_roi_, min_eye, max_eye, 1.15f, 2, eye_hits);

        // the strongest hit in the left half and in the right half of the box
        const HaarHit* left = nullptr;   // on the image-left side = the person's RIGHT eye
        const HaarHit* right = nullptr;
        for (const HaarHit& e : eye_hits) {
            const float cx = e.box.x + e.box.w / 2;
            if (cx < roi.w * 0.5f) { if (!left || e.neighbors > left->neighbors) left = &e; }
            else                   { if (!right || e.neighbors > right->neighbors) right = &e; }
        }
        if (!left || !right) return;

        Point eye0 = {(roi.x + left->box.x + left->box.w / 2) / ratio, (roi.y + left->box.y + left->box.h / 2) / ratio};
        Point eye1 = {(roi.x + right->box.x + right->box.w / 2) / ratio, (roi.y + right->box.y + right->box.h / 2) / ratio};
        const float dx = eye1.x - eye0.x, dy = eye1.y - eye0.y;
        const float eye_distance = std::sqrt(dx * dx + dy * dy);
        if (eye_distance < 4.0f || dx <= 0) return;

        // landmarks: eyes from the detector, nose and mouth placed like an average face
        const Point* tmpl = aligned_template_112();
        const float tdx = tmpl[1].x - tmpl[0].x, tdy = tmpl[1].y - tmpl[0].y;
        const float k = eye_distance / std::sqrt(tdx * tdx + tdy * tdy);       // scale
        const float angle = std::atan2(dy, dx) - std::atan2(tdy, tdx);         // rotation
        const float ca = std::cos(angle), sa = std::sin(angle);
        f.landmarks[0] = eye0;
        f.landmarks[1] = eye1;
        for (int i = 2; i < 5; ++i) {
            const float px = tmpl[i].x - tmpl[0].x, py = tmpl[i].y - tmpl[0].y;
            f.landmarks[i].x = eye0.x + k * (ca * px - sa * py);
            f.landmarks[i].y = eye0.y + k * (sa * px + ca * py);
        }
        f.has_landmarks = true;

        // pose: roll from the eye line, yaw/pitch from where the eyes sit in the box
        const float mid_x = 0.5f * (eye0.x + eye1.x), mid_y = 0.5f * (eye0.y + eye1.y);
        const float box_cx = f.box.x + f.box.w / 2, box_cy = f.box.y + f.box.h / 2;
        const float head_depth = 0.6f * eye_distance;  // distance from the eye plane to the head centre
        const float yaw_sin = std::max(-1.0f, std::min(1.0f, (mid_x - box_cx) / head_depth));
        // In a frontal Haar box the eyes sit about 10% of the box height above the box centre.
        const float pitch_sin = std::max(-1.0f, std::min(1.0f, -((mid_y - box_cy) + 0.10f * f.box.h) / head_depth));
        f.pose.roll = std::atan2(dy, dx) * 180.0f / kPi;
        f.pose.yaw = std::asin(yaw_sin) * 180.0f / kPi;
        f.pose.pitch = std::asin(pitch_sin) * 180.0f / kPi;
        f.pose.valid = true;
    }

    HaarCascade face_, eyes_;
    bool has_eyes_ = false;
    int threads_ = 2;
    Image gray_, work_, eye_roi_, roi_image_;  // reused between frames
    std::vector<Rect> last_boxes_;             // faces of the previous frame (working-image coordinates)
    int frame_counter_ = 0, last_width_ = 0, last_height_ = 0;
};

}  // namespace

std::unique_ptr<FaceDetector> create_haar_detector(const ModelProfile& profile) {
    auto detector = std::make_unique<HaarDetector>();
    if (!detector->load(profile)) {
        std::cerr << "detector: cannot load Haar cascade " << profile.haar_face_model << "\n";
        return nullptr;
    }
    return detector;
}

}  // namespace fa
