// extractor.h - the feature extraction engine.
//
// A feature (also called embedding) is a list of 128..512 numbers that
// describes how a face looks. Two images of the same person give features that
// are close to each other; images of different people give features that are
// far apart. Two families exist behind the same interface:
//   * deep learning: SFace (128 numbers) or ArcFace (512 numbers) networks
//   * classical:     LBP texture histograms reduced to 256 numbers
//
// Every extractor needs a face that is cut out and rotated so that eyes, nose
// and mouth are at fixed positions ("alignment"). The landmarks of the face
// crop make that possible.
#ifndef FA_EXTRACTOR_H
#define FA_EXTRACTOR_H

#include <memory>

#include "profile.h"
#include "types.h"

namespace fa {

class FeatureExtractor {
public:
    virtual ~FeatureExtractor() {}

    // Length of the features this extractor produces.
    virtual int dim() const = 0;

    // Compute the feature of a stored face crop. The result has length 1.
    // Returns false when the crop has no landmarks or the network fails.
    virtual bool extract(const FaceCrop& crop, Feature& out) = 0;

    // Same, for an image that is already aligned to 112x112 (for callers that do the
    // alignment themselves; the two extract() paths of this project call it internally).
    virtual bool extract_aligned(const Image& aligned, Feature& out) = 0;

    virtual const char* name() const = 0;

    // True when this extractor runs on the CPU (every extractor of this project does; no GPU is used anywhere).
    // The deep extractors ask their network (Net::runs_on_cpu); the classical LBP extractor is plain CPU code.
    virtual bool runs_on_cpu() const { return true; }
};

// Build the extractor described by the profile (recognizer.type = sface,
// arcface or lbp). Returns nullptr and prints the reason on failure.
std::unique_ptr<FeatureExtractor> create_extractor(const ModelProfile& profile);

std::unique_ptr<FeatureExtractor> create_deep_extractor(const ModelProfile& profile);
std::unique_ptr<FeatureExtractor> create_lbp_extractor(const ModelProfile& profile);

// Cut the face out of the camera image with a margin around the detected box
// (so that alignment never runs out of picture) and move the landmarks into the
// crop's own coordinates. Returns false for an empty frame or a degenerate box.
bool make_face_crop(const Image& frame, const Face& face, FaceCrop& crop);

// Classical extractor calibration. The LBP histogram of every face looks much like the
// histogram of every other face, which makes all similarities high. Subtracting the
// average histogram and dividing by the usual spread of each value fixes that. This is
// no training of a model: it only needs the average and spread over some faces.
//   lbp_raw_histogram : the 2891 histogram numbers (square-rooted) of an aligned 112x112 face
//   lbp_raw_size      : how many numbers that is
//   lbp_save_calibration writes the calibration file models/lbp_calibration.bin (mean, then
//   1/spread); it is read back by the LBP extractor when it is created (create_lbp_extractor)
int lbp_raw_size();
bool lbp_raw_histogram(const Image& aligned, std::vector<float>& raw);
bool lbp_save_calibration(const std::string& path, const std::vector<float>& mean, const std::vector<float>& inverse_spread);

// Scale a vector to length 1 (an all-zero vector stays zero).
void normalize_feature(Feature& f);

}  // namespace fa

#endif  // FA_EXTRACTOR_H
