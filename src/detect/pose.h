// pose.h - head rotation (pitch, yaw, roll) from the 5 face landmarks.
//
// Angle conventions (all in degrees):
//   roll  > 0 : the head is tilted clockwise as seen on the screen
//               (the eye on the right side of the image is lower)
//   yaw   > 0 : the nose points toward the RIGHT side of the image
//   pitch > 0 : the person looks UP (the chin rises)
//
// The method is a closed-form formula based on a simple 3-D model of an
// average face (see pose.cpp). It is cheap (a few multiplications) so it runs
// for every face on every frame.
#ifndef FA_POSE_H
#define FA_POSE_H

#include <string>

#include "types.h"

namespace fa {

// The proportions of an average FRONTAL face as a particular landmark detector
// reports them, in units of the eye distance. Different detectors place the nose
// and mouth landmarks slightly differently, so each one has its own model, which
// is measured from real frontal faces by tools/calibrate_pose.
struct PoseModel {
    float nose_y = 0.570f;   // nose tip below the line through the eyes
    float mouth_y = 0.960f;  // middle of the mouth corners below that line
    float nose_z = 0.45f;    // how far the nose sticks out in front of the eye plane (anatomy)
    float mouth_z = 0.0f;    // how far the mouth is in front of the eye plane (anatomy)
    float nose_x0 = -0.024f;  // sideways offset of the nose tip of a frontal face (a detector habit)
    PoseModel() {}
    PoseModel(float nose_y_, float mouth_y_, float nose_z_, float mouth_z_, float nose_x0_)
        : nose_y(nose_y_), mouth_y(mouth_y_), nose_z(nose_z_), mouth_z(mouth_z_), nose_x0(nose_x0_) {}
};

// The model to use for "yunet", "scrfd", "yunet-refined", "scrfd-refined" (landmarks of the
// refinement step) or anything else (default model). The Haar detector does not use it.
const PoseModel& pose_model_for(const std::string& detector_type);

// Position of the nose and mouth relative to the eyes, after turning the face so
// the eye line is horizontal, in eye distances: nose_x (sideways from the middle
// of the eyes), nose_y and mouth_y (downwards). Used for calibration and by
// estimate_pose. Returns false if the face has no usable landmarks.
bool landmark_geometry(const Face& face, float& nose_x, float& nose_y, float& mouth_y);

// Estimate the rotation from the landmarks of `face`. Returns an invalid pose
// (valid == false) when the face has no landmarks or the eyes coincide.
Pose estimate_pose(const Face& face, const PoseModel& model);
Pose estimate_pose(const Face& face);  // uses the default model

// True when the pose is valid and |pitch|, |yaw|, |roll| are inside the
// tolerances FA_POSE_PITCH_TOL_DEG, FA_POSE_YAW_TOL_DEG, FA_POSE_ROLL_TOL_DEG.
bool pose_within_tolerance(const Pose& pose);

// Build the five landmarks of an average face with the given rotation, centred
// at (cx, cy) with eye distance `eye_distance` pixels. Used by the tests to
// create faces with a known rotation.
void synthesize_landmarks(float pitch_deg, float yaw_deg, float roll_deg, float cx, float cy,
                          float eye_distance, Point landmarks[5], const PoseModel& model = PoseModel());

}  // namespace fa

#endif  // FA_POSE_H
