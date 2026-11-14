// test_match.cpp - verifies the feature matching engine and the database of registered people (CSV and SQLite)
// on 10,000 synthetic features (generated here, no files needed).
//
// Checks: M:N answers (M query faces against N registered features) equal the plain reference
// answers, for M and N that differ in every direction; speed; how often strangers are accepted
// at several thresholds; register / delete / modify + CSV round trip; and the managed database
// (write-through, undo when the file cannot be written, lock against a second program).
#include <direct.h>  // _mkdir, _rmdir

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>

#include "feature_store.h"
#include "sqlite3.h"
#include "sqlite_db.h"
#include "matcher.h"
#include "synthetic.h"
#include "timer.h"

static int g_failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }   \
        else std::printf("ok:   %s\n", msg);                              \
    } while (0)

using namespace fa;

// Run `fn` a number of times and return the median time in milliseconds.
template <class F>
static double median_ms(int repeats, F fn) {
    TimingStats t;
    for (int i = 0; i < repeats; ++i) {
        double a = now_ms();
        fn();
        t.add(now_ms() - a);
    }
    return t.median();
}

static void test_matching(int dim) {
    std::printf("\n=== matching, 10000 registered features of %d numbers ===\n", dim);
    const int gallery_size = 10000;
    FeatureStore store;
    for (Record& r : make_synthetic_gallery(gallery_size, dim, 12345)) store.add(r);
    const FeatureMatcher& matcher = store.matcher();
    CHECK(matcher.size() == gallery_size && matcher.dim() == dim, "gallery holds 10000 features");

    std::mt19937 rng(777);

    // ---- correctness: the fast M:N code must agree with the plain reference ----
    for (int q_count : {1, 4, 16}) {  // M queries against N = 10000
        std::vector<Feature> queries;
        for (int q = 0; q < q_count; ++q)
            queries.push_back(noisy_copy(store.records()[rng() % gallery_size].feature, 1.0f, rng));
        std::vector<MatchResult> fast, ref;
        matcher.match(queries, fast);
        matcher.match_reference(queries, ref);
        bool same = true;
        for (int q = 0; q < q_count; ++q)
            if (fast[q].index != ref[q].index || std::fabs(fast[q].score - ref[q].score) > 1e-4f) same = false;
        char msg[96];
        std::snprintf(msg, sizeof(msg), "M:N result equals the reference result for M = %d simultaneous faces against N = 10000", q_count);
        CHECK(same, msg);
    }

    // ---- genuine people: found as the right person? ----
    {
        int correct = 0;
        const int trials = 2000;
        std::vector<Feature> queries;
        std::vector<int> truth;
        for (int i = 0; i < trials; ++i) {
            int who = rng() % gallery_size;
            truth.push_back(who);
            queries.push_back(noisy_copy(store.records()[who].feature, 1.0f, rng));  // cosine ~0.71
        }
        std::vector<MatchResult> results;
        matcher.match(queries, results);
        double sum_score = 0;
        for (int i = 0; i < trials; ++i) {
            if (results[i].index == truth[i]) ++correct;
            sum_score += results[i].score;
        }
        std::printf("      genuine probes (noise 1.0): %d / %d identified, mean score %.3f\n", correct, trials, sum_score / trials);
        CHECK(correct == trials, "every genuine probe is identified as the right person");
    }

    // ---- strangers: how often is a stranger accepted? ----
    {
        const int trials = 20000;
        std::vector<Feature> queries;
        for (int i = 0; i < trials; ++i) queries.push_back(random_feature(rng, dim));
        std::vector<MatchResult> results;
        matcher.match(queries, results);
        // The similarity of two random directions has a spread of 1/sqrt(dim), and the
        // best of 10000 strangers lies about 4 spreads above zero. So the safe
        // threshold depends on the feature length: that is one reason to prefer
        // 256..512 numbers over 128.
        const float spread = 1.0f / std::sqrt(static_cast<float>(dim));
        const float thresholds[] = {3.0f * spread, 4.0f * spread, 5.0f * spread, 6.0f * spread};
        for (float t : thresholds) {
            int accepted = 0;
            for (const MatchResult& r : results) if (r.score >= t) ++accepted;
            std::printf("      strangers accepted at threshold %.3f (%.0f spreads): %d of %d (%.4f%%)\n", t,
                        t / spread, accepted, trials, 100.0 * accepted / trials);
        }
        int accepted_safe = 0;
        for (const MatchResult& r : results) if (r.score >= 6.0f * spread) ++accepted_safe;
        CHECK(accepted_safe == 0, "no random stranger reaches 6 spreads (6/sqrt(dim)) against 10000 people");
        // (This describes RANDOM directions. Real faces of different people are
        //  more alike than random vectors, so the real threshold must be calibrated
        //  on real data. See docs/RESULTS.md.)
    }

    // ---- how long does it take to save / load the 10000 features as CSV? ----
    {
        const double a = now_ms();
        const bool saved = store.save("build\\store_10000.csv");
        const double save_ms = now_ms() - a;
        FeatureStore reloaded;
        const double b = now_ms();
        const bool loaded = reloaded.load("build\\store_10000.csv");
        const double load_ms = now_ms() - b;
        std::printf("      CSV with 10000 x %d numbers: save %.0f ms, load %.0f ms\n", dim, save_ms, load_ms);
        CHECK(saved && loaded && reloaded.count() == gallery_size &&
                  reloaded.records()[9999].feature.values == store.records()[9999].feature.values,
              "10000 features survive the CSV round trip exactly");
        std::remove("build\\store_10000.csv");
    }

    // ---- speed: one M:N call versus M separate searches (the reference) ----
    std::printf("      speed (median of repeated runs, N = 10000 registered):\n");
    std::printf("        faces (M) |  M:N AVX2 (ms) | M separate 1:N searches, reference (ms)\n");
    double fast_8 = 0, ref_8 = 0;
    for (int q_count : {1, 2, 4, 8, 16}) {
        std::vector<Feature> queries;
        for (int q = 0; q < q_count; ++q) queries.push_back(random_feature(rng, dim));
        std::vector<MatchResult> out;
        double fast = median_ms(21, [&] { matcher.match(queries, out); });
        double ref = median_ms(5, [&] { matcher.match_reference(queries, out); });
        std::printf("        %9d | %14.3f | %18.3f\n", q_count, fast, ref);
        if (q_count == 8) { fast_8 = fast; ref_8 = ref; }
    }
    CHECK(fast_8 < ref_8, "M:N AVX2 matching is faster than M separate searches (M = 8 faces)");
}

