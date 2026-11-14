// gen_features.cpp - make a synthetic feature database (requirement 4.2).
//
//   gen_features <output.csv|output.db> [count=10000] [dim=512] [seed=1]
//
// Writes `count` random people as a database of registered features (see feature_store.h), for speed
// tests and matching tests of any size. The format follows the file name: .db .sqlite .sqlite3 gives a
// SQLite database, anything else the default CSV file.
#include <cstdio>
#include <cstdlib>
#include "fs_compat.h"

#include "feature_store.h"
#include "synthetic.h"
#include "timer.h"

int main(int argc, char** argv) {
    using namespace fa;
    if (argc < 2) {
        std::printf("usage: gen_features <output.csv|output.db> [count=10000] [dim=512] [seed=1]\n");
        return 2;
    }
    const int count = argc > 2 ? std::atoi(argv[2]) : 10000;
    const int dim = argc > 3 ? std::atoi(argv[3]) : 512;
    const unsigned seed = argc > 4 ? static_cast<unsigned>(std::atoi(argv[4])) : 1u;
    if (count <= 0 || dim <= 0 || dim > FA_MAX_FEATURE_DIM) {
        std::printf("count must be > 0 and dim in 1..%d\n", FA_MAX_FEATURE_DIM);
        return 2;
    }
    const double start = now_ms();
    fa::fs::path parent = fa::fs::path(argv[1]).parent_path();
    if (!parent.empty()) fa::fs::create_directories(parent);  // make the output folder if needed
    FeatureStore store;
    for (Record& r : make_synthetic_gallery(count, dim, seed)) store.add(r);
    if (!store.save(argv[1])) {
        std::printf("cannot write %s: %s\n", argv[1], store.last_error().c_str());
        return 1;
    }
    std::printf("wrote %d random features of %d numbers to %s (%.0f ms)\n", count, dim, argv[1], now_ms() - start);
    return 0;
}
