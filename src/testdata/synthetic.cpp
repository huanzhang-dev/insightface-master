// synthetic.cpp - see synthetic.h.
#include "synthetic.h"

#include <cmath>
#include <cstdio>

namespace fa {

void normalize(Feature& f) {
    double sum = 0;
    for (float v : f.values) sum += static_cast<double>(v) * v;
    const float inv = sum > 0 ? static_cast<float>(1.0 / std::sqrt(sum)) : 0.0f;
    for (float& v : f.values) v *= inv;
}

Feature random_feature(std::mt19937& rng, int dim) {
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    Feature f;
    f.values.resize(dim);
    for (float& v : f.values) v = gauss(rng);
    normalize(f);
    return f;
}

Feature noisy_copy(const Feature& feature, float noise, std::mt19937& rng) {
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    const float per_value = noise / std::sqrt(static_cast<float>(feature.dim()));  // noise vector has length ~noise
    Feature f = feature;
    for (float& v : f.values) v += per_value * gauss(rng);
    normalize(f);
    return f;
}

std::vector<Record> make_synthetic_gallery(int count, int dim, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<Record> records;
    records.reserve(count);
    char id[16];
    for (int i = 0; i < count; ++i) {
        std::snprintf(id, sizeof(id), "P%05d", i);
        Record r;
        r.id = id;
        r.name = "Person " + std::to_string(i);
        r.feature = random_feature(rng, dim);
        records.push_back(std::move(r));
    }
    return records;
}

}  // namespace fa
