// scene.h - generates test video of people approaching the camera (requirement 4.2).
//
// There is no real camera footage in the project (and none from the inspection could be found on the
// development PC). Instead we build a scene (optionally with the look of a real camera in a real room,
// see CameraLook):
//   * the background is a smooth gray-blue gradient with a little noise;
//   * every "actor" is a portrait photo (head and shoulders), cut out with soft
//     edges and pasted into the frame;
//   * the actor follows a path of key frames: where the face is, how wide it is
//     (a person walking toward the camera makes the face grow) and how much the
//     head is tilted (roll). A person who just arrived tilts the head, then
//     looks straight at the camera.
// Because we place every face ourselves, we know the exact box of every face in
// every frame ("ground truth") and can check detector and tracker against it.
//
// LIMIT: a flat photo can be moved, scaled and tilted in the image plane, but
// not turned in 3-D. Tilt (roll) is therefore the only rotation we can create
// here; yaw and pitch gating is tested with synthetic landmarks instead.
#ifndef FA_SCENE_H
#define FA_SCENE_H

#include <string>
#include <vector>

#include "types.h"

namespace fa {

// One point on an actor's path.
struct PathPoint {
    double time_ms = 0;
    float center_x = 0, center_y = 0;  // where the middle of the face is, in the video frame
    float face_width = 100;            // width of the face box in the video frame, pixels
    float roll_deg = 0;                // head tilt (clockwise positive)
    PathPoint() {}
    PathPoint(double time_ms_, float center_x_, float center_y_, float face_width_, float roll_deg_)
        : time_ms(time_ms_), center_x(center_x_), center_y(center_y_), face_width(face_width_), roll_deg(roll_deg_) {}
};

// A person in the scene.
struct Actor {
    std::string label;          // e.g. the person id, for the ground-truth file
    Image sprite;               // head-and-shoulders picture
    Rect face_in_sprite;        // where the face box is inside the sprite
    std::vector<PathPoint> path;   // at least two points, ordered by time
    double appear_ms = 0, disappear_ms = 1e12;  // the actor exists only between these times
};

// The face box of an actor at a given time (ground truth). visible == false when
// the actor is not in the scene or the face is outside the frame.
struct TruthFace {
    std::string label;
    Rect box;
    float roll_deg = 0;
    bool visible = false;
};

// Make an actor from a portrait photo. `face` is the face box found in the
// photo by a detector (or known by the caller). The sprite is cut around it.
Actor make_actor(const std::string& label, const Image& portrait, const Rect& face);

// How a real camera and a real room change the picture (a SIMULATION: no camera is involved). Without it the
// scene is a smooth gradient with a little noise, which is much easier than real footage. With it:
//   * the room: static furniture, a window, a door, a notice board with text-like lines (high-contrast shapes that
//     a detector has to ignore), the same in every frame of a video;
//   * the light: the left side darker than the right, darker corners (lens vignette), slow brightness changes
//     (auto exposure hunting) and a warm colour cast;
//   * the lens and the exposure: the whole picture is a little soft, and a person who moves is smeared in the
//     direction of the movement (motion blur, as long as the exposure is). LIMIT: the smear is only drawn when it
//     is at least 2 pixels long, and the people of the generated walks move less than 2 pixels per frame, so the
//     footage made by tools/gen_video contains NO motion blur; the effect is only seen with a faster mover
//     (tests/test_common.cpp tests it with one);
//   * the sensor: noise whose size grows with the brightness, like shot noise (proportional to the square root of
//     the signal: 0.24 x noise_sigma in black, 1.0 x at mid gray, 1.4 x in white), so a dark area is relatively
//     noisier but not noisier in levels; partly different in each colour channel.
// All of it is deterministic (seeded by `seed` and the frame index) and leaves the ground truth untouched: the face
// boxes are where they were. The H.264 compression of the video (a lower bitrate gives visible blocks) comes on top
// of this and is chosen when the video is written (tools/gen_video --camera-look).
struct CameraLook {
    bool clutter = true;           // draw the room
    float noise_sigma = 4.0f;      // sensor noise at mid gray, in 8-bit levels (about 1.5% of the range; larger in bright areas, see above)
    float blur_sigma = 0.9f;       // lens softness: Gaussian blur of the whole picture, pixels
    float motion_blur = 0.4f;      // exposure as a fraction of the frame time: a face that moves m pixels between frames is smeared over 0.4 * m
    float vignette = 0.25f;        // corners darker by this fraction (0 = none)
    float light_slope = 0.20f;     // the left edge darker than the right edge by this fraction (0 = even light)
    float exposure_drift = 0.06f;  // brightness wanders by up to +-6% over a few seconds
    float warm = 0.06f;            // colour cast: red up and blue down by this fraction
    double frame_ms = 1000.0 / 15.0;  // time between frames (the motion blur needs it)
    int seed = 1;                  // another seed gives another room
};

// Draw the scene at `time_ms` into a frame of the given size, and report the
// ground truth of every actor. `frame_index` only seeds the noise. `look` (when given) makes the
// picture look like a real camera in a real room (see CameraLook); null = the plain test scene.
void render_frame(int width, int height, double time_ms, int frame_index, const std::vector<Actor>& actors,
                  Image& frame, std::vector<TruthFace>& truth, const CameraLook* look = nullptr);

// Path helper: walk from `start` to `end` linearly, growing the face from
// `width_start` to `width_end`; the head is tilted by `roll_start` at the start
// and straight (0) from `settle_ms` on.
std::vector<PathPoint> approach_path(double duration_ms, float start_x, float start_y, float end_x, float end_y,
                                     float width_start, float width_end, float roll_start, double settle_ms);

}  // namespace fa

#endif  // FA_SCENE_H
