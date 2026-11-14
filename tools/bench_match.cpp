// bench_match.cpp - speed of the feature matching engine alone.
//
//   bench_match [--i5-factor F]
//
// Requirement: as fast as possible (M:N, AVX2). Prints the time of one match of M = 1, 4 and 16 query
// faces against N = 10,000 registered features of 128, 256 and 512 numbers. Needs no model, no picture and
// no network library: the features are generated (src/testdata/synthetic.cpp).
// THIS COMPUTER IS NOT THE TARGET PC: see bench_common.h for what "est. i5" means.
#include <cstdio>

#include "bench_common.h"
#include "bench_match_part.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BenchOptions options;
    if (!parse_bench_options(argc, argv, "bench_match", false, options)) return 2;
    bench_matching();
    return 0;
}
