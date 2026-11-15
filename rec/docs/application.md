# Application

> Current Unicode update: `--ocr-languages` selects models (default eng+chi_sim+kor+jpn, or latin for the legacy backend). Exports now use quoted UTF-8 fields and complete code points. The CSV header is characters; JSON has character_text plus a characters symbol array. See [Unicode reference](unicode-and-multilingual.md).

[Documentation index](README.md)

Entry point and the capture-to-export frame loop.

## `car::run`

```cpp
int run(const Options& options)
```

**Source:** [src/main.cpp](../src/main.cpp).

Execute camera/video capture, inference, tracking, preview, export, and performance reporting.

**Inputs:** Validated `Options`. A single source character `0`?`9` selects a DirectShow camera; other source strings go to file/stream capture.

**Returns / output:** 0 if at least one frame was processed; 2 if no frames were processed. Failures usually throw and are caught by `main`.

**Behavior, ownership, and failure:** Sets OpenCV threads; tries a nonrecursive output-directory creation; opens capture; requests one-frame buffering and configured camera FPS. Valid source FPS is 1?240, otherwise falls back to 30. Uses wall-clock timestamps for cameras and source-frame timestamps for files. Offline mode performs synchronous selected-frame inference; live-style mode submits to `Worker`, discards results older than `maxAge`, and updates the authoritative tracker at result.timestamp and projects a read-only track view for each captured frame. Writes `tracks.csv` and per-character `recognition.jsonl`, snapshots, optional MJPEG recording, and `performance.json`. Displays separate fields after temporal stability and accepts Esc or lowercase q. Preview scheduling follows configured FPS; files are paced at native source FPS in nonoffline mode. Live-style recording follows presented frames and min(source FPS, target FPS); offline recording preserves all frames. Open/output/model/recording errors throw. Initial model setup and final worker/recording teardown are outside the reported loop duration. In headless mode `display_fps` measures scheduled preview opportunities, not physical monitor refresh. [Options](data-model.md) lists fields.

## `main`

```cpp
int main(int argc, char** argv)
```

**Source:** [src/main.cpp](../src/main.cpp).

Resolve default project paths, parse CLI options, and dispatch application actions.

**Inputs:** Process argument count and argument strings.

**Returns / output:** 0 for help/successful actions; normal run can return 2 for an empty source; caught exceptions print `ERROR:` and return 1.

**Behavior, ownership, and failure:** Derives the project root two directories above the executable, matching `bin/Release` and `bin/VS2015`; supports path overrides. Parses capture, model/output, recording, backend, FPS, threading, numeric-only, generation, and testing options. Validates configured basic numeric ranges, sets OpenCV threads, and calls `run`, `generateVideos`, or `runTests`. Numeric conversion errors and unknown/missing arguments fail. GPU-device indices are passed through; adapter/session validation occurs later. Does not launch reinforcement-learning code or write square-packing artifacts.

`--lighting auto|off` controls correction (default auto); `--generate-lighting directory` generates the two simulated lighting clips. performance.json now includes lighting_correction.

Current behavior: [plate evidence and tracking fixes](evidence-and-tracking.md) define timestamp-aware updates, current/history labeling, safe overlays, evidence crops, test-only heuristics and 55-check validation.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.

Current association fixes: [active recovery, class gates, switch counting and new function descriptions](association-fixes.md).
