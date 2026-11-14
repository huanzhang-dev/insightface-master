// draw_text.cpp - see draw_text.h.
//
// How it works: we let Windows draw the text as white letters on a black bitmap that is
// just big enough, then use the brightness of every bitmap pixel as the opacity ("alpha")
// when we blend the wanted color into the picture. The outline is made by blending the
// same text four more times, shifted by one pixel, in black first.
#include "draw_text.h"

#include <windows.h>

#include <algorithm>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace fa {
namespace {

// Blend `color` into the picture wherever the (white-on-black) text bitmap is bright.
void blend(Image& image, const uint8_t* bitmap, int bw, int bh, int x0, int y0, uint8_t b, uint8_t g, uint8_t r) {
    for (int y = 0; y < bh; ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= image.height) continue;
        for (int x = 0; x < bw; ++x) {
            const int px = x0 + x;
            if (px < 0 || px >= image.width) continue;
            const uint8_t* s = bitmap + (static_cast<size_t>(y) * bw + x) * 4;
            const int alpha = std::max(s[0], std::max(s[1], s[2]));  // brightness of the text pixel
            if (alpha == 0) continue;
            uint8_t* d = image.row(py) + px * 3;
            d[0] = static_cast<uint8_t>((d[0] * (255 - alpha) + b * alpha) / 255);
            d[1] = static_cast<uint8_t>((d[1] * (255 - alpha) + g * alpha) / 255);
            d[2] = static_cast<uint8_t>((d[2] * (255 - alpha) + r * alpha) / 255);
        }
    }
}

}  // namespace

void draw_text(Image& image, int x, int y, const std::string& text, uint8_t b, uint8_t g, uint8_t r, int pixel_height) {
    if (image.empty() || image.channels != 3 || text.empty()) return;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HFONT font = CreateFontA(-pixel_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);

    SIZE extent = {0, 0};
    GetTextExtentPoint32A(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    const int bw = extent.cx + 4, bh = extent.cy + 4;  // a little room for the outline

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bw;
    info.bmiHeader.biHeight = -bh;  // negative = top row first
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        std::fill_n(static_cast<uint8_t*>(bits), static_cast<size_t>(bw) * bh * 4, uint8_t(0));  // black
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 2, 2, text.c_str(), static_cast<int>(text.size()));
        GdiFlush();

        const uint8_t* pixels = static_cast<const uint8_t*>(bits);
        // outline: the text in black, shifted one pixel in each direction, then the colored text on top
        const int shifts[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& s : shifts) blend(image, pixels, bw, bh, x + s[0], y + s[1], 0, 0, 0);
        blend(image, pixels, bw, bh, x, y, b, g, r);

        SelectObject(dc, old_bitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}

}  // namespace fa
