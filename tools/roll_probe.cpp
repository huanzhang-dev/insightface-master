// roll_probe.cpp - does the detector really follow an in-plane head tilt?
// Renders one portrait tilted by a series of angles (scene.h) and prints the
// roll that the detector measures from its landmarks.
//   roll_probe <profile.ini> <portrait.jpg>
#include <cstdio>
#include <vector>

#include "detector.h"
#include "image_io.h"
#include "pose.h"
#include "scene.h"

int main(int argc, char** argv) {
    using namespace fa;
    if (argc < 3) return 2;
    ModelProfile profile;
    if (!load_profile(argv[1], ".", profile)) return 2;
    auto det = create_detector(profile);
    auto pose_det = create_pose_detector(profile);  // refines, as in the pipeline
    Image portrait;
    if (!det || !pose_det || !load_image(argv[2], portrait)) return 1;
    std::vector<Face> faces;
    det->detect(portrait, faces);
    if (faces.empty()) { std::printf("no face in the portrait\n"); return 1; }
    Actor actor = make_actor("a", portrait, faces[0].box);
    std::printf("portrait pose: p%+.1f y%+.1f r%+.1f\n", faces[0].pose.pitch, faces[0].pose.yaw, faces[0].pose.roll);
    std::printf("face width | true roll | coarse: roll  yaw  pitch | refined: roll  yaw  pitch\n");
    for (float width : {120.0f, 250.0f, 400.0f}) {
        for (float roll : {-30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f}) {
            actor.path = {{0, 640, 540, width, roll}};
            Image frame;
            std::vector<TruthFace> truth;
            render_frame(1280, 1080, 0, 0, {actor}, frame, truth);
            std::vector<Face> found;
            det->detect(frame, found);
            if (found.empty()) { std::printf("%10.0f | %9.0f | no face\n", width, roll); continue; }
            const Face& f = found[0];
            Face r;
            const bool ok = refine_face(*pose_det, frame, f, r);
            std::printf("%10.0f | %9.0f | %12.1f %5.1f %6.1f | ", width, roll, f.pose.roll, f.pose.yaw, f.pose.pitch);
            if (ok) std::printf("%13.1f %5.1f %6.1f\n", r.pose.roll, r.pose.yaw, r.pose.pitch);
            else std::printf("refinement failed\n");
        }
    }
    return 0;
}
