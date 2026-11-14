// scene.cpp - see scene.h.
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "image_ops.h"

namespace fa {
namespace {

const float kPi = 3.14159265358979f;

// Value on the actor's path at `time_ms` (linear between the key frames).
PathPoint interpolate(const std::vector<PathPoint>& path, double time_ms) {
    if (path.size() == 1 || time_ms <= path.front().time_ms) return path.front();
    if (time_ms >= path.back().time_ms) return path.back();
    for (size_t i = 1; i < path.size(); ++i) {
        if (time_ms <= path[i].time_ms) {
            const PathPoint &a = path[i - 1], &b = path[i];
            const float k = static_cast<float>((time_ms - a.time_ms) / (b.time_ms - a.time_ms));
            PathPoint p;
            p.time_ms = time_ms;
            p.center_x = a.center_x + (b.center_x - a.center_x) * k;
            p.center_y = a.center_y + (b.center_y - a.center_y) * k;
            p.face_width = a.face_width + (b.face_width - a.face_width) * k;
            p.roll_deg = a.roll_deg + (b.roll_deg - a.roll_deg) * k;
            return p;
        }
    }
    return path.back();
}

// 0 inside, 1 outside, smooth in between: used to fade the sprite's edge.
float smoothstep(float edge0, float edge1, float x) {
    const float t = std::min(1.0f, std::max(0.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

// ---------------------------------------------------------------------------------------------------------------
// The look of a real camera in a real room (see CameraLook in scene.h)
// ---------------------------------------------------------------------------------------------------------------

inline uint8_t clamp8(float v) { return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f))); }

// A filled rectangle (colour in B, G, R) whose brightness goes from top_shade at its top edge to bottom_shade at its bottom edge.
void fill_rect(Image& f, float x0, float y0, float x1, float y1, int b, int g, int r, float top_shade, float bottom_shade) {
    const int xa = std::max(0, static_cast<int>(x0)), xb = std::min(f.width, static_cast<int>(x1));
    const int ya = std::max(0, static_cast<int>(y0)), yb = std::min(f.height, static_cast<int>(y1));
    for (int y = ya; y < yb; ++y) {
        const float k = y1 > y0 ? (y - y0) / (y1 - y0) : 0.0f;
        const float shade = top_shade + (bottom_shade - top_shade) * k;
        uint8_t* row = f.row(y);
        for (int x = xa; x < xb; ++x) {
            row[x * 3 + 0] = clamp8(b * shade);
            row[x * 3 + 1] = clamp8(g * shade);
            row[x * 3 + 2] = clamp8(r * shade);
        }
    }
}

// Make a rectangle brighter or darker by `delta` levels (wall panels).
void shade_rect(Image& f, float x0, float y0, float x1, float y1, int delta) {
    const int xa = std::max(0, static_cast<int>(x0)), xb = std::min(f.width, static_cast<int>(x1));
    const int ya = std::max(0, static_cast<int>(y0)), yb = std::min(f.height, static_cast<int>(y1));
    for (int y = ya; y < yb; ++y) {
        uint8_t* row = f.row(y);
        for (int x = xa * 3; x < xb * 3; ++x) row[x] = clamp8(static_cast<float>(row[x]) + delta);
    }
}

// The room: wall panels, a floor, a window, a door, furniture and a notice board with text-like lines. Static: the
// same seed gives the same room in every frame. People are drawn over it afterwards.
void draw_room(Image& frame, const CameraLook& look) {
    const float W = static_cast<float>(frame.width), H = static_cast<float>(frame.height);
    std::mt19937 rng(static_cast<unsigned>(look.seed) * 7919u + 13u);
    auto uni = [&rng](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    const float floor_y = H * 0.80f;

    for (int i = 0; i < 4; ++i) {  // wall panels of slightly different brightness
        const float x0 = uni(0, W * 0.8f);
        shade_rect(frame, x0, 0, x0 + uni(W * 0.12f, W * 0.30f), floor_y, static_cast<int>(uni(-16, 16)));
    }
    fill_rect(frame, 0, floor_y, W, H, 92, 112, 130, 1.0f, 0.62f);  // the floor, darker toward the camera
    shade_rect(frame, 0, floor_y - 6, W, floor_y, -30);              // skirting board

    const float wx = uni(W * 0.05f, W * 0.55f), ww = W * uni(0.12f, 0.20f), wy = H * uni(0.06f, 0.12f), wh = H * uni(0.25f, 0.33f);
    fill_rect(frame, wx - 8, wy - 8, wx + ww + 8, wy + wh + 8, 70, 80, 92, 1.0f, 1.0f);   // window frame
    fill_rect(frame, wx, wy, wx + ww, wy + wh, 238, 230, 216, 1.0f, 0.82f);               // bright daylight
    fill_rect(frame, wx + ww / 2 - 3, wy, wx + ww / 2 + 3, wy + wh, 70, 80, 92, 1.0f, 1.0f);  // cross bar
    fill_rect(frame, wx, wy + wh / 2 - 3, wx + ww, wy + wh / 2 + 3, 70, 80, 92, 1.0f, 1.0f);

    const float dx = uni(W * 0.62f, W * 0.86f), dw = W * 0.09f;
    fill_rect(frame, dx, H * 0.22f, dx + dw, floor_y, 58, 78, 104, 1.0f, 0.78f);          // a dark door
    fill_rect(frame, dx + dw * 0.8f, H * 0.52f, dx + dw * 0.88f, H * 0.55f, 170, 190, 200, 1.0f, 1.0f);  // its handle

    for (int i = 0; i < 4; ++i) {  // furniture along the floor line
        const float bx = uni(0, W * 0.9f), bw = W * uni(0.05f, 0.12f), bh = H * uni(0.08f, 0.20f);
        const int shade = static_cast<int>(uni(60, 140));
        fill_rect(frame, bx, floor_y - bh, bx + bw, floor_y + H * 0.04f, shade, shade + 10, shade + 22, 1.0f, 0.78f);
        shade_rect(frame, bx, floor_y - bh, bx + bw, floor_y - bh + 5, 40);                  // lit top edge
    }

    const float nx = uni(W * 0.10f, W * 0.55f), nw = W * 0.10f, ny = H * uni(0.38f, 0.48f), nh = H * 0.14f;
    fill_rect(frame, nx, ny, nx + nw, ny + nh, 215, 228, 236, 1.0f, 0.95f);                // notice board
    for (float y = ny + 12; y + 5 < ny + nh - 8; y += 14)                                   // lines like printed text
        fill_rect(frame, nx + 10, y, nx + nw - 10 - uni(0, nw * 0.4f), y + 5, 55, 55, 55, 1.0f, 1.0f);
}

// A Gaussian blur of the whole picture (lens and image processing are never perfectly sharp).
void gaussian_blur(Image& img, float sigma) {
    if (sigma < 0.3f) return;
    const int radius = std::max(1, static_cast<int>(std::ceil(2.5f * sigma)));
    std::vector<int> w(2 * radius + 1);
    int total = 0;
    for (int i = -radius; i <= radius; ++i) {
        w[i + radius] = static_cast<int>(1024.0f * std::exp(-0.5f * i * i / (sigma * sigma)) / (sigma * 2.5066283f) + 0.5f);
        total += w[i + radius];
    }
    w[radius] += 1024 - total;  // the weights add up to exactly 1024 (no change of brightness)
    const int W = img.width, H = img.height;
    Image tmp = img;
    for (int y = 0; y < H; ++y) {  // horizontal
        const uint8_t* src = img.row(y);
        uint8_t* dst = tmp.row(y);
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) {
                int acc = 0;
                for (int i = -radius; i <= radius; ++i) acc += w[i + radius] * src[std::min(W - 1, std::max(0, x + i)) * 3 + c];
                dst[x * 3 + c] = static_cast<uint8_t>(acc >> 10);
            }
    }
    for (int y = 0; y < H; ++y) {  // vertical
        uint8_t* dst = img.row(y);
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) {
                int acc = 0;
                for (int i = -radius; i <= radius; ++i) acc += w[i + radius] * tmp.row(std::min(H - 1, std::max(0, y + i)))[x * 3 + c];
                dst[x * 3 + c] = static_cast<uint8_t>(acc >> 10);
            }
    }
}

// Smear the rectangle [x0, x1) x [y0, y1) by a box of nx pixels horizontally and ny pixels vertically (motion blur).
void box_blur_region(Image& img, int x0, int y0, int x1, int y1, int nx, int ny) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(img.width, x1); y1 = std::min(img.height, y1);
    if (x1 - x0 < 2 || y1 - y0 < 2 || (nx < 2 && ny < 2)) return;
    const int rw = x1 - x0, rh = y1 - y0;
    std::vector<float> a(static_cast<size_t>(rw) * rh * 3), b(a.size());
    for (int y = 0; y < rh; ++y)
        for (int i = 0; i < rw * 3; ++i) a[static_cast<size_t>(y) * rw * 3 + i] = img.row(y0 + y)[x0 * 3 + i];
    for (int y = 0; y < rh; ++y)  // horizontal box, centred
        for (int x = 0; x < rw; ++x)
            for (int c = 0; c < 3; ++c) {
                float acc = 0;
                const int lo = x - nx / 2, hi = lo + std::max(1, nx);
                for (int k = lo; k < hi; ++k) acc += a[(static_cast<size_t>(y) * rw + std::min(rw - 1, std::max(0, k))) * 3 + c];
                b[(static_cast<size_t>(y) * rw + x) * 3 + c] = acc / std::max(1, nx);
            }
    for (int y = 0; y < rh; ++y)  // vertical box, centred
        for (int x = 0; x < rw; ++x)
            for (int c = 0; c < 3; ++c) {
                float acc = 0;
                const int lo = y - ny / 2, hi = lo + std::max(1, ny);
                for (int k = lo; k < hi; ++k) acc += b[(static_cast<size_t>(std::min(rh - 1, std::max(0, k))) * rw + x) * 3 + c];
                img.row(y0 + y)[(x0 + x) * 3 + c] = clamp8(acc / std::max(1, ny));
            }
}

