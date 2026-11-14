# Visual Studio builds, authentication dashboard, and engine inspection

`face_authentication.sln` contains only the **face_dashboard** Windows application.
Its build target builds and links the **face_authentication** engine library without
adding another project to the solution. Select `face_dashboard` as the
startup project and press F5. The detection, extraction and matching solutions
retain their library-only layout. See [Dashboard usage](DASHBOARD.md).

The five current solutions select **v142** and a Windows 10 SDK in Visual Studio
2019, or **v140** and the Windows 8.1 SDK in Visual Studio 2015. Both use C++14. Install **Desktop development with C++**, including the
v142 x64 tools and Windows 10 SDK. The checked-in third-party headers, libraries,
DLLs and models must remain available under `third_party/` and `models/`.

Open any engine solution and build Debug or Release, x64. Alternatively run:

```bat
scripts\build_vs2019.bat Release
scripts\build_vs2019.bat Debug
inspect_engines.bat
inspect_engines.bat -Configuration Debug
```

The build script locates VS2019 Community, Professional, Enterprise or Build Tools
through vswhere, builds all four engine libraries and the `_test` runner, and stops
on build errors. Outputs are isolated in `build/vs2019/<Configuration>/`; intermediate
objects are isolated by project and configuration. Debug retains the release CRT
because the supplied inference libraries use it.

`inspect_engines.bat` opens a Windows Forms inspection window using Windows
PowerShell 5.1. Select an engine and profile, then **Inspect selected**, or use
**Inspect all**. Each tab reports the original test output and PASS/FAIL based on
its exit code. Checks run sequentially in child processes while the window stays
responsive; output is shown when each check finishes. Closing the window stops
the active check. The tests may write their usual test databases and artifacts
under `build/`; use a development copy of the repository.

| Inspector engine | Original check |
|---|---|
| `face_detect` (library `face_detection`) | `test_detect_units` |
| `feature_extraction` | `test_feature <profile>` |
| `feature_matching` | `test_match` |
| `face_authentication` | `test_pipeline <profile>` |

Detection inspection covers deterministic tracker/geometry/pose checks; use the
runner's `detect_image` or `eval_detect` commands for model-based image inspection.
Feature and authentication tests require the portrait test data in `data/portraits`;
the profile selector applies to these two checks. Authentication tests use generated
video, so passing them does not establish accuracy on a real inspection camera.
`inspect_engines.bat -Check` checks runner availability and lists its commands
without opening a window. The existing `build.bat` standalone programs remain available.

Regenerate projects using `python scripts/make_vs_projects.py`. Generation preserves
the VS2019 toolset and output paths. F5 from the detection, extraction or matching project starts its corresponding
check through `_test.exe`; `_test.sln` supports the complete command list.

Validated on this workspace with VS2019 on 2026-11-13: all five solutions build
in Release and Debug x64. Release `test_common`, `test_detect_units`, `test_feature`,
`test_match`, and `test_pipeline` completed with zero failures using the default
permissive profile. Debug detection and extraction checks also passed. A Windows
Forms smoke test opened the inspector, ran detection inspection, and confirmed its
PASS result. Full logs are in `build/vs2019_*.log` (local build artifacts).

If MSB8020 reports missing v142 tools in VS2015, reload the updated projects:
they now select v140 in that IDE. VS2019 still selects v142. VS2015 output goes to
`build/vs2015`; VS2019 output goes to `build/vs2019`, so their objects never mix.
An explicit `/p:PlatformToolset=...` override remains supported. The inspection
window uses the VS2019 runner; use `scripts/build_vs2019.bat` before launching it.

The older VS2015 notes below describe historical layouts and measurements.

---

# Visual Studio 2015: current solution layout

Every solution contains exactly one C++ project, with Debug/Release x64 configurations and toolset v140:

| Solution | Only project | Output |
|---|---|---|
| `face_detection.sln` | `face_detection` | `face_detection.lib` |
| `feature_extraction.sln` | `feature_extraction` | `feature_extraction.lib` |
| `feature_matching.sln` | `feature_matching` | `feature_matching.lib` |
| `face_authentication.sln` | `face_authentication` | `face_authentication.lib` |
| `_test.sln` | `_test` | `_test.exe` |

