// pose.cpp - see pose.h.
//
// THE 3-D MODEL
// Everything is measured in units of the distance D between the two eyes.
// Coordinates: x = to the right in the image, y = DOWN, z = toward the camera.
// The origin is the middle between the eyes. An average frontal face then has
//
//   right eye   (-0.5, 0.000, 0)           left eye   (+0.5, 0.000, 0)
//   nose tip    ( 0.0, nose_y, nose_z)     mouth      ( 0.0, mouth_y, mouth_z)
//
// nose_y and mouth_y are measured per landmark detector from real frontal faces
// (see PoseModel in pose.h), so an average frontal face gives 0 degrees.
// nose_z (how far the nose sticks out) and mouth_z are rough anatomical values.
//
// THE FORMULAS (orthographic camera; yaw is applied first, then pitch, then roll)
//   roll : angle of the line through the eyes.
//   yaw  : the nose tip moves sideways by nose_z * sin(yaw) while the eye
//          distance shrinks to D * cos(yaw), hence  tan(yaw) = (nose_x / eye_dist) / nose_z.
//   pitch: the ratio r = (nose height below the eyes) / (mouth height below the
//          eyes) does not depend on the face size. With the rotation applied it
//          equals (nose_y - nose_z cos(yaw) tan(p)) / (mouth_y - mouth_z cos(yaw) tan(p));
//          solving that for tan(p) gives the code below.
#include "pose.h"

#include <algorithm>
#include <cmath>

