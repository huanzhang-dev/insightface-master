// detector_deep.cpp - deep-learning face detectors (YuNet and SCRFD).
//
// Both networks work the same way:
//   1. The camera image is shrunk (keeping its shape) into a 640x640 square. (When the
//      profile names a smaller model and the camera image is small, a 320x320 square
//      and the faster 320 model are used instead; see detector.model_small. A SCRFD profile with
//      detector.layout = chw uses the smallest rectangle instead of the square, for example 640x384
//      for a 1920x1080 image: the same scale, 40% less work.)
//   2. The network looks at the square at three zoom levels called "strides"
//      (8, 16, 32). At stride 8 it makes one prediction for every 8x8 pixel
//      cell, at stride 16 for every 16x16 cell, and so on. Small faces are
//      found at stride 8, large faces at stride 32.
//   3. Each prediction says "a face is here with this score", the size of the
//      box and the 5 landmark points, relative to its cell.
//   4. We turn the predictions back into pixel coordinates of the ORIGINAL
//      camera image and remove duplicate boxes (non-maximum suppression).
#include <algorithm>
#include <cmath>
#include <iostream>

#include "detector.h"
#include "image_ops.h"
#include "inference.h"
#include "pose.h"

namespace fa {
namespace {

const int kStrides[3] = {8, 16, 32};

bool finite_face(const Face& face) {
    if (!std::isfinite(face.score) || !std::isfinite(face.box.x) || !std::isfinite(face.box.y) ||
        !std::isfinite(face.box.w) || !std::isfinite(face.box.h) || !std::isfinite(face.box.right()) ||
        !std::isfinite(face.box.bottom()) || !std::isfinite(face.box.area())) return false;
    for (const Point& point : face.landmarks)
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    return true;
}

class DeepDetector : public FaceDetector {
public:
    DeepDetector(const ModelProfile& profile, bool is_yunet) : profile_(profile), is_yunet_(is_yunet) {}

    // Load the network. YuNet runs on ONNX Runtime, SCRFD on ncnn or ONNX Runtime.
    bool load() {
        if (profile_.detector_backend == "ncnn") {
            std::string bin = profile_.detector_model;
            const size_t dot = bin.rfind(".param");
            if (dot != std::string::npos) bin.replace(dot, 6, ".bin");
            net_ = create_ncnn_net(profile_.detector_model, bin, profile_.detector_input,
                                   profile_.detector_outputs, profile_.threads);
        } else {
            net_ = create_onnx_net(profile_.detector_model, profile_.threads);
        }
        if (!net_) return false;
        // Optional faster network for small camera images (same model, smaller input square).
        if (!profile_.detector_model_small.empty() && profile_.detector_backend == "onnx") {
            net_small_ = create_onnx_net(profile_.detector_model_small, profile_.threads);
            if (!net_small_) std::cerr << "detector: cannot load " << profile_.detector_model_small << " (ignored)\n";
        }

        // How the picture is converted to network input numbers:
        if (is_yunet_) {
            // YuNet: plain BGR pixel values 0..255.
        } else {
            // SCRFD: RGB, (pixel - 127.5) / 128.
            prep_.swap_rb = true;
            for (int c = 0; c < 3; ++c) {
                prep_.mean[c] = 127.5f;
                prep_.norm[c] = 1.0f / 128.0f;
            }
        }
        name_ = std::string(is_yunet_ ? "yunet/" : "scrfd/") + net_->backend();
        return true;
    }

    const char* name() const override { return name_.c_str(); }
    const char* type() const override { return profile_.detector_type.c_str(); }
    bool runs_on_cpu() const override { return net_ && net_->runs_on_cpu() && (!net_small_ || net_small_->runs_on_cpu()); }