// M and N are independent: the number of currently detected faces (M, the queries) has nothing to do
// with the number of registered faces (N, the gallery). Every combination, including M > N, M = 0 and
// N = 0, must give exactly M results, equal to the reference's, each pointing at a real gallery entry.
static void test_m_to_n_shapes() {
    std::printf("\n=== M:N: M detected faces against N registered faces, M and N independent ===\n");
    std::mt19937 rng(2024);
    bool all_same = true, all_sized = true, all_in_range = true, all_found = true;
    int combinations = 0, m_larger_than_n = 0;
    for (int dim : {128, 512}) {
        for (int n : {0, 1, 2, 5, 17, 100}) {  // N registered
            FeatureStore store;
            for (Record& r : make_synthetic_gallery(n, dim, 9000 + n)) store.add(r);
            const FeatureMatcher& matcher = store.matcher();
            for (int m : {0, 1, 2, 3, 16, 40}) {  // M detected
                std::vector<Feature> queries;
                std::vector<int> who;  // which registered person each query was made from
                for (int q = 0; q < m; ++q) {
                    if (n > 0) {
                        who.push_back(static_cast<int>(rng() % n));
                        queries.push_back(noisy_copy(store.records()[who.back()].feature, 0.3f, rng));
                    } else {
                        who.push_back(-1);
                        queries.push_back(random_feature(rng, dim));
                    }
                }
                std::vector<MatchResult> fast, ref;
                matcher.match(queries, fast);
                matcher.match_reference(queries, ref);
                ++combinations;
                if (m > n) ++m_larger_than_n;
                if (static_cast<int>(fast.size()) != m || static_cast<int>(ref.size()) != m) { all_sized = false; continue; }
                for (int q = 0; q < m; ++q) {
                    if (fast[q].index != ref[q].index || std::fabs(fast[q].score - ref[q].score) > 1e-4f) all_same = false;
                    if (n == 0 ? fast[q].index != -1 : (fast[q].index < 0 || fast[q].index >= n)) all_in_range = false;
                    if (n > 0 && who[q] != fast[q].index && n <= 17) all_found = false;  // 0.3 noise: still the right person (small N)
                }
            }
        }
    }
    std::printf("      %d combinations of M (0..40) and N (0..100), %d of them with M > N\n", combinations, m_larger_than_n);
    CHECK(all_sized, "for every M and N the matcher returns exactly M results");
    CHECK(all_same, "for every M and N the fast M:N result equals the reference result");
    CHECK(all_in_range, "every result points at a registered person (index 0..N-1), or is -1 when N = 0");
    CHECK(all_found, "with M > N and M < N alike, every face is found as the person it came from (N <= 17)");

    // M and N of different sizes in the same matcher: shrink and grow the gallery between calls
    FeatureStore store;
    for (Record& r : make_synthetic_gallery(5, 128, 77)) store.add(r);
    std::vector<Feature> sixteen;
    for (int q = 0; q < 16; ++q) sixteen.push_back(noisy_copy(store.records()[q % 5].feature, 0.3f, rng));
    std::vector<MatchResult> r;
    store.matcher().match(sixteen, r);
    bool right = r.size() == 16;
    for (int q = 0; right && q < 16; ++q) right = r[q].index == q % 5;
    CHECK(right, "M = 16 faces (3 of each of the N = 5 people, and one more) are each matched to their own person");
    for (Record& extra : make_synthetic_gallery(3, 128, 78)) { extra.id = "X" + extra.id; store.add(extra); }
    store.matcher().match(sixteen, r);
    CHECK(r.size() == 16 && store.matcher().size() == 8, "after registering three more people (N = 8) the same 16 faces are matched against all 8");
    store.matcher().match({sixteen[0]}, r);
    CHECK(r.size() == 1 && r[0].index == 0, "and one face (M = 1) against N = 8 gives one result");
}

