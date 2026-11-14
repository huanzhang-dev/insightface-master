# Test data generators (`src/testdata`)

**What this is for.** The project has no recorded camera footage and no database of real registered people, so it makes its own test data. (Footage recorded by the inspection camera was looked for on the development PC and not found; the built-in camera of the PC offers 1280x720 at most. The 1920x1080 "camera" footage is therefore a **simulation**: see `CameraLook` below. It is **not** real camera footage, and real footage from the inspection camera is still missing.) `synthetic.h/.cpp` makes *fake feature vectors* (random lists of numbers that stand in for people) so that the matching engine and the database store (CSV and SQLite) can be tested for speed and correctness with thousands of registered people. `scene.h/.cpp` makes *fake video*: real portrait photographs are cut out, pasted onto a plain background and moved, scaled and tilted frame by frame, so that the detector, tracker and the whole pipeline can be tested on people "walking toward the camera" whose exact face positions are known. Optionally (`CameraLook`) the picture is given the look of a camera in a room: static furniture, a window, a door and a notice board behind the people, uneven light with darker corners, a slightly soft lens, motion blur of people who move fast, and sensor noise. Both generators are deterministic: the same inputs always give the same output. What they can show is limited. The synthetic features say nothing about how good a real face model is. The scene generator can only move, scale and tilt a flat photo **in the picture plane**: the only head rotation it can create is *roll* (tilting the head toward a shoulder). It cannot turn a head left/right (yaw) or nod it (pitch), so yaw and pitch gating is tested elsewhere with synthetic landmarks (`tests/test_detect_units.cpp`). Accuracy on real faces is measured by `tools/eval_features.cpp`; see `docs/RESULTS.md` for numbers.

Words used below.

| Term | Meaning |
|---|---|
| Feature | A list of 128 to 512 numbers, scaled to length 1, that describes a face. Two features of the same person point in nearly the same direction. |
| Cosine similarity | How alike two features are: 1 means the same direction, 0 means unrelated, -1 means opposite. For features of length 1 it is simply the dot product. |
| Ground truth | The correct answer, known because the generator placed the face itself. |
| Sprite | The cut-out piece of a portrait photo (head and shoulders) that is pasted into the video frame. |
| Roll | Head tilt in the picture plane; positive means clockwise as seen on the screen (same convention as `pose.h`). |

## src/testdata/synthetic.h and src/testdata/synthetic.cpp

How the fake people are made: every registered "person" is a random direction in a 128 to 512 dimensional space (a random unit vector); a new camera image of the same person is that vector plus some noise; a stranger is a fresh random vector. The size of the noise decides how similar two images of one person are.

| Function | Purpose |
|---|---|
| `normalize` | Scale a feature to length 1, in place. |
| `random_feature` | A random feature of length 1, all directions equally likely. |
| `noisy_copy` | A noisy second image of the same person. |
| `make_synthetic_gallery` | A list of `count` random people with ids `P00000`, `P00001`, ... |

### `normalize`

```cpp
void normalize(Feature& f);
```

**What it does.** Divides every number of `f` by the length of the vector, so that the length becomes 1. An all-zero vector stays all zero. It changes `f` in place and returns nothing.

**How it works.** It sums the squares in a `double`, then multiplies every value by `1 / sqrt(sum)` (or by 0 when the sum is 0). The same operation exists in the feature engine as `normalize_feature` in `src/feature/extractor.cpp`; the two are separate copies. The comment in `synthetic.h` says the same ("Scale `f` (in place) to length 1").

**Used by.** `random_feature` and `noisy_copy` in `synthetic.cpp`. No other file calls this one.

### `random_feature`

```cpp
Feature random_feature(std::mt19937& rng, int dim);
```

**What it does.** Returns a feature with `dim` numbers and length 1 that points in a random direction. `rng` is the caller's random generator: using the same generator state gives the same feature, which makes tests repeatable.

**How it works.** Each of the `dim` numbers is drawn from a normal (Gauss) distribution with mean 0 and spread 1; a vector of independent normal numbers points in every direction with equal probability. It is then normalized. Note on how unrelated people look: the cosine similarity of two such random vectors is close to 0 and its spread (standard deviation) is about `1 / sqrt(dim)`, which is about 0.088 for 128 numbers, 0.0625 for 256 and 0.044 for 512 (a calculation, not a measurement). `tests/test_match.cpp` calls this size one "spread".

**Used by.** `make_synthetic_gallery`; `tests/test_match.cpp` (`test_matching`, `test_m_to_n_shapes`, `test_store`, `test_card_first`, `test_database`, `test_format_choice`: strangers, speed tests, queries, store records, cards); `tools/bench_match_part.h` (`bench_matching`, query features for the matching benchmark; run by `bench` and `bench_match`).

### `noisy_copy`

```cpp
Feature noisy_copy(const Feature& feature, float noise, std::mt19937& rng);
```

**What it does.** Makes a second, slightly different "image" of the same person: the result is `feature` plus random noise, scaled back to length 1. The parameter `noise` is the *length of the noise compared to the length 1 of the feature*. `noise = 0` returns the same direction; larger values give a less similar copy. The input is not changed.

**How it works.** Each number of a copy of `feature` gets `(noise / sqrt(dim)) * gauss(rng)` added, so that the added noise vector has a length of about `noise` (the average of `dim` squared Gauss numbers is 1; the noise numbers are independent, so the length is `noise` on average, not exactly, as the comment in `synthetic.h` says). Then the result is normalized. Because the noise points in a random direction and the space has many dimensions, the noise is almost exactly at a right angle to the original feature. The two vectors form a right-angled triangle: original (length 1) plus noise (length `noise`) gives a vector of length `sqrt(1 + noise^2)`, and the cosine of the angle between the result and the original is "near side divided by long side":

