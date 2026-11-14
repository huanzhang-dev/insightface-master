// fs_compat.h - the file-system library under one name, for every compiler the project supports.
//
// C++17 compilers (Visual Studio 2017 15.7 and later, in C++17 mode) have <filesystem>.
// Visual Studio 2015 has only the older "Filesystem TS" (<experimental/filesystem>), which has the
// same names for everything this project uses (path, directory_iterator, recursive_directory_iterator,
// exists, is_directory, create_directories, remove_all). Write
//
//     #include "fs_compat.h"
//     namespace fs = fa::fs;
//
// instead of std::filesystem. One difference to remember: directory_entry::is_directory() exists only
// in C++17, so write fs::is_directory(entry.path()), which works in both.
#ifndef FA_FS_COMPAT_H
#define FA_FS_COMPAT_H

// (MSVC reports the language version in _MSVC_LANG; __cplusplus stays at 199711L unless /Zc:__cplusplus is set.)
#if (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || (!defined(_MSVC_LANG) && __cplusplus >= 201703L)
#include <filesystem>
namespace fa {
namespace fs = std::filesystem;
}
#else
#ifndef _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING  // newer Visual Studio versions warn about the TS header
#endif
#include <experimental/filesystem>
namespace fa {
namespace fs = std::experimental::filesystem;
}
#endif

#endif  // FA_FS_COMPAT_H
