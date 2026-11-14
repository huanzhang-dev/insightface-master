// test_detect_units.cpp - tests of the detection engine parts that need no photos:
//   * head pose from landmarks (exact round trip + effect of landmark noise)
//   * the pose tolerance gate
//   * non-maximum suppression and the tracker
//   * loading both Haar cascades
#include <algorithm>  // std::max (Visual Studio 2015 does not include it through other headers)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

#include "detector.h"
#include "haar_cascade.h"
#include "pose.h"
#include "tracker.h"

static int g_failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }   \
        else std::printf("ok:   %s\n", msg);                              \
    } while (0)

using namespace fa;

static Face face_with_pose(float pitch, float yaw, float roll, float eye_distance = 40.0f) {
    Face f;
    synthesize_landmarks(pitch, yaw, roll, 200, 200, eye_distance, f.landmarks);
    f.has_landmarks = true;
    f.box = {200 - eye_distance * 1.2f, 200 - eye_distance * 1.0f, eye_distance * 2.4f, eye_distance * 2.6f};
    f.score = 0.9f;
    return f;
}

static Face box_face(float x, float y, float w, float h, float score = 0.9f) {
    Face f;
    f.box = {x, y, w, h};
    f.score = score;
    return f;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ---------- pose: one axis at a time must be recovered almost exactly ----------
    {
        float worst = 0;
        for (float angle : {-20.0f, -10.0f, -5.0f, -2.0f, 0.0f, 2.0f, 5.0f, 10.0f, 20.0f}) {
            Pose p_pitch = estimate_pose(face_with_pose(angle, 0, 0));
            Pose p_yaw = estimate_pose(face_with_pose(0, angle, 0));
            Pose p_roll = estimate_pose(face_with_pose(0, 0, angle));
            worst = std::max({worst, std::fabs(p_pitch.pitch - angle), std::fabs(p_yaw.yaw - angle),
                              std::fabs(p_roll.roll - angle)});
            // the other two angles must stay near zero
            worst = std::max({worst, std::fabs(p_pitch.yaw), std::fabs(p_pitch.roll), std::fabs(p_yaw.pitch),
                              std::fabs(p_yaw.roll), std::fabs(p_roll.pitch), std::fabs(p_roll.yaw)});
        }
        std::printf("      worst single-axis error: %.3f degrees\n", worst);
        CHECK(worst < 0.5f, "pose: single-axis rotations recovered within 0.5 degrees");
    }

    // ---------- pose: all three angles together, inside the tolerance region ----------
    {
        float worst = 0;
        for (float pitch : {-5.0f, 0.0f, 5.0f})
            for (float yaw : {-5.0f, 0.0f, 5.0f})
                for (float roll : {-8.0f, 0.0f, 8.0f}) {
                    Pose p = estimate_pose(face_with_pose(pitch, yaw, roll));
                    worst = std::max({worst, std::fabs(p.pitch - pitch), std::fabs(p.yaw - yaw), std::fabs(p.roll - roll)});
                }
        std::printf("      worst combined error: %.3f degrees\n", worst);
        CHECK(worst < 1.0f, "pose: combined rotations recovered within 1 degree");
    }

    // ---------- pose: how much do landmark errors disturb the angles? ----------
    // Every real landmark detector is off by a fraction of a pixel. This shows
    // what that does to the +-5 degree gate for different face sizes.
    {
        std::mt19937 rng(1);
        std::normal_distribution<float> noise(0.0f, 0.5f);  // 0.5 pixel standard deviation
        for (float eye_distance : {30.0f, 60.0f, 120.0f}) {
            double sum_yaw = 0, sum_pitch = 0, sum_roll = 0;
            const int n = 2000;
            for (int i = 0; i < n; ++i) {
                Face f = face_with_pose(0, 0, 0, eye_distance);
                for (int k = 0; k < 5; ++k) { f.landmarks[k].x += noise(rng); f.landmarks[k].y += noise(rng); }
                Pose p = estimate_pose(f);
                sum_yaw += p.yaw * p.yaw; sum_pitch += p.pitch * p.pitch; sum_roll += p.roll * p.roll;
            }
            std::printf("      0.5 px landmark noise, eye distance %3.0f px: rms error yaw %.2f  pitch %.2f  roll %.2f deg\n",
                        eye_distance, std::sqrt(sum_yaw / n), std::sqrt(sum_pitch / n), std::sqrt(sum_roll / n));
        }
        std::printf("      (an angle estimate can only be as exact as the landmarks; see docs/RESULTS.md)\n");
    }

    // ---------- the tolerance gate ----------
    {
        CHECK(pose_within_tolerance(estimate_pose(face_with_pose(0, 0, 0))), "gate: frontal face accepted");
        CHECK(pose_within_tolerance(estimate_pose(face_with_pose(4, 4, 7))), "gate: 4/4/7 degrees accepted");
        CHECK(!pose_within_tolerance(estimate_pose(face_with_pose(7, 0, 0))), "gate: pitch 7 rejected");
        CHECK(!pose_within_tolerance(estimate_pose(face_with_pose(0, 7, 0))), "gate: yaw 7 rejected");
        CHECK(!pose_within_tolerance(estimate_pose(face_with_pose(0, 0, 10))), "gate: roll 10 rejected (limit 8)");
        CHECK(pose_within_tolerance(estimate_pose(face_with_pose(0, 0, 7.5f))), "gate: roll 7.5 accepted (limit 8)");
        Face no_landmarks = box_face(0, 0, 50, 50);
        CHECK(!pose_within_tolerance(estimate_pose(no_landmarks)), "gate: face without landmarks is never accepted");
    }

    // ---------- non-maximum suppression ----------
    {
        std::vector<Face> faces = {box_face(100, 100, 80, 80, 0.7f), box_face(104, 102, 80, 80, 0.9f),
                                   box_face(400, 300, 80, 80, 0.8f)};
        non_max_suppression(faces, 0.3f);
        CHECK(faces.size() == 2 && faces[0].score == 0.9f, "nms: overlapping boxes merged, best score kept");
        CHECK(std::fabs(iou(box_face(0, 0, 10, 10).box, box_face(5, 0, 10, 10).box) - 1.0f / 3.0f) < 1e-5f,
              "iou: half-overlapping boxes give 1/3");
    }

    // ---------- tracker ----------
    {
        FaceTracker tracker;
        // two faces walking in different directions for 20 frames
        int id_a = 0, id_b = 0;
        bool ids_stable = true;
        for (int frame = 0; frame < 20; ++frame) {
            std::vector<Face> dets = {box_face(100 + 8.0f * frame, 100, 90, 90), box_face(800 - 8.0f * frame, 300, 90, 90)};
            tracker.update(dets);
            if (tracker.tracks().size() != 2) ids_stable = false;
            if (frame == 0) { id_a = tracker.tracks()[0].id; id_b = tracker.tracks()[1].id; }
            else if (tracker.tracks()[0].id != id_a || tracker.tracks()[1].id != id_b) ids_stable = false;
        }
        CHECK(ids_stable && id_a != id_b, "tracker: two moving faces keep their IDs for 20 frames");

        // a face disappears for 3 frames and comes back: same ID
        tracker.reset();
        tracker.update({box_face(100, 100, 90, 90)});
        const int id = tracker.tracks()[0].id;
        for (int i = 0; i < 3; ++i) tracker.update({});
        tracker.update({box_face(102, 100, 90, 90)});
        CHECK(tracker.tracks().size() == 1 && tracker.tracks()[0].id == id, "tracker: short disappearance keeps the ID");

        // a face missing for too long is deleted
        for (int i = 0; i < FA_TRACK_MAX_MISSED_FRAMES + 2; ++i) tracker.update({});
        CHECK(tracker.tracks().empty(), "tracker: lost track is removed after FA_TRACK_MAX_MISSED_FRAMES");

        // a fast mover (jumps 40 px per frame, its box is only 90 px wide) is still followed
        tracker.reset();
        bool followed = true;
        int fast_id = 0;
        for (int frame = 0; frame < 15; ++frame) {
            tracker.update({box_face(50 + 40.0f * frame, 200, 90, 90)});
            if (frame == 0) fast_id = tracker.tracks()[0].id;
            if (tracker.tracks().size() != 1 || tracker.tracks()[0].id != fast_id) followed = false;
        }
        CHECK(followed, "tracker: fast movement (40 px per frame) keeps one ID (motion prediction)");
    }

    // ---------- the two Haar cascades load ----------
    {
        HaarCascade face, eye;
        CHECK(face.load("models/permissive/haar_frontalface.xml") && face.window_width() == 24,
              "haar: frontal-face cascade loads (24x24 window)");
        CHECK(eye.load("models/permissive/haar_eye.xml") && eye.window_width() == 20,
              "haar: eye cascade loads (20x20 window)");
        // a flat gray image contains no face
        Image flat;
        flat.create(320, 240, 1);
        for (auto& v : flat.data) v = 128;
        std::vector<HaarHit> hits;
        face.detect(flat, 24, 240, 1.2f, 3, hits);
        CHECK(hits.empty(), "haar: no face found in a flat image");
    }

    // ---------- the pose detector of a profile (pose.detector_profile) ----------
    {
        auto write = [](const char* path, const char* text) { std::ofstream(path) << text; };
        ModelProfile permissive, dev;
        CHECK(load_profile("models/permissive.ini", ".", permissive) && load_profile("models/dev_noncommercial.ini", ".", dev),
              "pose detector: the two shipped profiles load");
        CHECK(permissive.pose_detector_profile.empty(), "pose detector: the permissive profile names none");
        CHECK(!dev.pose_detector_profile.empty() && dev.detector_type == "scrfd",
              "pose detector: the SCRFD profile names another profile for the pose check");

        // No key: a second instance of the profile's own detector.
        auto own = create_pose_detector(permissive);
        CHECK(own && std::string(own->type()) == "yunet", "pose detector: without the key it is the profile's own detector type");
        // With the key: the detector of the named profile, although the profile's own is SCRFD.
        auto other = create_pose_detector(dev);
        CHECK(other && std::string(other->type()) == "yunet", "pose detector: the SCRFD profile gets a YuNet pose detector");
        auto main_detector = create_detector(dev);
        CHECK(main_detector && std::string(main_detector->type()) == "scrfd",
              "pose detector: the SCRFD profile's own detector is still SCRFD");

        // Wrong use is refused with a message instead of silently using something else.
        ModelProfile bad;
        write("build/pose_missing.ini", "pose.detector_profile = build/does_not_exist.ini\n");
        CHECK(load_profile("build/pose_missing.ini", ".", bad) && create_pose_detector(bad) == nullptr,
              "pose detector: a named profile file that does not exist is refused");
        write("build/pose_b.ini", "pose.detector_profile = models/permissive.ini\n");
        write("build/pose_a.ini", "pose.detector_profile = build/pose_b.ini\n");
        ModelProfile chained;
        CHECK(load_profile("build/pose_a.ini", ".", chained) && create_pose_detector(chained) == nullptr,
              "pose detector: a named profile that names another one itself is refused");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