// Uneven light, vignette, exposure drift, colour cast, and sensor noise (grows with the brightness like shot noise; partly different per channel).
void apply_light_and_noise(Image& frame, const CameraLook& look, double time_ms, int frame_index) {
    const int W = frame.width, H = frame.height;
    const float cx = W * 0.5f, cy = H * 0.5f;
    const float exposure = 1.0f + look.exposure_drift * (0.7f * std::sin(2 * kPi * static_cast<float>(time_ms) / 7000.0f + 0.7f) +
                                                          0.3f * std::sin(2 * kPi * static_cast<float>(time_ms) / 2300.0f + 2.1f));
    std::vector<float> col_factor(W), col_dx2(W);
    for (int x = 0; x < W; ++x) {
        col_factor[x] = 1.0f - look.light_slope * (1.0f - static_cast<float>(x) / W);  // the left darker than the right
        const float dx = (x - cx) / cx;
        col_dx2[x] = dx * dx;
    }
    std::mt19937 rng(static_cast<unsigned>(look.seed) * 104729u + static_cast<unsigned>(frame_index) * 7919u + 17u);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    for (int y = 0; y < H; ++y) {
        const float dy = (y - cy) / cy, dy2 = dy * dy;
        uint8_t* row = frame.row(y);
        for (int x = 0; x < W; ++x) {
            const float r2 = (col_dx2[x] + dy2) * 0.5f;
            const float f = col_factor[x] * (1.0f - look.vignette * r2) * exposure;
            float b = row[x * 3 + 0] * f * (1.0f - look.warm), g = row[x * 3 + 1] * f, r = row[x * 3 + 2] * f * (1.0f + look.warm);
            if (look.noise_sigma > 0) {
                const float mean = (b + g + r) / 3.0f;
                const float sigma = look.noise_sigma * std::sqrt((std::max(0.0f, mean) + 8.0f) / 136.0f);  // = noise_sigma at mid gray
                const float luma = gauss(rng) * sigma;
                b += luma + gauss(rng) * 0.5f * sigma;
                g += luma + gauss(rng) * 0.5f * sigma;
                r += luma + gauss(rng) * 0.5f * sigma;
            }
            row[x * 3 + 0] = clamp8(b);
            row[x * 3 + 1] = clamp8(g);
            row[x * 3 + 2] = clamp8(r);
        }
    }
}

}  // namespace