Outputs are under `build\vs2015\Debug` or `build\vs2015\Release`. Each engine project compiles
its required sources directly, including its common dependencies. The public headers and engine
implementations are unchanged. The matching engine still needs no neural-network runtime.

Open `_test.sln` to run or debug. Its only project links `face_authentication.lib`, building it
through an MSBuild target without adding an engine project to the solution. It also compiles the
original test/tool translation units, renaming only their entry points using per-file compiler
settings. Each invocation selects one original program, with the same arguments and exit status:

```
build\vs2015\Release\_test.exe --list
build\vs2015\Release\_test.exe test_detect_units
build\vs2015\Release\_test.exe test_feature models\permissive.ini
build\vs2015\Release\_test.exe test_match
build\vs2015\Release\_test.exe test_pipeline models\permissive.ini --async
build\vs2015\Release\_test.exe check_video models\permissive.ini data\videos\fhd_3people\video.mp4
```

With no command arguments, the runner executes `test_detect_units`, so F5 in `_test.sln` has a
small default test. To debug another engine, set `_test` Project Properties > Debugging > Command
Arguments to its test name and arguments. Breakpoints can be set in the original engine sources.
The four engine projects produce libraries and have no executable entry point; debug them through
`_test.sln`. `build.bat` and `run_tests.bat` continue to provide the original standalone executables
under `build\bin`; those command-line interfaces have not changed. `scripts\run_tests_vs2015.bat`
builds all five solutions and uses the runner for its tests, tools and evaluations.

Regenerate the projects with `python scripts\make_vs_projects.py`. Old standalone project files
remain in `msvc` for compatibility but are not part of the five solutions.

## VS2015 F5 diagnosis on this PC

The Windows Web Services trace captured socket error 10013 (`0x8007271D`) connecting to
`net.tcp://[::1]:<port>/Microsoft/VisualStudio/msvsmon`. Astrill's unconditional IPv6 connect
block also blocks loopback. IPv4 loopback works. The engine binaries themselves launch normally.
`scripts/repair_vs2015_debugger.cpp` contains a reversible, narrow repair: TCP ::1/128 permits
for the VS2015 IDE and x64 debugger helper, restricted to the specified Windows account and
Astrill's own filtering sublayer. External IPv6 and other security sublayers remain unchanged.
Installation and F5 verification are pending explicit firewall-change approval.

## Earlier layout and historical measurements

The notes below describe the earlier multi-project layout and earlier validation runs; use the
current layout and runner commands above for today's solutions.
# Visual Studio 2015 solutions

The project comes as **four separate solutions** for **Visual Studio 2015** (toolset `v140`, platform
**x64**, configurations **Debug** and **Release**): one for each engine and one for the integrated
authentication project. The command-line build (`build.bat`, used by `run_tests.bat`) stays as it is, uses
Visual Studio 2019 and builds every program; all of them build the same source files.

| Solution | Project | Programs | Needs a neural network library |
|---|---|---|---|
| `face_detection.sln` | high-speed face detection: detectors (YuNet, SCRFD, Haar), tracker, head pose | `test_detect_units`, `eval_detect`, `detect_image`, `calibrate_pose`, `roll_probe`, `bench_detect` | yes (ncnn, ONNX Runtime) |
| `feature_extraction.sln` | high-speed feature extraction: SFace, ArcFace, LBP | `test_feature`, `eval_features`, `calibrate_lbp`, `bench_extract` | yes |
| `feature_matching.sln` | high-speed feature matching: the M:N matcher, the card-first comparison and the database (store, CSV or SQLite) | `test_match`, `gen_features`, `bench_match`, `db_convert` | **no** |
| `face_authentication.sln` | the face authentication project: pipeline, worker thread, camera and video files | `check_video`, `eval_footage`, `test_pipeline`, `test_common`, `gen_video`, `camera_check`, `bench` | yes |