namespace fa {
namespace {

const float kPi = 3.14159265358979f;

float deg(float rad) { return rad * 180.0f / kPi; }
float rad(float degrees) { return degrees * kPi / 180.0f; }

}  // namespace

const PoseModel& pose_model_for(const std::string& detector_type) {
    // Measured with tools/calibrate_pose on the frontal portraits in data/portraits
    // (see docs/RESULTS.md for the numbers and their spread).
    // Measured on the public-domain portraits in data/portraits (n = 135 faces). nose_y /
    // mouth_y / nose_x0 are the means; see docs/RESULTS.md. Measured again on 185 faces
    // (153 people): every mean moved by 0.004 or less, so the constants were kept; the
    // spread, not the mean, limits how many straight-on faces pass the gate.
    static const PoseModel kDefault;  // = the YuNet numbers (see the defaults in pose.h)
    static const PoseModel kYunet = {0.570f, 0.960f, 0.45f, 0.0f, -0.024f};
    static const PoseModel kScrfd = {0.604f, 0.973f, 0.45f, 0.0f, -0.034f};
    // Landmarks of the refinement step (zoomed-in second look), measured with
    // tools/calibrate_pose --refine on the same 135 portraits (re-checked on 185, see above).
    // These are the ones the authentication gate uses.
    static const PoseModel kYunetRefined = {0.567f, 0.986f, 0.45f, 0.0f, -0.017f};
    static const PoseModel kScrfdRefined = {0.607f, 0.976f, 0.45f, 0.0f, -0.036f};
    if (detector_type == "yunet") return kYunet;
    if (detector_type == "scrfd") return kScrfd;
    if (detector_type == "yunet-refined") return kYunetRefined;
    if (detector_type == "scrfd-refined") return kScrfdRefined;
    return kDefault;
}

bool landmark_geometry(const Face& face, float& nose_x, float& nose_y, float& mouth_y) {
    if (!face.has_landmarks) return false;
    const Point* lm = face.landmarks;

    const float ex = lm[1].x - lm[0].x, ey = lm[1].y - lm[0].y;
    const float eye_distance = std::sqrt(ex * ex + ey * ey);
    if (eye_distance < 1e-3f) return false;
    const float roll = std::atan2(ey, ex);
    const float c = std::cos(roll), s = std::sin(roll);

    // Rotate everything so the eye line is horizontal, origin at the eye centre.
    const float mid_x = 0.5f * (lm[0].x + lm[1].x), mid_y = 0.5f * (lm[0].y + lm[1].y);
    auto unrolled = [&](Point p, float& x, float& y) {
        const float dx = p.x - mid_x, dy = p.y - mid_y;
        x = (c * dx + s * dy) / eye_distance;  // in eye distances
        y = (-s * dx + c * dy) / eye_distance;
    };
    float m1x, m1y, m2x, m2y;
    unrolled(lm[2], nose_x, nose_y);
    unrolled(lm[3], m1x, m1y);
    unrolled(lm[4], m2x, m2y);
    mouth_y = 0.5f * (m1y + m2y);
    return true;
}

Pose estimate_pose(const Face& face, const PoseModel& model) {
    Pose pose;
    float nose_x, nose_y, mouth_y;
    if (!landmark_geometry(face, nose_x, nose_y, mouth_y)) return pose;
    const float roll = std::atan2(face.landmarks[1].y - face.landmarks[0].y, face.landmarks[1].x - face.landmarks[0].x);

    // --- yaw ---
    const float yaw = std::atan((nose_x - model.nose_x0) / model.nose_z);

    // --- pitch ---
    // r = nose_y / mouth_y ; solve  r*(MY - MZ*cy*T) = NY - NZ*cy*T  for T = tan(pitch)
    float pitch = 0.0f;
    if (std::fabs(mouth_y) > 1e-3f) {
        const float r = nose_y / mouth_y;
        const float denominator = std::cos(yaw) * (model.nose_z - r * model.mouth_z);
        if (std::fabs(denominator) > 1e-3f) pitch = std::atan((model.nose_y - r * model.mouth_y) / denominator);
    }

    pose.pitch = deg(pitch);
    pose.yaw = deg(yaw);
    pose.roll = deg(roll);
    pose.valid = true;
    return pose;
}

Pose estimate_pose(const Face& face) { return estimate_pose(face, PoseModel()); }

bool pose_within_tolerance(const Pose& pose) {
    return pose.valid && std::fabs(pose.pitch) <= FA_POSE_PITCH_TOL_DEG &&
           std::fabs(pose.yaw) <= FA_POSE_YAW_TOL_DEG && std::fabs(pose.roll) <= FA_POSE_ROLL_TOL_DEG;
}

void synthesize_landmarks(float pitch_deg, float yaw_deg, float roll_deg, float cx, float cy, float eye_distance,
                          Point out[5], const PoseModel& m) {
    // The 3-D points of the average face (see the top of this file). Order:
    // right eye, left eye, nose, right mouth corner, left mouth corner.
    const float model[5][3] = {
        {-0.5f, 0.0f, 0.0f},
        {+0.5f, 0.0f, 0.0f},
        {m.nose_x0, m.nose_y, m.nose_z},
        {-0.38f, m.mouth_y, m.mouth_z},
        {+0.38f, m.mouth_y, m.mouth_z},
    };
    const float yaw = rad(yaw_deg), pitch = rad(pitch_deg), roll = rad(roll_deg);
    for (int i = 0; i < 5; ++i) {
        float x = model[i][0], y = model[i][1], z = model[i][2];
        // 1. yaw: turn about the vertical axis (nose moves to +x for positive yaw)
        float x1 = x * std::cos(yaw) + z * std::sin(yaw);
        float z1 = -x * std::sin(yaw) + z * std::cos(yaw);
        // 2. pitch: tilt about the horizontal axis (looking up moves the nose up = smaller y)
        float y2 = y * std::cos(pitch) - z1 * std::sin(pitch);
        // 3. roll: rotate in the image plane (orthographic projection drops z)
        float rx = x1 * std::cos(roll) - y2 * std::sin(roll);
        float ry = x1 * std::sin(roll) + y2 * std::cos(roll);
        out[i].x = cx + rx * eye_distance;
        out[i].y = cy + ry * eye_distance;
    }
}

}  // namespace fa