    bool detect(const Image& frame, std::vector<Face>& faces, bool accurate = false) override {
        faces.clear();
        if (frame.empty() || frame.channels != 3) return false;

        // Small camera images may use the faster, smaller network (never when `accurate` is asked for).
        const bool small = net_small_ && !accurate &&
                           std::max(frame.width, frame.height) <= profile_.detector_small_max_side;
        Net& net = small ? *net_small_ : *net_;
        const int size = small ? profile_.detector_small_size : FA_DETECT_INPUT_SIZE;

        // 1. resize to the network size (keeps the aspect ratio, pads with gray). A SCRFD profile with detector.layout = chw
        //    uses the smallest rectangle (a multiple of 32 on both sides) instead of the square: 640x384 for 1920x1080.
        const bool fitted = !is_yunet_ && profile_.detector_layout == "chw";
        float scale;
        int pad_x, pad_y;
        if (fitted) letterbox_fit(frame, size, 32, square_, scale, pad_x, pad_y);
        else letterbox(frame, size, square_, scale, pad_x, pad_y);

        // 2. run the network
        if (!net.run(square_, prep_, outputs_) || outputs_.size() != 12 - (is_yunet_ ? 0 : 3)) return false;
        // Validate every tensor before comparisons, sigmoid/clamping or coordinate arithmetic
        // can hide NaNs/infinities. This covers YuNet and both SCRFD layouts.
        for (const Tensor& output : outputs_)
            for (float value : output.data)
                if (!std::isfinite(value)) {
                    std::cerr << "detector: network returned a non-finite output\n";
                    return false;
                }

        // 3. decode the predictions of the three strides
        std::vector<Face> found;
        for (int s = 0; s < 3; ++s) {
            const bool decoded = is_yunet_ ? decode_yunet(s, size, found) :
                fitted ? decode_scrfd_chw(s, square_.width, square_.height, found) :
                         decode_scrfd(s, size, found);
            if (!decoded) return false;
        }

        // 4. remove duplicates, then map into the camera image
        for (const Face& face : found)
            if (!finite_face(face)) {
                std::cerr << "detector: decoded coordinates overflowed\n";
                return false;
            }
        non_max_suppression(found, FA_DETECT_NMS_IOU);
        for (Face& f : found) {
            if (!to_original(f, scale, pad_x, pad_y, frame.width, frame.height)) {
                faces.clear();
                std::cerr << "detector: camera coordinates overflowed\n";
                return false;
            }
            if (f.box.w < FA_MIN_FACE_PIXELS || f.box.h < FA_MIN_FACE_PIXELS) continue;
            f.pose = estimate_pose(f, pose_model_for(profile_.detector_type));
            faces.push_back(f);
            if (static_cast<int>(faces.size()) >= FA_MAX_FACES) break;
        }
        return true;
    }

private:
    // ---- YuNet ----------------------------------------------------------
    // Outputs (12): cls_8, cls_16, cls_32, obj_8, obj_16, obj_32, bbox_8..32, kps_8..32.
    // For the cell (row r, column c) of a stride s:
    //   score = sqrt(cls * obj)
    //   box centre = ((c + bbox[0]) * s, (r + bbox[1]) * s)
    //   box size   = (exp(bbox[2]) * s,  exp(bbox[3]) * s)
    //   landmark k = ((c + kps[2k]) * s, (r + kps[2k+1]) * s)
    bool decode_yunet(int level, int size, std::vector<Face>& out) {
        const int stride = kStrides[level];
        const int cols = size / stride;
        const int count = cols * cols;
        const float* cls = outputs_[level].data.data();
        const float* obj = outputs_[3 + level].data.data();
        const float* bbox = outputs_[6 + level].data.data();
        const float* kps = outputs_[9 + level].data.data();
        // All four tensors must have the sizes this stride needs; a profile with the output
        // names in the wrong order would otherwise make the loop read past the end of a buffer.
        if (static_cast<int>(outputs_[level].data.size()) != count || static_cast<int>(outputs_[3 + level].data.size()) != count ||
            static_cast<int>(outputs_[6 + level].data.size()) != 4 * count ||
            static_cast<int>(outputs_[9 + level].data.size()) != 10 * count) {
            std::cerr << "detector: output sizes do not fit a " << size << "x" << size << " YuNet network (profile / model mismatch)\n";
            return false;
        }

        for (int i = 0; i < count; ++i) {
            const float c = std::min(std::max(cls[i], 0.0f), 1.0f);
            const float o = std::min(std::max(obj[i], 0.0f), 1.0f);
            const float score = std::sqrt(c * o);
            if (score < profile_.detector_score) continue;  // most cells end here

            const int row = i / cols, col = i % cols;
            const float cx = (col + bbox[i * 4 + 0]) * stride;
            const float cy = (row + bbox[i * 4 + 1]) * stride;
            const float w = std::exp(bbox[i * 4 + 2]) * stride;
            const float h = std::exp(bbox[i * 4 + 3]) * stride;

            Face f;
            f.score = score;
            f.box = {cx - w / 2, cy - h / 2, w, h};
            for (int k = 0; k < 5; ++k) {
                f.landmarks[k].x = (col + kps[i * 10 + 2 * k]) * stride;
                f.landmarks[k].y = (row + kps[i * 10 + 2 * k + 1]) * stride;
            }
            f.has_landmarks = true;
            out.push_back(f);
        }
        return true;
    }