static void test_store() {
    std::printf("\n=== feature store (register / delete / modify, CSV) ===\n");
    std::mt19937 rng(5);
    FeatureStore store;

    Record a{"E001", "Kim, Min-su", random_feature(rng, 256)};   // a comma in the name tests the CSV quoting
    Record b{"E002", "Lee \"JJ\" Park", random_feature(rng, 256)};  // quotes in the name
    CHECK(store.add(a) && store.add(b), "register two persons");
    CHECK(!store.add(a), "registering an existing id is refused");
    Record wrong_dim{"E003", "x", random_feature(rng, 128)};
    CHECK(!store.add(wrong_dim), "a feature of another length is refused");

    CHECK(store.save("build\\store_test.csv"), "save writes the CSV file");
    FeatureStore loaded;
    CHECK(loaded.load("build\\store_test.csv") && loaded.count() == 2, "load reads it back");
    bool exact = loaded.records()[0].name == a.name && loaded.records()[1].name == b.name;
    for (int d = 0; d < 256; ++d)
        if (loaded.records()[0].feature.values[d] != a.feature.values[d] ||
            loaded.records()[1].feature.values[d] != b.feature.values[d]) exact = false;
    CHECK(exact, "names (with comma and quotes) and every feature number survive the CSV round trip exactly");

    // modify: change the name, then the feature
    Feature fresh = random_feature(rng, 256);
    CHECK(store.update("E001", "Kim Minsu", Feature()), "modify the name only");
    CHECK(store.find("E001")->name == "Kim Minsu" && store.find("E001")->feature.values == a.feature.values,
          "name changed, feature untouched");
    CHECK(store.update("E001", "", fresh) && store.find("E001")->feature.values == fresh.values, "modify the feature only");
    CHECK(!store.update("E999", "x", Feature()), "modifying an unknown id is refused");

    // the matcher must follow the changes
    std::vector<MatchResult> r;
    store.matcher().match({fresh}, r);
    CHECK(r[0].index == 0 && r[0].score > 0.999f, "after modify, the new feature matches the person");
    store.matcher().match({a.feature}, r);
    CHECK(r[0].score < 0.5f, "after modify, the old feature no longer matches that person");

    // delete
    CHECK(store.remove("E001") && store.count() == 1 && !store.remove("E001"), "delete a person (twice is refused)");
    store.matcher().match({fresh}, r);
    CHECK(r[0].index == 0 && r[0].score < 0.5f, "after delete, the matcher only knows the remaining person");

    // damaged file: load must fail and keep the old content
    {
        std::ofstream bad("build\\store_bad.csv");
        bad << "E1,Name,3,0.1,0.2\n";  // says 3 numbers, has 2
    }
    CHECK(!store.load("build\\store_bad.csv") && store.count() == 1, "a damaged CSV is rejected and the old data is kept");
    {
        std::ofstream mixed("build\\store_mixed.csv");  // two lines with different feature lengths
        mixed << "A,a,2,0.6,0.8\nB,b,3,0.6,0.8,0\n";  // both are valid length-1 vectors, only the lengths differ
    }
    CHECK(!store.load("build\\store_mixed.csv") && store.count() == 1, "a CSV with features of different lengths is rejected");
    CHECK(store.last_error().find("differs from the first line") != std::string::npos, "... and the reason given is the different length");
    {
        std::ofstream bad_values("build\\store_inf.csv");
        bad_values << "A,a,2,inf,0\n";
    }
    CHECK(!store.load("build\\store_inf.csv") && store.count() == 1, "a CSV with a non-finite number is rejected");
    {
        std::ofstream big("build\\store_big.csv");
        big << "A,a,2,30,40\n";  // length 50: would score high against every query
    }
    CHECK(!store.load("build\\store_big.csv") && store.count() == 1, "a CSV with a feature that is not of length 1 is rejected");
    {
        std::ofstream bom("build\\store_bom.csv", std::ios::binary);
        bom << "\xEF\xBB\xBF" << "A,a,2,0.6,0.8\n";  // a byte order mark in front of the first id
    }
    FeatureStore with_bom;
    CHECK(with_bom.load("build\\store_bom.csv") && with_bom.count() == 1 && with_bom.records()[0].id == "A",
          "a UTF-8 byte order mark at the start of the CSV is skipped");
    Feature huge;
    huge.values = {3.0f, 4.0f};
    CHECK(!store.add(Record{"E8", "huge", huge}) , "add refuses a feature that is not of length 1");
    Feature not_a_number;
    not_a_number.values = {std::nanf(""), 0.0f};
    CHECK(!store.add(Record{"E9", "nan", not_a_number}), "add refuses a feature with a NaN");
    Record with_break{"E7", "two\nlines", random_feature(rng, 256)};
    CHECK(!store.add(with_break), "a name with a line break is refused (the CSV could not be read back)");
    CHECK(!store.update("E002", "bad\nname", Feature()), "modify with a line break in the name is refused too");
    // saving over an existing file replaces it completely
    CHECK(store.save("build\\store_replace.csv") && store.save("build\\store_replace.csv"), "saving twice over the same file works");
    FeatureStore again;
    CHECK(again.load("build\\store_replace.csv") && again.count() == store.count(), "the file saved over an existing one is complete");
    CHECK(!store.load("build\\does_not_exist.csv"), "a missing CSV is reported");
}

static bool file_is_there(const std::string& path) {
    return std::ifstream(path).good();
}

