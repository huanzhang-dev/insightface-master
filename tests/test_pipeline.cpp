// test_pipeline.cpp - verifies the COMPLETE authentication project on generated video.
//
//   test_pipeline [profile.ini] [--async] [--sqlite] [--size WxH]     (default profile models/permissive.ini, default size 1920x1080)
//
// --size WxH plays the videos at another picture size (default 1920x1080, Full HD).
//
// --sqlite keeps the registered people in a SQLite database (build\test_pipeline.db) instead of the default CSV file;
// everything else is the same.
//
// --async runs the pipeline with its worker thread (pose check and feature extraction off the
// main thread) and plays every video in REAL TIME, because the worker is meant to keep up with a
// camera: it checks that the same things still happen, that a frame never takes much longer than
// the detection alone, and that the worker can be reset and shut down while it has work.
//
// Data: official portraits in data/portraits (public domain, see LICENSES.csv),
// and generated 1920x1080 (Full HD, the default camera size) video of people walking toward the camera
// (src/testdata/scene.h) with known face positions.
//
// What is checked
//   - enrolling people from photos (register)
//   - a registered person approaching the camera is detected, tracked with ONE
//     track ID, gets a crop only when the head tilt is inside the tolerance,
//     is verified, and after that gets no new crop (checked by counting crops; the
//     pipeline code does not extract or match verified tracks)
//   - an unregistered person is never verified, and gets a new crop about once
//     per second
//   - two people at once are handled in the same frames
//   - a stranger who steps into the place of a verified person who left (same track ID)
//     does not inherit the verification (video 4)
//   - register from the video, delete and modify change the outcome
//   - the same walk at several camera picture sizes (640x480 ... 2560x1440, the default is 1920x1080)
//   - card first (video 6): the RF cards are looked up in the database first and the faces are compared only with the
//     features stored for those cards (N is a subset of the database; M < N, M = N and M > N; an unknown card fails)
//   - time per frame for every stage at 1920x1080
//
// NOTE: the actor is a flat photo, and the enrolled photo is the same picture,
// so this tests the system integration, not recognition accuracy. Accuracy is
// measured by eval_features on separate photos.
#include <windows.h>  // Sleep

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "fs_compat.h"
#include <map>
#include <set>

#include "auth_pipeline.h"
#include "image_io.h"
#include "image_ops.h"
#include "pose.h"
#include "scene.h"
#include "timer.h"
#include "cpu_only_check.h"

namespace fs = fa::fs;
using namespace fa;

static int g_failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }   \
        else std::printf("ok:   %s\n", msg);                              \
    } while (0)

static int kWidth = FA_CAMERA_DEFAULT_WIDTH, kHeight = FA_CAMERA_DEFAULT_HEIGHT;   // 1920x1080: Full HD, the default camera size (--size WxH changes it)
static const double kFps = 15.0;
static bool g_async = false;   // --async: the pipeline works with its worker thread and the video is played in real time
static fa::TimingStats g_all_frames;   // process() time of every frame of every video

struct Portrait {
    std::string id;
    Image image;
    Face face;   // as found by the pipeline's detector
};

// ---- what we learn from one run of a video ----
struct ActorLog {
    std::set<int> track_ids;
    int frames_visible = 0, frames_detected = 0;
    double first_verified_ms = -1;
    std::string verified_as;
    std::vector<double> crop_times;
    std::vector<float> crop_roll;       // true head tilt at the moment a crop was stored
    int crops_after_verified = 0;
    bool wrong_person = false;          // verified as somebody else
};
// What one RF card of a card-first check did during a video (from FrameResult::cards).
struct CardLog {
    bool found = false;                 // the card was in the database
    bool ever_matched = false;          // in some frame a face was verified as its owner
    double first_matched_ms = -1;
    int matched_track = -1;
    float best_score = kNoScore;        // highest similarity any face had to the owner
};
struct RunLog {
    std::map<std::string, ActorLog> actors;
    TimingStats detect, extract, match, total, track, gate, worker;
    int max_faces_in_frame = 0;
    int max_batch = 0;                  // most features (M) matched in a single M:N call
    int frames = 0;
    std::map<std::string, int> last_track;  // actor -> last track id (for enrolling from the video)
    bool card_check_ok = false;             // begin_card_check said yes (every card was in the database)
    std::map<std::string, CardLog> cards;   // by card number (card-first check only)
};

