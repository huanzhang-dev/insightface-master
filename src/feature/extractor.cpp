// extractor.cpp - helpers shared by the extractors, and the create_extractor factory.
#include "extractor.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "image_ops.h"

namespace fa {

void normalize_feature(Feature& f) {
    double sum = 0;
    for (float v : f.values) sum += static_cast<double>(v) * v;
    if (sum <= 0) return;
    const float inv = static_cast<float>(1.0 / std::sqrt(sum));
    for (float& v : f.values) v *= inv;
}

bool make_face_crop(const Image& frame, const Face& face, FaceCrop& crop) {
    if (frame.empty() || face.box.w < 2 || face.box.h < 2) return false;
    // Take 30% more than the box on every side.
    const float margin_x = 0.3f * face.box.w, margin_y = 0.3f * face.box.h;
    Rect region = {face.box.x - margin_x, face.box.y - margin_y, face.box.w + 2 * margin_x, face.box.h + 2 * margin_y};
    // keep inside the camera image
    const float x1 = std::max(0.0f, region.x), y1 = std::max(0.0f, region.y);
    const float x2 = std::min(static_cast<float>(frame.width), region.right());
    const float y2 = std::min(static_cast<float>(frame.height), region.bottom());
    if (x2 - x1 < 2 || y2 - y1 < 2) return false;
    region = {std::floor(x1), std::floor(y1), std::ceil(x2) - std::floor(x1), std::ceil(y2) - std::floor(y1)};

    fa::crop(frame, region, crop.image);  // copy the pixels
    // Move the box and the landmarks into the crop's own coordinate system.
    crop.face = face;
    crop.face.box.x -= region.x;
    crop.face.box.y -= region.y;
    for (int k = 0; k < 5; ++k) {
        crop.face.landmarks[k].x -= region.x;
        crop.face.landmarks[k].y -= region.y;
    }
    return !crop.image.empty();
}

std::unique_ptr<FeatureExtractor> create_extractor(const ModelProfile& profile) {
    if (profile.recognizer_type == "sface" || profile.recognizer_type == "arcface") return create_deep_extractor(profile);
    if (profile.recognizer_type == "lbp") return create_lbp_extractor(profile);
    std::cerr << "extractor: unknown recognizer.type '" << profile.recognizer_type << "'\n";
    return nullptr;
}

}  // namespace fa