`cosine similarity  ~  1 / sqrt(1 + noise^2)`

| `noise` | Cosine similarity of the copy with the original | Source |
|---|---|---|
| 0.5 | about 0.89 | stated in `synthetic.h`; formula gives 0.894 |
| 1.0 | about 0.71 | stated in `synthetic.h`; formula gives 0.707 |
| 1.5 | about 0.55 | stated in `synthetic.h` ("typical of a difficult real image"); formula gives 0.555 |
| 0.25 | about 0.97 | formula only (0.970) |
| 2.0 | about 0.45 | formula only (0.447) |
| 3.0 | about 0.32 | formula only (0.316) |

The table is a geometric calculation, accurate for the high-dimensional features used here, with small random deviations for each individual draw. It is not a measurement of any face model. If two *different* noisy copies of the same person are compared with each other (not with the original), both contain independent noise, and the same reasoning gives about `1 / (1 + noise^2)`, for example about 0.5 for `noise = 1.0` (calculation only). With the same formula a probe reaches a similarity threshold `t` only while `1 / sqrt(1 + noise^2) >= t`, that is `noise <= sqrt(1 / t^2 - 1)`: about 1.73 for `t = 0.50` and about 2.29 for `t = 0.40`, the two values used by the shipped profiles (`recognizer.threshold` in the `.ini` files). This is a calculation only.

**Used by.** `tests/test_match.cpp` (`test_matching`: queries with `noise = 1.0`, "cosine ~0.71", for the M:N versus reference comparison and for the 2000 genuine probes; `test_m_to_n_shapes` (noise 0.3) and `test_card_first` (noise 1.0 for the queries of `score_matrix`, noise 0.3 for the faces of the card-first comparisons) use it too, for queries that belong to registered people).

### `make_synthetic_gallery`

```cpp
std::vector<Record> make_synthetic_gallery(int count, int dim, uint32_t seed);
```

**What it does.** Makes `count` imaginary registered people, each with a random feature of `dim` numbers. The ids are `P00000`, `P00001`, ... (letter P and five digits) and the names are `Person 0`, `Person 1`, .... The `seed` selects the random sequence: the same `count`, `dim` and `seed` always give identical records. A `Record` (from `record.h`) holds `id`, `name`, `feature` and `card_id`; the synthetic records have no RF card (`card_id` is empty). Because `random_feature` returns vectors of length 1, every record is accepted by `FeatureStore::add`, which refuses features that are not finite vectors of length 1.

**How it works.** It creates a `std::mt19937` random generator from `seed`, reserves space for `count` records and fills each record in a loop (`snprintf` with `"P%05d"` for the id, `random_feature` for the feature). With more than 99,999 people the id simply grows to six digits.

**Used by.** `tools/gen_features.cpp` (`main`, writes them to a database file: CSV, or SQLite when the file name is `.db`, `.sqlite` or `.sqlite3`); `tools/bench_match_part.h` (`bench_matching`, matching benchmark with 10,000 people; run by `bench` and `bench_match`); `tests/test_match.cpp` (`test_matching`, seed 12345, 10,000 people; `test_m_to_n_shapes`, galleries of 0 to 100 people; `test_card_first`, 200 people; `test_format_choice`, 60 people; `test_database_speed`, 10,000 people with 512 numbers).

## src/testdata/scene.h and src/testdata/scene.cpp

The scene is built like this. The background is a smooth gray-blue vertical gradient with a little noise; with a `CameraLook` a room is drawn over it (see `draw_room`). Every actor is a portrait photo (head and shoulders), cut out with soft edges and pasted into the frame. With a `CameraLook`, the lens, the light and the sensor are applied to the finished picture at the end. The actor follows a *path* of key frames: where the face is, how wide it is (a person walking toward the camera makes the face grow) and how much the head is tilted. A person who has just arrived tilts the head and then looks straight at the camera. Because the generator places every face itself, the exact face box of every frame is known (*ground truth*), and detectors and trackers can be checked against it.

What is and is not simulated:

| Simulated | Not simulated |
|---|---|
| Movement of the face in the picture (position changes over time) | Any 3-D head rotation: **no yaw** (turning left/right) and **no pitch** (nodding); a flat photo cannot be turned in depth |
| Growing of the face (walking toward the camera), as a uniform scale of the photo | Perspective, changing viewing angle, different parts of the head becoming visible |
| **In-plane roll** (tilting the head toward a shoulder, clockwise positive) | Changes of expression, blinking, mouth movement: the face is one still photo |
| Several people at once; people appearing and disappearing at given times | One person hiding another: later actors in the list are simply drawn on top (no depth) |
| Soft pasted edge; a fresh random noise pattern per frame in the background | **Without a `CameraLook`:** lighting changes, motion blur, camera noise of a real sensor, lens effects, a room behind the people. **Never:** shadows of the people |
| **Only with a `CameraLook`:** a static room (furniture, window, door, notice board), uneven light with darker corners, slow exposure changes, a warm colour cast, a soft lens, motion blur of a person who moves fast enough, sensor noise | **Even with a `CameraLook`:** a real room, a real lens and a real sensor (every effect is a simple formula applied to the same flat photos), glasses, hats, masks, one person hiding another, backlight (a bright window behind a person), camera shake, auto-focus hunting, rolling shutter, a real camera's colour processing |

