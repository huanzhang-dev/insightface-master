// image_ops.cpp - see image_ops.h for what each function does.
#include "image_ops.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fa {

// ---------------------------------------------------------------------------
// resize_bilinear
//
// For every output pixel we need the 4 nearest input pixels and their weights.
// Doing the maths per pixel is slow, so we compute the horizontal part once
// per output COLUMN (x0, x1, weight) and the vertical part once per output ROW.
// Weights are integers 0..255 (8 fractional bits) instead of floats.
// ---------------------------------------------------------------------------
void resize_bilinear(const Image& src, Image& dst, int new_width, int new_height) {
    const int ch = src.channels;
    dst.create(new_width, new_height, ch);
    if (src.empty() || new_width <= 0 || new_height <= 0) return;

    const float x_ratio = static_cast<float>(src.width) / new_width;
    const float y_ratio = static_cast<float>(src.height) / new_height;

    // Column tables: for output column x, read input columns x0 and x1.
    std::vector<int> x0(new_width), x1(new_width), wx(new_width);
    for (int x = 0; x < new_width; ++x) {
        float fx = (x + 0.5f) * x_ratio - 0.5f;  // centre-aligned mapping
        if (fx < 0) fx = 0;
        int ix = static_cast<int>(fx);
        if (ix > src.width - 1) ix = src.width - 1;
        x0[x] = ix * ch;
        x1[x] = std::min(ix + 1, src.width - 1) * ch;
        wx[x] = static_cast<int>((fx - ix) * 256.0f);
    }

    for (int y = 0; y < new_height; ++y) {
        float fy = (y + 0.5f) * y_ratio - 0.5f;
        if (fy < 0) fy = 0;
        int iy = static_cast<int>(fy);
        if (iy > src.height - 1) iy = src.height - 1;
        const int iy1 = std::min(iy + 1, src.height - 1);
        const int wy = static_cast<int>((fy - iy) * 256.0f);
        const uint8_t* r0 = src.row(iy);
        const uint8_t* r1 = src.row(iy1);
        uint8_t* out = dst.row(y);
        for (int x = 0; x < new_width; ++x) {
            const int a = x0[x], b = x1[x], w = wx[x];
            for (int c = 0; c < ch; ++c) {
                // interpolate along x on the two rows, then along y
                int top = (r0[a + c] << 8) + (r0[b + c] - r0[a + c]) * w;
                int bot = (r1[a + c] << 8) + (r1[b + c] - r1[a + c]) * w;
                int v = (top << 8) + (bot - top) * wy;  // 16 fractional bits
                out[x * ch + c] = static_cast<uint8_t>((v + 32768) >> 16);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// letterbox
// ---------------------------------------------------------------------------
void letterbox(const Image& src, int size, Image& dst, float& scale, int& pad_x, int& pad_y) {
    scale = std::min(static_cast<float>(size) / src.width, static_cast<float>(size) / src.height);
    const int new_w = std::max(1, static_cast<int>(src.width * scale + 0.5f));
    const int new_h = std::max(1, static_cast<int>(src.height * scale + 0.5f));
    Image resized;
    resize_bilinear(src, resized, new_w, new_h);

    dst.create(size, size, src.channels);
    std::memset(dst.data.data(), 114, dst.data.size());  // gray padding
    pad_x = (size - new_w) / 2;
    pad_y = (size - new_h) / 2;
    for (int y = 0; y < new_h; ++y) {
        std::memcpy(dst.row(y + pad_y) + pad_x * src.channels, resized.row(y),
                    static_cast<size_t>(new_w) * src.channels);
    }
}

void letterbox_fit(const Image& src, int long_side, int multiple, Image& dst, float& scale, int& pad_x, int& pad_y) {
    scale = static_cast<float>(long_side) / std::max(src.width, src.height);
    const int new_w = std::max(1, static_cast<int>(src.width * scale + 0.5f));
    const int new_h = std::max(1, static_cast<int>(src.height * scale + 0.5f));
    const int m = std::max(1, multiple);
    const int out_w = (new_w + m - 1) / m * m, out_h = (new_h + m - 1) / m * m;
    Image resized;
    resize_bilinear(src, resized, new_w, new_h);

    dst.create(out_w, out_h, src.channels);
    std::memset(dst.data.data(), 114, dst.data.size());  // gray padding
    pad_x = (out_w - new_w) / 2;
    pad_y = (out_h - new_h) / 2;
    for (int y = 0; y < new_h; ++y) {
        std::memcpy(dst.row(y + pad_y) + pad_x * src.channels, resized.row(y),
                    static_cast<size_t>(new_w) * src.channels);
    }
}

// ---------------------------------------------------------------------------
// to_gray: gray = (29*B + 150*G + 77*R) / 256  (integer version of the usual
// 0.114 B + 0.587 G + 0.299 R)
// ---------------------------------------------------------------------------
void to_gray(const Image& src, Image& dst) {
    dst.create(src.width, src.height, 1);
    if (src.channels == 1) {
        for (int y = 0; y < src.height; ++y) std::memcpy(dst.row(y), src.row(y), src.width);
        return;
    }
    for (int y = 0; y < src.height; ++y) {
        const uint8_t* p = src.row(y);
        uint8_t* q = dst.row(y);
        for (int x = 0; x < src.width; ++x, p += 3) q[x] = static_cast<uint8_t>((29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8);
    }
}

// ---------------------------------------------------------------------------
// crop
// ---------------------------------------------------------------------------
void crop(const Image& src, const Rect& box, Image& dst) {
    const int x0 = static_cast<int>(std::floor(box.x));
    const int y0 = static_cast<int>(std::floor(box.y));
    const int w = std::max(1, static_cast<int>(std::ceil(box.w)));
    const int h = std::max(1, static_cast<int>(std::ceil(box.h)));
    dst.create(w, h, src.channels);
    for (int y = 0; y < h; ++y) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= src.height) continue;
        // Range of destination columns that fall inside the source image.
        const int dx_begin = std::max(0, -x0);
        const int dx_end = std::min(w, src.width - x0);
        if (dx_end <= dx_begin) continue;
        std::memcpy(dst.row(y) + dx_begin * src.channels, src.row(sy) + (x0 + dx_begin) * src.channels,
                    static_cast<size_t>(dx_end - dx_begin) * src.channels);
    }
}

// ---------------------------------------------------------------------------
// Similarity transform (Umeyama method, 2-D, 5 points)
//
// We look for a, b, tx, ty so that   to ~= [a -b; b a] * from + [tx; ty].
// Subtract the mean of each point set, then
//   a = sum(from . to) / sum(|from|^2)       b = sum(from x to) / sum(|from|^2)
// ---------------------------------------------------------------------------
void estimate_similarity(const Point from[5], const Point to[5], float m[6]) {
    float fx = 0, fy = 0, tx = 0, ty = 0;
    for (int i = 0; i < 5; ++i) {
        fx += from[i].x; fy += from[i].y;
        tx += to[i].x;   ty += to[i].y;
    }
    fx /= 5; fy /= 5; tx /= 5; ty /= 5;

    float dot = 0, cross = 0, norm = 0;
    for (int i = 0; i < 5; ++i) {
        const float ax = from[i].x - fx, ay = from[i].y - fy;
        const float bx = to[i].x - tx,   by = to[i].y - ty;
        dot += ax * bx + ay * by;
        cross += ax * by - ay * bx;
        norm += ax * ax + ay * ay;
    }
    if (norm < 1e-6f) norm = 1e-6f;
    const float a = dot / norm;
    const float b = cross / norm;
    m[0] = a;  m[1] = -b; m[2] = tx - (a * fx - b * fy);
    m[3] = b;  m[4] = a;  m[5] = ty - (b * fx + a * fy);
}

void invert_similarity(const float m[6], float inv[6]) {
    // Matrix [a -b; b a] has the inverse [a b; -b a] / (a^2 + b^2).
    const float a = m[0], b = m[3];
    float det = a * a + b * b;
    if (det < 1e-12f) det = 1e-12f;
    const float ia = a / det, ib = -b / det;
    inv[0] = ia;  inv[1] = -ib; inv[2] = -(ia * m[2] - ib * m[5]);
    inv[3] = ib;  inv[4] = ia;  inv[5] = -(ib * m[2] + ia * m[5]);
}

// ---------------------------------------------------------------------------
// warp_affine (bilinear)
// ---------------------------------------------------------------------------
void warp_affine(const Image& src, const float t[6], Image& dst, int width, int height) {
    const int ch = src.channels;
    dst.create(width, height, ch);
    for (int y = 0; y < height; ++y) {
        uint8_t* out = dst.row(y);
        for (int x = 0; x < width; ++x) {
            const float sx = t[0] * x + t[1] * y + t[2];
            const float sy = t[3] * x + t[4] * y + t[5];
            const int ix = static_cast<int>(std::floor(sx));
            const int iy = static_cast<int>(std::floor(sy));
            if (ix < 0 || iy < 0 || ix >= src.width - 1 || iy >= src.height - 1) continue;  // stays 0
            const float fx = sx - ix, fy = sy - iy;
            const uint8_t* p00 = src.row(iy) + ix * ch;
            const uint8_t* p10 = p00 + ch;
            const uint8_t* p01 = src.row(iy + 1) + ix * ch;
            const uint8_t* p11 = p01 + ch;
            for (int c = 0; c < ch; ++c) {
                float top = p00[c] + (p10[c] - p00[c]) * fx;
                float bot = p01[c] + (p11[c] - p01[c]) * fx;
                out[x * ch + c] = static_cast<uint8_t>(top + (bot - top) * fy + 0.5f);
            }
        }
    }
}

// The ArcFace / SFace standard positions of the 5 landmarks in a 112x112 face.
const Point* aligned_template_112() {
    static const Point kTemplate[5] = {
        {38.2946f, 51.6963f},  // right eye (person's right = image left)
        {73.5318f, 51.5014f},  // left eye
        {56.0252f, 71.7366f},  // nose tip
        {41.5493f, 92.3655f},  // right mouth corner
        {70.7299f, 92.2041f},  // left mouth corner
    };
    return kTemplate;
}

void align_face(const Image& src, const Face& face, Image& aligned) {
    Point from[5];
    for (int i = 0; i < 5; ++i) from[i] = face.landmarks[i];
    float to_aligned[6], back[6];
    estimate_similarity(from, aligned_template_112(), to_aligned);
    invert_similarity(to_aligned, back);  // warp_affine wants "output -> source"
    warp_affine(src, back, aligned, FA_ALIGNED_FACE_SIZE, FA_ALIGNED_FACE_SIZE);
}

// ---------------------------------------------------------------------------
// drawing (only used to make pictures for humans to look at)
// ---------------------------------------------------------------------------
static void put_pixel(Image& image, int x, int y, uint8_t b, uint8_t g, uint8_t r) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height || image.channels != 3) return;
    uint8_t* p = image.row(y) + x * 3;
    p[0] = b; p[1] = g; p[2] = r;
}

void draw_rect(Image& image, const Rect& box, uint8_t b, uint8_t g, uint8_t r, int thickness) {
    const int x1 = static_cast<int>(box.x), y1 = static_cast<int>(box.y);
    const int x2 = static_cast<int>(box.right()), y2 = static_cast<int>(box.bottom());
    for (int t = 0; t < thickness; ++t) {
        for (int x = x1; x <= x2; ++x) { put_pixel(image, x, y1 + t, b, g, r); put_pixel(image, x, y2 - t, b, g, r); }
        for (int y = y1; y <= y2; ++y) { put_pixel(image, x1 + t, y, b, g, r); put_pixel(image, x2 - t, y, b, g, r); }
    }
}

void draw_dot(Image& image, Point center, int radius, uint8_t b, uint8_t g, uint8_t r) {
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            if (dx * dx + dy * dy <= radius * radius)
                put_pixel(image, static_cast<int>(center.x) + dx, static_cast<int>(center.y) + dy, b, g, r);
}

// ---------------------------------------------------------------------------
// integral_image
// ---------------------------------------------------------------------------
void integral_image(const Image& gray, std::vector<int64_t>& sum, std::vector<int64_t>& sqsum) {
    const int w = gray.width, h = gray.height;
    const int sw = w + 1;
    sum.assign(static_cast<size_t>(sw) * (h + 1), 0);
    sqsum.assign(static_cast<size_t>(sw) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* p = gray.row(y);
        int64_t row_sum = 0, row_sq = 0;
        int64_t* s = &sum[static_cast<size_t>(y + 1) * sw];
        int64_t* q = &sqsum[static_cast<size_t>(y + 1) * sw];
        const int64_t* s_prev = &sum[static_cast<size_t>(y) * sw];
        const int64_t* q_prev = &sqsum[static_cast<size_t>(y) * sw];
        for (int x = 0; x < w; ++x) {
            row_sum += p[x];
            row_sq += static_cast<int64_t>(p[x]) * p[x];
            s[x + 1] = s_prev[x + 1] + row_sum;
            q[x + 1] = q_prev[x + 1] + row_sq;
        }
    }
}

}  // namespace fa
