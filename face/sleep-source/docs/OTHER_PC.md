# Set up on another Windows PC

No connection to another PC has been provided. These steps prepare a separate setup; local transfer testing does not mean another physical PC was accessed.

## Prepare and transfer

`Z:\sleep-transfer\sleep-source.zip` contains only application source, CMake/configuration, scripts and docs. `sleep-assets.zip` contains the verified runtime SDK, model weights, public-domain fixture and required license files. `SHA256.json` records the archives' SHA-256 values. Copy all three to the other PC and compare `Get-FileHash -Algorithm SHA256` against that metadata.

On the other PC use separate directories, for example:

```powershell
New-Item -ItemType Directory -Path C:\Work\sleep -Force
Expand-Archive C:\Transfer\sleep-source.zip C:\Work\sleep
powershell -ExecutionPolicy Bypass -File C:\Work\sleep\tools\setup.ps1 -DataRoot C:\Work\sleep-data -AssetArchive C:\Transfer\sleep-assets.zip
```

Setup extracts only manifest-listed assets, verifies every size/hash, rejects asset paths escaping the external data directory, and creates external `tests`/`output` directories. It does not write binary assets into the source tree. Existing corrupt assets cause an error instead of silent replacement.

## Compiler prerequisites

Install the Visual Studio C++ compiler/toolset you need (v140 for VS2015 or v142 for VS2019), a compatible Windows SDK, CMake 3.20+ and Ninja. The prebuilt runtime requires Windows 10 x64 or later with a recent Microsoft VC redistributable. WIC/Media Foundation Windows features must be present. No Python or image-processing SDK is needed.

Open the selected compiler's **x86-to-x64 developer command prompt**; ensure `cl`, `cmake`, `ninja` and `ctest` are available. Use distinct build trees for each toolset. For v142:

```bat
cmake -S C:\Work\sleep -B C:\Work\sleep-build\v142-x64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:\Work\sleep\cmake\windows-x64.cmake -DCMAKE_BUILD_TYPE=Release -DSLEEP_DATA_ROOT=C:\Work\sleep-data -DSLEEP_ORT_ROOT=C:\Work\sleep-data\third_party\onnxruntime
cmake --build C:\Work\sleep-build\v142-x64 --parallel 2
ctest --test-dir C:\Work\sleep-build\v142-x64 --output-on-failure
```

For v140, enter the VS2015 x86-to-x64 environment, change the build directory to `v140-x64`, and run the same commands. This compiler selection is determined by the developer environment, not by reusing a cache from another toolset. The batch wrappers are shortcuts for the installed default VS locations; use these manual CMake commands for other installations.

## Install and run independently

```bat
cmake --install C:\Work\sleep-build\v142-x64 --prefix C:\Apps\Sleep
C:\Apps\Sleep\bin\Sleep.exe --self-test
C:\Apps\Sleep\bin\Sleep.exe --source 0 --infrared --fps 30 --sound
```

Install copies the application/runtime DLLs to `bin`, models/fixture and writable test/output directories to sibling `data`, and upstream license notices to `licenses`. The installed program detects its sibling data directory before falling back to developer-build paths, so it can run without the original source or build directory. Install to a user-writable directory, or pass a writable `--output` path for normal camera runs. Generated results never go into the source tree.

A real NIR camera and illumination are still needed for physical nighttime tests. Camera/decoder capability and actual accuracy must be checked on that PC; local compile/playback tests cannot establish them.