| Function | Purpose |
|---|---|
| `PathPoint` (struct) | One key frame of an actor's path. |
| `Actor` (struct) | A person in the scene: sprite, face box inside the sprite, path, time window. |
| `TruthFace` (struct) | The ground truth of one actor in one frame. |
| `CameraLook` (struct) | The settings of the simulated camera and room: clutter, noise, blur, motion blur, vignette, uneven light, exposure drift, colour cast, frame time, seed. |
| `kPi` (constant) | The value of pi used in this file (anonymous namespace). |
| `interpolate` | Value of a path at an arbitrary time (anonymous namespace). |
| `smoothstep` | Smooth 0-to-1 ramp used for the soft edge (anonymous namespace). |
| `clamp8` | Round a number and limit it to 0..255 (anonymous namespace). |
| `fill_rect` | Fill a rectangle with a colour whose brightness changes from its top edge to its bottom edge (anonymous namespace). |
| `shade_rect` | Make a rectangle brighter or darker by a number of levels (anonymous namespace). |
| `draw_room` | Draw the static room behind the people: wall panels, floor, window, door, furniture, notice board (anonymous namespace). |
| `gaussian_blur` | Blur the whole picture a little, as a lens does (anonymous namespace). |
| `box_blur_region` | Smear a rectangle horizontally and vertically: the motion blur of one person (anonymous namespace). |
| `apply_light_and_noise` | Uneven light, vignette, exposure drift, colour cast and sensor noise (anonymous namespace). |
| `make_actor` | Build an `Actor` from a portrait photo and the face box found in it. |
| `approach_path` | Make a three-key-frame path of a person walking toward the camera. |
| `render_frame` | Draw the whole scene at one time (optionally with the camera look) and report the ground truth. |

### `PathPoint` (struct)

```cpp
struct PathPoint {
    double time_ms = 0;
    float center_x = 0, center_y = 0;
    float face_width = 100;
    float roll_deg = 0;
    PathPoint() {}
    PathPoint(double time_ms_, float center_x_, float center_y_, float face_width_, float roll_deg_);
};
```

**What it does.** One key frame of an actor's path: at time `time_ms` (milliseconds since the start of the video) the middle of the face is at (`center_x`, `center_y`) in the video frame (pixels), the face box is `face_width` pixels wide (default `100`) and the head is tilted by `roll_deg` degrees (clockwise positive, default `0`).

**How it works.** A plain struct with two constructors: `PathPoint()` keeps the default values, and the constructor with five arguments sets the five members in the order they are declared, so callers can write a path point as `{time, x, y, width, roll}`, as `eval_detect`, `roll_probe` and the benchmark header `bench_frames.h` do for a one-point path. (The constructor is there because Visual Studio 2015 does not accept braces with values for a struct that has default member values; see `docs/VISUAL_STUDIO.md`.)

**Used by.** `Actor::path`; `interpolate`, `approach_path` and `render_frame` in `scene.cpp`; one-point paths in `tools/eval_detect.cpp`, `tools/roll_probe.cpp`, `tools/bench_frames.h` (`make_frame`, line 30).

### `Actor` (struct)

```cpp
struct Actor {
    std::string label;
    Image sprite;
    Rect face_in_sprite;
    std::vector<PathPoint> path;
    double appear_ms = 0, disappear_ms = 1e12;
};
```

**What it does.** A person in the scene. `label` is a name used in the ground truth (for example the person id). `sprite` is the cut-out head-and-shoulders picture; `face_in_sprite` is where the face box lies inside the sprite. `path` holds the key frames and must have at least two points ordered by time (a single point also works and means "stands still"; an **empty** path means the actor is never drawn). The actor exists only between `appear_ms` (default `0`) and `disappear_ms` (default `1e12`, practically never).

**How it works.** A plain struct filled by `make_actor` (sprite, face box, label) and by the caller (path and times).

**Used by.** `make_actor`, `render_frame`; created and used in `tools/gen_video.cpp`, `tools/eval_detect.cpp`, `tools/roll_probe.cpp`, `tools/bench_frames.h` (`make_frame`) and `tests/test_pipeline.cpp`.

### `TruthFace` (struct)

```cpp
struct TruthFace {
    std::string label;
    Rect box;
    float roll_deg = 0;
    bool visible = false;
};
```

**What it does.** The ground truth of one actor in one frame: the actor's `label`, the exact face `box` in video-frame pixels, the head tilt `roll_deg` at that time, and `visible`. `visible` is `false` when the actor is not in the scene at that time or when the face box is not completely inside the frame. For an actor that is not present, `box` is all zeros.

**How it works.** A plain struct filled by `render_frame`. When the head is tilted, the box is the smallest *upright* rectangle that contains the tilted face rectangle, so it is slightly larger than a face that is not tilted (see `render_frame`).

**Used by.** `render_frame` fills it; read in `tools/eval_detect.cpp` (`truth[0].box`), `tools/gen_video.cpp` (writes `truth.csv`) and `tests/test_pipeline.cpp` (`run_video`).

### `CameraLook` (struct)

```cpp
struct CameraLook {
    bool clutter = true;
    float noise_sigma = 4.0f;
    float blur_sigma = 0.9f;
    float motion_blur = 0.4f;
    float vignette = 0.25f;
    float light_slope = 0.20f;
    float exposure_drift = 0.06f;
    float warm = 0.06f;
    double frame_ms = 1000.0 / 15.0;
    int seed = 1;
};
```

**What it does.** The settings of the **simulated** camera and room that `render_frame` applies when it is given a pointer to one. It is a **simulation**: no camera is involved, and every effect is a simple formula applied to the same flat photos. Without it the scene is a smooth gradient with a little noise, which is much easier than real footage. Every effect is deterministic (seeded by `seed` and the frame index), and none of them moves a face: the ground-truth boxes are exactly the same with and without the look (a check of `test_common`). The H.264 compression of a video (a lower bitrate gives visible blocks) comes on top of this and is chosen when the video is written (`gen_video --camera-look` uses 4000 kbit/s instead of 8000).

