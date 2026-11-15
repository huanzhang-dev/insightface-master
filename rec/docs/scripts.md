# Scripts and command files

These files are top-level scripts, not named C++ functions. Paths are relative to the project root unless noted.

## `tools/setup.ps1`

Reads `models/manifest.json`, verifies each artifact with SHA-256, and skips matching files. Creates missing parent directories and downloads to `.download` files, verifies hashes, then moves valid artifacts into place. Uses TLS 1.2, a 300-second request timeout, and three attempts with two-second retry delays; final failures terminate the script. Extracts the OpenCV self-extracting SDK in a hidden process and waits for its exit. Uses `ZipFile.ExtractToDirectory` to extract ONNX Runtime, DirectML and native Tesseract NuGet archives when their target directories do not exist. Does not repair a partially extracted directory automatically. Requires network access for missing artifacts; no camera capture or training is started. Run with process-local execution policy bypass if machine policy blocks unsigned scripts.

## `tools/validate_outputs.ps1`

Builds both toolsets and runs both current self-tests, recording dynamic counts and hashes. Generates new test outputs under a unique output/verification_<UTC stamp> directory. Runs 15/30 FPS synthetic previews with explicit --allow-text-candidates and separately checks detector-only positive/no-inserted-label clips. Verifies source timestamps, current/history fields, evidence PNGs, Unicode exports and absent removed strings. Writes running/passed/failed status in tests/verification.json. Does not claim real-camera plate accuracy. See [evidence reference](evidence-and-tracking.md).

## `build_vs2015.cmd`

Initializes the installed VS2015 amd64 compiler, adds the Windows SDK tool directory, and invokes MSBuild Release/x64/v140. Writes `bin/VS2015` and separate intermediate files. Native OpenCV/ONNX Runtime/DirectML/Tesseract DLLs are copied by the project post-build event. Returns the build exit status; does not execute tests. Compiler/SDK paths must match the local installation.

## `build_vs2019.cmd`

Initializes installed VS2019 x64 compiler tools, exposes the Visual Studio Installer helper, and builds Release/x64/v142 into `bin/Release`. Same dependency copying and exit-status behavior as the VS2015 build.

## `run_camera.cmd`

Changes to the project directory, prompts for preview FPS (default 30) and OCR languages (default eng+chi_sim+kor+jpn), and runs camera 0 with DirectML. The user controls 15/30 FPS and language selection; the actual inference rate is measured separately. Pauses after termination. Requires a camera, models, GPU support and built executable.

## `run_test_video.cmd`

Enables --allow-text-candidates and its visible synthetic-text banner. Uses the photographic composite test video with artificial plates, prompts for FPS and OCR languages, and runs the DirectML preview. Does not establish accuracy on actual vehicle plates. Pauses after exit.

## `run_real_traffic.cmd`

Runs the public traffic clip with DirectML at a 30 FPS preview target, using the default multilingual model selection. Its source frame rate and actual inference rate may be lower. No ground-truth real-plate accuracy is available for that clip.

## Manifest and generated data

`models/manifest.json` records dependency sources, byte counts and hashes; setup verifies these records. `include/UnicodeNumbers.inc` is generated data, with no executable functions. Vendor DLL internal functions, uninvoked reference Python scripts, build-system internals, and compiler-generated special members are outside the first-party function reference.

Additional executable action: `Car.exe --generate-lighting testdata` creates reproducible simulated dark/uneven versions of the photographic composite. Both clips are labeled simulated, with exact degradation described in [lighting reference](lighting.md).

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.

`play_tracking_review.cmd` opens the generated half-speed AVI with the registered Windows video player. It does not start training or change the tracking outputs.
