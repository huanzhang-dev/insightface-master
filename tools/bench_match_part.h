// bench_match_part.h - the speed measurement of the feature matching engine.
//
// Requirement: as fast as possible, M:N (all M currently detected faces against all N registered people
// in one call). Measured on N = 10,000 synthetic registered people, with M = 1, 4 and 16.
#ifndef FA_BENCH_MATCH_PART_H
#define FA_BENCH_MATCH_PART_H

#include <cstdio>
#include <random>
#include <vector>

#include "bench_common.h"
#include "feature_store.h"
#include "matcher.h"
#include "synthetic.h"
#include "timer.h"

// Time of one M:N match of M = 1, 4 and 16 query faces against N = 10,000 registered features of 128, 256
// and 512 numbers (50 runs each, after one warm-up run). Independent of any model profile.
static void bench_matching() {
    using namespace fa;
    std::printf("\n=== matching, M:N AVX2: M query faces against N = 10,000 registered features ===\n");
    for (int dim : {128, 256, 512}) {
        FeatureStore store;
        for (Record& r : make_synthetic_gallery(10000, dim, 1)) store.add(r);
        const FeatureMatcher& m = store.matcher();
        std::mt19937 rng(3);
        for (int q : {1, 4, 16}) {
            std::vector<Feature> queries;
            for (int i = 0; i < q; ++i) queries.push_back(random_feature(rng, dim));
            std::vector<MatchResult> out;
            TimingStats t;
            m.match(queries, out);
            for (int k = 0; k < 50; ++k) { double a = now_ms(); m.match(queries, out); t.add(now_ms() - a); }
            char label[64];
            std::snprintf(label, sizeof(label), "%3d numbers, %2d face(s)", dim, q);
            row(label, t, 0);
        }
    }
}

#endif  // FA_BENCH_MATCH_PART_H