| Member | Default | Meaning |
|---|---|---|
| `clutter` | `true` | Draw the room (`draw_room`): furniture, a window, a door and a notice board with text-like lines behind the people, the same in every frame of a video. |
| `noise_sigma` | `4.0` | Sensor noise, in 8-bit levels, at a mid-gray pixel (about 1.5% of the range). The noise grows with the brightness, like shot noise: stronger in bright pixels in levels, weaker in dark ones (`apply_light_and_noise`). 0 switches the noise off. |
| `blur_sigma` | `0.9` | Lens softness: the standard deviation, in pixels, of a Gaussian blur of the whole picture (`gaussian_blur`). Below 0.3 there is no blur. |
| `motion_blur` | `0.4` | The exposure as a fraction of the frame time: a person whose face moves `m` pixels between two frames is smeared over `0.4 * m` pixels (`box_blur_region`). 0 switches it off. |
| `vignette` | `0.25` | The corners are darker by this fraction (0.25 means 25% darker in the corner). 0 = none. |
| `light_slope` | `0.20` | The left edge is darker than the right edge by this fraction (uneven light). 0 = even light. |
| `exposure_drift` | `0.06` | The brightness of the whole picture wanders by up to this fraction (plus and minus 6%) over a few seconds, as an auto-exposure does. 0 = steady. |
| `warm` | `0.06` | Colour cast: red is multiplied by `1 + warm` and blue by `1 - warm`. 0 = none. |
| `frame_ms` | `1000 / 15` | The time between two frames in milliseconds. Only the motion blur needs it (it turns a speed in pixels per millisecond into pixels per frame). |
| `seed` | `1` | Another seed gives another room (`draw_room`) and other noise (`apply_light_and_noise`, together with the frame index). |

**How it works.** A plain struct with default member values; nothing is checked (a negative value is not rejected). With every effect off (`clutter = false`, the noise, blur, motion blur, vignette, slope, drift and cast all 0) `render_frame` returns the same picture as without a look; `tests/test_common.cpp` builds such a "quiet" look and switches on one effect at a time to show that each effect is really there.

**Used by.** `render_frame` (reads every member); `tools/gen_video.cpp` (`main`: with `--camera-look` it passes a `CameraLook` whose `frame_ms` is `1000 / fps` and whose `seed` is `7 + people * 13 + first portrait index`, so that another group of people gets another room; the other members keep their defaults); `tests/test_common.cpp` (`main`, the "camera look" checks).

### `kPi`

```cpp
const float kPi = 3.14159265358979f;
```

**What it does.** The constant pi, used to convert the roll from degrees to radians, and to make the slow brightness waves of the exposure drift.

**How it works.** A file-local constant in the anonymous namespace. (`pose.cpp` has its own copy.)

**Used by.** `render_frame` (the roll), `apply_light_and_noise` (the exposure drift).

### `interpolate`

```cpp
PathPoint interpolate(const std::vector<PathPoint>& path, double time_ms);
```

**What it does.** Returns where the actor is, how wide the face is and how tilted the head is at `time_ms`, by drawing a straight line between the two key frames around that time. Before the first key frame it returns the first point, after the last one the last point (no extrapolation). `path` must not be empty.

**How it works.** If there is one point, or the time is before the first or after the last key frame, it returns the first or last point. Otherwise it finds the first key frame `b` whose time is not before `time_ms`, computes `k = (time - a.time) / (b.time - a.time)` with `a` the key frame before `b`, and blends `center_x`, `center_y`, `face_width` and `roll_deg` as `a + (b - a) * k`. The path times must be in increasing order; this is not checked.

**Used by.** `render_frame`, after it has made sure that the path is not empty.

### `smoothstep`

```cpp
float smoothstep(float edge0, float edge1, float x);
```

**What it does.** A smooth ramp: 0 when `x <= edge0`, 1 when `x >= edge1`, and an S-shaped curve in between. It is used to fade the sprite's edge.

**How it works.** `t = clamp((x - edge0) / (edge1 - edge0), 0, 1)`, result `t * t * (3 - 2 * t)`. The comment in the code ("0 inside, 1 outside") describes how it is used with `edge0 = 0.8`, `edge1 = 1.0`.

**Used by.** `render_frame` (soft elliptical edge).

### `clamp8`

```cpp
inline uint8_t clamp8(float v);
```

**What it does.** Turns a number into a colour value: rounds it to the nearest whole number and limits it to 0..255 (a negative number gives 0, a number above 255 gives 255). It returns the value as one byte.

**How it works.** `static_cast<uint8_t>(min(255, max(0, v + 0.5)))`. Adding 0.5 before the conversion, which cuts off the decimals, rounds to the nearest.

**Used by.** `fill_rect`, `shade_rect`, `box_blur_region`, `apply_light_and_noise` (all in `scene.cpp`).

### `fill_rect`

```cpp
void fill_rect(Image& f, float x0, float y0, float x1, float y1, int b, int g, int r, float top_shade, float bottom_shade);
```

**What it does.** Paints a filled rectangle into the picture `f`: the pixels from column `x0` up to (not including) `x1` and from row `y0` up to `y1`. The colour is (`b`, `g`, `r`) (blue, green, red, as `Image` stores them), multiplied by a brightness factor that goes in a straight line from `top_shade` in the top row of the rectangle to `bottom_shade` in the bottom row (1.0 = the colour as given). The old picture content in the rectangle is replaced (no blending). The part outside the picture is cut off; the coordinates are cut to whole numbers.

