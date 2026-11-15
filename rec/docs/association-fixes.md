# Association and ID-switch corrections

This update fixes the three reproducible review findings: active fast-motion duplicate IDs, class-incompatible archived recovery, and an ID-switch counter based on distinct IDs rather than transitions.

## Updated association sequence

1. Existing motion/overlap matching handles ordinary active-track updates. It can tolerate category noise while the spatial track is continuous.
2. Before archive recovery or new ID allocation, unmatched active tracks receive a stricter appearance-supported recovery attempt. Recovery uses the same voted class, strong appearance, plausible size/displacement, one-to-one assignment and an ambiguity margin.
3. Unmatched detections are then considered against archived identities using the same class and ambiguity rules, with the archive's existing appearance threshold and time-expanded spatial gate.
4. New IDs are allocated only after these matching stages fail. Recovery resets motion at the actual detection and increments reacquisitions while preserving the existing ID.

The reproduced fast-motion case moves an 80-pixel-wide vehicle from x=20 to x=160 in 0.3 seconds. It now retains ID #1 with one active track. A bus with the same descriptor/box as an archived car receives a new ID; the archived car record is retained. These are deterministic regression checks, not proof of all real-camera identity associations.

Class compatibility is strict for appearance-supported recovery, not for every ordinary motion match: short-lived detector class noise does not automatically split a spatially continuous vehicle. The active fallback's >=0.97 similarity and best-versus-competitor margin avoid simply widening the motion gate for all objects. Ambiguous or weak evidence may still require a new track; arbitrary physical identity cannot be guaranteed for indistinguishable vehicles.

## Correct video metrics

The review retains unique_ids as a distinct-ID count. id_changes now counts transitions between successive positive IDs, using IdSwitchCounter. Missing detections leave the last positive ID intact and do not themselves increment the counter. Thus 1,2,1 produces unique_ids=2 and id_changes=2, while 1,0,1 produces zero changes. The CSV/frame evaluation and video now use this event counter instead of unique_ids minus one.

The default normal-speed, half-speed and real-traffic tracking videos are regenerated after the code changes. Current binary/source hashes, dynamic test counts and review results are recorded by the fresh validator in tests/verification.json.

## New function reference

### `car::identityRecoveryCost`

```cpp
static double identityRecoveryCost(const Track& track,const Detection& detection,double now,bool active)
```

Source: [src/Car.cpp](../src/Car.cpp). Returns a candidate recovery cost or 2.0 when disallowed. Requires the detection class to equal the voted track class. Cosine appearance similarity must be >=0.97 for an unmatched active track or >=0.93 for an archived track. Missing/mismatched/zero descriptors therefore cannot justify recovery. Active recovery uses the predicted box, an age-expanded distance gate min(6,1+8*age), and size ratio [0.5,2]; archived recovery uses the last observed box, min(8,2+0.5*age), and size ratio [0.4,2.5]. Distances are normalized by at least 30 pixels or the detection width/height, whichever is larger. Returns 1-similarity+0.01*distance. Inputs are read-only, now uses source-observation seconds, and no ID or filter is changed.

### `car::recoveryAssignment`

```cpp
static std::vector<int> recoveryAssignment(const std::vector<std::vector<double>>& costs)
```

Source: [src/Car.cpp](../src/Car.cpp). Accepts a rectangular candidate cost matrix; empty input returns empty. Copies the matrix and rejects candidate edges costing >=0.16 or lacking a 0.02 margin against every competing edge in the same row and column. Runs Hungarian assignment on the filtered matrix, returning one detection column per track row or -1. This provides one-to-one matching and declines near ties rather than arbitrarily selecting an old identity. Caller supplies consistently sized rows and treats rejected candidates as unmatched; no tracking state is mutated.

### `car::IdSwitchCounter::observe`

```cpp
void IdSwitchCounter::observe(int id)
```

Source: [src/TrackingReview.cpp](../src/TrackingReview.cpp). Updates previousId and switches for actual positive-ID transitions. The first valid ID establishes the baseline. Every subsequent different positive ID increments switches, including a return to an earlier ID: 1,2,1 yields 2. Repeated IDs and nonpositive missing/invalid observations do not count; the last positive ID remains remembered across gaps. No output or I/O. Shared by the review video and its regression tests.

### `car::runAssociationFixTests`

```cpp
static int runAssociationFixTests()
```

Source: [src/Tests.cpp](../src/Tests.cpp). Runs eight regressions: 140-pixel movement in 0.3 seconds retains one active ID; weak appearance cannot bypass motion gating; active appearance recovery rejects car/bus mismatch; a bus cannot inherit an archived car ID with identical appearance; near-tied active identities are not guessed; absent descriptors cannot justify recovery; 1,2,1 counts two switches; gaps/repetitions do not inflate switches. Returns the passed count or require throws. No camera is opened. The full suite is now 55 checks.

## Existing functions changed

Tracker::update now tracks which active rows have already matched, performs active recovery before archive recovery, and uses shared recoveryAssignment rules for both stages. It does not allocate a duplicate ID in the fast-motion regression. Class-incompatible recovery is rejected before Hungarian assignment. resetMotion, observe and plate/current-history gates keep their previous responsibilities.

trackingReview owns one IdSwitchCounter per evaluated fixture vehicle, calls observe with the current visible ID or zero, and exports its transition count to video panels and tracking_review.json. Existing unique-ID sets remain separate. runTests adds runAssociationFixTests to the previous 47 checks, yielding 55 checks in both compiler builds.

IdSwitchCounter is declared in include/Car.hpp; previousId defaults to zero and switches defaults to zero. All other public option/export schemas are unchanged by this correction. Hungarian matching continues to use the existing project-owned assign implementation; no additional dependencies are required.