// ---------------------------------------------------------------------------------------------------------------
// CARD FIRST. The RF card is identified first: it is looked up in the database and the feature stored with it is
// retrieved; then the M faces of the current frame are compared with exactly the N features that the cards stand
// for. M and N are independent: fewer faces than cards (M < N), as many (M = N) or more (M > N). A card that is not
// in the database is a failed comparison, with no fallback to the whole database.
// ---------------------------------------------------------------------------------------------------------------
static void test_card_first() {
    std::printf("\n=== card first: M faces against the N features of N RF cards (M < N, M = N, M > N) ===\n");
    std::mt19937 rng(31);

    // ---- the matcher's table of similarities between M queries and a subset of N_s registered features ----
    {
        const int dim = 256, n = 200;
        FeatureStore store;
        for (Record& r : make_synthetic_gallery(n, dim, 4242)) store.add(r);
        const FeatureMatcher& matcher = store.matcher();
        bool sized = true, same = true;
        int combinations = 0, more_queries = 0, fewer_queries = 0, equal = 0;
        for (int m : {0, 1, 3, 7, 16}) {
            for (int ns : {0, 1, 3, 7, 40}) {
                std::vector<Feature> queries;
                for (int q = 0; q < m; ++q) queries.push_back(noisy_copy(store.records()[rng() % n].feature, 1.0f, rng));
                std::vector<int> positions;
                for (int k = 0; k < ns; ++k) positions.push_back(static_cast<int>(rng() % n));
                std::vector<float> scores;
                matcher.score_matrix(queries, positions, scores);
                ++combinations;
                if (m > ns) ++more_queries;
                else if (m < ns) ++fewer_queries;
                else ++equal;
                if (scores.size() != static_cast<size_t>(m) * ns) { sized = false; continue; }
                for (int q = 0; q < m; ++q)
                    for (int k = 0; k < ns; ++k) {
                        float s = 0;  // brute force
                        for (int d = 0; d < dim; ++d) s += store.records()[positions[k]].feature.values[d] * queries[q].values[d];
                        if (std::fabs(scores[q * ns + k] - s) > 1e-4f) same = false;
                    }
            }
        }
        std::printf("      %d combinations of M (0..16) and N_s (0..40): %d with M > N_s, %d with M < N_s, %d with M = N_s\n", combinations,
                    more_queries, fewer_queries, equal);
        CHECK(sized, "score_matrix returns exactly M x N_s similarities for every M and N_s");
        CHECK(same, "every similarity equals the brute-force dot product (rows = faces, columns = the chosen registered features)");

        std::vector<float> scores;
        const std::vector<Feature> one_face(1, store.records()[7].feature);
        matcher.score_matrix(one_face, std::vector<int>(1, 7), scores);
        CHECK(scores.size() == 1 && scores[0] > 0.999f, "N_s = 1: a face against the one registered feature it was made from (1:1)");
        std::vector<int> odd_positions;
        odd_positions.push_back(-1);
        odd_positions.push_back(5000);
        odd_positions.push_back(7);
        matcher.score_matrix(one_face, odd_positions, scores);
        CHECK(scores.size() == 3 && scores[0] == kNoScore && scores[1] == kNoScore && scores[2] > 0.999f, "a position outside 0 .. N-1 gives no similarity (kNoScore)");
        matcher.score_matrix(std::vector<Feature>(1, random_feature(rng, 64)), std::vector<int>(1, 7), scores);
        CHECK(scores.size() == 1 && scores[0] == kNoScore, "a face feature of the wrong length gets no similarity");
        FeatureMatcher empty_matcher;
        empty_matcher.score_matrix(one_face, std::vector<int>(1, 0), scores);
        CHECK(scores.size() == 1 && scores[0] == kNoScore, "an empty registered set (N = 0): no similarity");
    }

    // ---- the database record holds the card number next to the face feature ----
    FeatureStore s;
    std::vector<Feature> person;  // the registered people P0 .. P5 (P5 has no card)
    for (int i = 0; i < 6; ++i) person.push_back(random_feature(rng, 128));
    const char* cards[] = {"CARD-0", "CARD-1", "CARD-2", "CARD-3", "CARD-4", ""};
    bool registered = true;
    for (int i = 0; i < 6; ++i) registered = registered && s.add(Record{"P" + std::to_string(i), "Person " + std::to_string(i), person[i], cards[i]});
    CHECK(registered, "register six people, five with an RF card and one without");
    CHECK(!s.add(Record{"X1", "Dup", random_feature(rng, 128), "CARD-1"}) && s.last_error().find("P1") != std::string::npos,
          "a card that belongs to another person is refused (the error names the owner)");
    CHECK(!s.add(Record{"X2", "Line", random_feature(rng, 128), "bad\ncard"}), "a card number with a line break is refused");
    CHECK(s.find_by_card("CARD-3") && s.find_by_card("CARD-3")->id == "P3" && s.index_of_card("CARD-3") == 3 && s.index_of_card("nobody") == -1,
          "find_by_card / index_of_card look a person up by card (the index is his position in the gallery)");
    CHECK(!s.find_by_card("") && s.index_of_card("") == -1, "an empty card number is not a card that somebody owns");

    // faces: a new camera image of a registered person (noise 0.3: cosine about 0.96)
    auto face_of = [&](int i) { return noisy_copy(person[i], 0.3f, rng); };
    auto cardlist = [](std::initializer_list<const char*> c) { return std::vector<std::string>(c.begin(), c.end()); };
    CardMatch r;

    // M < N: one face, three cards
    {
        std::vector<Feature> faces(1, face_of(1));
        const bool ok = s.match_cards(faces, cardlist({"CARD-0", "CARD-1", "CARD-2"}), r);
        CHECK(ok && r.cards.size() == 3 && r.queries.size() == 1, "M = 1 face against N = 3 cards: 3 card entries, 1 face entry");
        CHECK(r.queries[0].best_card == 1 && r.queries[0].score > 0.9f, "... the face is most like the owner of the second card");
        CHECK(r.cards[1].best_query == 0 && r.cards[1].best_score > 0.9f && r.cards[0].best_score < 0.5f && r.cards[2].best_score < 0.5f,
              "... and only that card has a face that looks like its owner");
        CHECK(r.cards[0].found && r.cards[0].record == 0 && r.cards[2].record == 2, "... every card says which record it was retrieved from");
    }
    // M = N: three faces, three cards, the people in another order than the cards
    {
        std::vector<Feature> faces;
        faces.push_back(face_of(4));
        faces.push_back(face_of(0));
        faces.push_back(face_of(2));
        const bool ok = s.match_cards(faces, cardlist({"CARD-0", "CARD-2", "CARD-4"}), r);
        CHECK(ok && r.queries.size() == 3 && r.cards.size() == 3, "M = 3 faces against N = 3 cards");
        CHECK(r.queries[0].best_card == 2 && r.queries[1].best_card == 0 && r.queries[2].best_card == 1,
              "... each face is matched to the card of its own person");
        CHECK(r.cards[0].best_query == 1 && r.cards[1].best_query == 2 && r.cards[2].best_query == 0,
              "... and each card to the face of its owner");
    }
    // M > N: four faces, two cards
    {
        std::vector<Feature> faces;
        faces.push_back(face_of(0));
        faces.push_back(face_of(1));
        faces.push_back(face_of(3));
        faces.push_back(face_of(5));  // registered, but without a card
        const bool ok = s.match_cards(faces, cardlist({"CARD-1", "CARD-3"}), r);
        CHECK(ok && r.queries.size() == 4 && r.cards.size() == 2, "M = 4 faces against N = 2 cards");
        CHECK(r.cards[0].best_query == 1 && r.cards[1].best_query == 2 && r.cards[0].best_score > 0.9f && r.cards[1].best_score > 0.9f,
              "... the two faces whose people own the cards are found");
        CHECK(r.queries[0].score < 0.5f && r.queries[3].score < 0.5f,
              "... the other two faces (registered people, but not among the cards) do not look like either owner");
    }
    // a registered person with somebody else's card: compared with the owner only, not with everybody
    {
        std::vector<Feature> faces(1, face_of(2));
        s.match_cards(faces, cardlist({"CARD-4"}), r);
        CHECK(r.queries[0].best_card == 0 && r.queries[0].score < 0.5f, "a registered person with another person's card: low similarity to that owner (no search among all people)");
    }
    // card not in the database: a failed comparison, no fallback
    {
        std::vector<Feature> faces(1, face_of(2));
        const bool ok = s.match_cards(faces, cardlist({"NO-SUCH-CARD"}), r);
        CHECK(!ok && r.cards.size() == 1 && !r.cards[0].found && r.cards[0].record == -1 && r.cards[0].best_query == -1,
              "a card that is not in the database: the comparison fails (found = false)");
        CHECK(r.queries.size() == 1 && r.queries[0].best_card == -1 && r.queries[0].score == kNoScore,
              "... and the face is NOT searched among the registered people instead (it is registered, but gets no match)");
        std::vector<Feature> two_faces;
        two_faces.push_back(face_of(0));
        two_faces.push_back(face_of(3));
        const bool mixed = s.match_cards(two_faces, cardlist({"CARD-0", "NO-SUCH-CARD", "CARD-3"}), r);
        CHECK(!mixed && r.cards[0].found && !r.cards[1].found && r.cards[2].found, "one unknown card among three: the call fails, but the other cards are marked found");
        CHECK(r.cards[0].best_query == 0 && r.cards[2].best_query == 1 && r.cards[1].best_query == -1,
              "... the found cards are still compared, the unknown one is not");
        CHECK(!s.match_cards(two_faces, std::vector<std::string>(), r) && r.cards.empty() && r.queries.size() == 2, "no card at all: the comparison fails");
        CHECK(!s.match_cards(two_faces, cardlist({""}), r) && !r.cards[0].found, "an empty card number is not a card: the comparison fails");
    }
    // M = 0 faces; a face of the wrong length; the same card twice
    {
        const bool ok = s.match_cards(std::vector<Feature>(), cardlist({"CARD-0", "CARD-1"}), r);
        CHECK(ok && r.queries.empty() && r.cards.size() == 2 && r.cards[0].found && r.cards[0].best_query == -1,
              "M = 0 faces: both cards are found, nobody to compare");
        std::vector<Feature> odd(1, random_feature(rng, 64));
        CHECK(s.match_cards(odd, cardlist({"CARD-0"}), r) && r.queries[0].best_card == -1 && r.cards[0].best_query == -1, "a face feature of the wrong length is not compared");
        std::vector<Feature> faces(1, face_of(0));
        CHECK(s.match_cards(faces, cardlist({"CARD-0", "CARD-0"}), r) && r.cards.size() == 2 && r.cards[0].best_query == 0 && r.cards[1].best_query == 0,
              "the same card given twice is accepted (two entries)");
    }
    // the database changes are seen at once: the card of a deleted person is no longer found
    {
        std::vector<Feature> faces(1, face_of(4));
        CHECK(s.match_cards(faces, cardlist({"CARD-4"}), r) && r.queries[0].best_card == 0, "(setup) the owner is found by his card");
        CHECK(s.remove("P4") && !s.match_cards(faces, cardlist({"CARD-4"}), r) && !r.cards[0].found, "after the owner is deleted his card is not in the database: the comparison fails");
    }

    // changing the card
    CHECK(s.set_card_id("P5", "CARD-5") && s.find_by_card("CARD-5")->id == "P5", "give a person a card");
    CHECK(!s.set_card_id("P5", "CARD-1"), "... but not a card that belongs to another person");
    CHECK(s.set_card_id("P5", "CARD-5"), "setting the card a person already has is fine");
    CHECK(!s.set_card_id("E999", "X") && !s.set_card_id("P5", "bad\ncard"), "set_card_id refuses an unknown id and a card with a line break");
    CHECK(s.set_card_id("P5", "") && !s.find_by_card("CARD-5"), "an empty card number takes the card away");
    CHECK(s.set_card_id("P5", "CARD-5") && s.set_card_id("P1", "") && s.set_card_id("P5", "") && s.set_card_id("P1", "CARD-1"),
          "a card that was taken away can be given again");

    // the CSV file: header line, card column, round trip; older files without the column are still read
    s.add(Record{"P4", "Person 4", person[4], "card,with \"quote\""});
    CHECK(s.save("build\\store_cards.csv"), "save the database with cards");
    std::string first_line;
    {
        std::ifstream csv_file("build\\store_cards.csv");
        std::getline(csv_file, first_line);
    }
    CHECK(first_line == "id,name,card_id,dim,values", "the CSV starts with the header line id,name,card_id,dim,values");
    FeatureStore back;
    CHECK(back.load("build\\store_cards.csv") && back.count() == s.count() && back.find("P1")->card_id == "CARD-1" &&
              back.find("P4")->card_id == "card,with \"quote\"" && back.find("P5")->card_id.empty() &&
              back.find("P1")->feature.values == person[1].values,
          "the cards (also one with a comma and quotes) survive the CSV round trip with the features");
    {
        std::ofstream legacy("build\\store_legacy.csv");
        legacy << "A,old a,2,0.6,0.8\nB,old b,2,0.8,0.6\n";  // the older format: no header, no card column
    }
    FeatureStore old;
    CHECK(old.load("build\\store_legacy.csv") && old.count() == 2 && old.find("B")->card_id.empty() && old.find("B")->feature.values[0] == 0.8f,
          "a file in the older format (no header, no card column) is still read, the cards are empty");
    CHECK(old.save("build\\store_legacy.csv") && back.load("build\\store_legacy.csv") && back.count() == 2, "... and it is written in the new format afterwards");
    {
        std::ofstream dup("build\\store_dup_card.csv");
        dup << "id,name,card_id,dim,values\nA,a,K1,2,0.6,0.8\nB,b,K1,2,0.8,0.6\n";
    }
    CHECK(!old.load("build\\store_dup_card.csv") && old.last_error().find("duplicate card_id") != std::string::npos && old.count() == 2,
          "a file in which two people have the same card is refused, and the old data is kept");
    {
        std::ofstream no_cards("build\\store_no_cards.csv");
        no_cards << "id,name,card_id,dim,values\nA,a,,2,0.6,0.8\nB,b,,2,0.8,0.6\n";
    }
    CHECK(old.load("build\\store_no_cards.csv") && old.count() == 2, "many empty card numbers in a file are fine");
    {
        std::ofstream header_only("build\\store_header_only.csv");
        header_only << "id,name,card_id,dim,values\n";
    }
    CHECK(old.load("build\\store_header_only.csv") && old.count() == 0, "a file with only the header line is an empty database");
    std::remove("build\\store_cards.csv");
    std::remove("build\\store_legacy.csv");
    std::remove("build\\store_dup_card.csv");
    std::remove("build\\store_no_cards.csv");
    std::remove("build\\store_header_only.csv");
}