**How it works.** The limits are clipped to the picture (`max(0, int(x0))` and `min(width, int(x1))`, the same for the rows). For every row `y` the factor is `top_shade + (bottom_shade - top_shade) * k` with `k = (y - y0) / (y1 - y0)` (0 when the rectangle has no height); `k` is measured from the rectangle's own `y0`, also when the top of the rectangle lies above the picture. Each of the three values is multiplied by the factor and written with `clamp8`.

**Used by.** `draw_room` (floor, window frame, glass, cross bars, door and its handle, furniture, notice board and its text-like lines).

### `shade_rect`

```cpp
void shade_rect(Image& f, float x0, float y0, float x1, float y1, int delta);
```

**What it does.** Makes the rectangle brighter (`delta` above 0) or darker (`delta` below 0) by `delta` levels: the number is added to the blue, green and red value of every pixel inside, and the result is limited to 0..255. The rectangle is clipped to the picture the same way as in `fill_rect`.

**How it works.** A loop over the rows and over the values of the clipped columns, `row[x] = clamp8(row[x] + delta)`. The pixels keep their texture and noise (unlike `fill_rect`, which replaces them).

**Used by.** `draw_room` (wall panels of slightly different brightness, the skirting board, the lit top edge of the furniture).

### `draw_room`

```cpp
void draw_room(Image& frame, const CameraLook& look);
```

**What it does.** Draws a room behind the people, into a picture that already holds the gradient background: wall panels, a floor, a skirting board, a window, a door, four pieces of furniture and a notice board with lines that look like printed text. These are shapes with sharp edges and strong contrast that a face detector has to ignore, which the plain gradient does not offer. The room is **static**: it depends only on `look.seed` and the picture size, so it is the same in every frame of a video (the noise of the sensor is added later and differs from frame to frame). People are pasted over it afterwards. Positions and sizes are fractions of the picture width `W` and height `H`, so the room scales with the picture size.

**How it works.** A `std::mt19937` random generator is seeded with `seed * 7919 + 13`, and a small helper `uni(a, b)` draws a random number between `a` and `b`; the order of the draws is fixed, so the same seed always gives the same room. The floor line is at `0.80 H`. In this order:
1. Four wall panels: each starts at a random column between 0 and `0.8 W`, is `0.12 W` to `0.30 W` wide, reaches from the top down to the floor line, and is made 16 levels brighter or darker at most (`shade_rect`).
2. The floor, from the floor line to the bottom, colour (blue 92, green 112, red 130), brightness 1.0 at the top to 0.62 at the bottom (darker toward the camera); a skirting board 6 rows high just above it, 30 levels darker.
3. A window: its left edge between `0.05 W` and `0.55 W`, `0.12 W` to `0.20 W` wide, top between `0.06 H` and `0.12 H`, `0.25 H` to `0.33 H` high; a dark frame 8 pixels wide around bright glass (brightness 1.0 to 0.82 from top to bottom) and a cross bar 6 pixels wide, horizontal and vertical.
4. A dark door, `0.09 W` wide, between `0.62 W` and `0.86 W` from the left, from `0.22 H` down to the floor line, with a small light handle.
5. Four pieces of furniture along the floor line: each starts between 0 and `0.9 W`, is `0.05 W` to `0.12 W` wide and `0.08 H` to `0.20 H` high, with a random brown-gray shade (60 to 140), reaching `0.04 H` below the floor line; the top edge is 40 levels brighter over 5 rows.
6. A notice board, `0.10 W` wide and `0.14 H` high, its left edge between `0.10 W` and `0.55 W` and its top between `0.38 H` and `0.48 H`, light, with dark horizontal bars 5 rows high every 14 rows (the first 12 rows below the top edge). Each bar starts 10 pixels from the left edge and ends at a random distance from the right edge, between 10 pixels and 10 pixels plus 40% of the board's width, so that the bars look like lines of printed text.

**Used by.** `render_frame`, right after the background gradient, only when a `CameraLook` is given and its `clutter` is `true`.

### `gaussian_blur`

```cpp
void gaussian_blur(Image& img, float sigma);
```

**What it does.** Blurs the whole picture a little, in place, the way a lens and the image processing of a camera never give a perfectly sharp picture. `sigma` is the width of the blur in pixels; a value below 0.3 does nothing (the function returns at once). The default `CameraLook::blur_sigma` is 0.9.

**How it works.** A Gaussian blur done in two passes: one along the rows and then one along the columns (a separable filter). The weights are whole numbers that add up to exactly 1024 (the middle weight takes up the rounding difference, so the brightness is not changed); the radius is `ceil(2.5 * sigma)` pixels, at least 1 (3 pixels, 7 weights for 0.9). At the edges of the picture the nearest edge pixel is used. The horizontal pass reads the picture and writes a copy (`tmp`), the vertical pass reads the copy and writes back. The sum is divided by 1024 by a shift, which cuts the decimals off instead of rounding, so the picture becomes darker by at most one level (half a level on average).

**Used by.** `render_frame` (after the motion blur, before the light and the noise, so the noise is not blurred).

### `box_blur_region`

```cpp
void box_blur_region(Image& img, int x0, int y0, int x1, int y1, int nx, int ny);
```

**What it does.** Smears the rectangle from column `x0` up to `x1` and from row `y0` up to `y1`, in place: horizontally over `nx` pixels and vertically over `ny` pixels. This is the motion blur of a person who moves while the shutter is open. The rectangle is clipped to the picture. Nothing happens when the clipped rectangle is less than 2 pixels wide or high, or when both `nx` and `ny` are below 2; a length of 0 or 1 in one direction leaves that direction sharp.

