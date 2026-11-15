# Run from an x86-to-x64 MSVC developer environment (vcvarsall x86_amd64).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)
