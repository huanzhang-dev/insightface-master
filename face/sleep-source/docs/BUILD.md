# CMake build and directory separation

Only application source/config/docs/scripts stay in the source tree. CMake enforces external build, asset and runtime-SDK paths. Default siblings are `sleep-build` and `sleep-data` next to the source directory.

```bat
build_vs2015.cmd
build_vs2019.cmd
```

The wrappers select v140/v142 x86-hosted compilers targeting Windows x64 and configure Ninja. They use the local VS2019 Community CMake/Ninja tools; adjust paths or use a developer prompt and the portable commands in OTHER_PC.md for another installation.

CMake cache paths:

| Variable | Meaning |
|---|---|
| `SLEEP_DATA_ROOT` | External models/fixtures/results root; default sibling sleep-data. |
| `SLEEP_ORT_ROOT` | External runtime SDK; default SLEEP_DATA_ROOT/third_party/onnxruntime. |
| `CMAKE_TOOLCHAIN_FILE` | cmake/windows-x64.cmake, applied from a selected x86-to-x64 MSVC environment. |
| `CMAKE_BUILD_TYPE` | Release for the Ninja configuration. |

The wrappers honor environment `SLEEP_DATA_ROOT` and an optional second build-directory argument to tools/build_cmake.cmd. A manually relocated SDK must be passed explicitly with SLEEP_ORT_ROOT. Existing old CMake caches can retain stale paths; the wrappers pass both paths explicitly.

Compiler checks inspect x86 compiler PE machine 0x014c and x64 application machine 0x8664, plus _M_X64 and pointer-width assertions. The machine running these checks is Windows AMD64. This is an x86-hosted target-x64 toolchain check, not a claim of Linux-to-Windows compilation. Both binaries can execute locally and run CTest without emulation.

`cmake --install BUILD --prefix EXTERNAL_INSTALL` produces bin/data/licenses under a separate installation. Installed executables prefer sibling data/models to their CMake-configured developer data path. `--self-test`/`--demo` write only under the selected external/installed data root. See OTHER_PC.md for exact clean-machine commands.