**How it works.** The rectangle is copied into a float array. A box filter (every pixel becomes the average of `nx` neighbours, centred on it: from `x - nx/2` for `nx` pixels) is applied along the rows, then one of `ny` pixels along the columns; neighbours outside the rectangle are replaced by the nearest pixel of the rectangle. The result is written back with `clamp8`. Only the pixels of the rectangle change; the picture around it is untouched.

**Used by.** `render_frame` (one call for each actor that moves fast enough).

### `apply_light_and_noise`

```cpp
void apply_light_and_noise(Image& frame, const CameraLook& look, double time_ms, int frame_index);
```

**What it does.** The last step of the camera look, in place, on the finished picture: uneven light (the left side darker than the right), darker corners (vignette), the slow brightness changes of an auto-exposure, a warm colour cast, and the noise of a sensor.

**How it works.** Every pixel is multiplied by one factor `f = col_factor[x] * (1 - vignette * r2) * exposure`:
* `col_factor[x] = 1 - light_slope * (1 - x / W)` (a table made once per frame, as is `col_dx2`, the squared horizontal distance from the middle): the left edge is darker by `light_slope` (0.8 for 0.20), the right edge has no change;
* `r2 = (dx*dx + dy*dy) / 2` with `dx` and `dy` the distances from the picture middle in units of half the width and half the height (so the corner has `r2 = 1`, the middle of the top edge 0.5), which makes the corners darker by `vignette`;
* `exposure = 1 + exposure_drift * (0.7 * sin(2 pi t / 7000 + 0.7) + 0.3 * sin(2 pi t / 2300 + 2.1))` with `t = time_ms`: two slow waves of 7 s and 2.3 s, the same for every pixel of a frame, at most plus or minus `exposure_drift`.

Then blue is multiplied by `1 - warm` and red by `1 + warm` (green stays), which gives the warm cast. If `noise_sigma` is above 0 the noise is added: with `mean` the average of the three values, the noise width is `sigma = noise_sigma * sqrt((mean + 8) / 136)` (equal to `noise_sigma` at a mean of 128, larger for bright pixels, smaller for dark ones); a random number with the width `sigma` is added to all three values (a noise of brightness) and one more random number with the width `0.5 * sigma` to each value alone (so the noise is partly different in each colour). The random generator is seeded with `seed * 104729 + frame_index * 7919 + 17`: the same seed and frame index give exactly the same noise, another frame index another pattern. The result is written with `clamp8`.

**Used by.** `render_frame`, the last step when a `CameraLook` is given.

### `make_actor`

```cpp
Actor make_actor(const std::string& label, const Image& portrait, const Rect& face);
```

**What it does.** Makes an `Actor` from a portrait photo. `face` is the face box in that photo, normally found by a detector (the callers pass the first detected face, `found[0].box`; `tests/test_pipeline.cpp` passes the largest one) or otherwise known. The returned actor has `label`, the sprite and `face_in_sprite`; its `path` is empty, so the caller must set `actor.path` before the actor can appear. `appear_ms` and `disappear_ms` keep their defaults.

**How it works.** It cuts a region around the face out of the portrait: 0.9 face widths to each side, 0.8 face heights above the face and 1.6 below it (hair, neck and shoulders), i.e. `x - 0.9 w`, `y - 0.8 h`, size `2.8 w` by `3.4 h`. The region is clipped to the portrait (near an edge the sprite is smaller), copied with `crop` (from `image_ops.h`) into `sprite`, and `face_in_sprite` is the face box minus the region's top-left corner.

**Used by.** `tools/gen_video.cpp`, `tools/eval_detect.cpp`, `tools/roll_probe.cpp`, `tools/bench_frames.h` (`make_frame`, line 29; used by the programs `bench`, `bench_detect` and `bench_extract`) and `tests/test_pipeline.cpp` (`main`).

### `approach_path`

```cpp
std::vector<PathPoint> approach_path(double duration_ms, float start_x, float start_y, float end_x, float end_y,
                                     float width_start, float width_end, float roll_start, double settle_ms);
```

**What it does.** Builds the path of a person who walks toward the camera. Over `duration_ms` the middle of the face moves in a straight line from (`start_x`, `start_y`) to (`end_x`, `end_y`) while the face grows from `width_start` to `width_end` pixels. The head is tilted by `roll_start` degrees at the start and straightens linearly until it is straight (0 degrees) at `settle_ms`; from then on it stays at 0. Typical use: a person arrives tilted and then looks straight at the camera.

**How it works.** It returns three key frames. A: time 0, start position, `width_start`, `roll_start`. B: time `settle_ms`, roll 0, and position and width taken on the straight line at the fraction `k = settle_ms / duration_ms`. C: time `duration_ms`, end position, `width_end`, roll 0. Since `render_frame` blends linearly between key frames, the roll goes from `roll_start` to 0 between A and B and stays 0 between B and C, while position and width change at a steady rate over the whole walk. The caller must give `duration_ms > 0` and `0 <= settle_ms <= duration_ms`; nothing is checked.

**Used by.** `tools/gen_video.cpp` (`main`) and `tests/test_pipeline.cpp` (`main`, every video).

### `render_frame`

```cpp
void render_frame(int width, int height, double time_ms, int frame_index, const std::vector<Actor>& actors,
                  Image& frame, std::vector<TruthFace>& truth, const CameraLook* look = nullptr);
```