Actor make_actor(const std::string& label, const Image& portrait, const Rect& face) {
    Actor actor;
    actor.label = label;
    // Cut a region around the face: 0.9 face widths to each side, 0.8 above,
    // 1.6 below (hair, neck and shoulders). Keep it inside the portrait.
    Rect region = {face.x - 0.9f * face.w, face.y - 0.8f * face.h, face.w * 2.8f, face.h * 3.4f};
    const float x1 = std::max(0.0f, region.x), y1 = std::max(0.0f, region.y);
    const float x2 = std::min(static_cast<float>(portrait.width), region.right());
    const float y2 = std::min(static_cast<float>(portrait.height), region.bottom());
    region = {x1, y1, x2 - x1, y2 - y1};
    crop(portrait, region, actor.sprite);
    actor.face_in_sprite = {face.x - region.x, face.y - region.y, face.w, face.h};
    return actor;
}

std::vector<PathPoint> approach_path(double duration_ms, float sx, float sy, float ex, float ey, float w0, float w1,
                                     float roll_start, double settle_ms) {
    std::vector<PathPoint> path;
    PathPoint a, b, c;
    a.time_ms = 0; a.center_x = sx; a.center_y = sy; a.face_width = w0; a.roll_deg = roll_start;
    b.time_ms = settle_ms; b.roll_deg = 0;
    const float k = static_cast<float>(settle_ms / duration_ms);
    b.center_x = sx + (ex - sx) * k; b.center_y = sy + (ey - sy) * k; b.face_width = w0 + (w1 - w0) * k;
    c.time_ms = duration_ms; c.center_x = ex; c.center_y = ey; c.face_width = w1; c.roll_deg = 0;
    path.push_back(a);
    path.push_back(b);
    path.push_back(c);
    return path;
}