// Play a generated video through the pipeline. The pictures are `width` x `height` (the size of the speed
// requirement by default); with count_timing the frame times also go into the statistic over all videos.
// `card_check` (when given) is a card-first check: the RF cards are read before the video starts
// (AuthPipeline::begin_card_check), so every face is compared with the features of those cards only.
static RunLog run_video(AuthPipeline& pipeline, std::vector<Actor>& actors, double duration_ms,
                        const std::string& save_folder = "", int width = kWidth, int height = kHeight, bool count_timing = true,
                        const std::vector<std::string>* card_check = nullptr) {
    pipeline.reset_tracking();
    RunLog log;
    if (card_check) log.card_check_ok = pipeline.begin_card_check(*card_check);
    if (!save_folder.empty()) fs::create_directories(save_folder);
    Image frame;
    std::vector<TruthFace> truth;
    int index = 0;
    // true head tilt of every actor at the time of every frame, to look up the tilt at the time a crop was cut
    std::map<std::string, std::vector<std::pair<double, float>>> roll_history;
    const double wall_start = now_ms();
    for (double t = 0; t <= duration_ms; t += 1000.0 / kFps, ++index) {
        // Worker mode is meant for real time: play the video at its own speed (the picture is
        // rendered first, as a camera would have it ready, then we wait for its time).
        render_frame(width, height, t, index, actors, frame, truth);
        for (const TruthFace& tf : truth) roll_history[tf.label].push_back({t, tf.roll_deg});
        if (g_async) {
            const double wait = t - (now_ms() - wall_start);
            if (wait > 1.0) Sleep(static_cast<DWORD>(wait));
        }
        if (!save_folder.empty()) {
            char name[64];
            std::snprintf(name, sizeof(name), "\\frame_%04d.jpg", index);
            save_image(save_folder + name, frame);
        }
        FrameResult result;
        if (!pipeline.process(frame, t, result)) continue;
        ++log.frames;
        log.detect.add(result.detect_ms); log.track.add(result.track_ms);
        log.extract.add(result.extract_ms); log.match.add(result.match_ms); log.total.add(result.total_ms);
        log.gate.add(result.gate_ms);
        if (result.worker_ms > 0) log.worker.add(result.worker_ms);
        if (count_timing) g_all_frames.add(result.total_ms);
        log.max_faces_in_frame = std::max(log.max_faces_in_frame, static_cast<int>(result.faces.size()));
        log.max_batch = std::max(log.max_batch, result.matched_queries);
        for (const CardStatus& c : result.cards) {  // the card-first check, if one is active
            CardLog& cl = log.cards[c.card_id];
            cl.found = c.found;
            cl.best_score = std::max(cl.best_score, c.score);
            if (c.matched && !cl.ever_matched) {
                cl.ever_matched = true;
                cl.first_matched_ms = t;
                cl.matched_track = c.track_id;
            }
        }

        for (const TruthFace& tf : truth) {
            if (!tf.visible) continue;
            ActorLog& a = log.actors[tf.label];
            ++a.frames_visible;
            // which detected face is this actor? the one overlapping the true box most
            const FaceStatus* best = nullptr;
            float best_iou = 0.4f;
            for (const FaceStatus& s : result.faces) {
                const float v = iou(s.box, tf.box);
                if (v > best_iou) { best_iou = v; best = &s; }
            }
            if (!best) continue;
            ++a.frames_detected;
            a.track_ids.insert(best->track_id);
            log.last_track[tf.label] = best->track_id;
            if (best->crop_stored) {
                // the crop was cut from the frame at best->crop_time_ms (= now in synchronous mode,
                // earlier in worker mode): use that time for the spacing rule and the tilt gate
                a.crop_times.push_back(best->crop_time_ms);
                float roll_then = tf.roll_deg;
                double nearest = 1e18;
                for (const auto& h : roll_history[tf.label])
                    if (std::fabs(h.first - best->crop_time_ms) < nearest) { nearest = std::fabs(h.first - best->crop_time_ms); roll_then = h.second; }
                a.crop_roll.push_back(roll_then);
                if (a.first_verified_ms >= 0 && best->crop_time_ms > a.first_verified_ms) ++a.crops_after_verified;
            }
            if (best->verified && a.first_verified_ms < 0) {
                a.first_verified_ms = t;
                a.verified_as = best->person_id;
            }
            if (best->verified && best->person_id != a.verified_as) a.wrong_person = true;
        }
    }
    if (card_check) pipeline.end_card_check();
    return log;
}

