// haar_cascade.h - the classical (Viola-Jones) object detector.
//
// HOW IT WORKS, IN SHORT
//  * A "Haar feature" is a few rectangles; its value is the sum of the pixels
//    in the dark rectangle(s) minus the sum in the bright one(s). A face has
//    typical patterns, e.g. the eye band is darker than the cheeks below it.
//  * The sum of any rectangle takes only 4 memory reads thanks to the
//    "integral image" (see integral_image() in image_ops.h).
//  * A "cascade" is a list of stages. Each stage adds up the answers of many
//    small decision stumps (feature value < threshold ? left value : right
//    value). If a stage's total is too low, the window is NOT a face and we
//    stop immediately. Most windows are rejected by the first stages, which is
//    why the method is fast.
//  * The window is slid over the image at many sizes (scales).
//
// The cascade file is the "stump-based" XML format of OpenCV
// (haarcascade_frontalface_default.xml, haarcascade_eye.xml).
#ifndef FA_HAAR_CASCADE_H
#define FA_HAAR_CASCADE_H

#include <string>
#include <vector>

#include "types.h"

namespace fa {

// One rectangle of a feature, in the coordinates of the training window
// (for example 24 x 24), plus its weight.
struct HaarRect {
    int x = 0, y = 0, w = 0, h = 0;
    float weight = 0;
};

struct HaarFeature {
    int count = 0;       // number of rectangles used (2 or 3)
    HaarRect rect[3];
};

// A decision stump: feature value compared with a threshold gives one of two numbers.
struct HaarStump {
    int feature = 0;
    float threshold = 0;
    float left = 0;      // used when the (normalized) feature value is below the threshold
    float right = 0;
};

struct HaarStage {
    float threshold = 0;  // the stage passes when the sum of its stump outputs reaches this value
    std::vector<HaarStump> stumps;
};

// A detected object after grouping: the box and how many raw windows agreed on it.
struct HaarHit {
    Rect box;
    int neighbors = 0;
};

class HaarCascade {
public:
    // Read the cascade from an OpenCV XML file. Returns false when the file is
    // missing or not a stump-based Haar cascade.
    bool load(const std::string& xml_path);

    int window_width() const { return window_w_; }
    int window_height() const { return window_h_; }

    // Search a gray image for objects.
    //   min_size / max_size : smallest / largest object width in pixels
    //   scale_step          : size ratio between two scan levels (e.g. 1.2)
    //   min_neighbors       : a group of overlapping hits needs at least this many members
    //   threads             : the scan levels (sizes) are spread over this many CPU threads
    void detect(const Image& gray, int min_size, int max_size, float scale_step, int min_neighbors,
                std::vector<HaarHit>& hits, int threads = 1) const;

private:
    int window_w_ = 0, window_h_ = 0;
    std::vector<HaarStage> stages_;
    std::vector<HaarFeature> features_;
};

}  // namespace fa

#endif  // FA_HAAR_CASCADE_H