What the programs do is described in `docs/TESTING.md` and `docs/tools.md`. A solution contains the libraries of
another engine only where its programs need them: the extraction programs need a detector to find the face
landmarks that the alignment uses, so `feature_extraction.sln` also holds `fa_detect`; the authentication
solution uses all three engines. **`feature_matching.sln` needs neither ncnn nor ONNX Runtime**: it needs only the
SQLite source in `third_party\sqlite` (the library `fa_sqlite`, below). (Before the SQLite library was added it was checked
that it builds and runs with `third_party\` removed; that is no longer true, because the database format needs SQLite.)

## What is in it

All four solutions share the same project files in `msvc/`:

| Library project | Sources | Needs |
|---|---|---|
| `fa_core` | `src/common`: image operations, picture files (WIC), model profiles, text drawing | |
| `fa_inference` | `src/common/inference.cpp`: the ncnn and ONNX Runtime wrapper; also copies the two ONNX Runtime DLLs next to the programs | `fa_core` |
| `fa_detect` | `src/detect`: the face detection engine | `fa_core`, `fa_inference` |
| `fa_feature` | `src/feature`: the feature extraction engine | `fa_core`, `fa_inference` |
| `fa_sqlite` | `third_party/sqlite/sqlite3.c` (the SQLite amalgamation, 3.54.1, public domain): the SQLite database format. The only C source of the project: compiled **as C** (`CompileAsC`), without warnings and OpenMP, with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_THREADSAFE=1`, `SQLITE_DEFAULT_MEMSTATUS=0` and `SQLITE_OMIT_SHARED_CACHE` (the options `build.bat` uses) | |
| `fa_match` | `src/match` and `src/store`: the M:N matcher, the card-first comparison and the database (store: CSV and SQLite, `sqlite_db.cpp`) | `fa_core`, `fa_sqlite` |
| `fa_scene` | `src/testdata/scene.cpp`: generated test pictures (people in a frame), including the simulated camera look (`CameraLook`: the room, the blurs, the light and the noise; all of it is in this one file, so the library list did not change). `test_common` links it too now, because it renders camera-look pictures for its 13 checks | `fa_core` |
| `fa_synthetic` | `src/testdata/synthetic.cpp`: generated test features | `fa_match` |
| `fa_camera` | `src/camera`: camera, video-file and picture-folder sources, MP4 writer | `fa_core` |
| `fa_pipeline` | `src/pipeline`: the authentication pipeline | `fa_detect`, `fa_feature`, `fa_match` |

| File | Purpose |
|---|---|
| `msvc/<program>.vcxproj` | one console program for every file in `tools/` and `tests/`, linked with the libraries it needs |
| `msvc/face_auth.props` | compiler settings shared by every project: include folders, `/arch:AVX2`, `/openmp`, `/MD`, `NOMINMAX`, ... |
| `msvc/face_auth_app.props` | linker settings of the programs that use a network library: ncnn, ONNX Runtime and the Windows libraries |
| `msvc/face_auth_app_basic.props` | linker settings of the programs that need none (the matching programs, including `db_convert`, and `camera_check`): only the Windows libraries |

**`psapi.lib`.** `tests/cpu_only_check.h` (the proof that no GPU library is loaded; included by `test_common`, `test_feature` and `test_pipeline`) uses `EnumProcessModules` from the Windows library `psapi.lib` and asks the linker for it itself with `#pragma comment(lib, "psapi.lib")`. It is therefore **not** listed in the `.props` files, in `SYSTEM_LIBS` of `make_vs_projects.py` or in `build.bat`; nothing has to be added to a project for it. (Like `gdi32.lib` and `user32.lib` for `draw_text.cpp`, which are asked for the same way and are also listed.)
| `scripts/make_vs_projects.py` | writes all of the above and the four `.sln` files (ten libraries; the library list is `LIBS`, a library whose files are not in `src/`, like `fa_sqlite`, is described in `THIRD_PARTY`) |

Output goes to `build\vs2015\<Configuration>\` (programs, the libraries, the ONNX Runtime DLLs) and
`build\vs2015\obj\`, so it never mixes with the output of `build.bat`; the four solutions share it. **Debugging
starts in the project root**, because the programs read `models\`, `data\` and write `build\` relative to it.

**Debug** means no optimization, full debug information and asserts on, but it still uses the *release* C++
runtime (`/MD`): the ncnn and ONNX Runtime libraries are release builds, and mixing them with the debug
runtime (`/MDd`) does not link.

**Which libraries a program links is not written by hand.** `make_vs_projects.py` reads the program's `#include`
lines (also those of its own headers in `tools/`, such as `bench_detect_part.h`), maps every project header to its
library and adds what that library needs. After adding a program (`tools/x.cpp` or `tests/x.cpp`), list it in
`SOLUTIONS` at the top of the script and run `python scripts\make_vs_projects.py`; the script refuses to run when a
program is in no solution or in two. The project GUIDs come from the names, so a second run changes nothing.

## Building

* In Visual Studio 2015: open one of the four `.sln` files, choose `Release` and `x64`, Build Solution.
* From a command prompt, with the MSBuild of Visual Studio 2015:
  `"C:\Program Files (x86)\MSBuild\14.0\Bin\MSBuild.exe" face_detection.sln /m /p:Configuration=Release /p:Platform=x64`
  (same for the other three).
* With the IDE's own command line: `"C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE\devenv.com" face_detection.sln /Build "Release|x64"`.

What you need: **Visual Studio 2015 Update 3** with the Visual C++ tools, and the Windows SDK (the projects
leave the SDK version at the default, 8.1; Media Foundation, WIC and GDI are in it) with the Universal CRT. The
libraries are in `third_party\`: ncnn (a VS2015 x64 build, see `docs/MODELS.md`), ONNX Runtime 1.17.3 (a C
interface, so the Visual Studio version does not matter for linking) and SQLite (its source, `third_party\sqlite`, which
the project `fa_sqlite` compiles). The models are not needed to build, only to run.

Running the checks of each solution (from the project root, like `run_tests.bat` does; each prints
`ALL TESTS PASSED` or `TESTS FAILED`):

```
build\vs2015\Release\test_detect_units.exe                              (face_detection)
build\vs2015\Release\test_feature.exe models\permissive.ini             (feature_extraction; also dev_noncommercial.ini, classical.ini)
build\vs2015\Release\test_match.exe                                     (feature_matching)
build\vs2015\Release\test_common.exe                                    (face_authentication)
build\vs2015\Release\test_pipeline.exe models\permissive.ini            (also --async, and models\dev_noncommercial.ini)
```

## What was checked, and what was not

**Checked with the real Visual Studio 2015 only.** Visual Studio Community 2015 with Update 3 (`devenv.exe` 14.0.25420.1,
compiler `cl.exe` 19.00.24210, toolset `v140`) is installed on the development PC next to Visual Studio 2019.
`scripts\run_tests_vs2015.bat` builds and runs **everything with Visual Studio 2015 and nothing of Visual Studio 2019**
(not `build.bat`, not `run_tests.bat`). **The results below were obtained with the version before the card-first matching, the
SQLite database format and the 1920x1080 evaluation** (an earlier full run, repeated once after the M:N, managed-database and
RF-card changes, ending `ALL DONE (Visual Studio 2015, Release)`). Since then the solutions have grown: there are **ten
libraries** (the new one is `fa_sqlite`), the solutions hold 10, 9, 8 and 17 projects (`face_detection`, `feature_extraction`,
`feature_matching`, `face_authentication`: libraries and programs together), and the new programs are `db_convert` and
`eval_footage`. The SQLite amalgamation compiles with the real Visual Studio 2015 C compiler.

The Visual Studio 2015 run on this version is described just below.

**The Visual Studio 2015 run of the final version** (card-first matching, SQLite, the 1920x1080 evaluation, the simulated camera look `fhd_cam_*`, the CPU-only checks, the explicit CPU-only Media Foundation settings): `scripts\run_tests_vs2015.bat`, Release|x64, from an empty output folder, with the real Visual Studio 2015 and nothing of Visual Studio 2019.

1. **Build:** the four solutions were rebuilt with `devenv.com /Rebuild`: 10, 9, 8 and 17 projects succeeded, 0 failed; 21 programs. (`fa_scene` now also holds the camera look; `test_common`, `test_feature` and `test_pipeline` link `psapi.lib` through `tests\cpu_only_check.h`.)
2. **Tests:** 11 test runs printed `ALL TESTS PASSED` (`test_detect_units`; `test_feature` with the four profiles, each with the new CPU checks; `test_match` with the CSV, SQLite and card-first checks; `test_common` with the camera look and the CPU checks; `test_pipeline` with the permissive profile, the dev profile, the SQLite database and the worker thread with the permissive profile). The script then **stopped at `test_pipeline --async` with the dev profile**: the worker-thread timing checks failed (99% of the frames under 40 ms: p99 43.5 ms; no frame of 60 ms or more: slowest frame 157 ms). That is the intermittent timing noise of this PC and not a code failure: the same program rerun twice on the quiet machine passed once (p99 36.4 ms, slowest frame 45.4 ms) and failed once (p99 33.3 ms, one slow frame of 91.9 ms); with the Visual Studio 2019 programs, and with the last commit from before this work, the same check passed or failed depending on the moment. The limits were not loosened.
3. **The rest of the script**, run from a copy of it after that step: the six 1920x1080 videos were made, `eval_footage` ran for all four profiles on all six videos (24 runs), the 8 `check_video` runs (the three clean and three camera-look videos with the permissive profile, the worker thread on one of each, and the 1280x720 video) all printed `VIDEO CHECK PASSED (0 failed checks)`, `camera_check` and `bench` ran, and the script's last line `ALL DONE (Visual Studio 2015, Release)` was reached. `all engines run on the CPU: yes` was printed 32 times and `NO` never. The accuracy of the Visual Studio 2015 programs equals that of the Visual Studio 2019 build (for example permissive on `fhd_cam_3people`: 333 of 333 faces, 56 registered faces all first, 0 of 26 strangers accepted).
4. **Not run again with Visual Studio 2015:** the Debug configuration (the earlier Debug run is described below).

**The Visual Studio 2015 run on the SCRFD fitted rectangle (2026-11-12, `build\run_vs2015_final4.log`).** The SCRFD detector of `models\dev_noncommercial.ini` now runs the network on the smallest multiple-of-32 rectangle (640x384 for 1920x1080) instead of a 640x640 square (`letterbox_fit` in `src\common\image_ops.cpp`, the profile key `detector.layout` in `src\common\profile.cpp`, `DeepDetector::decode_scrfd_chw` in `src\detect\detector_deep.cpp`, the model files `det_dyn.param` / `det_dyn.bin`, `docs/RESULTS.md` section 5e). No source file was added and the project files in `msvc\` were not touched by this change (their last change is older than the change), so the solutions list the same files as before. The profile `models\dev_noncommercial_square.ini` (the old square) was added to the `eval_detect`, `bench_detect` and `eval_footage` lines of `scripts\run_tests_vs2015.bat`, so those now run five profiles instead of four. **The full script was run with Visual Studio 2015 only on this change and went through in one go (nothing stopped it):** (1) the four solutions were rebuilt with `devenv.com /Rebuild`: 10, 9, 8 and 17 projects succeeded, 0 failed; (2) 12 test runs printed `ALL TESTS PASSED`; (3) the 8 video checks printed `VIDEO CHECK PASSED`; (4) the five-profile lists (with the square variant) ran in `eval_detect`, `bench_detect` and the `eval_footage` loop; (5) the dev worker-thread timing check passed (`process()` median 11.7 ms, p99 21.4 ms, slowest frame 41.9 ms; limits 40 ms and 60 ms); (6) the script's last line was `ALL DONE (Visual Studio 2015, Release)`. It was the Release flow; the Debug configuration was not part of it. `scripts\check_vs2015_compat.bat` was not run on this change; the real Visual Studio 2015 compiler built it.

**After that run five checks were added to `test_common` and the comparison block of `test_pipeline` was rewritten (small changes to two test files only).** They were checked with **incremental** Visual Studio 2015 builds of the four solutions (8, 5, 5 and 8 projects built, 0 failed), `test_common` (74 checks, all ok) and `test_pipeline` with the dev profile, synchronous (passed), with the Visual Studio 2015 programs. **The full Visual Studio 2015 script was not repeated after those small changes.** (The same changes were checked with Visual Studio 2019: a full rebuild, `test_common` 74 ok, `test_pipeline` dev synchronous 70 ok; the Visual Studio 2019 full flow `run_tests.bat` of `build\run_tests_final5.log` was also run before them and not repeated, `docs/TESTING.md`.)

The list that follows describes the earlier version:

1. **Build:** each of the four solutions was rebuilt from an empty output folder with the Visual Studio 2015 IDE's own
   build command (`devenv.com /Rebuild "Release|x64"`): 10, 9, 6 and 15 projects succeeded, 0 failed (the earlier version; the
   solutions now hold 10, 9, 8 and 17 projects). The same in Debug.
   (`msbuild.exe` of Visual Studio 2015 gives the same result.)
2. **Proof of the toolchain:** the PE header of all 19 programs carries linker version 14.0 (Visual Studio 2015);
   the same program built by `build.bat` carries 14.29 (Visual Studio 2019).
3. **Run, Release:** every program of every project was run from `build\vs2015\Release`: all tests print
   `ALL TESTS PASSED` (`test_detect_units`; `test_feature` with the permissive, dev (ncnn), dev (ONNX Runtime) and classical
   profiles; `test_match`; `test_common`; `test_pipeline` with both profiles, synchronous and with the worker thread);
   `check_video` passes on the generated MP4 (both registered people recognized after 1.73 s and 0.80 s); and the
   evaluations, calibrations, generators and benchmarks run to the end (`eval_detect`, `eval_features`, `calibrate_pose`,
   `calibrate_lbp`, `roll_probe`, `detect_image`, `gen_features`, `gen_video`, `camera_check`, `bench_*`, `bench`).
   The worker-thread timing limits hold (p99 10.2 ms and 24.2 ms, slowest frame 13.4 ms and 27.9 ms).
4. **Same numbers as the Visual Studio 2019 build:** the accuracy results of the Visual Studio 2015 programs are identical
   to those of the 2019 build (for example the SFace genuine scores 0.844 mean / 0.654 minimum, the highest stranger score
   0.498, ArcFace 0.809 / 0.511 / 0.327, 106 of 185 faces through the pose gate).
5. **Run, Debug:** the Debug build of all four solutions passes the functional tests (`test_detect_units`, `test_feature`,
   `test_match`, `test_common`, `test_pipeline`); timing tests and benchmarks are not run unoptimized.
6. **Run by the IDE itself:** the IDE opened `feature_matching.sln` correctly (solution folders `libraries`, `tests`, `tools` and
   all 6 projects show up) and **Debug > Start Without Debugging (Ctrl+F5)** started the startup project: `bench_match.exe` was
   seen running with the parent chain `cmd.exe` <- `devenv.exe` (the IDE opens a console window for it).

Two problems that the real compiler found and that Visual Studio 2019 in C++14 mode had hidden were fixed earlier: the C++
wrapper header of ONNX Runtime (`onnxruntime_cxx_api.h`, "relaxed constexpr", error C3249; `OnnxNet` now uses the C
interface) and a missing `#include <algorithm>` for `std::max`.

**Known problem on the development PC: the debugger (F5) cannot start a program.** The IDE shows "Unable to start program
`...\bench_match.exe`. Unrecognized error occurred in the Windows Web Services framework." Building, Ctrl+F5 and every
command-line run work; only debugging fails. It is a problem of the old VS2015 debugger on this Windows 11 PC (it talks to its
helper `msvsmon.exe` over a local web connection), not of the projects: the same error appears for any program. No proxy,
host-name or local-port problem was found. Possibly related (not verified): the VS2015 web installer also failed because of the old TLS it
uses (see below); changing system security settings to find out was not tried.

Workaround: run `scripts\debug_vs2015_binary.bat test_detect_units Debug`. This builds the selected program with
VS2015 MSBuild and toolset `v140`, then opens the resulting executable in the installed VS2019 debugger and steps into it.
It does not retarget the solutions. The program defaults to `test_detect_units` and the configuration defaults to `Debug`;
`Release` is also accepted. On 2026-11-13, Windows confirmed that the VS2019 debugger attached to
`build\vs2015\Debug\test_detect_units.exe`, while VS2015 F5 reproduced the Web Services error.
This is a workaround for the IDE debugger; the underlying VS2015 F5 failure remains unresolved. To run without debugging,
use Ctrl+F5 in VS2015 or run the executable from the project root.

**Startup project:** the IDE picks the alphabetically first program (for example `bench_match` in `feature_matching.sln`, not
`test_match`); the order in the `.sln` file does not matter. Choose another with right-click on a project > Set as StartUp Project
(the IDE remembers it in its own `.vs` folder, which git ignores).

**Not checked:** debugging inside the IDE (see above), and the `devenv.com` command line prints an `AccessViolationException`
after a finished build (from inside the IDE's own task code; the build log is complete, so `run_tests_vs2015.bat` judges a build
by its log line `Build: N succeeded, 0 failed`).

**Result of the Visual Studio 2015 run on this version** (card-first matching, SQLite, 1920x1080 evaluation), `scripts\run_tests_vs2015.bat` from an empty output folder, Release|x64, with the real Visual Studio 2015 and nothing of Visual Studio 2019; it ended `ALL DONE (Visual Studio 2015, Release)`:

1. **Build:** the four solutions were rebuilt from scratch with `devenv.com /Rebuild`: `face_detection.sln` 10 projects, `feature_extraction.sln` 9, `feature_matching.sln` 8 and `face_authentication.sln` 17 projects succeeded, 0 failed (ten libraries, including `fa_sqlite`, the SQLite C file compiled by the Visual Studio 2015 C compiler; 21 programs).
2. **Run:** 12 test runs printed `ALL TESTS PASSED` (`test_detect_units`, `test_feature` with four profiles, `test_match` with the CSV and SQLite database checks and the card-first checks, `test_common`, `test_pipeline` with the permissive profile, the dev profile, the registered people in a SQLite database, and with the worker thread for both profiles), and the 5 video checks of the 1920x1080 footage and the 1280x720 video printed `VIDEO CHECK PASSED (0 failed checks)`; 0 lines reported a failure.
3. **Engines on the footage:** `eval_footage` ran for all four profiles on all three 1920x1080 videos; the Visual Studio 2015 programs gave the same accuracy as the Visual Studio 2019 programs (for example the permissive profile on `fhd_3people`: 333 of 333 faces found, 1 track ID per person, 60 genuine faces all found first, 0 of 25 strangers accepted).
4. **Worker-thread timing (dev profile) in this run:** p99 22.3 ms, slowest frame 28.4 ms (limits 40 ms and 60 ms): passed. (On the same PC the same check failed during an earlier, noisy period, with the Visual Studio 2019 programs and also with the programs of the commit before this work, so that failure was the PC and not the code; it passed again with both toolchains when the PC was quiet.)
5. **After that run** three small changes were made (an empty `.db` file is opened as an empty SQLite database, with its test; one comment of `auth_pipeline.h` and two header comments of the tests). `feature_matching.sln` and `face_authentication.sln` were rebuilt incrementally with Visual Studio 2015 (5 projects each succeeded, 0 failed) and `test_match` was run from that build: 196 checks, all passed. The rest of the programs were not run again after those changes.

## Running and testing with Visual Studio 2015 only

```
cmd /c "start /wait /b /affinity FFF scripts\run_tests_vs2015.bat"          Release: rebuild, then run everything (about 15 minutes, as the script's header says)
cmd /c "start /wait /b /affinity FFF scripts\run_tests_vs2015.bat debug"    Debug: rebuild, then the functional tests
```

(`/affinity FFF` pins the run to the fast cores of the development PC so that the timing limits of the worker-thread tests are
meaningful, see `docs/TESTING.md`; on other PCs run the script directly.) The script stops at the first failing build or
program and prints `ALL DONE (Visual Studio 2015, ...)` at the end. Its logs: `build\vs2015_build_<solution>.log`. It runs, among
the other programs: `test_match` (both database formats), `gen_features` (a CSV file and a SQLite database), `db_convert`, the
`test_pipeline` runs (with `--sqlite` and `--async`), `scripts\make_fhd_footage.bat` (the six 1920x1080 videos: three clean and
three with the simulated camera look) and `eval_footage` on them (five profiles since the SCRFD fitted rectangle: `dev_noncommercial_square` was added; `eval_detect` and `bench_detect` list the same five); see `docs/tools.md`, section `scripts/run_tests_vs2015.bat`.
## Rules for the code (so that it keeps compiling with Visual Studio 2015)

1. **C++14 only.** No `<filesystem>` (use `#include "fs_compat.h"` and `namespace fs = fa::fs;`, see
   `docs/common.md`), no `std::optional`, `std::string_view`, `if constexpr`, structured bindings,
   `[[nodiscard]]`, inline variables, nested namespaces (`namespace a::b`).
2. **A struct with default member values (`float x = 0;`) cannot be created with braces and values in
   Visual Studio 2015** (`Rect r = {1, 2, 3, 4};` is an error there; Visual Studio 2017 accepts it). Give such a
   struct a constructor, as `Rect`, `Point`, `PoseModel` and `PathPoint` have. Braces *without* values
   (`Rect r = {};`, `Face face;`) are fine.
3. **No C++14 "relaxed constexpr"** (a `constexpr` function with several statements or a loop): this includes
   third-party headers. ONNX Runtime is therefore used through its C header only; do not include
   `onnxruntime_cxx_api.h`.
4. **Include what you use.** The library of Visual Studio 2015 includes less indirectly than that of 2019: write
   `#include <algorithm>` for `std::max`, `std::sort` and so on.
5. **A `.c` file is compiled as C.** The only one is `third_party/sqlite/sqlite3.c`, in its own project `fa_sqlite` with
   `CompileAsC`; the C++ code includes only `sqlite3.h` (which has the `extern "C"` guards) and calls the C library through
   `sqlite_db.cpp`.

`scripts\check_vs2015_compat.bat` is a quick check that needs no Visual Studio 2015: it compiles every source with the
Visual Studio 2019 compiler as C++14, in a copy where each struct with default member values has been made non-aggregate
(`scripts/vs2015_emulation.py`), and prints `VS2015 CHECK PASSED` or the errors. It takes a few seconds and changes nothing
in the project (its work happens in `build\vs2015_emulation`). It catches rules 1 and 2; **it did not catch rules 3 and 4**,
so a real Visual Studio 2015 build remains the final check.

## Installing Visual Studio 2015 Community on another PC

Microsoft's small **web installer no longer works** (it cannot make a secure connection with its old TLS, and the
download address of its package list now answers 404; it ends with error 0x80070002). Use the **offline ISO** instead:

1. Download `vs2015.3.com_enu.iso` (Visual Studio Community 2015 with Update 3, 7,617,847,296 bytes, volume label
   `VS2015_COM_ENU`) from `https://download.microsoft.com/download/b/e/d/bedddfc4-55f4-4748-90a8-ffe38a40e89f/vs2015.3.com_enu.iso`.
   The server drops long connections; `curl -L -C - -o vs2015.3.com_enu.iso <address>` repeated until it completes works, and
   several range requests at once are faster. Check that `vs_community.exe` inside the ISO has a valid Microsoft signature.
2. Mount the ISO. Visual C++ is **not** in the default selection. Make an installer answer file
   (`vs_community.exe /CreateAdminFile AdminDeployment.xml`) and in it set `NoWeb="yes"` on `BundleCustomizations` and
   `Selected="yes"` for `NativeLanguageSupport_VCV1` (Visual C++), `Win81SDK_HiddenV1` (Windows 8.1 SDK) and
   `Win10SDK_VisibleV1` (Windows 10 SDK with the Universal CRT); `Selected="no"` for Git and the web tools saves space.
3. Run as administrator:
   `vs_community.exe /Passive /NoRestart /NoWeb /NoRefresh /AdminFile AdminDeployment.xml /Log install.log`.
   It takes about 35 minutes and uses about 3 GB of the system drive. The log ends with
   `Apply complete, result: 0x0` and `Shutting down, exit code: 0x0` when it worked.

Installing the Community edition means accepting its license terms. The **Visual C++ Build Tools 2015** (a separate small
installer, no IDE) use the same web installer and fail the same way.
## Using the solutions with Visual Studio 2019 or later

Open one and accept the "retarget" question (choose the v142 toolset and a Windows SDK), or build from the
command line without changing any file:

```
msbuild face_detection.sln /m /p:Configuration=Release /p:Platform=x64 /p:VisualStudioVersion=16.0 ^
        /p:PlatformToolset=v142 /p:WindowsTargetPlatformVersion=10.0.19041.0
```

(`VisualStudioVersion` is needed because the solution files say 14.0, which would make a newer MSBuild look
for the `v140` build files. Use the SDK version that is installed.)
## Offline dependency bundle

Only Visual Studio 2015 (v140) and Visual Studio 2019 (v142) are required. The
`pkgs` directory contains the current library dependencies, models, and cached
x64 VC++ runtime installer. See [offline package instructions](../pkgs/README.md)
for verification and restoration commands. The C++ workload and matching Windows
SDK must already be installed; this bundle does not contain complete Visual
Studio or SDK installation media.

