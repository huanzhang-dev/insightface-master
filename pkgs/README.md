# Offline packages: VS2015 and VS2019

The packages are organized by configuration and target architecture:

```text
pkgs/
  common/                 Headers, SQLite source, licenses, model profiles and weights
  release/
    x64/third_party/      Actual x64 Release libraries, DLLs and their Release PDB symbols
    x64/runtime/          VC++ 14.29.30157 x64 installer
    x86/runtime/          VC++ 14.29.30157 x86 installer (no x86 engine libraries available)
  debug/
    x64/                  Uses the Release/x64 dependencies with /MD; see its README
    x86/                  Unavailable; see its README
  manifest.json           SHA-256 hashes, restore paths and variant availability
```

| Configuration | Architecture | Availability |
|---|---|---|
| Release | x64 | Complete current engine dependency set and runtime installer |
| Debug | x64 | Project supported using the same Release /MD dependencies; no separate Debug third-party build |
| Release | x86 | Runtime installer only; x86 engine dependencies and project configuration are not provided |
| Debug | x86 | Not available |

ONNX Runtime PDB files belong to the Release binaries; they are not Debug libraries. Both project configurations use `MultiThreadedDLL` (/MD), so Debug/x64 intentionally references the packages under `release/x64`. No packages were renamed to claim an unavailable build or architecture. Runtime installers are stored under Release because they install the release CRT; they are not copied into Debug directories or run automatically.

Verify the full bundle from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/restore_offline_packages.ps1 -VerifyOnly
```

Restore a supported configuration (overwrites corresponding files under `third_party` and `models`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/restore_offline_packages.ps1 -Configuration Release -Architecture x64
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/restore_offline_packages.ps1 -Configuration Debug -Architecture x64
```

An x86 restore fails before modifying project files. To refresh packages from the original prepared PC, run `scripts/prepare_offline_packages.ps1`. No script downloads packages or runs an installer.

Copy the repository together with `pkgs` to the offline PC. Visual Studio 2015 (v140 and Windows 8.1 SDK) or Visual Studio 2019 (v142 and Windows 10 SDK), with C++ tools, must already be installed. Full IDE/SDK installation media is not included. The five solutions retain one project each, with tests in `_test.sln`. Development model restrictions remain unchanged; see `../docs/MODELS.md`.
