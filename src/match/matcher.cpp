// matcher.cpp - see matcher.h.
#include "matcher.h"

#include <immintrin.h>  // AVX2 / FMA instructions

#include <algorithm>
#include <cstring>
#include <malloc.h>

namespace fa {
namespace {

// Keep the best and second best score seen so far.
inline void update_best(MatchResult& r, int index, float score) {
    if (score > r.score) {
        r.second_score = r.score;
        r.score = score;
        r.index = index;
    } else if (score > r.second_score) {
        r.second_score = score;
    }
}

// The M query features copied into one aligned, zero-padded block (they are few and small), the
// same layout as the registered table, so that dot_avx2 can read both. `usable[q]` is 0 for a
// query whose length is not the gallery's: it is left out of the matching.
struct QueryBlock {
    float* table = nullptr;
    std::vector<char> usable;
    QueryBlock() {}
    QueryBlock(const QueryBlock&) = delete;
    QueryBlock& operator=(const QueryBlock&) = delete;
    ~QueryBlock() {
        if (table) _aligned_free(table);
    }
    const float* row(int q, int padded_dim) const { return table + static_cast<size_t>(q) * padded_dim; }
    // Returns false when there is no memory (then nothing may be matched).
    bool build(const std::vector<Feature>& queries, int dim, int padded_dim) {
        const int count = static_cast<int>(queries.size());
        const size_t bytes = static_cast<size_t>(count) * padded_dim * sizeof(float);
        table = static_cast<float*>(_aligned_malloc(bytes, 32));
        if (!table) return false;
        std::memset(table, 0, bytes);
        usable.assign(count, 0);
        for (int q = 0; q < count; ++q) {
            if (queries[q].dim() != dim) continue;  // wrong length: the result stays "no match"
            std::memcpy(table + static_cast<size_t>(q) * padded_dim, queries[q].values.data(), dim * sizeof(float));
            usable[q] = 1;
        }
        return true;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// dot_avx2
//
// An AVX2 register holds 8 floats. _mm256_fmadd_ps(a, b, acc) does
// acc[i] += a[i] * b[i] for all 8 lanes in ONE instruction. We use two
// accumulators so that the CPU can work on two independent chains at once.
// At the end the 8 lanes of each accumulator are added together.
// ---------------------------------------------------------------------------
float dot_avx2(const float* a, const float* b, int padded_dim) {
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    for (int d = 0; d < padded_dim; d += 16) {
        acc0 = _mm256_fmadd_ps(_mm256_load_ps(a + d), _mm256_load_ps(b + d), acc0);
        acc1 = _mm256_fmadd_ps(_mm256_load_ps(a + d + 8), _mm256_load_ps(b + d + 8), acc1);
    }
    __m256 acc = _mm256_add_ps(acc0, acc1);
    // horizontal sum of the 8 lanes
    __m128 low = _mm256_castps256_ps128(acc);
    __m128 high = _mm256_extractf128_ps(acc, 1);
    __m128 sum4 = _mm_add_ps(low, high);
    sum4 = _mm_hadd_ps(sum4, sum4);
    sum4 = _mm_hadd_ps(sum4, sum4);
    return _mm_cvtss_f32(sum4);
}

FeatureMatcher::~FeatureMatcher() {
    if (table_) _aligned_free(table_);
}

bool FeatureMatcher::set_gallery(const std::vector<const Feature*>& features) {
    if (table_) {
        _aligned_free(table_);
        table_ = nullptr;
    }
    count_ = 0;
    dim_ = 0;
    padded_dim_ = 0;
    if (features.empty()) return true;

    const int dim = features[0]->dim();
    if (dim <= 0) return false;
    for (const Feature* f : features)
        if (f->dim() != dim) return false;

    dim_ = dim;
    padded_dim_ = (dim + 15) / 16 * 16;
    count_ = static_cast<int>(features.size());
    const size_t bytes = static_cast<size_t>(count_) * padded_dim_ * sizeof(float);
    table_ = static_cast<float*>(_aligned_malloc(bytes, 32));
    if (!table_) {
        count_ = 0;
        return false;
    }
    std::memset(table_, 0, bytes);
    for (int i = 0; i < count_; ++i)
        std::memcpy(table_ + static_cast<size_t>(i) * padded_dim_, features[i]->values.data(), dim * sizeof(float));
    return true;
}

void FeatureMatcher::match(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const {
    results.assign(queries.size(), MatchResult());
    if (count_ == 0 || queries.empty()) return;

    // M queries (the currently detected faces) against N = count_ registered features.
    const int q_count = static_cast<int>(queries.size());  // M
    QueryBlock block;
    if (!block.build(queries, dim_, padded_dim_)) return;

    // One pass over the N registered features. The row `g` (2 KB for 512 numbers) stays in the
    // L1 cache while all M queries are compared with it.
    for (int i = 0; i < count_; ++i) {
        const float* g = table_ + static_cast<size_t>(i) * padded_dim_;
        for (int q = 0; q < q_count; ++q) {
            if (!block.usable[q]) continue;
            update_best(results[q], i, dot_avx2(g, block.row(q, padded_dim_), padded_dim_));
        }
    }
}

void FeatureMatcher::score_matrix(const std::vector<Feature>& queries, const std::vector<int>& positions,
                                  std::vector<float>& scores) const {
    const size_t n_s = positions.size();
    scores.assign(queries.size() * n_s, kNoScore);
    if (count_ == 0 || queries.empty() || positions.empty()) return;

    QueryBlock block;
    if (!block.build(queries, dim_, padded_dim_)) return;

    // The listed registered rows are read once, each against all M queries (the same cache argument as in match()).
    for (size_t k = 0; k < n_s; ++k) {
        const int i = positions[k];
        if (i < 0 || i >= count_) continue;  // not a registered position: its column stays kNoScore
        const float* g = table_ + static_cast<size_t>(i) * padded_dim_;
        for (size_t q = 0; q < queries.size(); ++q) {
            if (!block.usable[q]) continue;
            scores[q * n_s + k] = dot_avx2(g, block.row(static_cast<int>(q), padded_dim_), padded_dim_);
        }
    }
}

void FeatureMatcher::match_reference(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const {
    results.assign(queries.size(), MatchResult());
    for (size_t q = 0; q < queries.size(); ++q) {
        if (queries[q].dim() != dim_ || count_ == 0) continue;
        for (int i = 0; i < count_; ++i) {  // a whole gallery pass for EACH query
            const float* g = table_ + static_cast<size_t>(i) * padded_dim_;
            float s = 0;
            for (int d = 0; d < dim_; ++d) s += g[d] * queries[q].values[d];
            update_best(results[q], i, s);
        }
    }
}

}  // namespace fa
