// matcher.h - the feature matching engine (M:N comparison, written for speed).
//
// Two features of the same person point in nearly the same direction, so the
// "cosine similarity" of two features (their dot product, because every feature
// has length 1) is high. Matching = finding, for each camera face, the
// registered feature with the highest dot product.
//
// M:N MATCHING
//   M = the number of currently detected faces (their features are the "queries"; usually 1
//   to 4 in a camera frame) and N = the number of faces registered in the database (up to
//   10,000 or more). All M query features are compared with all N registered features in ONE
//   call: M x N dot products, M results. M and N are independent: M = 1 against N = 10,000 is the
//   normal case, M may also be larger than N (16 faces against 5 registered people), and
//   either may be 0 (then nothing matches). The two numbers are not tied to each other.
//
// WHY ONE M:N CALL AND NOT M SEPARATE 1:N SEARCHES
//   A loop over the faces (for each camera face, scan the whole registered list) reads the
//   big registered list from memory once PER FACE. The M:N matcher below reads
//   each registered feature ONCE and compares it with ALL M camera faces while it
//   is still in the fast CPU cache. With 10,000 registered features of 512
//   numbers (20 MB) the memory traffic, not the arithmetic, is the bottleneck,
//   so reading the list once is the main speed-up. The dot products themselves
//   use AVX2 instructions (8 numbers per instruction).
#ifndef FA_MATCHER_H
#define FA_MATCHER_H

#include <vector>

#include "types.h"

namespace fa {

// "No similarity": below the lowest possible cosine similarity (-1), so it never wins and never passes a threshold.
const float kNoScore = -2.0f;

struct MatchResult {
    int index = -1;            // position of the best registered feature, -1 if the gallery is empty
    float score = kNoScore;    // its cosine similarity (-1 .. 1)
    float second_score = kNoScore;  // similarity of the second best (available for callers that want to check the margin; the pipeline does not use it)
};

class FeatureMatcher {
public:
    FeatureMatcher() = default;
    ~FeatureMatcher();
    FeatureMatcher(const FeatureMatcher&) = delete;
    FeatureMatcher& operator=(const FeatureMatcher&) = delete;

    // Replace the registered features (the N side). All features must have the same length.
    // The numbers are copied into an internal, aligned and zero-padded table.
    bool set_gallery(const std::vector<const Feature*>& features);

    int size() const { return count_; }  // N, the number of registered features
    int dim() const { return dim_; }

    // M:N matching: `queries` holds M features, the gallery holds N; `results` receives exactly M
    // results (one per query, same order), each the best of the N registered features. M and N
    // are independent (any M >= 0, any N >= 0). The gallery is read once, for all M queries.
    // Queries must have the gallery's dimension; otherwise their result stays at index -1.
    void match(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const;

    // M:N matching where N is a SUBSET of the registered features, with ALL similarities returned. The
    // subset is `positions`: positions (0 .. N-1, as in the gallery given to set_gallery) of the
    // N_s registered features that the M queries are compared with, and with no others. The typical
    // use: the features that RF cards stand for were retrieved from the database, and the faces of
    // the current frame are compared with just those (FeatureStore::match_cards).
    //   * `scores` is resized to M * N_s; scores[q * N_s + k] is the similarity of query q and the
    //     registered feature at positions[k]. M and N_s are independent (fewer queries than
    //     positions, as many, or more);
    //   * a query of the wrong length, or a position outside 0 .. N-1, gives kNoScore for its entries;
    //   * the caller picks what it needs from the table: the best candidate of every query (a row),
    //     the best query for every candidate (a column), or the whole table.
    // Only the listed rows of the registered table are read, so the time grows with N_s, not with N.
    void score_matrix(const std::vector<Feature>& queries, const std::vector<int>& positions, std::vector<float>& scores) const;

    // Plain reference version (one gallery pass per query, no SIMD). Used by the
    // tests to check that match() gives the same answers (tests/test_match.cpp).
    void match_reference(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const;

private:
    int count_ = 0;          // N: number of registered features
    int dim_ = 0;            // true feature length
    int padded_dim_ = 0;     // length rounded up to a multiple of 16 (zeros added)
    float* table_ = nullptr;  // count_ x padded_dim_ numbers, 32-byte aligned
};

// Dot product of two aligned arrays of `padded_dim` floats (multiple of 16).
float dot_avx2(const float* a, const float* b, int padded_dim);

}  // namespace fa

#endif  // FA_MATCHER_H
