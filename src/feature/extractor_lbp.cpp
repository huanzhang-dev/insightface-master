// extractor_lbp.cpp - the classical (non-deep) feature extractor.
//
// LBP = "Local Binary Pattern". For every pixel we compare it with its 8
// neighbours and write one bit per neighbour (1 if the neighbour is at least as
// bright as the centre). The 8 bits give a code 0..255 that describes the local
// texture (edge, corner, flat area...).
//
// Recipe:
//   1. Align the face to 112x112, convert to gray, equalize the brightness.
//   2. Compute the LBP code of every pixel.
//   3. Keep only 59 kinds of code ("uniform patterns": at most 2 changes
//      between 0 and 1 around the circle, plus one bin for all other codes).
//   4. Cut the face into a 7x7 grid and count the codes in every cell (a
//      histogram of 59 numbers per cell = 2891 numbers).
//   5. Divide every cell histogram by its total, take the square root of each value
//      (reduces the influence of very frequent codes), subtract a mean vector and
//      multiply by 1/spread (the calibration file; without it mean = 0, spread = 1).
//   6. Reduce 2891 numbers to 256 with a fixed random projection and scale to
//      length 1.
//
// HONEST LIMIT: this has no training. It separates people far worse than the
// deep networks and cannot reach 1 error in 10,000; it is the classical
// alternative requested in the specification.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>

#include "extractor.h"
#include "image_ops.h"

namespace fa {
namespace {

const int kGrid = 7;       // 7 x 7 cells
const int kBins = 59;      // uniform LBP bins
const int kRawDim = kGrid * kGrid * kBins;  // 2891

// Table that maps an 8-bit LBP code to its bin (0..58).
struct UniformTable {
    unsigned char bin[256];
    UniformTable() {
        int next = 0;
        for (int code = 0; code < 256; ++code) {
            // count the 0/1 changes while walking around the 8 bits
            int transitions = 0;
            for (int b = 0; b < 8; ++b) {
                const int bit = (code >> b) & 1, next_bit = (code >> ((b + 1) % 8)) & 1;
                if (bit != next_bit) ++transitions;
            }
            bin[code] = transitions <= 2 ? static_cast<unsigned char>(next++) : static_cast<unsigned char>(kBins - 1);
        }
    }
};

// Counts of LBP codes per grid cell, step 2-5 of the recipe above, WITHOUT the mean
// subtraction. Shared by the extractor and by the calibration tool.
void compute_raw_histogram(const Image& gray, const UniformTable& table, std::vector<float>& raw) {
    raw.assign(kRawDim, 0.0f);
    const int w = gray.width, h = gray.height;
    const int inner = w - 2;  // the 1-pixel border has no full neighbourhood
    for (int y = 1; y < h - 1; ++y) {
        const uint8_t* up = gray.row(y - 1);
        const uint8_t* mid = gray.row(y);
        const uint8_t* down = gray.row(y + 1);
        const int cell_y = (y - 1) * kGrid / inner;
        for (int x = 1; x < w - 1; ++x) {
            const uint8_t c = mid[x];
            int code = 0;
            code |= (up[x - 1] >= c) << 0;
            code |= (up[x] >= c) << 1;
            code |= (up[x + 1] >= c) << 2;
            code |= (mid[x + 1] >= c) << 3;
            code |= (down[x + 1] >= c) << 4;
            code |= (down[x] >= c) << 5;
            code |= (down[x - 1] >= c) << 6;
            code |= (mid[x - 1] >= c) << 7;
            const int cell_x = (x - 1) * kGrid / inner;
            raw[(cell_y * kGrid + cell_x) * kBins + table.bin[code]] += 1.0f;
        }
    }
    // per-cell L1 normalization, then square root
    for (int cell = 0; cell < kGrid * kGrid; ++cell) {
        float total = 0;
        for (int b = 0; b < kBins; ++b) total += raw[cell * kBins + b];
        const float inv = total > 0 ? 1.0f / total : 0.0f;
        for (int b = 0; b < kBins; ++b) raw[cell * kBins + b] = std::sqrt(raw[cell * kBins + b] * inv);
    }
}

// Histogram equalization: spreads the gray values so that lighting differences matter less.
void equalize(Image& gray) {
    int histogram[256] = {0};
    for (int y = 0; y < gray.height; ++y)
        for (int x = 0; x < gray.width; ++x) ++histogram[gray.row(y)[x]];
    const int total = gray.width * gray.height;
    int map[256], cumulative = 0;
    for (int v = 0; v < 256; ++v) {
        cumulative += histogram[v];
        map[v] = std::min(255, cumulative * 255 / total);
    }
    for (int y = 0; y < gray.height; ++y)
        for (int x = 0; x < gray.width; ++x) gray.row(y)[x] = static_cast<uint8_t>(map[gray.row(y)[x]]);
}

class LbpExtractor : public FeatureExtractor {
public:
    explicit LbpExtractor(int dim)
        : dim_(dim), mean_(kRawDim, 0.0f), inverse_spread_(kRawDim, 1.0f), projection_(static_cast<size_t>(dim) * kRawDim) {
        // The random projection is the same on every run (fixed seed).
        std::mt19937 rng(20240601);
        std::normal_distribution<float> gauss(0.0f, 1.0f);
        const float scale = 1.0f / std::sqrt(static_cast<float>(kRawDim));
        for (float& v : projection_) v = gauss(rng) * scale;
    }