static void print_timing(const char* title, const RunLog& log) {
    std::printf("      %s: per frame (ms)  detect %.1f | track %.2f | pose check %.1f (max %.1f) | extract %.1f (max %.1f) | match %.2f | TOTAL median %.1f  p95 %.1f  max %.1f\n",
                title, log.detect.median(), log.track.median(), log.gate.median(), log.gate.max(), log.extract.median(),
                log.extract.max(), log.match.median(), log.total.median(), log.total.p95(), log.total.max());
    if (log.worker.count() > 0)
        std::printf("      %s: worker thread: %zu results arrived, work per result median %.1f ms (max %.1f ms), not counted in the frame time\n",
                    title, log.worker.count(), log.worker.median(), log.worker.max());
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string profile_path = "models/permissive.ini";
    bool use_sqlite = false;   // --sqlite: the registered people live in a SQLite database instead of the default CSV file
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--async")) g_async = true;
        else if (!std::strcmp(argv[i], "--sqlite")) use_sqlite = true;
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {  // the picture size of the videos 1-4 and 6
            if (std::sscanf(argv[++i], "%dx%d", &kWidth, &kHeight) != 2 || kWidth < 320 || kHeight < 240) { std::printf("--size needs WxH, for example 1280x1080\n"); return 2; }
        }
        else profile_path = argv[i];
    }
    ModelProfile profile;
    if (!load_profile(profile_path, ".", profile)) return 2;

    const std::string db_path = std::string("build\\test_pipeline") + (g_async ? "_async" : "") + (use_sqlite ? ".db" : ".csv");
    std::remove(db_path.c_str());
    AuthPipeline pipeline;
    if (!pipeline.init(profile, db_path, g_async, use_sqlite ? DatabaseFormat::Sqlite : DatabaseFormat::Csv)) return 2;
    std::printf("database: %s (%s)\n", db_path.c_str(), use_sqlite ? "SQLite" : "CSV, the default format");
    std::printf("profile %s: detector %s, extractor %s (%d numbers), threshold %.3f, mode: %s\n", profile_path.c_str(),
                pipeline.detector().name(), pipeline.extractor().name(), pipeline.extractor().dim(), pipeline.match_threshold(),
                g_async ? "WORKER THREAD, videos played in real time" : "synchronous");
    // A profile with pose.detector_profile lets a second detector measure the head pose; otherwise the
    // main detector does. Everything below that refines a face must use pipeline.pose_detector().
    std::printf("pose check by: %s\n", pipeline.pose_detector().name());
    CHECK((&pipeline.pose_detector() != &pipeline.detector()) == !profile.pose_detector_profile.empty(),
          "the pipeline has a separate pose detector exactly when the profile names one");

    // ---------------------------------------------------------------- portraits
    std::vector<Portrait> portraits;
    int considered = 0, pose_rejected = 0;
    for (const auto& dir : fs::directory_iterator("data/portraits")) {
        if (!fs::is_directory(dir.path())) continue;
        Portrait p;
        p.id = dir.path().filename().string();
        const std::string file = (dir.path() / "117th.jpg").string();
        if (!fs::exists(file) || !load_image(file, p.image)) continue;
        std::vector<Face> faces;
        pipeline.detector().detect(p.image, faces);
        if (faces.size() < 1) continue;
        p.face = *std::max_element(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.box.area() < b.box.area(); });
        ++considered;
        // The test needs actors that the pose gate accepts. The pipeline decides with the REFINED
        // pose (a second, zoomed-in look), so choose with that same pose; the plain detection pose
        // can differ by a few degrees. (A 5-landmark pose has +-5..6 degrees of person-to-person
        // spread, so a fair share of "frontal" portraits is outside +-5 degrees.)
        Face refined;
        // (enrolling a person from a photo uses the plain detection, so it must have landmarks too)
        if (!p.face.has_landmarks || !refine_face(pipeline.pose_detector(), p.image, p.face, refined) ||
            !pose_within_tolerance(refined.pose)) {
            ++pose_rejected;
            continue;
        }
        portraits.push_back(std::move(p));
        if (portraits.size() >= 24) break;
    }
    std::printf("portraits considered %d, rejected by the pose gate %d, usable %zu\n", considered, pose_rejected, portraits.size());
    CHECK(portraits.size() >= 12, "enough usable portraits (>= 12)");
    if (portraits.size() < 12) return 1;

    // ---------------------------------------------------------------- SCRFD: fitted rectangle against square
    // A SCRFD profile with detector.layout = chw (models/dev_noncommercial.ini) runs the network on the smallest rectangle
    // (640x384 for a 1920x1080 picture) instead of the 640x640 square of the original conversion, 40% faster at the same
    // scale. It must find the same faces: the profile models/dev_noncommercial_square.ini is the square version.
    if (profile.detector_layout == "chw") {
        ModelProfile square_profile;
        std::unique_ptr<FaceDetector> square;
        if (load_profile("models/dev_noncommercial_square.ini", ".", square_profile)) square = create_detector(square_profile);
        CHECK(square != nullptr, "the square variant of the SCRFD profile (dev_noncommercial_square.ini) loads");
        if (square && portraits.size() >= 3) {
            std::vector<Actor> trio;
            // (positions and sizes follow the picture size, so that the check also holds with --size)
            const float xs[3] = {0.22f * kWidth, 0.50f * kWidth, 0.78f * kWidth};
            const float face_scale = std::min(kWidth / 1920.0f, kHeight / 1080.0f);
            const float widths[3] = {190 * face_scale, 260 * face_scale, 330 * face_scale};
            for (int i = 0; i < 3; ++i) {
                Actor a = make_actor(portraits[i].id, portraits[i].image, portraits[i].face.box);
                a.path = {{0, xs[i], 0.5f * kHeight, widths[i], 0.0f}, {1000, xs[i], 0.5f * kHeight, widths[i], 0.0f}};
                trio.push_back(a);
            }
            Image picture;
            std::vector<TruthFace> truth;
            render_frame(kWidth, kHeight, 0, 0, trio, picture, truth);
            std::vector<Face> fitted, boxed_faces;
            const bool ran = pipeline.detector().detect(picture, fitted) && square->detect(picture, boxed_faces);
            double worst_iou = 1.0, worst_landmark = 0.0;
            for (const Face& f : fitted) {
                float best = 0;
                const Face* mate = nullptr;
                for (const Face& g : boxed_faces) {
                    const float v = iou(f.box, g.box);
                    if (v > best) { best = v; mate = &g; }
                }
                worst_iou = std::min(worst_iou, static_cast<double>(best));
                if (mate)
                    for (int k = 0; k < 5; ++k)
                        worst_landmark = std::max(worst_landmark, static_cast<double>(std::hypot(f.landmarks[k].x - mate->landmarks[k].x, f.landmarks[k].y - mate->landmarks[k].y)));
            }
            std::printf("      fitted rectangle %zu faces, square %zu faces; worst box overlap %.3f, largest landmark difference %.2f px\n", fitted.size(), boxed_faces.size(),
                        worst_iou, worst_landmark);
            CHECK(ran && fitted.size() == 3 && boxed_faces.size() == 3, "the fitted rectangle and the square find the same three faces in a 1920x1080 picture");
            CHECK(worst_iou > 0.95 && worst_landmark < 3.0, "... with the same boxes (overlap above 0.95) and the same landmarks (within 3 pixels)");
        }
    }

    // ---------------------------------------------------------------- register
    // 20 people are registered, the rest are strangers.
    const size_t enrolled_count = std::min<size_t>(20, portraits.size() - 3);
    TimingStats enroll_time;
    size_t enrolled = 0;
    for (size_t i = 0; i < enrolled_count; ++i) {
        double a = now_ms();
        if (pipeline.enroll_image(portraits[i].image, portraits[i].id, "Name of " + portraits[i].id)) ++enrolled;
        enroll_time.add(now_ms() - a);
    }
    std::printf("      registering from a photo (detect + extract + save): median %.1f ms\n", enroll_time.median());
    CHECK(enrolled == enrolled_count && pipeline.store().count() == static_cast<int>(enrolled_count), "registered people from photos");

    const Portrait& known_a = portraits[0];
    const Portrait& known_b = portraits[1];
    const Portrait& stranger = portraits[enrolled_count];       // not registered
    const Portrait& stranger2 = portraits[enrolled_count + 1];  // not registered

    // =========================================================================
    // Video 1: ONE registered person walks toward the camera
    // =========================================================================
    std::printf("\n=== video 1: a registered person approaches (6 s, %.0f fps, %dx%d) ===\n", kFps, kWidth, kHeight);
    {
        Actor a = make_actor(known_a.id, known_a.image, known_a.face.box);
        // starts far away (face 110 px) tilted by 25 degrees, straightens at 2.5 s, ends 420 px wide
        a.path = approach_path(6000, 400, 500, 640, 540, 110, 420, 25.0f, 2500);
        std::vector<Actor> actors = {a};
        RunLog log = run_video(pipeline, actors, 6000, "data\\videos\\approach_registered");
        const ActorLog& l = log.actors[known_a.id];
        std::printf("      frames visible %d, detected %d; track ids:", l.frames_visible, l.frames_detected);
        for (int id : l.track_ids) std::printf(" %d", id);
        std::printf("; verified at %.0f ms as %s\n", l.first_verified_ms, l.verified_as.c_str());
        CHECK(l.frames_visible > 60 && l.frames_detected >= l.frames_visible * 0.8, "face detected in at least 80% of the frames");
        CHECK(l.track_ids.size() == 1, "one single track ID for the whole walk");
        CHECK(l.first_verified_ms >= 0 && l.verified_as == known_a.id && !l.wrong_person, "verified as the correct registered person");
        CHECK(l.first_verified_ms >= 0 && l.first_verified_ms <= 4500, "verified within 4.5 s");

        bool gate_ok = true, interval_ok = true;
        for (float roll : l.crop_roll) if (std::fabs(roll) > FA_POSE_ROLL_TOL_DEG + 3.0f) gate_ok = false;  // 3 deg detector noise
        for (size_t i = 1; i < l.crop_times.size(); ++i) if (l.crop_times[i] - l.crop_times[i - 1] < FA_CROP_STORE_INTERVAL_MS - 1) interval_ok = false;
        std::printf("      crops stored at (ms):");
        for (size_t i = 0; i < l.crop_times.size(); ++i) std::printf(" %.0f(roll %.0f)", l.crop_times[i], l.crop_roll[i]);
        std::printf("\n");
        CHECK(!l.crop_times.empty() && gate_ok, "crops are only stored while the head tilt is inside the tolerance");
        CHECK(interval_ok, "crops are at least 1 s apart");
        CHECK(l.crops_after_verified == 0, "after verification no more crops are stored (the face is only tracked)");
        print_timing("1920x1080", log);
    }

    // =========================================================================
    // Video 2: an UNREGISTERED person
    // =========================================================================
    std::printf("\n=== video 2: an unregistered person approaches ===\n");
    {
        Actor a = make_actor(stranger.id, stranger.image, stranger.face.box);
        a.path = approach_path(7000, 900, 520, 640, 540, 110, 400, 20.0f, 2000);
        std::vector<Actor> actors = {a};
        RunLog log = run_video(pipeline, actors, 7000);
        const ActorLog& l = log.actors[stranger.id];
        std::printf("      stranger is '%s'; detected %d / %d frames, crops stored: %zu, verified: %s%s%s\n", stranger.id.c_str(),
                    l.frames_detected, l.frames_visible, l.crop_times.size(), l.first_verified_ms >= 0 ? "YES as '" : "no",
                    l.verified_as.c_str(), l.first_verified_ms >= 0 ? "'" : "");
        CHECK(l.first_verified_ms < 0, "a stranger is never verified");
        // after the head is straight (2 s) the face gets a fresh crop about once per second
        CHECK(l.crop_times.size() >= 3 && l.crop_times.size() <= 8, "an unverified face gets a new crop about once per second");
    }

    // =========================================================================
    // Video 3: two people at the same time (one registered, one stranger)
    // =========================================================================
    std::printf("\n=== video 3: two people at once ===\n");
    int stranger_track = 0;
    {
        Actor a = make_actor(known_b.id, known_b.image, known_b.face.box);
        a.path = approach_path(6000, 250, 540, 400, 540, 120, 330, 15.0f, 1500);
        Actor b = make_actor(stranger2.id, stranger2.image, stranger2.face.box);
        b.path = approach_path(6000, 1030, 540, 880, 540, 120, 330, -15.0f, 1500);
        std::vector<Actor> actors = {a, b};
        RunLog log = run_video(pipeline, actors, 6000);
        const ActorLog &la = log.actors[known_b.id], &lb = log.actors[stranger2.id];
        std::printf("      registered: verified at %.0f ms as %s | stranger: verified %s | most faces in one frame %d, most features in one M:N call %d\n",
                    la.first_verified_ms, la.verified_as.c_str(), lb.first_verified_ms >= 0 ? "YES" : "no",
                    log.max_faces_in_frame, log.max_batch);
        CHECK(log.max_faces_in_frame >= 2, "two faces are detected and tracked in the same frame");
        CHECK(la.first_verified_ms >= 0 && la.verified_as == known_b.id, "the registered person is verified correctly");
        CHECK(lb.first_verified_ms < 0, "the stranger next to him is not verified");
        CHECK(la.track_ids.size() == 1 && lb.track_ids.size() == 1 && *la.track_ids.begin() != *lb.track_ids.begin(),
              "each person keeps a separate track ID");
        stranger_track = log.last_track[stranger2.id];
        print_timing("two faces", log);
    }


    // =========================================================================
    // Register from the video, modify, delete
    // =========================================================================
    std::printf("\n=== register / modify / delete during operation ===\n");
    {
        // The pipeline still holds the stranger's last crop (track %d of video 3).
        CHECK(pipeline.enroll_track(stranger_track, stranger2.id, "Registered from the camera"),
              "register the stranger from the face crop stored for his track");
        CHECK(pipeline.store().count() == static_cast<int>(enrolled_count) + 1, "the store has one more person");

        Actor b = make_actor(stranger2.id, stranger2.image, stranger2.face.box);
        b.path = approach_path(5000, 700, 540, 640, 540, 120, 330, 10.0f, 1500);
        std::vector<Actor> actors = {b};
        RunLog log = run_video(pipeline, actors, 5000);
        CHECK(log.actors[stranger2.id].first_verified_ms >= 0 && log.actors[stranger2.id].verified_as == stranger2.id,
              "the newly registered person is verified when he comes back");

        CHECK(pipeline.rename_or_refresh(stranger2.id, "New Name", nullptr) && pipeline.store().find(stranger2.id)->name == "New Name",
              "modify: the name changes");
        {   // the track that is still verified as this person must show the new name at once
            Image frame;
            std::vector<TruthFace> truth;
            render_frame(kWidth, kHeight, 5066, 9999, actors, frame, truth);
            FrameResult after;
            pipeline.process(frame, 5066, after);
            bool shows_new_name = false;
            for (const FaceStatus& s : after.faces) if (s.verified && s.person_name == "New Name") shows_new_name = true;
            CHECK(shows_new_name, "modify: a face that is being tracked as this person shows the new name immediately");
        }
        CHECK(pipeline.remove_person(stranger2.id), "delete the person");
        log = run_video(pipeline, actors, 5000);
        std::printf("      after deletion: verified at %.0f ms as '%s'\n", log.actors[stranger2.id].first_verified_ms,
                    log.actors[stranger2.id].verified_as.c_str());
        CHECK(log.actors[stranger2.id].first_verified_ms < 0, "after deletion the person is no longer verified");

        // the database file on disk must agree with the memory
        FeatureStore reread;
        CHECK(reread.load(db_path) && reread.count() == pipeline.store().count(), "the database file on disk matches the registered people");
    }

    // =========================================================================
    // Video 5: the same walk at several camera picture sizes
    // A camera may deliver 640x480, 1280x720, 1920x1080 (the default), a 4:3 picture or more: the whole system
    // has to work with every size. The scene is laid out for 1280x1080 and scaled: positions with the picture,
    // face widths with its height (so a 1920x1080 picture shows the same faces as 1280x1080, only wider).
    // =========================================================================
    std::printf("\n=== video 5: the same registered person at several camera picture sizes (default %dx%d) ===\n",
                FA_CAMERA_DEFAULT_WIDTH, FA_CAMERA_DEFAULT_HEIGHT);
    {
        struct Size { int w, h; };
        const Size sizes[] = {{640, 480}, {1280, 720}, {FA_CAMERA_DEFAULT_WIDTH, FA_CAMERA_DEFAULT_HEIGHT}, {1600, 1200}, {2560, 1440}};
        for (const Size& s : sizes) {
            const float sx = s.w / 1280.0f, sy = s.h / 1080.0f, sf = std::min(sx, sy);
            Actor a = make_actor(known_a.id, known_a.image, known_a.face.box);
            a.path = approach_path(6000, 400 * sx, 500 * sy, 640 * sx, 540 * sy, 110 * sf, 420 * sf, 25.0f, 2500);
            std::vector<Actor> actors = {a};
            RunLog log = run_video(pipeline, actors, 6000, "", s.w, s.h, false);  // (its timing is not part of the 1920x1080 statistic)
            const ActorLog& l = log.actors[known_a.id];
            char label[32], text[160];
            std::snprintf(label, sizeof(label), "%dx%d", s.w, s.h);
            std::printf("      %s: frames visible %d, detected %d, track ids %zu; verified at %.0f ms as %s\n", label, l.frames_visible,
                        l.frames_detected, l.track_ids.size(), l.first_verified_ms, l.verified_as.c_str());
            std::snprintf(text, sizeof(text), "%s: the face is detected in at least 80%% of the frames", label);
            CHECK(l.frames_visible > 60 && l.frames_detected >= l.frames_visible * 0.8, text);
            std::snprintf(text, sizeof(text), "%s: one single track ID for the whole walk", label);
            CHECK(l.track_ids.size() == 1, text);
            std::snprintf(text, sizeof(text), "%s: verified as the correct registered person within 5 s", label);
            CHECK(l.first_verified_ms >= 0 && l.first_verified_ms <= 5000 && l.verified_as == known_a.id && !l.wrong_person, text);
            print_timing(label, log);
        }
    }

    // =========================================================================
    // Video 4: a verified person leaves and somebody else steps into the same spot
    // (placed last because every run_video() starts with new track IDs)
    // =========================================================================
    std::printf("\n=== video 4: a stranger takes the place of a verified person ===\n");
    {
        // Same position and size for both, so the tracker gives the stranger the same track ID.
        // The registered actor must be one whose pose, measured in THIS scene with a safety margin,
        // is accepted by the gate (a portrait right at the 5 degree limit would never be stored).
        const std::vector<PathPoint> still = {{0, 640, 540, 300, 0.0f}, {8000, 640, 540, 300, 0.0f}};
        size_t actor_index = enrolled_count;  // enrolled_count = none found
        for (size_t i = 0; i < enrolled_count && actor_index == enrolled_count; ++i) {
            Actor trial = make_actor(portraits[i].id, portraits[i].image, portraits[i].face.box);
            trial.path = still;
            Image frame;
            std::vector<TruthFace> truth;
            render_frame(kWidth, kHeight, 0, 0, {trial}, frame, truth);
            std::vector<Face> found;
            Face refined;
            if (pipeline.detector().detect(frame, found) && !found.empty() &&
                refine_face(pipeline.pose_detector(), frame, found[0], refined) && refined.pose.valid &&
                std::fabs(refined.pose.pitch) <= 3.0f && std::fabs(refined.pose.yaw) <= 3.0f && std::fabs(refined.pose.roll) <= 4.0f)
                actor_index = i;
        }
        CHECK(actor_index < enrolled_count, "a registered person with a comfortable pose margin exists for this test");
        if (actor_index >= enrolled_count) actor_index = 0;
        const Portrait& registered = portraits[actor_index];
        Actor a = make_actor(registered.id, registered.image, registered.face.box);
        a.path = still;
        a.disappear_ms = 3000;
        Actor b = make_actor(stranger.id, stranger.image, stranger.face.box);
        b.path = a.path;
        b.appear_ms = 3500;  // 0.5 s (7 frames) without any face in between
        std::vector<Actor> actors = {a, b};
        RunLog log = run_video(pipeline, actors, 8000);
        const ActorLog &la = log.actors[registered.id], &lb = log.actors[stranger.id];
        std::printf("      registered person verified at %.0f ms; track IDs of the two: ", la.first_verified_ms);
        for (int id : la.track_ids) std::printf("%d ", id);
        std::printf("/ ");
        for (int id : lb.track_ids) std::printf("%d ", id);
        std::printf("; stranger verified: %s\n", lb.first_verified_ms >= 0 ? "YES" : "no");
        CHECK(la.first_verified_ms >= 0 && la.first_verified_ms < 3000, "the registered person is verified before he leaves");
        CHECK(!la.track_ids.empty() && !lb.track_ids.empty() && *la.track_ids.begin() == *lb.track_ids.begin(),
              "the stranger gets the same track ID (the situation this test is about)");
        CHECK(lb.first_verified_ms < 0, "the stranger does NOT inherit the verification of the person who left");
    }

    // =========================================================================
    // Video 6: card first. The RF cards are identified FIRST: each card is looked up in the database and
    // the feature stored with it is retrieved; then the faces of the frames are compared with exactly those
    // N features. M (faces) and N (cards) are independent: M < N, M = N and M > N are all played.
    // =========================================================================
    std::printf("\n=== video 6: card first: the faces are compared only with the features of the RF cards (M < N, M = N, M > N) ===\n");
    {
        const std::string card_a = "CARD-A-0001", card_b = "CARD-B-0002", card_x = "CARD-NOBODY";
        CHECK(pipeline.store().set_card_id(known_a.id, card_a) && pipeline.store().set_card_id(known_b.id, card_b),
              "two registered people get an RF card (stored with their face features in the database file)");
        {
            FeatureStore reread;
            CHECK(reread.load(db_path) && reread.find(known_a.id) && reread.find(known_a.id)->card_id == card_a,
                  "the card number is in the database file on disk");
        }
        CHECK(!pipeline.enroll_image(stranger.image, "S-CARD", "card thief", card_a) && !pipeline.store().find("S-CARD"),
              "registering somebody with a card that belongs to another person is refused");

        const Portrait& pa = known_a;
        const Portrait& pb = known_b;
        auto walk_a = [&](float x0, float x1, float roll) {
            Actor a = make_actor(pa.id, pa.image, pa.face.box);
            a.path = approach_path(6000, x0, 520, x1, 540, 110, 330, roll, 2000);
            return a;
        };
        auto walk_b = [&](float x0, float x1, float roll) {
            Actor b = make_actor(pb.id, pb.image, pb.face.box);
            b.path = approach_path(6000, x0, 520, x1, 540, 110, 330, roll, 2000);
            return b;
        };
        auto cards_of = [](const char* c1, const char* c2 = nullptr) {
            std::vector<std::string> v(1, c1);
            if (c2) v.push_back(c2);
            return v;
        };

        // (1) M = 1 face, N = 1 card: the owner
        {
            std::vector<Actor> actors = {walk_a(640, 640, 20.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog& l = log.actors[pa.id];
            const CardLog& c = log.cards[card_a];
            std::printf("      M=1 face, N=1 card (the owner): verified at %.0f ms as %s; card matched at %.0f ms, best similarity %.3f\n", l.first_verified_ms,
                        l.verified_as.c_str(), c.first_matched_ms, c.best_score);
            CHECK(log.card_check_ok && c.found, "begin_card_check finds the card in the database");
            CHECK(l.first_verified_ms >= 0 && l.first_verified_ms <= 4500 && l.verified_as == pa.id && !l.wrong_person, "M = 1, N = 1: the owner's face is verified as the owner");
            CHECK(c.ever_matched && c.matched_track >= 0 && l.track_ids.count(c.matched_track), "... and the frame result reports the card as matched, with the face's track");
        }
        // (2) M = 1, N = 1: a REGISTERED person with somebody else's card
        {
            std::vector<Actor> actors = {walk_b(640, 640, 20.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog& l = log.actors[pb.id];
            std::printf("      M=1 face, N=1 card (another registered person's card): verified: %s; best similarity to the owner %.3f\n",
                        l.first_verified_ms >= 0 ? "YES" : "no", log.cards[card_a].best_score);
            CHECK(l.frames_detected > 40 && l.first_verified_ms < 0 && !log.cards[card_a].ever_matched,
                  "a registered person with somebody else's card is NOT verified (his face is compared with the owner's feature only, not with everybody)");
        }
        // (3) M = 1, N = 1: a card that is not in the database. The registered person would be found by a search among all people.
        {
            std::vector<Actor> actors = {walk_b(640, 640, 20.0f)};
            std::vector<std::string> cards = cards_of(card_x.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog& l = log.actors[pb.id];
            std::printf("      M=1 face, N=1 card that is not in the database: begin_card_check %s; verified: %s\n", log.card_check_ok ? "true" : "false",
                        l.first_verified_ms >= 0 ? "YES" : "no");
            CHECK(!log.card_check_ok && !log.cards[card_x].found, "a card that is not in the database: the comparison fails (found = false)");
            CHECK(l.frames_detected > 40 && l.first_verified_ms < 0, "... and the face is NOT searched among all people instead: the registered person is not verified");
        }
        // (4) M = 2 faces, N = 1 card (M > N)
        {
            std::vector<Actor> actors = {walk_a(400, 480, 15.0f), walk_b(900, 840, -15.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog &la = log.actors[pa.id], &lb = log.actors[pb.id];
            std::printf("      M=2 faces, N=1 card (A's): A verified at %.0f ms as %s, B verified: %s; most faces in one frame %d\n", la.first_verified_ms,
                        la.verified_as.c_str(), lb.first_verified_ms >= 0 ? "YES" : "no", log.max_faces_in_frame);
            CHECK(log.max_faces_in_frame >= 2, "M = 2 faces are in the frames (more faces than cards)");
            CHECK(la.first_verified_ms >= 0 && la.verified_as == pa.id && !la.wrong_person && log.cards[card_a].ever_matched, "M > N: the face of the card's owner is verified");
            CHECK(lb.first_verified_ms < 0, "... the other face, a registered person without a card in this check, is not");
            CHECK(log.cards[card_a].matched_track >= 0 && la.track_ids.count(log.cards[card_a].matched_track), "... and the card is matched to the owner's track, not to the other face");
        }
        // (5) M = 1 face, N = 2 cards (M < N)
        {
            std::vector<Actor> actors = {walk_b(640, 640, 20.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str(), card_b.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog& l = log.actors[pb.id];
            std::printf("      M=1 face, N=2 cards (A's and B's): the face is verified at %.0f ms as %s; card A matched: %s, card B matched: %s\n", l.first_verified_ms,
                        l.verified_as.c_str(), log.cards[card_a].ever_matched ? "YES" : "no", log.cards[card_b].ever_matched ? "YES" : "no");
            CHECK(l.first_verified_ms >= 0 && l.first_verified_ms <= 4500 && l.verified_as == pb.id && !l.wrong_person,
                  "M < N: the one face is verified as the owner of the card it is most like (B)");
            CHECK(log.cards[card_b].ever_matched && !log.cards[card_a].ever_matched, "... card B is matched, card A (its owner is not in the picture) is not");
        }
        // (6) M = 2 faces, N = 2 cards (M = N)
        {
            std::vector<Actor> actors = {walk_a(400, 480, 15.0f), walk_b(900, 840, -15.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str(), card_b.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog &la = log.actors[pa.id], &lb = log.actors[pb.id];
            std::printf("      M=2 faces, N=2 cards: A verified at %.0f ms as %s, B at %.0f ms as %s\n", la.first_verified_ms, la.verified_as.c_str(),
                        lb.first_verified_ms, lb.verified_as.c_str());
            CHECK(la.first_verified_ms >= 0 && la.verified_as == pa.id && lb.first_verified_ms >= 0 && lb.verified_as == pb.id && !la.wrong_person && !lb.wrong_person,
                  "M = N: both faces are verified, each as the owner of its own card");
            CHECK(log.cards[card_a].ever_matched && log.cards[card_b].ever_matched &&
                      la.track_ids.count(log.cards[card_a].matched_track) && lb.track_ids.count(log.cards[card_b].matched_track),
                  "... and each card is matched to its owner's track");
        }
        // (7) M = 2 faces, N = 2 cards, but one card is not in the database: that card fails, the other still works
        {
            std::vector<Actor> actors = {walk_a(400, 480, 15.0f), walk_b(900, 840, -15.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str(), card_x.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            const ActorLog &la = log.actors[pa.id], &lb = log.actors[pb.id];
            std::printf("      M=2 faces, N=2 cards, one card unknown: begin_card_check %s; A verified: %s, B verified: %s\n", log.card_check_ok ? "true" : "false",
                        la.first_verified_ms >= 0 ? "YES" : "no", lb.first_verified_ms >= 0 ? "YES" : "no");
            CHECK(!log.card_check_ok && log.cards[card_a].found && !log.cards[card_x].found, "one unknown card among two: the check reports the failure, and which card failed");
            CHECK(la.first_verified_ms >= 0 && la.verified_as == pa.id && lb.first_verified_ms < 0,
                  "... the card that was found still verifies its owner; the face with no card is not verified (no fallback)");
        }
        // after end_card_check the normal search among all people works again
        {
            std::vector<Actor> actors = {walk_b(640, 640, 20.0f)};
            RunLog log = run_video(pipeline, actors, 6000);
            const ActorLog& l = log.actors[pb.id];
            CHECK(!pipeline.card_check_active() && l.first_verified_ms >= 0 && l.verified_as == pb.id,
                  "after the card check ends, a registered person is verified by the normal search among all people again");
        }
        // deleting the owner makes his card worthless
        CHECK(pipeline.remove_person(known_a.id) && !pipeline.store().find_by_card(card_a), "after the owner is deleted his card belongs to nobody");
        {
            std::vector<Actor> actors = {walk_a(640, 640, 20.0f)};
            std::vector<std::string> cards = cards_of(card_a.c_str());
            RunLog log = run_video(pipeline, actors, 6000, "", kWidth, kHeight, true, &cards);
            CHECK(!log.card_check_ok && !log.cards[card_a].found && log.actors[pa.id].first_verified_ms < 0,
                  "the deleted person with his old card: the card is not in the database, the comparison fails");
        }
    }

    // =========================================================================
    // The time one frame takes, over all videos
    // =========================================================================
    std::printf("\n=== time per frame over all videos (%zu frames) ===\n", g_all_frames.count());
    std::printf("      process(): median %.1f ms  p95 %.1f  p99 %.1f  max %.1f\n", g_all_frames.median(), g_all_frames.p95(),
                g_all_frames.percentile(99), g_all_frames.max());
    if (g_async) {
        // With the worker, a frame only pays for detection, tracking and matching. The limit of the
        // requirements for detection + tracking is 60 ms (named for 1280x1080; applied here to 1920x1080 by analogy).
        CHECK(g_all_frames.percentile(99) < 40.0, "worker mode: 99% of the frames take less than 40 ms");
        CHECK(g_all_frames.max() < 60.0, "worker mode: no frame takes 60 ms or more");
    }

    // =========================================================================
    // Worker lifecycle: reset and shutdown while the worker has work
    // =========================================================================
    if (g_async) {
        std::printf("\n=== worker thread: reset and shutdown while it has work ===\n");
        Actor a = make_actor(known_a.id, known_a.image, known_a.face.box);
        a.path = {{0, 640, 540, 300, 0.0f}, {8000, 640, 540, 300, 0.0f}};
        Image frame;
        std::vector<TruthFace> truth;
        render_frame(kWidth, kHeight, 0, 0, {a}, frame, truth);
        Image empty;
        render_frame(kWidth, kHeight, 0, 0, {}, empty, truth);

        // (1) a job is queued, the tracking is reset before its result is picked up
        pipeline.reset_tracking();
        FrameResult r;
        pipeline.process(frame, 100000.0, r);   // a face with an unverified track: a job goes to the worker
        const int queued = r.jobs_in_flight;
        pipeline.reset_tracking();              // the old track no longer exists
        const double t0 = now_ms();
        pipeline.flush();                       // must return, and must throw the old result away
        const double flush_ms = now_ms() - t0;
        pipeline.process(empty, 100100.0, r);
        std::printf("      jobs in flight after the first frame: %d; flush took %.0f ms\n", queued, flush_ms);
        CHECK(queued >= 1, "a pose-check job was handed to the worker");
        CHECK(flush_ms < 5000 && r.faces.empty() && pipeline.tracker().tracks().empty(),
              "after reset_tracking() the old result is ignored and nothing is left of the old track");

        // (2) a second pipeline is destroyed while its worker still has a job
        const double t1 = now_ms();
        {
            AuthPipeline second;
            if (second.init(profile, std::string("build\\test_pipeline_async2") + (use_sqlite ? ".db" : ".csv"), true,
                            use_sqlite ? DatabaseFormat::Sqlite : DatabaseFormat::Csv)) {
                FrameResult r2;
                second.process(frame, 0.0, r2);
                second.process(frame, 70.0, r2);
            }
        }  // ~AuthPipeline: stops the thread; a running job may finish, queued jobs are dropped
        const double shutdown_ms = now_ms() - t1;
        std::printf("      creating a second pipeline, giving it two frames and destroying it took %.0f ms\n", shutdown_ms);
        CHECK(shutdown_ms < 30000, "a pipeline with a busy worker can be destroyed (no hang, no crash)");
    }

    // Everything has run now: detection, pose check, extraction, matching, the worker thread (--async), the video decoding.
    CHECK(pipeline.runs_on_cpu(), "every engine of the pipeline (detector, pose detector, extractor, and the worker's own) runs on the CPU");
    {
        const std::vector<std::string> gpu = loaded_gpu_libraries();
        std::string loaded, info;
        for (const std::string& g : gpu) loaded += " " + g;
        for (const std::string& g : loaded_graphics_libraries()) info += " " + g;
        std::printf("      GPU libraries loaded in this process:%s; Direct3D 11 / DXGI libraries (informational):%s\n", gpu.empty() ? " none" : loaded.c_str(), info.empty() ? " none" : info.c_str());
        CHECK(gpu.empty(), "after all the videos, no GPU library (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) is loaded in the process");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
