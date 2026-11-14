// bench_common.h - what all the benchmark programs share: the estimate for the target PC, the
// result row, and the command-line options.
//
// Used by bench.cpp (every engine, through the pipeline), bench_detect.cpp, bench_extract.cpp and
// bench_match.cpp. The measurements themselves are in bench_detect_part.h, bench_extract_part.h and
// bench_match_part.h, so that every program times exactly the same thing in exactly the same way.
//
// THIS COMPUTER IS NOT THE TARGET PC. The measurements are real, but for this machine only. A
// 4th-generation i5 is slower; "est. i5" multiplies by the factor given with --i5-factor (default
// 3.5: Haswell core at ~2.6-3.3 GHz versus a modern ~4.5+ GHz core, same thread count). It is an
// ESTIMATE. Run the benchmarks on the target PC to get the real numbers; they print PASS / FAIL
// against the limits.
#ifndef FA_BENCH_COMMON_H
#define FA_BENCH_COMMON_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "timer.h"

static double g_factor = 3.5;

// One result line: median / p95 / max of the timings, the estimate for the target PC and, when
// limit_ms > 0, PASS / FAIL against the limit (for this PC and for the estimate).
static void row(const char* what, const fa::TimingStats& t, double limit_ms) {
    const double med = t.median();
    const double est = med * g_factor;
    std::printf("    %-34s median %6.1f  p95 %6.1f  max %6.1f ms   est. i5 %6.1f ms", what, med, t.p95(), t.max(), est);
    if (limit_ms > 0) std::printf("   limit %.0f: this PC %s, est. i5 %s", limit_ms, med <= limit_ms ? "PASS" : "FAIL", est <= limit_ms ? "PASS" : "FAIL");
    std::printf("\n");
}

struct BenchOptions {
    int threads = 0;                      // 0 = the thread count written in the profile
    int frames = 40;                      // repetitions of every timing
    std::vector<std::string> profiles;    // the profile files given on the command line
};

// Read  --threads N  --frames K  --i5-factor F  (the last one sets g_factor); every other word is a
// profile file. When needs_profile is true and no profile was given, prints
// "usage: <program> [--threads N] [--frames K] [--i5-factor F] profile.ini ..." and returns false (the caller then exits with 2).
static bool parse_bench_options(int argc, char** argv, const char* program, bool needs_profile, BenchOptions& options) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) options.threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) options.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--i5-factor") && i + 1 < argc) g_factor = std::atof(argv[++i]);
        else options.profiles.push_back(argv[i]);
    }
    if (needs_profile && options.profiles.empty()) {
        std::printf("usage: %s [--threads N] [--frames K] [--i5-factor F] profile.ini ...\n", program);
        return false;
    }
    return true;
}

#endif  // FA_BENCH_COMMON_H
