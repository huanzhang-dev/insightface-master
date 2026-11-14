// draw_text.h - write text into a picture (for the annotated result videos).
//
// Uses the text drawing of Windows (GDI) to make an anti-aliased label, then blends it
// into the picture. Only for pictures that people look at; no engine uses it.
#ifndef FA_DRAW_TEXT_H
#define FA_DRAW_TEXT_H

#include <string>

#include "types.h"

namespace fa {

// Draw `text` with its top-left corner at (x, y) in the given color (b, g, r). `pixel_height`
// is the height of the letters. A dark outline is added so the text is readable on any
// background. Parts outside the picture are cut off. Text may be plain ASCII.
void draw_text(Image& image, int x, int y, const std::string& text, uint8_t b, uint8_t g, uint8_t r, int pixel_height = 22);

}  // namespace fa

#endif  // FA_DRAW_TEXT_H
