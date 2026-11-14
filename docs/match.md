# Feature matching engine (`src/match/`)

## What this is for

The matching engine answers one question: "which registered person does this camera face look most like, and how similar is it?" Its input is a list of registered features (the "gallery", normally taken from the feature store, see `store.md`) and a list of query features (the faces seen by the camera, made by the extraction engine, see `feature.md`). Its output is one `MatchResult` per query: the position of the best registered feature, its similarity score, and the score of the second best. The engine only measures similarity; the decision "accept or not" is made by the caller, which compares the score with the profile's threshold (`recognizer.threshold`; in `AuthPipeline::process` a track is verified when `index >= 0` and `score >= threshold`). The matcher is written for speed and does **M:N matching**: **M** is the number of currently detected faces (their features are the "queries") and **N** is the number of faces registered in the database. All M query features are compared with all N registered features in **one call** (M x N dot products, M results). The matcher reads the registered list from memory only once, and uses AVX2 CPU instructions that process 8 numbers at a time. M and N are independent of each other (see "Background: M and N"). N is normally the whole database, but it can also be a **subset** of it: when the person was identified before, for example by an RF card, the card information is identified FIRST (the card is looked up in the database and the feature stored with it is retrieved), and then `score_matrix` compares the faces of the frame with just those retrieved features (N_s of them; N_s can be smaller than, equal to or larger than the number M of faces). See `store.md` (`FeatureStore::match_cards`). In the pipeline it is step 5, "Match" (see `ARCHITECTURE.md`). Measurements of speed are in `RESULTS.md`; no numbers are quoted here.

### Background: cosine similarity

A feature is a list of 128 to 512 numbers. Think of it as an arrow in a space with that many directions. Two photos of the same person give arrows that point in nearly the same direction. The **cosine similarity** is the cosine of the angle between two arrows: 1 means the same direction, 0 means unrelated (at a right angle), -1 means opposite. In a formula it is `dot(a, b) / (length(a) * length(b))`, where `dot(a, b) = a0*b0 + a1*b1 + a2*b2 + ...`.

The extraction engine scales every feature to **length 1** (`normalize_feature`). Then the two lengths in the formula are both 1 and the cosine similarity is simply the dot product. Example: `a = (0.6, 0.8)` and `b = (0.8, 0.6)` both have length 1, and `dot = 0.6*0.8 + 0.8*0.6 = 0.96`. That is why the matcher only needs dot products: no division and no square root. The matcher does not check or force the length 1; if it is given features that are not scaled, the numbers it returns are plain dot products, not cosine similarities. The feature store (see `store.md`) protects the registered side: it refuses to hold a feature that is not a finite vector of length 1. Query features are not checked by the matcher.

### Background: M and N

Two different numbers are involved in every match, and the matcher treats them as independent:

| Letter | Meaning | Typical size |
|---|---|---|
| **M** | The number of currently detected faces. Their features are the "queries" (in the pipeline: the new features of one camera frame). | 1 to 4 in a camera frame |
| **N** | The number of faces registered in the database. Their features are the "gallery". | up to 10,000 or more |

One call of `match` compares all M query features with all N registered features (M x N dot products) and returns M results, one per query, each the best of the N. M = 1 against N = 10,000 is the normal case. M may be larger than N (16 faces against 5 registered people), and either number may be 0: with N = 0 every query gets "no match", with M = 0 the result list is empty. Neither number is tied to the other. `tests/test_match.cpp` checks 72 combinations of M and N, 30 of them with M larger than N (`test_m_to_n_shapes`).

**N can be a subset of the database.** In certain scenes the set of candidates is derived from a subset of the data stored in the database. For example, the person shows an RF card. The card information is always identified FIRST: the card is looked up in the database and the feature stored with it is retrieved, and only then are the faces detected in the current frame compared with the retrieved features. A card that is not in the database is a failed comparison; nothing falls back to a search among everybody. The matcher's part of this is `score_matrix`: the caller names the positions of the registered features that the cards stand for (N_s of them), the matcher returns the full table of M x N_s similarities, and the caller picks from the table what it needs:

```
match(queries, results):                   query q  --compared with-->  all N registered features; the best one is kept
score_matrix(queries, positions, scores):  query q  --compared with-->  only the N_s features listed in positions;
                                           all M x N_s similarities are returned (row = face, column = listed feature)
```

M and N_s are independent of each other: fewer faces than cards (M < N_s), as many (M = N_s) or more faces than cards (M > N_s) are all fine, and either may be 0. The table can be read by rows (for every face: which card's owner does it look most like?), by columns (for every card: which face looks most like its owner?) or as a whole. `FeatureStore::match_cards` does exactly this, and `AuthPipeline::begin_card_check` uses it (see `store.md` and `pipeline.md`). The positions are positions in the whole gallery, as in `set_gallery`.

### Background: why one M:N call reads the registered list once

The simple way to match M faces is M separate 1:N searches (one query against the N registered features): a loop takes the first camera face and scans the whole registered list, then takes the second camera face and scans the whole list again, and so on. The registered list is large: 10,000 features of 512 numbers of 4 bytes each is 20,480,000 bytes (about 20 MB, as the header says). That is far more than fits in the CPU's **cache**, the small, very fast memory next to the processor. Data that comes from the main memory (RAM) is much slower to read than data that is already in the cache. So the loop of separate searches spends most of its time waiting for memory, and it reads those 20 MB once **per camera face** (M = 8 faces: 8 times).

The M:N matcher turns the loops around. The outer loop walks over the registered features. For each registered feature (one row of 512 numbers, 2 KB) it compares with **every** query face while that row is still in the fast cache, then moves on. The 20 MB are read once, however many faces (M) there are. The arithmetic is the same, only the order differs; the memory traffic, not the arithmetic, is the bottleneck, so reading the list once is the main speed-up. The dot products themselves use AVX2 (see `dot_avx2`).

```
M separate 1:N searches (match_reference)     one M:N call (match)

for each query q (M of them):                 for each registered row g (N of them):   <- g read ONCE
    for each registered row g (N of them):        for each query q (M of them):        <- q block is small, stays in cache
        score = dot(g, q)                             score = dot(g, q)
        keep best/second of q                         keep best/second of q
```

### Background: how the registered list is stored inside the matcher

The matcher keeps its own copy of the gallery in one block of memory (`table_`), one row per registered feature. Each row is **padded with zeros** up to a multiple of 16 numbers, and the whole block starts at an address divisible by 32 bytes:

```
table_  (count_ rows, each padded_dim_ floats)

row 0:  [ v0 v1 v2 ... v(dim-1) | 0 0 0 ... ]      <- zeros up to a multiple of 16
row 1:  [ v0 v1 v2 ... v(dim-1) | 0 0 0 ... ]
...
```

For 128, 256 and 512 numbers nothing needs to be added (they are already multiples of 16). For example, a feature of 100 numbers is padded to 112, and one of 130 numbers to 144. The padding zeros add 0 to every dot product, so results are unchanged; the benefit is that the fast loop can always take 16 numbers at a time and never needs a special case for a leftover tail.

## src/match/matcher.h

The public header of the engine. Macro: `FA_MATCHER_H` is the include guard (it stops the header from being read twice).

| Function | Purpose |
|---|---|
| `kNoScore` (constant) | The value `-2.0f` that stands for "no similarity". |
| `MatchResult` (struct) | The answer for one query: best index, best score, second-best score. |
| `FeatureMatcher` (class) | Holds the gallery and does the matching. |
| `FeatureMatcher()` | Creates an empty matcher. |
| `FeatureMatcher(const FeatureMatcher&)` and `operator=` (deleted) | Forbid copying. |
| `size()` | Number of registered features in the gallery. |
| `dim()` | True feature length of the gallery. |

Declared here and documented under `matcher.cpp`, where they are defined: `~FeatureMatcher`, `set_gallery`, `match`, `score_matrix`, `match_reference`, `dot_avx2`.

### kNoScore

```cpp
const float kNoScore = -2.0f;
```

**What it does.** "No similarity": a value below the lowest possible cosine similarity (-1), so it never wins a comparison and never passes a threshold. It is the default of every score in `MatchResult`, the value that `score_matrix` leaves in an entry that could not be computed, and the value that `CardMatch` and `CardStatus` (see `store.md`, `pipeline.md`) use for "no face / no card compared".

**How it works.** A `const float` in `matcher.h` (namespace `fa`). It is the default that `MatchResult` uses for both of its scores (the same value as the literal `-2.0f` that the struct used before it had a name).

**Used by.** `MatchResult`, `FeatureMatcher::score_matrix`, `CardMatch::Card::best_score`, `CardMatch::Query::score`, `CardStatus::score`, `FeatureStore::match_cards`, `AuthPipeline::begin_card_check`, `AuthPipeline::fill_card_status`, `tests/test_match.cpp` (`test_card_first`), `tests/test_pipeline.cpp` (`CardLog`).

### MatchResult (struct)

```cpp
struct MatchResult {
    int index = -1;            // position of the best registered feature, -1 if the gallery is empty
    float score = kNoScore;    // its cosine similarity (-1 .. 1)
    float second_score = kNoScore;  // similarity of the second best (available for callers that want to check the margin; the pipeline does not use it)
};
```

**What it does.** The result for one query face. `index` is the position of the best registered feature in the list that was given to `set_gallery` (with the feature store this is the position in `FeatureStore::records()`). `score` is its similarity, `second_score` the similarity of the runner-up.

**How it works.** The default values mean "nothing found": `index = -1`, and `kNoScore` (`-2.0f`) for both scores, which no real similarity can equal (cosine similarity lies between -1 and 1, up to tiny float rounding). A query keeps these defaults if the gallery is empty or if the query's length differs from the gallery's. If the gallery holds exactly one feature, `second_score` stays at `kNoScore`, because there is no second best.

**Used by.** `FeatureMatcher::match` and `match_reference` (fill it), `AuthPipeline::match_results` (reads `index` and `score`; `FeatureStore::match_cards` does not use it, it works on the table of `score_matrix`), `tools/eval_footage.cpp` (`main`), `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp`. As the comment says, `second_score` is there for callers that want to check the margin between the best and the runner-up; the pipeline does not use it, and a search of the project finds no code that reads it.

### FeatureMatcher (class)

```cpp
class FeatureMatcher {
public:
    ...
private:
    int count_ = 0;          // number of registered features
    int dim_ = 0;            // true feature length
    int padded_dim_ = 0;     // length rounded up to a multiple of 16 (zeros added)
    float* table_ = nullptr;  // count_ x padded_dim_ numbers, 32-byte aligned
};
```

**What it does.** Holds a copy of the registered features in a form made for fast matching, and finds the best matches for a list of query features.

**How it works.** The four private members describe the table: `count_` rows, `dim_` real numbers per row, `padded_dim_` = `dim_` rounded up to a multiple of 16, and `table_`, a raw pointer to `count_ * padded_dim_` floats (zero-padded, 32-byte aligned; see the background section above). The class owns this memory and frees it itself, which is why copying is forbidden. `match` is `const` and only reads the table, so it does not change the matcher.

**Used by.** `FeatureStore` holds one as its member `matcher_` and hands it out through `FeatureStore::matcher()`.

### FeatureMatcher()

```cpp
FeatureMatcher() = default;
```

**What it does.** Creates an empty matcher: `size()` is 0 and any query gets "no match".

**How it works.** The members have default values (`count_ = 0`, `dim_ = 0`, `padded_dim_ = 0`, `table_ = nullptr`), so the compiler-generated constructor is enough.

**Used by.** Created implicitly as part of `FeatureStore` (member `matcher_`).

### FeatureMatcher(const FeatureMatcher&) and operator= (deleted)

```cpp
FeatureMatcher(const FeatureMatcher&) = delete;
FeatureMatcher& operator=(const FeatureMatcher&) = delete;
```

**What it does.** Makes it a compile error to copy a `FeatureMatcher`.

**How it works.** A copy would copy the pointer `table_`, and then two objects would both free the same memory when destroyed (a bug called a double free). Deleting the copy operations prevents that. As a consequence, a `FeatureStore`, which contains a matcher, cannot be copied either (the compiler removes its copy automatically).

**Used by.** Nothing calls them; they exist to stop mistakes.

### size

```cpp
int size() const { return count_; }
```

**What it does.** Returns the number of registered features currently in the gallery (0 if empty or if the last `set_gallery` failed).

**How it works.** A one-line getter on `count_`.

**Used by.** `tests/test_match.cpp` (`test_matching` checks that the gallery holds 10,000 features; `test_m_to_n_shapes` checks that it holds 8 after three more people were registered).

### dim

```cpp
int dim() const { return dim_; }
```

**What it does.** Returns the true feature length of the gallery (not the padded one), or 0 for an empty gallery.

**How it works.** A one-line getter on `dim_`.

**Used by.** `tests/test_match.cpp`.

## src/match/matcher.cpp

The implementation. It includes `<immintrin.h>` for the AVX2/FMA instructions and `<malloc.h>` for `_aligned_malloc` and `_aligned_free`, which are Windows (Microsoft C runtime) functions; the project is built with Visual Studio with `/arch:AVX2` (see `build.bat`).

| Function | Purpose |
|---|---|
| `update_best` | Keep the best and second-best score seen so far for one query. |
| `QueryBlock` (struct) | The M query features copied into one aligned, zero-padded block (shared by `match` and `score_matrix`). |
| `dot_avx2` | Fast dot product of two aligned, padded arrays. |
| `FeatureMatcher::~FeatureMatcher` | Free the table. |
| `FeatureMatcher::set_gallery` | Copy the registered features into the aligned, padded table. |
| `FeatureMatcher::match` | The M:N matching: one pass over the gallery for all M queries. |
| `FeatureMatcher::score_matrix` | The M:N matching against a SUBSET of the gallery (the features that RF cards stand for), returning all M x N_s similarities. |
| `FeatureMatcher::match_reference` | Plain version, one gallery pass per query (M separate 1:N searches), used to check `match`. |

### update_best

```cpp
inline void update_best(MatchResult& r, int index, float score)
```

**What it does.** Offers one new score to a query's `MatchResult`. After the call the result holds the highest score offered so far (with its index) and the second-highest score. It sits in an anonymous namespace, so it is private to this file.

**How it works.**
1. If `score` is strictly greater than the current best: the old best becomes the second best, `score` becomes the best and `index` is stored.
2. Otherwise, if `score` is strictly greater than the current second best: it becomes the new second best (the index of the second best is not kept).
3. Otherwise nothing changes.

Because the comparison is "strictly greater", a tie never replaces the best: the registered feature that comes first wins. Worked example, scores arriving for registered features 0, 1, 2, 3 = `0.30, 0.80, 0.55, 0.80`:

| After feature | best (index) | second best |
|---|---|---|
| start | -2.0 (-1) | -2.0 |
| 0: 0.30 | 0.30 (0) | -2.0 |
| 1: 0.80 | 0.80 (1) | 0.30 |
| 2: 0.55 | 0.80 (1) | 0.55 |
| 3: 0.80 | 0.80 (1) | 0.80 (a tie is not "greater" than the best, but it is greater than 0.55) |

Only the two highest scores are remembered, so each query needs just two numbers of memory however many registered features there are.

**Used by.** `FeatureMatcher::match` and `FeatureMatcher::match_reference` (`score_matrix` does not use it: it returns every similarity and keeps no best and second best).

### QueryBlock (struct)

```cpp
struct QueryBlock {
    float* table = nullptr;
    std::vector<char> usable;
    ~QueryBlock();
    const float* row(int q, int padded_dim) const;
    bool build(const std::vector<Feature>& queries, int dim, int padded_dim);
};
```

**What it does.** The M query features copied into one block of aligned, zero-padded memory, in the same layout as the registered table (`table_`), so that `dot_avx2` can read both. `usable[q]` is 0 for a query whose length is not the gallery's: that query is left out of the matching and keeps "no match". It sits in an anonymous namespace, so it is private to this file, and it is used by `match` and `score_matrix`.

**How it works.**
* `build(queries, dim, padded_dim)`: allocates `M * padded_dim` floats with 32-byte alignment (`_aligned_malloc`) and returns `false` if there is no memory (then nothing may be matched; `table` stays null). Otherwise the block is filled with zeros, `usable` gets one entry per query, and every query of length `dim` is copied to the start of its row and marked usable.
* `row(q, padded_dim)`: a pointer to the first number of the row of query `q`.
* The destructor frees the block (`_aligned_free`), so every way out of `match` and `score_matrix` releases it. Copying is forbidden (deleted), for the same reason as for `FeatureMatcher`: two objects would free the same memory.

**Used by.** `FeatureMatcher::match` and `FeatureMatcher::score_matrix`.

### dot_avx2

```cpp
float dot_avx2(const float* a, const float* b, int padded_dim);
```

**What it does.** Returns the dot product `a[0]*b[0] + a[1]*b[1] + ... + a[padded_dim-1]*b[padded_dim-1]`. Both arrays must hold `padded_dim` floats, `padded_dim` must be a multiple of 16, and both pointers must be 32-byte aligned. The function does not check any of that; breaking those rules can crash the program (the aligned load below faults on an unaligned address). The CPU must support AVX2 and FMA (`docs/TESTING.md` lists AVX2 as a requirement; the code has no run-time check). It is declared in `matcher.h` (outside the class) but is used only inside this file.

**How it works, step by step.** Background: an ordinary loop multiplies one pair of numbers at a time. AVX2 is a set of CPU instructions that work on a **register** holding 8 floats at once (256 bits); one instruction does the same operation on all 8 "lanes". `fmadd` ("fused multiply-add") does `acc = a * b + acc` in a single instruction.

1. Make two accumulator registers, `acc0` and `acc1`, each holding 8 zeros (`_mm256_setzero_ps`).
2. Loop with `d = 0, 16, 32, ...` up to `padded_dim` (that is why `padded_dim` must be a multiple of 16: each turn handles exactly 16 numbers):
   1. Load 8 floats from `a + d` and 8 from `b + d` (`_mm256_load_ps`, the "aligned" load that requires 32-byte aligned addresses), and do `acc0[i] += a[d+i] * b[d+i]` for all 8 lanes with one `_mm256_fmadd_ps`.
   2. Do the same for the next 8 numbers (`a + d + 8`, `b + d + 8`) into `acc1`.
3. Why two accumulators: each `fmadd` needs the previous result of the same accumulator, so one accumulator would make the CPU wait; with two independent chains it can work on both at once (this is the reason given in the code's comment).
4. After the loop add the two accumulators lane by lane (`_mm256_add_ps`). Now 8 partial sums are left, lane `k` holding the sum of every product whose position is `k` modulo 8.
5. **Horizontal sum** (adding the 8 lanes together): take the lower 4 lanes (`_mm256_castps256_ps128`) and the upper 4 lanes (`_mm256_extractf128_ps`) and add them, giving 4 numbers. `_mm_hadd_ps` adds neighbouring pairs; doing it twice leaves the total of all lanes in lane 0, and `_mm_cvtss_f32` returns lane 0 as an ordinary `float`.

A picture for `padded_dim = 16` (p0..p15 are the products `a[i]*b[i]`):

```
acc0 lanes:  p0  p1  p2  p3  p4  p5  p6  p7
acc1 lanes:  p8  p9  p10 p11 p12 p13 p14 p15
acc0+acc1:   p0+p8  p1+p9  p2+p10  p3+p11  p4+p12  p5+p13  p6+p14  p7+p15
low + high:  (p0+p8)+(p4+p12)   (p1+p9)+(p5+p13)   (p2+p10)+(p6+p14)   (p3+p11)+(p7+p15)
hadd, hadd:  sum of all 16 products   (in lane 0)
```

Because the additions happen in a different order than in a plain left-to-right loop, the result can differ from `match_reference` in the last digits (float rounding). `tests/test_match.cpp` accepts a difference up to `1e-4` in the score and requires the same best index.

**Used by.** `FeatureMatcher::match` and `FeatureMatcher::score_matrix`.

### FeatureMatcher::~FeatureMatcher

```cpp
FeatureMatcher::~FeatureMatcher()
```

**What it does.** Frees the table when the matcher is destroyed.

**How it works.** If `table_` is not null, `_aligned_free(table_)`. (Memory from `_aligned_malloc` must be freed with `_aligned_free`, not with `delete` or `free`.)

**Used by.** Runs automatically when a `FeatureMatcher` (for example inside a `FeatureStore`) goes out of scope.

### FeatureMatcher::set_gallery

```cpp
bool FeatureMatcher::set_gallery(const std::vector<const Feature*>& features);
```

**What it does.** Replaces the registered features. Input: a list of pointers to features (the features are only read during the call; they are copied). All features must have the same length. Returns `true` on success (also for an empty list, which gives an empty gallery) and `false` if the features are unusable. On `false` the matcher is left **empty** (the old gallery is gone). The pointers must not be null (the code does not check).

**How it works.**
1. Free the old table and reset `count_`, `dim_` and `padded_dim_` to 0.
2. Empty list: return `true`.
3. Take `dim` from the first feature. If it is 0 or less: return `false`. If any feature has a different length: return `false`.
4. Set `padded_dim_ = (dim + 15) / 16 * 16` (round up to a multiple of 16), `count_` = number of features.
5. Allocate `count_ * padded_dim_ * 4` bytes with 32-byte alignment (`_aligned_malloc`). If that fails: `count_` back to 0, return `false`. (For 10,000 features of 512 numbers this is 20,480,000 bytes.)
6. Fill the block with zeros (`memset`), then copy each feature's `dim` numbers to the start of its row. The padding stays zero.

Every row starts at `row * padded_dim_ * 4` bytes from the start of the block, a multiple of 64 bytes, so every row, and each group of 8 floats inside it, keeps the 32-byte alignment the aligned loads need.

The function does not normalize the features; it trusts that they have length 1.

**Used by.** `FeatureStore::matcher()` (`src/store/feature_store.cpp`), which calls it whenever the registered list changed. `FeatureStore::matcher()` ignores the returned value; a comment there explains that `set_gallery` can only fail for features of different lengths, which the store's `add()` and `load()` already refuse (see `store.md`).

### FeatureMatcher::match

```cpp
void FeatureMatcher::match(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const;
```

**What it does.** The M:N matching. Input: `queries`, M query features (the currently detected faces; M may be anything from 0 up), compared with the N features of the gallery (N may be 0 too). Output: `results` is resized to exactly one `MatchResult` per query (same order), each the best of the N registered features. A query gets a real answer only if its length equals the gallery's; a query of another length keeps the defaults (`index = -1`), and so do all queries when the gallery or the query list is empty. The gallery is read only once, for all M queries. M and N are independent: M may be larger than N, for example 16 faces against 5 registered people (`test_m_to_n_shapes`).

**How it works.**
1. `results` is filled with default `MatchResult`s (so every query starts as "no match").
2. If the gallery or the query list is empty: stop.
3. Copy all queries into one small `QueryBlock`, zero-padded and 32-byte aligned like the gallery (queries are few and small). A query whose `dim()` differs from `dim_` is skipped and marked unusable. If the allocation fails the function stops, leaving all results as "no match", without any message.
4. **One pass over the gallery.** The outer loop goes over the registered rows `i`; for each row `g` the inner loop goes over the usable queries `q`: `update_best(results[q], i, dot_avx2(g, block.row(q, padded_dim_), padded_dim_))`. The row `g` (2 KB for 512 numbers) stays in the L1 cache (the closest, fastest cache level) while every query is compared with it.
5. The `QueryBlock` frees its memory when the function returns.

The amount of arithmetic is `count_ * (number of queries) * padded_dim_` multiply-adds (N x M x the padded length), executed 8 at a time. The function runs on one thread (the code contains no parallel loop).

**Used by.** `AuthPipeline::match_results` (through `store_.matcher().match(...)`, once per frame for all faces that got a new feature, while no card check is active; during a card check `FeatureStore::match_cards` is called instead; `AuthPipeline::process` and `flush` reach it through `match_results`), `tools/eval_footage.cpp` (`main`, M faces of a frame against all N registered people), `tools/bench_match_part.h` (`bench_matching`, run by `bench` and `bench_match`), `tests/test_match.cpp`.

### FeatureMatcher::score_matrix

```cpp
void FeatureMatcher::score_matrix(const std::vector<Feature>& queries, const std::vector<int>& positions,
                                  std::vector<float>& scores) const;
```

**What it does.** The M:N matching where N is a **subset** of the registered features, with **all** similarities returned. `queries` holds the M faces. `positions` lists the positions (0 to N-1, as in the gallery given to `set_gallery`; with the feature store: the positions in `FeatureStore::records()`) of the N_s registered features that the faces are compared with, and with no others. `scores` is resized to `M * N_s` and `scores[q * N_s + k]` is the similarity of query `q` and the registered feature at `positions[k]`: one row per face, one column per listed feature. The typical use: the cards that were read stand for N_s registered features that were retrieved from the database, and the faces of the current frame are compared with just those (`FeatureStore::match_cards`). M and N_s are independent: fewer faces than positions, as many, or more. The rules:
* An entry that cannot be computed holds `kNoScore` (`-2.0f`): a query of the wrong length gives `kNoScore` for its whole row, and a position outside 0 to N-1 gives `kNoScore` for its whole column.
* Every entry starts as `kNoScore`: with M = 0, N_s = 0 or an empty gallery nothing is computed and `scores` has `M * N_s` entries of `kNoScore` (so it is empty when M or N_s is 0).
* A position listed twice gets two columns with the same values (the code does not remove duplicates).
* The function chooses nothing: it does not say which face is the best one for a card or which card is the best one for a face. The caller reads the table by rows, by columns or as a whole.

Only the listed rows of the registered table are read, so the time grows with N_s, not with N.

**How it works.**
1. `scores` is filled with `M * N_s` times `kNoScore`.
2. If the gallery is empty, the query list is empty or `positions` is empty: stop.
3. Copy the queries into a `QueryBlock` (as `match` does); if the allocation fails, stop (everything stays `kNoScore`).
4. For every position `k` (outer loop): skip it if `i = positions[k]` is `< 0` or `>= count_`; otherwise read the registered row `i` once and compare it with every usable query `q` (inner loop): `scores[q * N_s + k] = dot_avx2(row i of the table, row q of the block, padded_dim_)`. As in `match`, the registered row is read once and compared with all M faces while it is in the cache.

A small example, N = 100 registered features, M = 3 faces, `positions` = `{5, 40, 9}`: `scores` has 9 numbers; `scores[0..2]` are the similarities of face 0 to features 5, 40 and 9, `scores[3..5]` those of face 1 and `scores[6..8]` those of face 2. With `positions` = `{5, 500}` the second column (`scores[1]`, `scores[3]`, `scores[5]`) holds `kNoScore`, because 500 is not a position.

**Used by.** `FeatureStore::match_cards` and `tests/test_match.cpp` (`test_card_first`: 25 combinations of M and N_s, compared with a brute-force dot product).

### FeatureMatcher::match_reference

```cpp
void FeatureMatcher::match_reference(const std::vector<Feature>& queries, std::vector<MatchResult>& results) const;
```

**What it does.** Gives the same answers as `match`, but in the simplest possible way: for each query a whole pass over the gallery, with an ordinary loop and no AVX2. It does M separate 1:N searches, one gallery pass per query. It exists to prove that the fast code is correct and to be the baseline in speed comparisons.

**How it works.** For each query `q`: skip it if its length differs from `dim_` or the gallery is empty (it stays "no match"). Otherwise for every registered row `i` compute `s = sum over d < dim_ of g[d] * query[d]` in a plain loop over the true length (padding is not needed) and call `update_best`. The registered list is therefore read once per query.

**Used by.** `tests/test_match.cpp` (`test_matching` compares the result with `match` for 1, 4 and 16 simultaneous faces and times both, as "M separate 1:N searches, reference"; `test_m_to_n_shapes` compares them for 72 combinations of M and N). The comment in `matcher.h` names this test as its user, and a search of the project finds no other caller (`tools/bench_match_part.h` times only `match`).
