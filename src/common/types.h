// types.h - the small data types shared by every engine.
//
// Everything here is a plain struct. There is no inheritance and no template,
// so a beginner can read each type in a few seconds.
#ifndef FA_TYPES_H
#define FA_TYPES_H

#include <cstdint>
#include <vector>

#include "config.h"

namespace fa {

// ---------------------------------------------------------------------------
// Image: an 8-bit picture stored as B,G,R,B,G,R,... row by row (like Windows
// bitmaps). `stride` is the number of bytes from the start of one row to the
// start of the next (normally width * channels).
// ---------------------------------------------------------------------------
struct Image {
    int width = 0;
    int height = 0;
    int channels = 3;  // 3 = BGR color, 1 = gray
    int stride = 0;
    std::vector<uint8_t> data;

    bool empty() const { return data.empty() || width <= 0 || height <= 0; }
    // Make the image the given size and fill it with zeros.
    void create(int w, int h, int c) {
        width = w;
        height = h;
        channels = c;
        stride = w * c;
        data.assign(static_cast<size_t>(stride) * h, 0);
    }
    uint8_t* row(int y) { return data.data() + static_cast<size_t>(y) * stride; }
    const uint8_t* row(int y) const { return data.data() + static_cast<size_t>(y) * stride; }
};

// A rectangle in pixel coordinates: left/top corner plus size.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() {}
    // (Rect{x, y, w, h} needs this constructor: Visual Studio 2015 does not allow braces with
    // values for a struct that has default member values.)
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float area() const { return (w > 0 && h > 0) ? w * h : 0.0f; }
};

struct Point {
    float x = 0, y = 0;
    Point() {}
    Point(float x_, float y_) : x(x_), y(y_) {}
};

// Head rotation in degrees (see config.h for the meaning of each angle).
struct Pose {
    float pitch = 0, yaw = 0, roll = 0;
    bool valid = false;  // false when the detector gave no landmarks
};

// One detected face.
//   landmarks[0..4] = right eye, left eye, nose tip, right mouth corner,
//   left mouth corner. "Right eye" means the eye on the RIGHT SIDE OF THE
//   PERSON, which appears on the LEFT side of the image (the order used by the
//   YuNet and SCRFD models).
struct Face {
    Rect box;
    float score = 0;
    Point landmarks[5];
    bool has_landmarks = false;
    Pose pose;
};

// A face that is followed from frame to frame.
struct Track {
    int id = 0;
    Face face;                 // most recent detection
    int missed_frames = 0;     // frames in a row without a matching detection
    int age_frames = 0;        // frames since the track was created
    bool updated = false;      // true if the latest frame found this face
    float vx = 0, vy = 0;      // smoothed movement of the box centre, pixels per frame
};

// A stored face segment ("crop"): the part of the camera image around one face.
// The landmarks inside `face` are given in the coordinates of `image` (the crop),
// not of the camera image, so the crop can be processed on its own later.
struct FaceCrop {
    Image image;
    Face face;                 // box covers the face inside the crop; landmarks are crop-relative
    int track_id = 0;
    double time_ms = 0;        // when the crop was taken
};

// A feature vector (embedding). `values` is normalized to length 1, so the
// cosine similarity of two features is just their dot product.
struct Feature {
    std::vector<float> values;
    int dim() const { return static_cast<int>(values.size()); }
};

}  // namespace fa

#endif  // FA_TYPES_H