    // ---- SCRFD ----------------------------------------------------------
    // Outputs (9): score_8, score_16, score_32, bbox_8.., kps_8..
    // SCRFD makes 2 predictions ("anchors") per cell. The box is stored as four
    // distances from the cell corner to the left, top, right and bottom edge
    // (in units of the stride); a landmark is an offset from the cell corner.
    bool decode_scrfd(int level, int size, std::vector<Face>& out) {
        const int stride = kStrides[level];
        const int cols = size / stride;
        const int anchors = cols * cols * 2;
        const float* score = outputs_[level].data.data();
        const float* bbox = outputs_[3 + level].data.data();
        const float* kps = outputs_[6 + level].data.data();
        if (static_cast<int>(outputs_[level].data.size()) != anchors || static_cast<int>(outputs_[3 + level].data.size()) != 4 * anchors ||
            static_cast<int>(outputs_[6 + level].data.size()) != 10 * anchors) {
            std::cerr << "detector: output sizes do not fit a " << size << "x" << size << " SCRFD network (check detector.outputs)\n";
            return false;
        }

        for (int i = 0; i < anchors; ++i) {
            if (score[i] < profile_.detector_score) continue;
            const int cell = i / 2;
            const float ax = static_cast<float>((cell % cols) * stride);
            const float ay = static_cast<float>((cell / cols) * stride);

            Face f;
            f.score = score[i];
            const float x1 = ax - bbox[i * 4 + 0] * stride, y1 = ay - bbox[i * 4 + 1] * stride;
            const float x2 = ax + bbox[i * 4 + 2] * stride, y2 = ay + bbox[i * 4 + 3] * stride;
            f.box = {x1, y1, x2 - x1, y2 - y1};
            for (int k = 0; k < 5; ++k) {
                f.landmarks[k].x = ax + kps[i * 10 + 2 * k] * stride;
                f.landmarks[k].y = ay + kps[i * 10 + 2 * k + 1] * stride;
            }
            f.has_landmarks = true;
            out.push_back(f);
        }
        return true;
    }