    int dim() const override { return dim_; }
    const char* name() const override { return "lbp/classical"; }

    // Optionally load the calibration made by tools/calibrate_lbp: kRawDim means
    // followed by kRawDim values of 1/spread.
    bool load_calibration(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            // Without calibration all similarities are high and a normal threshold lets strangers in;
            // features stored without it do not match features made with it. Say so.
            std::cerr << "extractor: " << path << " not found: the classical extractor runs UNCALIBRATED "
                      << "(make the file with tools/calibrate_lbp)\n";
            return true;
        }
        std::vector<float> both(2 * kRawDim);
        file.read(reinterpret_cast<char*>(both.data()), both.size() * sizeof(float));
        if (file.gcount() != static_cast<std::streamsize>(both.size() * sizeof(float)) || file.peek() != std::char_traits<char>::eof()) {
            std::cerr << "extractor: " << path << " has the wrong calibration size\n";
            return false;
        }
        for (int i = 0; i < kRawDim; ++i) {
            if (!std::isfinite(both[i]) || !std::isfinite(both[kRawDim + i]) || both[kRawDim + i] <= 0) {
                std::cerr << "extractor: " << path << " has invalid calibration values\n";
                return false;
            }
        }
        mean_.assign(both.begin(), both.begin() + kRawDim);
        inverse_spread_.assign(both.begin() + kRawDim, both.end());
        return true;
    }

    bool extract(const FaceCrop& crop, Feature& out) override {
        out.values.clear();
        if (crop.image.empty() || !crop.face.has_landmarks) return false;
        align_face(crop.image, crop.face, aligned_);
        return extract_aligned(aligned_, out);
    }

    bool extract_aligned(const Image& aligned, Feature& out) override {
        out.values.clear();
        if (aligned.empty() || aligned.width != FA_ALIGNED_FACE_SIZE || aligned.height != FA_ALIGNED_FACE_SIZE ||
            (aligned.channels != 1 && aligned.channels != 3)) return false;
        to_gray(aligned, gray_);
        equalize(gray_);

        // --- steps 2-5: LBP histograms of the 7x7 grid, square-rooted ---
        std::vector<float>& raw = raw_;
        compute_raw_histogram(gray_, table_, raw);
        // --- calibration: subtract the average, divide by the usual spread ---
        for (int i = 0; i < kRawDim; ++i) raw[i] = (raw[i] - mean_[i]) * inverse_spread_[i];
        // --- step 6: random projection to dim_ numbers ---
        out.values.assign(dim_, 0.0f);
        for (int d = 0; d < dim_; ++d) {
            const float* row = &projection_[static_cast<size_t>(d) * kRawDim];
            float sum = 0;
            for (int i = 0; i < kRawDim; ++i) sum += row[i] * raw[i];
            out.values[d] = sum;
        }
        double squared_norm = 0;
        for (float v : out.values) {
            if (!std::isfinite(v)) { out.values.clear(); return false; }
            squared_norm += static_cast<double>(v) * v;
        }
        if (!(squared_norm > 0) || !std::isfinite(squared_norm)) { out.values.clear(); return false; }
        const double norm = std::sqrt(squared_norm);
        for (float& v : out.values) v = static_cast<float>(v / norm);
        return true;
    }

private:
    int dim_;
    std::vector<float> mean_, inverse_spread_;
    std::vector<float> projection_;
    std::vector<float> raw_;
    UniformTable table_;
    Image aligned_, gray_;
};

}  // namespace

int lbp_raw_size() { return kRawDim; }

bool lbp_raw_histogram(const Image& aligned, std::vector<float>& raw) {
    if (aligned.width != FA_ALIGNED_FACE_SIZE || aligned.height != FA_ALIGNED_FACE_SIZE) return false;
    static const UniformTable table;
    Image gray;
    to_gray(aligned, gray);
    equalize(gray);
    compute_raw_histogram(gray, table, raw);
    return true;
}

bool lbp_save_calibration(const std::string& path, const std::vector<float>& mean, const std::vector<float>& inverse_spread) {
    if (static_cast<int>(mean.size()) != kRawDim || static_cast<int>(inverse_spread.size()) != kRawDim) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(mean.data()), kRawDim * sizeof(float));
    file.write(reinterpret_cast<const char*>(inverse_spread.data()), kRawDim * sizeof(float));
    return static_cast<bool>(file);
}

std::unique_ptr<FeatureExtractor> create_lbp_extractor(const ModelProfile& profile) {
    const int dim = profile.feature_dim > 0 && profile.feature_dim <= FA_MAX_FEATURE_DIM ? profile.feature_dim : 256;
    auto extractor = std::make_unique<LbpExtractor>(dim);
    if (!extractor->load_calibration(profile.root + "/models/lbp_calibration.bin")) return nullptr;
    return extractor;
}

}  // namespace fa
