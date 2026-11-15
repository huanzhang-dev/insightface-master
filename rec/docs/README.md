# Car project function documentation

This directory documents the functions used by the C++ vehicle/plate project in `Z:\Car`. It describes the current implementation, including limitations, rather than proposing different behavior.

- [Complete function index](function-index.md): all named implemented functions/methods and explicitly deleted operations.
- [Association corrections](association-fixes.md): fast-motion ID recovery, vehicle-class gates and accurate ID-switch counting.
- [Persistent tracking and review videos](persistent-tracking.md): appearance-supported identity recovery, tracking states, video paths and all new function descriptions.
- [Plate evidence and review fixes](evidence-and-tracking.md): current/history fields, exact OCR crops, observation-time tracking and fresh validation.
- [Lighting correction](lighting.md): adaptive exposure, night detection, plate normalization and lighting stress tests.
- [Unicode and multilingual OCR](unicode-and-multilingual.md): UTF-8 fields, Unicode number categories, escaping, and native Tesseract integration.
- [Text and tracking](text-and-tracking.md): field extraction, assignment, Kalman tracking, and plate voting.
- [Detection and OCR](detection-and-ocr.md): vehicle/plate localization, glyph classification, and CRNN decoding.
- [Worker and display](worker-and-display.md): asynchronous inference, frame buffering, and separate text overlays.
- [Inference backends](inference.md): OpenCV CPU/OpenCL and native DirectML session/resource handling.
- [Application](application.md): command-line parsing, capture, preview pacing, recording, and export.
- [Tests and video generation](test-and-video-functions.md): self-tests, printed glyphs, and synthetic/composite footage.
- [Data types and options](data-model.md): argument/result structures, defaults, coordinates, and ownership.
- [External APIs](external-apis.md): OpenCV, ONNX Runtime, Windows, C++ library functions, and callbacks directly used by the source.
- [Scripts and commands](scripts.md): dependency setup, output validation, building, and launching.

Each project function entry includes its signature, source location, purpose, inputs, outputs, state/ownership effects, and errors or preconditions. Header-only accessors, private helpers, constructors, destructors, and test helpers are included. Deleted functions are identified as unavailable operations, not callable implementations.

The external reference covers APIs called directly by first-party source and groups overloaded constructors/container methods. It does not reproduce all internal functions of OpenCV/ONNX Runtime or uninvoked downloaded reference scripts. Vendor headers, licenses, and reference Python files in `third_party` remain third-party material.

## Reading the pipeline

`main` dispatches to `run`, `generateVideos`, or `runTests`. The normal frame loop predicts tracks, obtains new detections from `Pipeline::detect` directly or through `Worker`, updates track identities/consensus, draws separate Characters and Numbers fields, and exports results. Each model runs through `Network::forward`.

Coordinates are image pixels, timestamps are seconds, and measured inference durations are seconds internally or milliseconds in the performance report. Preview frame rate is distinct from AI inference rate. In headless mode the reported display rate is the preview scheduler rate, not a physical monitor measurement.

## Maintenance

When changing a function, update its module entry and the index. Update this reference when adding/removing functions or changing thresholds, model shapes, ownership, CLI options, or emitted files. Source line numbers are navigation aids for this snapshot and can shift after edits. These documentation files do not change application behavior or start camera capture.

The Unicode update is authoritative over older Latin-backend details: `Pipeline` defaults to configurable multilingual OCR, `letters` is a compatibility alias for `characters`, and exports use the `characters` CSV column. See the Unicode page for exact schemas and limitations.

The evidence-and-tracking reference supersedes earlier implicit claims that repeated OCR text is a verified physical plate. Current validation is tests/verification.json; older reports remain historical.
