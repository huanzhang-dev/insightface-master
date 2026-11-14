// extractor_deep.cpp - deep-learning feature extraction (SFace and ArcFace).
//
// Steps for one face crop:
//   1. Align: rotate / scale / shift the crop so that the 5 landmarks land on
//      the standard positions of a 112x112 face (align_face in image_ops.h).
//   2. Convert the 112x112 image to the numbers the network expects.
//   3. Run the network. It answers with a list of 128 (SFace) or 512 (ArcFace) numbers.
//   4. Scale that list to length 1, so that "similarity" is a plain dot product.
#include <iostream>
#include <cmath>

#include "extractor.h"
#include "image_ops.h"
#include "inference.h"

namespace fa {
namespace {

class DeepExtractor : public FeatureExtractor {
public:
    explicit DeepExtractor(const ModelProfile& profile) : profile_(profile) {}

    bool load() {
        if (profile_.recognizer_backend == "ncnn") {
            std::string bin = profile_.recognizer_model;
            const size_t dot = bin.rfind(".param");
            if (dot != std::string::npos) bin.replace(dot, 6, ".bin");
            net_ = create_ncnn_net(profile_.recognizer_model, bin, profile_.recognizer_input,
                                   {profile_.recognizer_output}, profile_.threads);
        } else {
            net_ = create_onnx_net(profile_.recognizer_model, profile_.threads);
        }
        if (!net_) return false;

        if (profile_.recognizer_type == "arcface") {
            // ArcFace (InsightFace w600k): RGB order, (pixel - 127.5) / 127.5  -> values in -1..1
            prep_.swap_rb = true;
            for (int c = 0; c < 3; ++c) {
                prep_.mean[c] = 127.5f;
                prep_.norm[c] = 1.0f / 127.5f;
            }
        } else {
            // SFace (OpenCV zoo): RGB order, raw 0..255 values
            prep_.swap_rb = true;
        }
        dim_ = profile_.feature_dim;
        name_ = profile_.recognizer_type + "/" + net_->backend();
        return true;
    }

    int dim() const override { return dim_; }
    const char* name() const override { return name_.c_str(); }
    bool runs_on_cpu() const override { return net_ && net_->runs_on_cpu(); }

    bool extract(const FaceCrop& crop, Feature& out) override {
        out.values.clear();
        if (crop.image.empty() || !crop.face.has_landmarks) return false;
        align_face(crop.image, crop.face, aligned_);
        return extract_aligned(aligned_, out);
    }

    bool extract_aligned(const Image& aligned, Feature& out) override {
        out.values.clear();
        if (aligned.width != FA_ALIGNED_FACE_SIZE || aligned.height != FA_ALIGNED_FACE_SIZE || aligned.channels != 3)
            return false;
        if (!net_->run(aligned, prep_, outputs_) || outputs_.size() != 1) return false;
        if (static_cast<int>(outputs_[0].data.size()) != dim_) {
            std::cerr << "extractor: network returned " << outputs_[0].data.size() << " numbers, profile says "
                      << dim_ << "\n";
            return false;
        }
        double squared_norm = 0;
        for (float v : outputs_[0].data) {
            if (!std::isfinite(v)) return false;
            squared_norm += static_cast<double>(v) * v;
        }
        if (!(squared_norm > 0) || !std::isfinite(squared_norm)) return false;
        // Divide in double precision so even very small nonzero outputs normalize safely.
        const double norm = std::sqrt(squared_norm);
        out.values = outputs_[0].data;
        for (float& v : out.values) v = static_cast<float>(v / norm);
        return true;
    }

private:
    ModelProfile profile_;
    std::unique_ptr<Net> net_;
    InputPrep prep_;
    int dim_ = 0;
    std::string name_;
    Image aligned_;
    std::vector<Tensor> outputs_;
};

}  // namespace

std::unique_ptr<FeatureExtractor> create_deep_extractor(const ModelProfile& profile) {
    auto extractor = std::make_unique<DeepExtractor>(profile);
    if (!extractor->load()) {
        std::cerr << "extractor: cannot load " << profile.recognizer_model << "\n";
        return nullptr;
    }
    return extractor;
}

}  // namespace fa
