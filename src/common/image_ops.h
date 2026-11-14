// image_ops.h - basic image processing written by hand (no OpenCV).
//
// All functions work on fa::Image (8-bit, interleaved, BGR or gray).
#ifndef FA_IMAGE_OPS_H
#define FA_IMAGE_OPS_H

#include "types.h"

namespace fa {

// Resize with bilinear interpolation. Works for 1 or 3 channels.
// Uses fixed-point arithmetic and pre-computed column tables, so it is fast.
void resize_bilinear(const Image& src, Image& dst, int new_width, int new_height);

// Shrink/enlarge `src` so that it fits inside a size x size square WITHOUT
// changing its aspect ratio, then pad the rest with gray (114). This is how a
// 1280x1080 camera image becomes the 640x640 image the face engines need.
//   scale = factor applied to the original image
//   pad_x/pad_y = size of the left/top padding in the result
// To map a point from the square image back to the camera image:
//   original_x = (x - pad_x) / scale
void letterbox(const Image& src, int size, Image& dst, float& scale, int& pad_x, int& pad_y);

// Same, but the result is the SMALLEST rectangle that holds the shrunk picture and has both sides a multiple of
// `multiple` (32 for the face networks), not a square: a 1920x1080 picture shrunk so that its long side is 640 is
// 640x360 and becomes a 640x384 picture instead of 640x640, so a network that accepts any size does 40% less work
// at exactly the same scale. scale, pad_x and pad_y mean the same as for letterbox().
void letterbox_fit(const Image& src, int long_side, int multiple, Image& dst, float& scale, int& pad_x, int& pad_y);

// Convert a BGR image to gray (one channel).
void to_gray(const Image& src, Image& dst);

// Copy the part of `src` inside `box` into `dst`. Pixels outside `src` become 0.
void crop(const Image& src, const Rect& box, Image& dst);

// Find the similarity transform (rotation + uniform scale + shift) that moves
// the 5 points `from` as close as possible to the 5 points `to` (least squares).
// Result: matrix m = [a, -b, tx, b, a, ty] meaning
//   x' = a*x - b*y + tx
//   y' = b*x + a*y + ty
void estimate_similarity(const Point from[5], const Point to[5], float m[6]);

// Reverse of a similarity matrix: inverse[] maps x',y' back to x,y.
void invert_similarity(const float m[6], float inverse[6]);

// Produce a width x height image where each output pixel (x', y') is read from
// `src` at the position given by `dst_to_src` (bilinear interpolation).
void warp_affine(const Image& src, const float dst_to_src[6], Image& dst, int width, int height);

// Cut out the face and rotate / scale it so the 5 landmarks land on the
// standard positions of a 112x112 aligned face (the ArcFace template).
void align_face(const Image& src, const Face& face, Image& aligned);

// The standard 5 landmark positions inside a 112x112 aligned face.
const Point* aligned_template_112();

// Drawing helpers for debugging pictures (color = B, G, R).
void draw_rect(Image& image, const Rect& box, uint8_t b, uint8_t g, uint8_t r, int thickness = 2);
void draw_dot(Image& image, Point center, int radius, uint8_t b, uint8_t g, uint8_t r);

// Build the integral image of a gray image: sum[y][x] is the sum of all pixels
// above and to the left. Haar features use it to add up any rectangle in O(1).
// `sum` has (width+1)*(height+1) entries; `sqsum` holds the sums of squares.
void integral_image(const Image& gray, std::vector<int64_t>& sum, std::vector<int64_t>& sqsum);

}  // namespace fa

#endif  // FA_IMAGE_OPS_H