// The database is a managed file (CSV or SQLite): opened once, changed through the store, always in step with memory.
// The same checks run for both formats.
static void test_database(DatabaseFormat format) {
    const bool sqlite = format == DatabaseFormat::Sqlite;
    std::printf("\n=== managed database, format %s: open, write-through, undo on failure, lock ===\n", sqlite ? "SQLite" : "CSV");
    const std::string db = sqlite ? "build\\db_test.db" : "build\\db_test.csv";
    std::remove(db.c_str());
    std::remove((db + ".tmp").c_str());
    std::remove((db + "-journal").c_str());
    std::mt19937 rng(11);

    // ---- open creates the file; every change is in the file at once ----
    FeatureStore a;
    CHECK(!a.is_open() && a.path().empty(), "a new store is not open");
    CHECK(a.open(db, format) && a.is_open() && a.count() == 0 && a.path() == db && a.format() == format,
          "opening a database that does not exist yet creates it, empty");
    CHECK(file_is_there(db), "... and the file exists on disk now");

    Record p{"E001", "Kim, Min-su", random_feature(rng, 128), "04A1B2C3"};
    Record q{"E002", "Lee", random_feature(rng, 128)};
    CHECK(a.add(p) && a.add(q), "register two people");
    FeatureStore view;  // an independent reader of the file
    CHECK(view.load(db) && view.count() == 2 && view.records()[0].name == p.name && view.records()[0].feature.values == p.feature.values && view.records()[0].card_id == p.card_id,
          "the file holds both at once (with name, feature and card), without a save() call");
    Feature fresh = random_feature(rng, 128);
    CHECK(a.update("E002", "Lee Jr", fresh) && view.load(db) && view.find("E002")->name == "Lee Jr" && view.find("E002")->feature.values == fresh.values,
          "modify is in the file at once");
    CHECK(a.set_card_id("E002", "CARD-2") && view.load(db) && view.find("E002")->card_id == "CARD-2", "giving a person a card is in the file at once");
    CHECK(a.set_card_id("E002", "") && view.load(db) && view.find("E002")->card_id.empty(), "taking the card away is in the file at once");
    CHECK(a.remove("E001") && view.load(db) && view.count() == 1 && !view.find("E001"), "delete is in the file at once");
    CHECK(!a.add(q) && view.load(db) && view.count() == 1, "a refused change (an id that exists) leaves the file as it was");
    CHECK(!file_is_there(db + ".tmp") && !file_is_there(db + "-journal"), "no temporary or journal file is left behind");

    // ---- one program at a time ----
    FeatureStore b;
    CHECK(!b.open(db, format) && !b.is_open() && b.last_error().find("another program") != std::string::npos,
          "a second store cannot open the database while the first one has it open");
    CHECK(!a.load(db) && a.last_error().find("is open") != std::string::npos && a.count() == 1,
          "load() is refused on an open store (the memory must not drift away from its file)");
    CHECK(a.open(db, format) && a.count() == 1 && a.find("E002"), "opening the path the store is bound to reads the file again (and keeps the lock)");
    CHECK(!b.open(db, format), "... the lock is still held after that");
    a.close();
    CHECK(!a.is_open() && a.count() == 1, "close() keeps the records in memory");
    CHECK(!file_is_there(db + ".lock"), "close() removes the lock file");
    CHECK(a.add(Record{"E003", "memory only", random_feature(rng, 128)}) && view.load(db) && view.count() == 1,
          "after close() a change stays in memory (the file is not touched)");
    CHECK(b.open(db, format) && b.count() == 1 && b.find("E002") && b.find("E002")->name == "Lee Jr", "another store can open the database now and sees the saved state");
    b.close();
    {
        FeatureStore c;
        CHECK(c.open(db, format), "a third store opens it");
    }  // destroyed here
    FeatureStore d;
    CHECK(d.open(db, format), "the lock is released when the store that held it is destroyed");

    // ---- a change that cannot be written is undone ----
    CHECK(d.add(Record{"E005", "Park", random_feature(rng, 128)}) && d.count() == 2, "(setup) the database holds E002 and E005");
    // Make writing impossible: CSV - the temporary file name is taken by a folder; SQLite - another connection holds the write lock.
    sqlite3* blocker = nullptr;
    if (sqlite) {
        const bool opened = sqlite3_open_v2(db.c_str(), &blocker, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK;
        CHECK(opened && sqlite3_exec(blocker, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK,
              "(setup) another connection takes the write lock of the SQLite file, so that it cannot be written");
    } else {
        CHECK(_mkdir((db + ".tmp").c_str()) == 0, "(setup) block the temporary file name, so that the file cannot be written");
    }
    Feature old_feature = d.find("E002")->feature;
    Record r4{"E004", "not saved", random_feature(rng, 128)};
    CHECK(!d.add(r4) && d.count() == 2 && !d.find("E004"), "register: the file cannot be written, so the registration is undone");
    CHECK(d.last_error().find("undone") != std::string::npos, "... and the error says so");
    CHECK(!d.update("E002", "Changed", random_feature(rng, 128)) && d.find("E002")->name == "Lee Jr" && d.find("E002")->feature.values == old_feature.values,
          "modify: the old name and the old feature are restored");
    std::vector<MatchResult> r;
    d.matcher().match({old_feature}, r);
    CHECK(r[0].index == 0 && r[0].score > 0.999f, "... and the matcher matches the old feature again");
    CHECK(!d.set_card_id("E002", "NEW-CARD") && d.find("E002")->card_id.empty() && !d.find_by_card("NEW-CARD"),
          "set_card_id: the new card is taken back");
    CHECK(!d.remove("E002") && d.count() == 2 && d.records()[0].id == "E002", "delete: the person is back, at the same place in the list");
    d.matcher().match({old_feature}, r);
    CHECK(r[0].index == 0 && r[0].score > 0.999f, "... and the matcher knows him again");
    CHECK(view.load(db) && view.count() == 2 && view.records()[0].id == "E002" && view.records()[1].id == "E005",
          "the file on disk is still the last good state");
    if (sqlite) {
        sqlite3_exec(blocker, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(blocker);
    } else {
        _rmdir((db + ".tmp").c_str());
    }
    CHECK(d.add(r4) && view.load(db) && view.count() == 3, "once the problem is gone, the same change works and is written");
    d.close();

    // ---- everything is there after a restart ----
    FeatureStore restarted;
    CHECK(restarted.open(db, format) && restarted.count() == 3 && restarted.find("E004") && restarted.find("E005") && restarted.find("E002")->name == "Lee Jr",
          "a new store that opens the file finds all the registered people");
    restarted.close();

    // ---- refused openings ----
    {
        std::ofstream bad("build\\db_bad.csv");
        bad << "E1,Name,3,0.1,0.2\n";  // says 3 numbers, has 2
    }
    FeatureStore e;
    CHECK(!e.open("build\\db_bad.csv", DatabaseFormat::Csv) && !e.is_open() && !file_is_there("build\\db_bad.csv.lock"),
          "a damaged database file is refused, and its lock is released again");
    FeatureStore g;
    CHECK(g.open(db, format) && g.count() == 3 && !g.open("build\\db_bad.csv", DatabaseFormat::Csv) && g.path() == db && g.count() == 3,
          "a refused open leaves the store bound to its old database, unchanged");
    CHECK(!g.open(""), "an empty path is refused");
    FeatureStore h;
    CHECK(!h.open("build\\no_such_folder\\db.csv") && !h.is_open() && !h.last_error().empty(), "a database in a folder that does not exist is refused");
    CHECK(!h.open("build\\no_such_folder\\db.db") && !h.is_open() && !h.last_error().empty(), "... also in the SQLite format");
    g.close();

    std::remove(db.c_str());
    std::remove("build\\db_bad.csv");
}

// What is special about the SQLite format and about choosing between the two formats.
static void test_format_choice() {
    std::printf("\n=== choosing the database format: CSV (default) or SQLite ===\n");
    std::mt19937 rng(77);
    std::vector<Record> people = make_synthetic_gallery(60, 128, 555);
    people[3].card_id = "CARD-3";
    people[10].card_id = "card,with \"quote\"";
    people[10].name = "Kim, \"Min\" Su";
    FeatureStore source;
    for (const Record& rec : people) source.add(rec);

    // ---- the format follows the file name (a new file) and the file content (an existing file) ----
    const char* names[] = {"build\\fmt_a.csv", "build\\fmt_b.db", "build\\fmt_c.sqlite", "build\\fmt_d.SQLITE3", "build\\fmt_e.txt", "build\\fmt_f"};
    const DatabaseFormat expected[] = {DatabaseFormat::Csv, DatabaseFormat::Sqlite, DatabaseFormat::Sqlite, DatabaseFormat::Sqlite, DatabaseFormat::Csv, DatabaseFormat::Csv};
    bool names_ok = true;
    for (int i = 0; i < 6; ++i) {
        std::remove(names[i]);
        FeatureStore s;
        if (!s.open(names[i]) || s.format() != expected[i]) names_ok = false;
    }
    CHECK(names_ok, "a new database is SQLite for .db .sqlite .sqlite3 (any letter case), and CSV (the default) for everything else");
    for (const char* n : names) std::remove(n);

    CHECK(source.save("build\\fmt_x.csv") && source.save("build\\fmt_x.db"), "save the same people as CSV and as SQLite (chosen by the extension)");
    CHECK(is_sqlite_file("build\\fmt_x.db") && !is_sqlite_file("build\\fmt_x.csv"), "the .db file really is a SQLite file, the .csv file is text");
    // a file's content wins over its name
    CHECK(source.save("build\\fmt_lies.db", DatabaseFormat::Csv) && source.save("build\\fmt_lies.csv", DatabaseFormat::Sqlite), "(setup) a CSV file named .db and a SQLite file named .csv");
    {
        FeatureStore s1, s2;
        CHECK(s1.open("build\\fmt_lies.db") && s1.format() == DatabaseFormat::Csv && s1.count() == 60 &&
                  s2.open("build\\fmt_lies.csv") && s2.format() == DatabaseFormat::Sqlite && s2.count() == 60,
              "an existing file is recognised by its content, not by its name");
    }
    // an explicit choice that does not fit the existing file is refused, never misread
    {
        FeatureStore s;
        CHECK(!s.open("build\\fmt_x.db", DatabaseFormat::Csv) && s.last_error().find("SQLite file") != std::string::npos && !s.is_open(),
              "asking for CSV on a SQLite file is refused (and the reason says so)");
        CHECK(!s.open("build\\fmt_x.csv", DatabaseFormat::Sqlite) && s.last_error().find("CSV file") != std::string::npos && !s.is_open(),
              "asking for SQLite on a CSV file is refused");
        CHECK(!file_is_there("build\\fmt_x.db.lock") && !file_is_there("build\\fmt_x.csv.lock"), "... without leaving a lock behind");
    }
    // the same people from both files
    FeatureStore from_csv, from_db;
    CHECK(from_csv.load("build\\fmt_x.csv") && from_db.load("build\\fmt_x.db") && from_csv.count() == 60 && from_db.count() == 60, "both files load");
    bool same = true;
    for (int i = 0; i < 60; ++i) {
        const Record &x = from_csv.records()[i], &y = from_db.records()[i], &z = source.records()[i];
        if (x.id != y.id || x.name != y.name || x.card_id != y.card_id || x.feature.values != y.feature.values || x.feature.values != z.feature.values ||
            y.name != z.name || y.card_id != z.card_id)
            same = false;
    }
    CHECK(same, "CSV and SQLite give exactly the same people, names, cards and feature numbers, in the same order");
    CHECK(from_db.find_by_card("CARD-3") && from_db.find_by_card("CARD-3")->id == people[3].id && from_db.find_by_card("card,with \"quote\""),
          "the card lookup works on the SQLite data");

    // ---- conversion in both directions loses nothing ----
    FeatureStore round;
    CHECK(from_db.save("build\\fmt_back.csv") && round.load("build\\fmt_back.csv") && round.count() == 60 &&
              round.records()[59].feature.values == people[59].feature.values && round.records()[10].name == people[10].name,
          "SQLite -> CSV conversion (save with the other format) loses nothing");
    CHECK(from_csv.save("build\\fmt_back.db") && round.load("build\\fmt_back.db") && round.count() == 60 && round.records()[3].card_id == "CARD-3",
          "CSV -> SQLite conversion loses nothing");
    CHECK(from_csv.save("build\\fmt_back.db") && round.load("build\\fmt_back.db") && round.count() == 60, "saving over an existing SQLite file replaces it completely");

    // ---- damaged or foreign SQLite files are refused ----
    {
        std::ofstream junk("build\\fmt_junk.db", std::ios::binary);
        junk << "this is not a database, it is a text file of some length so that it cannot be mistaken for an empty one\n";
    }
    FeatureStore t;
    CHECK(!t.open("build\\fmt_junk.db", DatabaseFormat::Sqlite) && !t.is_open(), "a file that is not a SQLite database is refused with the SQLite format asked for");
    {
        // an existing file of zero bytes (SQLite leaves one behind when a database is opened and nothing is written)
        { std::ofstream zero("build\\fmt_zero.db", std::ios::binary); }
        FeatureStore z;
        CHECK(z.open("build\\fmt_zero.db") && z.format() == DatabaseFormat::Sqlite && z.count() == 0 &&
                  z.add(Record{"Z1", "zero", random_feature(rng, 128)}) && is_sqlite_file("build\\fmt_zero.db"),
              "an empty (zero byte) .db file is an empty SQLite database: it is opened, given its table, and can be used");
        z.close();
        std::remove("build\\fmt_zero.db");
    }
    {
        sqlite3* foreign = nullptr;
        sqlite3_open_v2("build\\fmt_foreign.db", &foreign, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        sqlite3_exec(foreign, "CREATE TABLE something_else (x INTEGER); INSERT INTO something_else VALUES (1);", nullptr, nullptr, nullptr);
        sqlite3_close(foreign);
    }
    CHECK(!t.open("build\\fmt_foreign.db") && t.last_error().find("not a face_auth database") != std::string::npos,
          "a SQLite file of another application (no table 'people') is refused");
    {
        sqlite3* newer = nullptr;
        sqlite3_open_v2("build\\fmt_newer.db", &newer, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        sqlite3_exec(newer, "CREATE TABLE people (id TEXT); PRAGMA user_version = 99;", nullptr, nullptr, nullptr);
        sqlite3_close(newer);
    }
    CHECK(!t.open("build\\fmt_newer.db") && t.last_error().find("newer") != std::string::npos, "a database of a newer table layout is refused");
    {
        // a row whose feature is not a vector of length 1 (written behind the program's back)
        FeatureStore ok_store;
        ok_store.add(Record{"A", "a", random_feature(rng, 128)});
        ok_store.save("build\\fmt_bigrow.db");
        sqlite3* raw = nullptr;
        sqlite3_open_v2("build\\fmt_bigrow.db", &raw, SQLITE_OPEN_READWRITE, nullptr);
        std::vector<float> big(128, 5.0f);
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(raw, "INSERT INTO people (id, name, card_id, dim, feature) VALUES ('B', 'b', NULL, 128, ?1)", -1, &st, nullptr);
        sqlite3_bind_blob(st, 1, big.data(), 128 * 4, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
        sqlite3_close(raw);
        FeatureStore keep;
        keep.add(Record{"K", "keep", random_feature(rng, 128)});
        CHECK(!keep.load("build\\fmt_bigrow.db") && keep.last_error().find("length 1") != std::string::npos && keep.count() == 1,
              "a SQLite row whose feature is not of length 1 is refused (as in a CSV file), and the old data is kept");
    }
    {
        // SQLite itself enforces one owner per card number and one row per id
        sqlite3* raw = nullptr;
        sqlite3_open_v2("build\\fmt_back.db", &raw, SQLITE_OPEN_READWRITE, nullptr);
        const int dup_card = sqlite3_exec(raw, "UPDATE people SET card_id = 'CARD-3' WHERE card_id IS NULL AND rowid = 1", nullptr, nullptr, nullptr);
        const int dup_id = sqlite3_exec(raw, "INSERT INTO people (id, name, card_id, dim, feature) SELECT id, 'x', NULL, dim, feature FROM people WHERE rowid = 1", nullptr, nullptr, nullptr);
        sqlite3_close(raw);
        CHECK(dup_card != SQLITE_OK && dup_id != SQLITE_OK, "the SQLite table itself refuses a second owner of a card and a second row with the same id");
    }
    for (const char* n : {"build\\fmt_x.csv", "build\\fmt_x.db", "build\\fmt_lies.db", "build\\fmt_lies.csv", "build\\fmt_back.csv", "build\\fmt_back.db",
                          "build\\fmt_junk.db", "build\\fmt_foreign.db", "build\\fmt_newer.db", "build\\fmt_bigrow.db"})
        std::remove(n);
}

// The price of keeping the database in a file, for both formats: 10,000 registered people of 512 numbers.
static void test_database_speed() {
    std::printf("\n=== database file speed, 10,000 people x 512 numbers (this PC) ===\n");
    const int n = 10000, dim = 512;
    FeatureStore source;
    for (Record& r : make_synthetic_gallery(n, dim, 99)) source.add(r);
    std::mt19937 rng(5);
    double change_ms[2] = {0, 0};
    for (int f = 0; f < 2; ++f) {
        const bool sqlite = f == 1;
        const char* path = sqlite ? "build\\speed_10000.db" : "build\\speed_10000.csv";
        std::remove(path);
        double t = now_ms();
        const bool saved = source.save(path);
        const double save_ms = now_ms() - t;
        FeatureStore store;
        t = now_ms();
        const bool opened = store.open(path);  // reads the file, takes the lock
        const double open_ms = now_ms() - t;
        // registering one more person, changing a name, giving a card, deleting him: each is written to the file at once
        TimingStats add_t, update_t, card_t, remove_t;
        bool changes_ok = opened;
        for (int i = 0; i < 3; ++i) {
            const std::string id = "NEW" + std::to_string(i);
            t = now_ms();
            changes_ok = store.add(Record{id, "new person", random_feature(rng, dim)}) && changes_ok;
            add_t.add(now_ms() - t);
            t = now_ms();
            changes_ok = store.update(id, "renamed", Feature()) && changes_ok;
            update_t.add(now_ms() - t);
            t = now_ms();
            changes_ok = store.set_card_id(id, "CARD-" + id) && changes_ok;
            card_t.add(now_ms() - t);
            t = now_ms();
            changes_ok = store.remove(id) && changes_ok;
            remove_t.add(now_ms() - t);
        }
        std::printf("      %-6s file %6.1f MB | write the whole file %6.0f ms | open (read all) %6.0f ms | one change written: register %7.1f ms, rename %7.1f ms, give a card %7.1f ms, delete %7.1f ms\n",
                    sqlite ? "SQLite" : "CSV", static_cast<double>(std::ifstream(path, std::ios::binary | std::ios::ate).tellg()) / 1048576.0, save_ms, open_ms,
                    add_t.median(), update_t.median(), card_t.median(), remove_t.median());
        change_ms[f] = add_t.median();
        store.close();
        CHECK(saved && opened && store.count() == n && changes_ok, sqlite ? "SQLite: the 10,000 people are written, read, and changed one by one" : "CSV: the 10,000 people are written, read, and changed one by one");
        std::remove(path);
    }
    CHECK(change_ms[1] < change_ms[0], "with 10,000 people, registering one person is faster in SQLite (one row) than in CSV (the whole file)");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_matching(512);
    test_matching(128);
    test_m_to_n_shapes();
    test_store();
    test_card_first();
    test_database(DatabaseFormat::Csv);
    test_database(DatabaseFormat::Sqlite);
    test_format_choice();
    test_database_speed();
    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
