// synthetic.h - test data that the project generates by itself (requirement 4.2).
//
// SYNTHETIC FEATURES
//   A face feature is just a direction in a 128..512 dimensional space. We
//   imitate people with random directions:
//     * every registered "person" is a random unit vector;
//     * a new camera image of the same person is that vector plus some noise;
//     * a stranger is a fresh random unit vector.
//   The noise size controls how similar two images of one person are:
//        noise 0.5 -> cosine similarity about 0.89
//        noise 1.0 -> about 0.71
//        noise 1.5 -> about 0.55   (typical of a difficult real image)
//   This tests speed, M:N correctness and the storage; it does NOT measure how
//   good a face model is. Real accuracy needs real face images.
#ifndef FA_SYNTHETIC_H
#define FA_SYNTHETIC_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "feature_store.h"
#include "types.h"

namespace fa {

// A random feature of length 1 (all directions equally likely).
Feature random_feature(std::mt19937& rng, int dim);

// A noisy second image of the same person: normalize(feature + noise vector). The noise
// vector has independent Gaussian numbers whose total length is about `noise` on average.
Feature noisy_copy(const Feature& feature, float noise, std::mt19937& rng);

// `count` random people. Ids are "P00000", "P00001", ... and names "Person 0", ...
std::vector<Record> make_synthetic_gallery(int count, int dim, uint32_t seed);

// Scale `f` (in place) to length 1.
void normalize(Feature& f);

}  // namespace fa

#endif  // FA_SYNTHETIC_H
