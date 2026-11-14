# Debug / x64

The project Debug configuration is supported using the Release /MD dependencies in `../../release/x64`. Both Debug and Release projects use MultiThreadedDLL (/MD), not /MDd. No separate Debug third-party binaries are available locally. Release ONNX Runtime PDB symbols remain beside their matching Release DLLs.

Restore using `scripts/restore_offline_packages.ps1 -Configuration Debug -Architecture x64` from the repository root.
