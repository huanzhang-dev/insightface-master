// haar_cascade.cpp - see haar_cascade.h.
#include "haar_cascade.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "image_ops.h"

namespace fa {
namespace {

// ---------------------------------------------------------------------------
// A very small XML reader: we only need the text between known tags.
// ---------------------------------------------------------------------------

// Read all numbers found in `text` (separated by spaces/newlines; "-1." is fine).
std::vector<double> parse_numbers(const std::string& text) {
    std::vector<double> numbers;
    const char* p = text.c_str();
    char* end = nullptr;
    while (*p) {
        double v = std::strtod(p, &end);
        if (end == p) {
            ++p;  // not a number: skip one character
        } else {
            numbers.push_back(v);
            p = end;
        }
    }
    return numbers;
}

// Find the text between <tag> and </tag> that starts at or after `pos`.
// On success `pos` moves behind the closing tag.
bool next_between(const std::string& s, size_t& pos, size_t limit, const char* open, const char* close,
                  std::string& content) {
    size_t a = s.find(open, pos);
    if (a == std::string::npos || a >= limit) return false;
    a += std::char_traits<char>::length(open);
    size_t b = s.find(close, a);
    if (b == std::string::npos || b > limit) return false;
    content = s.substr(a, b - a);
    pos = b + std::char_traits<char>::length(close);
    return true;
}

}  // namespace

bool HaarCascade::load(const std::string& xml_path) {
    std::ifstream file(xml_path, std::ios::binary);
    if (!file) {
        std::cerr << "haar: cannot open " << xml_path << "\n";
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();
    stages_.clear();
    features_.clear();

    // --- window size ---
    std::string text;
    size_t pos = xml.find("<opencv_storage>");
    if (pos == std::string::npos) pos = 0;
    size_t p = pos;
    if (!next_between(xml, p, xml.size(), "<height>", "</height>", text)) return false;
    window_h_ = std::atoi(text.c_str());
    p = pos;
    if (!next_between(xml, p, xml.size(), "<width>", "</width>", text)) return false;
    window_w_ = std::atoi(text.c_str());
    if (xml.find("<stageType>BOOST") == std::string::npos || xml.find("<featureType>HAAR") == std::string::npos) {
        std::cerr << "haar: " << xml_path << " is not a BOOST/HAAR cascade\n";
        return false;
    }

    // --- stages ---
    const size_t stages_begin = xml.find("<stages>");
    const size_t stages_end = xml.find("</stages>");
    if (stages_begin == std::string::npos || stages_end == std::string::npos) return false;
    pos = stages_begin;
    while (true) {
        const size_t t = xml.find("<stageThreshold>", pos);
        if (t == std::string::npos || t >= stages_end) break;
        HaarStage stage;
        size_t q = t;
        next_between(xml, q, stages_end, "<stageThreshold>", "</stageThreshold>", text);
        stage.threshold = static_cast<float>(std::atof(text.c_str()));

        size_t next_stage = xml.find("<stageThreshold>", q);
        if (next_stage == std::string::npos || next_stage > stages_end) next_stage = stages_end;

        // every weak classifier of this stage: "<internalNodes> 0 -1 feature threshold </internalNodes>"
        // followed by "<leafValues> left right </leafValues>"
        while (true) {
            std::string nodes, leaves;
            if (!next_between(xml, q, next_stage, "<internalNodes>", "</internalNodes>", nodes)) break;
            if (!next_between(xml, q, next_stage, "<leafValues>", "</leafValues>", leaves)) return false;
            const std::vector<double> n = parse_numbers(nodes), l = parse_numbers(leaves);
            if (n.size() != 4 || l.size() != 2) {
                std::cerr << "haar: only stump classifiers (one node per tree) are supported\n";
                return false;
            }
            HaarStump stump;
            stump.feature = static_cast<int>(n[2]);
            stump.threshold = static_cast<float>(n[3]);
            stump.left = static_cast<float>(l[0]);
            stump.right = static_cast<float>(l[1]);
            stage.stumps.push_back(stump);
        }
        stages_.push_back(stage);
        pos = next_stage;
    }

    // --- features: each has a list of rectangles "x y w h weight." ---
    const size_t features_begin = xml.find("<features>");
    const size_t features_end = xml.find("</features>");
    if (features_begin == std::string::npos || features_end == std::string::npos) return false;
    pos = features_begin;
    while (true) {
        std::string rects;
        if (!next_between(xml, pos, features_end, "<rects>", "</rects>", rects)) break;
        HaarFeature feature;
        size_t r = 0;
        std::string one;
        while (feature.count < 3 && next_between(rects, r, rects.size(), "<_>", "</_>", one)) {
            const std::vector<double> v = parse_numbers(one);
            if (v.size() != 5) return false;
            HaarRect& hr = feature.rect[feature.count++];
            hr.x = static_cast<int>(v[0]);
            hr.y = static_cast<int>(v[1]);
            hr.w = static_cast<int>(v[2]);
            hr.h = static_cast<int>(v[3]);
            hr.weight = static_cast<float>(v[4]);
        }
        features_.push_back(feature);
    }

    for (const HaarStage& st : stages_)
        for (const HaarStump& s : st.stumps)
            if (s.feature < 0 || s.feature >= static_cast<int>(features_.size())) return false;
    return window_w_ > 0 && window_h_ > 0 && !stages_.empty() && !features_.empty();
}

namespace {

// A feature prepared for one particular scale: for every rectangle the four
// positions of its corners inside the integral image, relative to the top-left
// corner of the window.
struct ScaledRect {
    int tl, tr, bl, br;  // offsets into the integral image
    float weight;        // already includes the 1/area normalization
};
struct ScaledFeature {
    int count;
    ScaledRect rect[3];
};

// Union-find "find" with path compression, used to group raw hits.
int find_root(std::vector<int>& parent, int i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

// Two boxes are "similar" when all four edges are close compared to their size.
bool similar(const Rect& a, const Rect& b) {
    const float delta = 0.2f * (std::min(a.w, b.w) + std::min(a.h, b.h)) * 0.5f;
    return std::fabs(a.x - b.x) <= delta && std::fabs(a.y - b.y) <= delta &&
           std::fabs(a.right() - b.right()) <= delta && std::fabs(a.bottom() - b.bottom()) <= delta;
}

}  // namespace

void HaarCascade::detect(const Image& gray, int min_size, int max_size, float scale_step, int min_neighbors,
                         std::vector<HaarHit>& hits, int threads) const {
    hits.clear();
    if (gray.empty() || gray.channels != 1) return;

    // One integral image for the sums and one for the sums of squares (the
    // squares give the contrast of the window, used to normalize the features).
    std::vector<int64_t> sum, sqsum;
    integral_image(gray, sum, sqsum);
    const int stride = gray.width + 1;

    std::vector<Rect> raw;  // every window that passed all stages

    // The list of scan sizes. Each size is independent of the others, so the sizes
    // can be spread over several CPU threads (the loop below).
    std::vector<float> scales;
    const float first_scale = std::max(1.0f, static_cast<float>(min_size) / window_w_);
    for (float scale = first_scale; ; scale *= scale_step) {
        const int win_w = static_cast<int>(std::lround(window_w_ * scale));
        const int win_h = static_cast<int>(std::lround(window_h_ * scale));
        if (win_w > gray.width - 1 || win_h > gray.height - 1 || win_w > max_size) break;
        scales.push_back(scale);
    }

#pragma omp parallel for schedule(dynamic) num_threads(threads > 0 ? threads : 1)
    for (int level = 0; level < static_cast<int>(scales.size()); ++level) {
        const float scale = scales[level];
        const int win_w = static_cast<int>(std::lround(window_w_ * scale));
        const int win_h = static_cast<int>(std::lround(window_h_ * scale));
        std::vector<ScaledFeature> scaled(features_.size());  // this thread's copy of the scaled features
        std::vector<Rect> found;                               // windows of this size that passed

        // The contrast is measured on the window without its 1-pixel border.
        const int nx = static_cast<int>(std::lround(1 * scale)), ny = nx;
        const int nw = static_cast<int>(std::lround((window_w_ - 2) * scale));
        const int nh = static_cast<int>(std::lround((window_h_ - 2) * scale));
        const int norm_tl = ny * stride + nx, norm_tr = ny * stride + nx + nw;
        const int norm_bl = (ny + nh) * stride + nx, norm_br = (ny + nh) * stride + nx + nw;
        const float inv_area = 1.0f / (static_cast<float>(nw) * nh);

        // Scale every feature rectangle to this window size.
        for (size_t f = 0; f < features_.size(); ++f) {
            const HaarFeature& src = features_[f];
            ScaledFeature& dst = scaled[f];
            dst.count = src.count;
            float areas[3];
            for (int r = 0; r < src.count; ++r) {
                const int x = static_cast<int>(std::lround(src.rect[r].x * scale));
                const int y = static_cast<int>(std::lround(src.rect[r].y * scale));
                const int w = static_cast<int>(std::lround(src.rect[r].w * scale));
                const int h = static_cast<int>(std::lround(src.rect[r].h * scale));
                dst.rect[r].tl = y * stride + x;
                dst.rect[r].tr = y * stride + x + w;
                dst.rect[r].bl = (y + h) * stride + x;
                dst.rect[r].br = (y + h) * stride + x + w;
                dst.rect[r].weight = src.rect[r].weight * inv_area;
                areas[r] = static_cast<float>(w) * h;
            }
            // Rounding changes the rectangle sizes. Re-compute the weight of the
            // first (big) rectangle so that all weighted areas still add up to zero.
            float others = 0;
            for (int r = 1; r < src.count; ++r) others += src.rect[r].weight * areas[r];
            if (areas[0] > 0) dst.rect[0].weight = -others / areas[0] * inv_area;
        }

        // Slide the window. Small windows move in steps of 2 pixels, large ones faster.
        const int step = std::max(1, static_cast<int>(std::lround(scale > 2.0f ? scale : 2.0f * scale)));
        for (int wy = 0; wy + win_h < gray.height; wy += step) {
            for (int wx = 0; wx + win_w < gray.width; wx += step) {
                const int64_t* s = &sum[static_cast<size_t>(wy) * stride + wx];
                const int64_t* q = &sqsum[static_cast<size_t>(wy) * stride + wx];

                // contrast of this window
                const double mean = (s[norm_br] - s[norm_bl] - s[norm_tr] + s[norm_tl]) * static_cast<double>(inv_area);
                double variance = (q[norm_br] - q[norm_bl] - q[norm_tr] + q[norm_tl]) * static_cast<double>(inv_area) - mean * mean;
                if (variance < 25.0) continue;  // nearly flat area (std < 5): cannot be a face
                const float deviation = static_cast<float>(std::sqrt(variance));

                // run the stages; leave as soon as one fails
                bool passed = true;
                for (const HaarStage& stage : stages_) {
                    float total = 0;
                    for (const HaarStump& stump : stage.stumps) {
                        const ScaledFeature& f = scaled[stump.feature];
                        float value = 0;
                        for (int r = 0; r < f.count; ++r) {
                            const ScaledRect& sr = f.rect[r];
                            value += sr.weight * static_cast<float>(s[sr.br] - s[sr.bl] - s[sr.tr] + s[sr.tl]);
                        }
                        total += (value < stump.threshold * deviation) ? stump.left : stump.right;
                    }
                    if (total < stage.threshold) {
                        passed = false;
                        break;
                    }
                }
                if (passed) found.push_back({static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(win_w), static_cast<float>(win_h)});
            }
        }
#pragma omp critical
        raw.insert(raw.end(), found.begin(), found.end());
    }

    // The scan levels may finish in any order when several threads are used. Sort the hits so
    // that grouping (and so the result) is the same on every run.
    std::sort(raw.begin(), raw.end(), [](const Rect& a, const Rect& b) {
        if (a.y != b.y) return a.y < b.y;
        if (a.x != b.x) return a.x < b.x;
        return a.w < b.w;
    });

    // --- group the raw windows: many overlapping hits = one object ---
    const int n = static_cast<int>(raw.size());
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (similar(raw[i], raw[j])) parent[find_root(parent, j)] = find_root(parent, i);

    std::vector<HaarHit> clusters;
    std::vector<float> sx(n, 0), sy(n, 0), sw(n, 0), sh(n, 0);
    std::vector<int> count(n, 0);
    for (int i = 0; i < n; ++i) {
        const int r = find_root(parent, i);
        sx[r] += raw[i].x; sy[r] += raw[i].y; sw[r] += raw[i].w; sh[r] += raw[i].h;
        count[r]++;
    }
    for (int r = 0; r < n; ++r) {
        if (count[r] < min_neighbors) continue;
        const float k = 1.0f / count[r];
        HaarHit hit;
        hit.box = {sx[r] * k, sy[r] * k, sw[r] * k, sh[r] * k};
        hit.neighbors = count[r];
        clusters.push_back(hit);
    }
    // A small box completely inside a bigger, better supported box is a duplicate.
    for (const HaarHit& a : clusters) {
        bool inside_better = false;
        for (const HaarHit& b : clusters) {
            if (&a == &b || b.neighbors < a.neighbors) continue;
            const float m = 0.1f * b.box.w;
            if (a.box.x >= b.box.x - m && a.box.y >= b.box.y - m && a.box.right() <= b.box.right() + m &&
                a.box.bottom() <= b.box.bottom() + m && b.box.w > a.box.w * 1.1f) {
                inside_better = true;
                break;
            }
        }
        if (!inside_better) hits.push_back(a);
    }
}

}  // namespace fa
