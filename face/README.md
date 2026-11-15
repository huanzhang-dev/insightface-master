# Preserved model implementation

Contains verified source/configuration/preprocessing/postprocessing, neural weights, runtime DLLs and SDK headers, fixtures, licenses and restore scripts in separate source/assets archives. Original working directories have not been removed or overwritten.

The application folder is a ready-to-run Windows x64 installation with application-local neural runtimes, models and licenses. Run application/bin/Sleep.exe --self-test or --source 0 --infrared --fps 30. Windows media components and a suitable Microsoft VC redistributable are still prerequisites. Real IR-camera/drowsiness accuracy is not implied by successful execution.

To rebuild elsewhere, extract sleep-source.zip outside this snapshot, restore sleep-assets.zip using tools/setup.ps1, and follow docs/OTHER_PC.md. Rebuilding requires the Microsoft C++ toolset, Windows SDK, CMake and Ninja; those system installations are not included.

SHA256.json records every preserved payload. This snapshot is a separate local copy, not an off-machine backup. Z:/sleep remains source-only; live models/runtime SDK remain in Z:/sleep-data.