void render_frame(int width, int height, double time_ms, int frame_index, const std::vector<Actor>& actors,
                  Image& frame, std::vector<TruthFace>& truth, const CameraLook* look) {
    // ---- background: vertical gradient plus a little noise ----
    frame.create(width, height, 3);
    std::mt19937 rng(1000 + frame_index);
    std::uniform_int_distribution<int> noise(-3, 3);
    for (int y = 0; y < height; ++y) {
        const float t = static_cast<float>(y) / height;
        uint8_t* row = frame.row(y);
        for (int x = 0; x < width; ++x) {
            const int base = static_cast<int>(150 - 50 * t);
            row[x * 3 + 0] = static_cast<uint8_t>(std::min(255, std::max(0, base + 20 + noise(rng))));  // B
            row[x * 3 + 1] = static_cast<uint8_t>(std::min(255, std::max(0, base + 8 + noise(rng))));   // G
            row[x * 3 + 2] = static_cast<uint8_t>(std::min(255, std::max(0, base - 10 + noise(rng))));  // R
        }
    }
    if (look && look->clutter) draw_room(frame, *look);  // the room behind the people

    truth.clear();
    for (const Actor& actor : actors) {
        TruthFace tf;
        tf.label = actor.label;
        if (time_ms < actor.appear_ms || time_ms > actor.disappear_ms || actor.path.empty()) {
            truth.push_back(tf);
            continue;
        }
        const PathPoint p = interpolate(actor.path, time_ms);
        const float scale = p.face_width / actor.face_in_sprite.w;  // sprite pixel -> frame pixel
        const float roll = p.roll_deg * kPi / 180.0f;
        const float c = std::cos(roll), s = std::sin(roll);

        // The sprite point that must land on (center_x, center_y) is the middle of the face.
        const float face_cx = actor.face_in_sprite.x + actor.face_in_sprite.w / 2;
        const float face_cy = actor.face_in_sprite.y + actor.face_in_sprite.h / 2;
        const float half_w = actor.sprite.width * scale, half_h = actor.sprite.height * scale;
        const float radius = std::sqrt(half_w * half_w + half_h * half_h);
        const int x_begin = std::max(0, static_cast<int>(p.center_x - radius)), x_end = std::min(width - 1, static_cast<int>(p.center_x + radius));
        const int y_begin = std::max(0, static_cast<int>(p.center_y - radius)), y_end = std::min(height - 1, static_cast<int>(p.center_y + radius));

        for (int y = y_begin; y <= y_end; ++y) {
            uint8_t* out = frame.row(y);
            for (int x = x_begin; x <= x_end; ++x) {
                // frame pixel -> sprite pixel (undo move, tilt and scale)
                const float dx = x - p.center_x, dy = y - p.center_y;
                const float sx = (c * dx + s * dy) / scale + face_cx;
                const float sy = (-s * dx + c * dy) / scale + face_cy;
                if (sx < 0 || sy < 0 || sx >= actor.sprite.width - 1 || sy >= actor.sprite.height - 1) continue;
                // soft elliptical edge so the pasted photo has no hard border
                const float nx = (sx / actor.sprite.width - 0.5f) * 2, ny = (sy / actor.sprite.height - 0.5f) * 2;
                const float alpha = 1.0f - smoothstep(0.80f, 1.0f, std::sqrt(nx * nx + ny * ny));
                if (alpha <= 0) continue;
                // bilinear sample
                const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
                const float fx = sx - ix, fy = sy - iy;
                const uint8_t* p00 = actor.sprite.row(iy) + ix * 3;
                const uint8_t* p10 = p00 + 3;
                const uint8_t* p01 = actor.sprite.row(iy + 1) + ix * 3;
                const uint8_t* p11 = p01 + 3;
                for (int ch = 0; ch < 3; ++ch) {
                    const float top = p00[ch] + (p10[ch] - p00[ch]) * fx;
                    const float bot = p01[ch] + (p11[ch] - p01[ch]) * fx;
                    const float v = top + (bot - top) * fy;
                    out[x * 3 + ch] = static_cast<uint8_t>(out[x * 3 + ch] * (1 - alpha) + v * alpha + 0.5f);
                }
            }
        }

        // ground truth: the face box of this actor (axis-aligned, around the rotated box)
        const float fw = p.face_width, fh = actor.face_in_sprite.h * scale;
        const float ex = (std::fabs(c) * fw + std::fabs(s) * fh) / 2, ey = (std::fabs(s) * fw + std::fabs(c) * fh) / 2;
        tf.box = {p.center_x - ex, p.center_y - ey, 2 * ex, 2 * ey};
        tf.roll_deg = p.roll_deg;
        tf.visible = tf.box.x >= 0 && tf.box.y >= 0 && tf.box.right() <= width && tf.box.bottom() <= height;
        truth.push_back(tf);
    }

    if (!look) return;
    // ---- the camera: motion blur of the people who move (only drawn when it is 2 or more pixels long), lens softness, light, noise ----
    // A person who moves m pixels between two frames is smeared over motion_blur * m pixels (the exposure is a
    // fraction of the frame time). The speed comes from the path; the picture around the person is blurred.
    if (look->motion_blur > 0) {
        for (const Actor& actor : actors) {
            if (time_ms < actor.appear_ms || time_ms > actor.disappear_ms || actor.path.empty()) continue;
            const PathPoint before = interpolate(actor.path, std::max(0.0, time_ms - 5)), after = interpolate(actor.path, time_ms + 5);
            const PathPoint now = interpolate(actor.path, time_ms);
            const float vx = (after.center_x - before.center_x) / 10.0f, vy = (after.center_y - before.center_y) / 10.0f;  // pixels per ms
            const int nx = static_cast<int>(std::lround(std::fabs(vx) * look->frame_ms * look->motion_blur));
            const int ny = static_cast<int>(std::lround(std::fabs(vy) * look->frame_ms * look->motion_blur));
            if (nx < 2 && ny < 2) continue;
            // the picture of this person: the sprite around the face (a little extra for the tilt)
            const float scale = now.face_width / actor.face_in_sprite.w;
            const float face_cx = actor.face_in_sprite.x + actor.face_in_sprite.w / 2, face_cy = actor.face_in_sprite.y + actor.face_in_sprite.h / 2;
            const float margin = 0.2f * now.face_width;
            box_blur_region(frame, static_cast<int>(now.center_x - face_cx * scale - margin), static_cast<int>(now.center_y - face_cy * scale - margin),
                            static_cast<int>(now.center_x + (actor.sprite.width - face_cx) * scale + margin),
                            static_cast<int>(now.center_y + (actor.sprite.height - face_cy) * scale + margin), nx, ny);
        }
    }
    gaussian_blur(frame, look->blur_sigma);
    apply_light_and_noise(frame, *look, time_ms, frame_index);
}

}  // namespace fa