    // SCRFD with detector.layout = chw: the nine outputs are the raw outputs of the last convolutions of the three
    // strides, each as `channels` pictures of rows x cols numbers (rows = height / stride, cols = width / stride):
    //   score: 2 channels (one per anchor), the value BEFORE the sigmoid;
    //   bbox:  8 channels, channel a * 4 + k = distance k of anchor a;
    //   kps:   20 channels, channel a * 10 + j = landmark number j of anchor a.
    // That is the same information as the flat outputs (checked against them in tests/test_common.cpp), in the order
    // the network makes it, so the network does not have to reshuffle it and the picture may be any rectangle. The
    // faces come out in the same order as with the flat layout (cell by cell, anchor 0 before anchor 1).
    bool decode_scrfd_chw(int level, int width, int height, std::vector<Face>& out) {
        const int stride = kStrides[level];
        const int cols = width / stride, rows = height / stride, cells = cols * rows;
        const float* score = outputs_[level].data.data();
        const float* bbox = outputs_[3 + level].data.data();
        const float* kps = outputs_[6 + level].data.data();
        if (static_cast<int>(outputs_[level].data.size()) != 2 * cells || static_cast<int>(outputs_[3 + level].data.size()) != 8 * cells ||
            static_cast<int>(outputs_[6 + level].data.size()) != 20 * cells) {
            std::cerr << "detector: output sizes do not fit a " << width << "x" << height << " SCRFD network (check detector.outputs and detector.layout)\n";
            return false;
        }
        for (int cell = 0; cell < cells; ++cell) {
            const float ax = static_cast<float>((cell % cols) * stride);
            const float ay = static_cast<float>((cell / cols) * stride);
            for (int a = 0; a < 2; ++a) {
                const float logit = score[a * cells + cell];
                Face f;
                f.score = 1.0f / (1.0f + std::exp(-logit));
                // Compare the actual score, including thresholds 0 and 1 and rounded equality.
                if (f.score < profile_.detector_score) continue;
                const float* b = bbox + static_cast<size_t>(a) * 4 * cells + cell;
                const float x1 = ax - b[0] * stride, y1 = ay - b[cells] * stride;
                const float x2 = ax + b[2 * cells] * stride, y2 = ay + b[3 * cells] * stride;
                f.box = {x1, y1, x2 - x1, y2 - y1};
                const float* k = kps + static_cast<size_t>(a) * 10 * cells + cell;
                for (int j = 0; j < 5; ++j) {
                    f.landmarks[j].x = ax + k[(2 * j) * cells] * stride;
                    f.landmarks[j].y = ay + k[(2 * j + 1) * cells] * stride;
                }
                f.has_landmarks = true;
                out.push_back(f);
            }
        }
        return true;
    }

    // Undo the letterbox: remove the padding, divide by the scale, clip to the image.
    static bool to_original(Face& f, float scale, int pad_x, int pad_y, int width, int height) {
        auto fx = [&](float x) { return (x - pad_x) / scale; };
        auto fy = [&](float y) { return (y - pad_y) / scale; };
        float x1 = fx(f.box.x), y1 = fy(f.box.y), x2 = fx(f.box.right()), y2 = fy(f.box.bottom());
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2)) return false;
        x1 = std::max(0.0f, x1);
        y1 = std::max(0.0f, y1);
        x2 = std::min(static_cast<float>(width), x2);
        y2 = std::min(static_cast<float>(height), y2);
        f.box = {x1, y1, x2 - x1, y2 - y1};
        for (int k = 0; k < 5; ++k) {
            f.landmarks[k].x = fx(f.landmarks[k].x);
            f.landmarks[k].y = fy(f.landmarks[k].y);
        }
        return finite_face(f);
    }

    ModelProfile profile_;
    bool is_yunet_;
    std::unique_ptr<Net> net_;
    std::unique_ptr<Net> net_small_;  // optional: the same model with a smaller input square
    InputPrep prep_;
    std::string name_;
    Image square_;                  // the resized image, reused for every frame
    std::vector<Tensor> outputs_;   // network outputs, reused for every frame
};

}  // namespace

std::unique_ptr<FaceDetector> create_deep_detector(const ModelProfile& profile) {
    auto detector = std::make_unique<DeepDetector>(profile, profile.detector_type == "yunet");
    if (!detector->load()) {
        std::cerr << "detector: cannot load " << profile.detector_model << "\n";
        return nullptr;
    }
    return detector;
}

}  // namespace fa
