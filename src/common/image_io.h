// image_io.h - read and write picture files (JPEG, PNG, BMP) with the
// Windows Imaging Component (WIC), which is part of Windows. No extra library.
#ifndef FA_IMAGE_IO_H
#define FA_IMAGE_IO_H

#include <string>

#include "types.h"

namespace fa {

// Read a picture file into a 3-channel BGR image. Returns false on failure.
bool load_image(const std::string& path, Image& out);

// Write an image as JPEG when the name ends in .jpg or .jpeg, otherwise as PNG data
// (whatever the extension is). Returns false on failure.
bool save_image(const std::string& path, const Image& image);

}  // namespace fa

#endif  // FA_IMAGE_IO_H