**What it does.** Draws the whole scene at time `time_ms` into `frame` (a new 3-channel BGR image of `width` x `height` pixels) and fills `truth` with one `TruthFace` for every actor, in the same order as `actors`. `frame_index` seeds the background noise (and, with a look, the sensor noise): the same `frame_index` gives the same noise pattern, and a different one gives a new pattern, so consecutive frames are not pixel-identical in the background even when nothing moves. An actor outside its `appear_ms`..`disappear_ms` window, or with an empty path, is not drawn and gets a `TruthFace` with only its label and `visible = false`. Actors drawn later are painted over earlier ones.

The last parameter, `look`, is new and optional. A null pointer (the default) gives the plain test scene described below, exactly as before. A pointer to a `CameraLook` gives the **simulated camera picture**: a room behind the people (step 1b) and the lens, the light, the motion blur and the sensor noise on top of everything (step 9). The ground truth is the same with and without it: the boxes are computed from the paths alone, before any of those effects, and the effects never move a face.

**How it works.**
1. **Background.** `frame.create(width, height, 3)`, then for every pixel a base value `150 - 50 * y / height` (darker toward the bottom) is turned into blue `base + 20`, green `base + 8` and red `base - 10` (which gives the gray-blue look), each plus a random whole number from -3 to 3 (from `std::mt19937` seeded with `1000 + frame_index`) and clamped to 0..255.
1b. **The room (only with a look whose `clutter` is true).** `draw_room(frame, *look)` draws the static room over the background.
2. **Actor position.** For each actor that exists at `time_ms`, `interpolate` gives the path values `p`. The scale factor is `p.face_width / face_in_sprite.w` (one factor for both directions, so the photo is not distorted), and the roll is converted to radians.
3. **Area to visit.** Only pixels in a square around the face centre are processed. The code computes the sprite size in frame pixels (its variables are called `half_w` and `half_h`, but they hold the full sprite width and height), takes the diagonal of that size as `radius`, and visits the square from `center - radius` to `center + radius` in both directions, clipped to the frame. This is a safe over-estimate of where the sprite can reach.
4. **Pixel mapping (backwards).** For each frame pixel it asks "which sprite pixel lands here?": it subtracts the face centre, undoes the tilt (rotation by minus the roll), divides by the scale and adds the face-centre position inside the sprite. Pixels that map outside the sprite (keeping a one-pixel margin) are skipped.
5. **Soft edge.** The sprite coordinates are normalized to -1..1 across the sprite width and height, and `r` is the distance from the sprite middle in these coordinates (so the shape is an ellipse); `alpha = 1 - smoothstep(0.80, 1.0, r)`. The photo is therefore fully opaque inside `r = 0.8`, fades out until `r = 1.0` (the middle of each side of the sprite) and is not drawn outside; the four corners of the sprite are never visible.
6. **Colour.** The sprite is sampled with bilinear interpolation (a weighted mix of the four nearest pixels) and blended onto the background with `alpha`.
7. **Ground truth.** The tilted face rectangle has width `fw = p.face_width` and height `fh = face_in_sprite.h * scale`. The reported box is the upright rectangle around it: half-width `(|cos| * fw + |sin| * fh) / 2` and half-height `(|sin| * fw + |cos| * fh) / 2`, centred on (`center_x`, `center_y`). With roll 0 this is exactly the face box. `roll_deg` is copied from the path, and `visible` is true only if the whole box lies inside the frame (a face that is partly outside is still drawn, but reported as not visible).
8. The `TruthFace` is appended to `truth` (which was cleared at the start).
9. **The camera (only when `look` is not null; the function returns right after step 8 otherwise).** Three things happen to the finished picture, in this order:
   * **Motion blur of the people who move** (when `look->motion_blur > 0`). For each actor that exists at `time_ms`, the speed of the middle of his face is taken from his path: the positions at `time_ms - 5` ms (not before 0) and `time_ms + 5` ms are read with `interpolate` and divided by 10 ms, which gives `vx` and `vy` in pixels per millisecond. The smear lengths are `nx = round(|vx| * frame_ms * motion_blur)` and `ny = round(|vy| * frame_ms * motion_blur)` pixels: the distance the face moves between two frames, times the exposure fraction. When both are below 2 the actor is not smeared (a person who stands still, or moves slowly, stays sharp). Otherwise `box_blur_region` smears the rectangle around his sprite (the sprite's place in the frame, plus 0.2 face widths on every side for the tilt) horizontally by `nx` and vertically by `ny` pixels. Everything inside that rectangle is smeared, the background and a neighbour's edge too.
   * **Lens softness:** `gaussian_blur(frame, look->blur_sigma)` blurs the whole picture.
   * **Light and sensor:** `apply_light_and_noise(frame, *look, time_ms, frame_index)`.

Direction of the tilt: for positive roll the picture turns clockwise on the screen, the same convention as `pose.h`. Roll is the only rotation implemented (see the table at the top of this section).

**Used by.** `tools/gen_video.cpp` (`main`, writes the frames and `truth.csv`; passes a `CameraLook` with `--camera-look`); `tools/eval_detect.cpp` (`main`, also one frame with no actors to reset the Haar detector); `tools/roll_probe.cpp` (`main`); `tools/bench_frames.h` (`make_frame`, line 36); `tests/test_pipeline.cpp` (`run_video`); `tests/test_common.cpp` (`main`, the "camera look" checks, with and without a look). All callers except `gen_video` and `test_common` pass no look.

## The 1920x1080 footage of the evaluation

This is not part of `src/testdata`, but it is made from it (`approach_path` and `render_frame` through `tools/gen_video.cpp`) and the evaluation of the engines uses it, so it is described here. The camera image of the evaluation is 1920 x 1080 (Full HD), and the footage has that size: `scripts\make_fhd_footage.bat` runs `gen_video` six times and writes six videos of people walking toward the camera into `data\videos\` (the folder `data\` is ignored by git; the footage is recreated by the script). Three are the **clean** footage (the plain scene) and three are the same people and walks with the **simulated camera look** (`gen_video --camera-look`, a `CameraLook`):

| Folder | People | Registered | `gen_video` arguments | Faces at the end of the walk |
|---|---|---|---|---|
| `fhd_1person` | 1 | 1 | `8 15 1 0 1` | 340 px wide |
| `fhd_3people` | 3 | 2 (and 1 stranger) | `8 15 3 3 2` | 328 px wide |
| `fhd_5people` | 5 | 3 (and 2 strangers) | `8 15 5 8 3` | 197 px wide |
| `fhd_cam_1person` | 1 | 1 | `8 15 1 0 1 --camera-look` | 340 px wide |
| `fhd_cam_3people` | 3 | 2 (and 1 stranger) | `8 15 3 3 2 --camera-look` | 328 px wide |
| `fhd_cam_5people` | 5 | 3 (and 2 strangers) | `8 15 5 8 3 --camera-look` | 197 px wide |

Each video is 8 seconds at 15 frames per second, 120 frames of 1920 x 1080, and each folder holds `video.mp4` (H.264), the same frames as `frame_0000.jpg` ..., `truth.csv` (the exact face box of every person in every frame), `scenario.txt` (who is registered) and `mp4_first_frame_readback.png` (the check that the MP4 is the right way up). The third `gen_video` argument is the number of people, the fourth how many usable portraits are skipped first, so the three sizes of footage use different people, and a `fhd_cam_*` video shows the same people, walks and truth as the clean video of the same size (`truth.csv` is identical). The clean videos are written at 8000 kbit/s, the camera-look videos at 4000 kbit/s (`gen_video --bitrate K` changes it): the files are 4.15 to 4.24 million bytes instead of 7.03 to 7.60 million bytes, and the compression changes the picture more: the first frame read back from the MP4 differs from the original by 3.6 (camera look) against 2.4 (clean) on the 0 to 255 scale. No camera and no person was filmed: every picture is made from the public-domain portraits.

**The `fhd_cam_*` footage is a simulation of camera footage, not camera footage.** The footage recorded by the inspection camera could not be found on the development PC (Videos, Downloads, Desktop, Documents, `Z:\AST` and `D:\` were searched), and no camera was switched on to record new footage: the built-in camera of the PC offers at most 1280x720. Simulated 1920x1080 footage was made instead. Each `fhd_cam_*` video has: a static room behind the people (furniture, a window, a door, a notice board with text-like lines; the room differs between the three videos because the seed is `7 + people * 13 + first portrait index`), the left side darker than the right with darker corners, slow brightness changes, a warm colour cast, a slightly soft lens, sensor noise, and H.264 at half the bitrate. **Motion blur is part of the look but does not occur in this footage:** the people of `gen_video` walk slowly (the middle of a face moves 180 pixels in the 120 frames of the video, at most 1.5 pixels per frame; 0.9 pixels per frame in the 5-people video, 0.8 pixels per frame vertically), so the smear length `0.4 * 1.5 = 0.6` pixels rounds to 1, which is below the 2 pixels that `render_frame` needs before it smears anything (calculated from the code; the frames were not measured for it). The effect exists and is tested with a fast mover (`tests/test_common.cpp`), but it would only show in footage of people who move several pixels per frame.

**Why the faces are smaller with more people.** The picture of one person (`make_actor`) is 2.8 face widths wide (head, hair, shoulders). When two people stand closer than about 1.9 face widths apart, the picture of one covers part of the face of the other. `gen_video` therefore limits the face width at the end of the walk to `spacing / 1.95`, where `spacing` is the picture width divided by the number of people, and it starts the people far enough apart for the same reason; one or two people still reach 340 px, three 328 px and five 197 px. A first version of the 5-people footage did not do this: faces were covered by the neighbours' pictures and one registered person was detected in only 64 of his 111 visible frames. That was a flaw of the footage, not of the engines; `check_video` found it, and the layout was fixed in `gen_video`.

What the footage cannot show is the same as for the rest of the scene generator, also with the camera look: the person is a flat photo that is only tilted in the picture plane (frontal faces, no glasses, no occlusion, no backlight, no camera shake), and the registered photo is the very picture that appears in the video. So the similarity of a registered person to himself in the footage (0.91 to 0.97) measures robustness against H.264 compression, scaling, tilt and (in the `fhd_cam_*` videos) the simulated noise, softness and light, not accuracy on different photos; `tools/eval_features` measures that on separate photos (`RESULTS.md`). **What is still missing:** real footage from the inspection camera. Until it is tested, the results on the `fhd_cam_*` footage say how the engines cope with a simulated, harder picture, not how they will behave in front of the real camera.

## Where code and comments disagree

The comments of `synthetic.h` now agree with the code (`normalize` works in place; the noise vector is made of independent Gaussian numbers whose length is about `noise` on average, so each individual copy has a slightly different similarity than the table under `noisy_copy`). One naming point remains in `scene.cpp`: two variables are called `half_w` and `half_h`, but they hold the full sprite width and height in frame pixels (the effect is a larger, safe search area, so the result is correct).

(The sensor noise of the camera look grows with the brightness, like the shot noise of a real sensor: `noise_sigma * sqrt((mean + 8) / 136)` is 0.24 times `noise_sigma` at black, 1.0 at mid gray and 1.4 times at white. The comments of `scene.h` and `scene.cpp` said "stronger in the dark" earlier; they now say what the code does.)
