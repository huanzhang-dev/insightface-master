// auth_pipeline.h - the complete facial authentication system.
//
// Per camera frame the pipeline does this:
//
//   1. DETECT   find all faces (resize to the model size happens inside).
//   2. TRACK    give every face a track ID that stays the same from frame to frame.
//   3. GATE     for every track that is NOT yet verified, whose face is at least
//               FA_MIN_CROP_FACE_PIXELS wide and whose crop is due (see below; the
//               pose is checked at most every FA_POSE_CHECK_INTERVAL_MS): look at the face
//               a second time with the refinement step (detector.h), which gives
//               exact landmarks and so an exact head rotation. (The "pose detector" that
//               does this is the profile's own detector unless the profile names another one
//               with pose.detector_profile: the SCRFD profile lets YuNet measure the pose.) If pitch/yaw/roll are
//               inside the tolerance (config.h) and at least FA_CROP_STORE_INTERVAL_MS
//               have passed since the track's last crop, cut the face out of the frame
//               and keep it as the track's crop (an older crop is overwritten).
//   4. EXTRACT  compute the feature of every NEW crop.
//   5. MATCH    compare ALL M new features (of the currently detected faces) with ALL N registered
//               ones (the faces in the database) in one M:N call. N can also be a SUBSET of the
//               database: when RF cards were read (begin_card_check), the cards are looked up
//               FIRST and the faces are compared only with the features stored for those cards.
//               A match at or above the threshold marks the track "verified".
//               A verified track is only tracked from then on: no new crop, no
//               new extraction, no new matching. Exception: a verified track that
//               was not found for FA_REVERIFY_AFTER_MISSED_FRAMES frames in a row
//               loses its verification when found again (it may be another person
//               who stepped into the same spot) and goes through steps 3-5 again.
//
// TWO MODES
//   synchronous (default)  steps 3 and 4 run inside process(). A frame in which a crop is
//                          due takes longer (an extra detector run plus a feature
//                          extraction), but every result is there when process() returns.
//   worker thread          init(..., async_worker = true). Steps 3 and 4 run on a second
//                          thread with its own detector and extractor, so process() only
//                          pays for detection, tracking and matching (a few milliseconds,
//                          the same in every frame). The result of a pose check / feature
//                          extraction is picked up by a LATER frame (usually 2 to 10 frames
//                          later), so verification arrives a little later than in
//                          synchronous mode. flush() waits for the worker (used by tests
//                          and before registering a person).
//
// Registration of new people uses the same pieces (see enroll_track and
// enroll_image).
#ifndef FA_AUTH_PIPELINE_H
#define FA_AUTH_PIPELINE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "detector.h"
#include "extractor.h"
#include "feature_store.h"
#include "tracker.h"

namespace fa {

struct GateJob;     // defined in auth_pipeline.cpp: work for the pose check / extraction
struct GateResult;  // defined in auth_pipeline.cpp: what that work found

// What happened to one face in one frame (for display, logging and the tests).
struct FaceStatus {
    int track_id = 0;
    Rect box;
    Pose pose;
    bool pose_ok = false;          // inside the rotation tolerance
    bool crop_stored = false;      // a new crop was stored in THIS frame (worker mode: its result arrived in this frame)
    double crop_time_ms = 0;       // capture time of that crop (worker mode: of the frame it was cut from, earlier than now)
    bool verified = false;         // this track has been matched with a registered person
    std::string person_id;         // who (empty until verified)
    std::string person_name;
    float score = 0;               // similarity of the latest match attempt
};

// The state of one RF card of a card-first check (AuthPipeline::begin_card_check) in one frame.
struct CardStatus {
    std::string card_id;           // as given to begin_card_check
    bool found = false;            // the card is in the database; false = the comparison for this card FAILED
    std::string person_id;         // its owner (empty when not found)
    std::string person_name;
    bool matched = false;          // in THIS frame a face is verified as the owner
    int track_id = -1;             // that face's track (-1 when not matched)
    float score = kNoScore;        // the highest similarity any face has had to the owner since begin_card_check
};

// Timing and counters of one processed frame.
struct FrameResult {
    std::vector<FaceStatus> faces;
    std::vector<CardStatus> cards;  // while a card-first check is active: one entry per card (N), in the order given; empty otherwise
    double detect_ms = 0, track_ms = 0, match_ms = 0, total_ms = 0;
    double gate_ms = 0;            // synchronous mode: pose check and crop, run inside process()
    double extract_ms = 0;         // synchronous mode: feature extraction, run inside process()
    int extractions = 0;           // how many features were computed (worker mode: arrived) in this frame
    int matched_queries = 0;       // how many features (M) went through the M:N matcher
    int jobs_in_flight = 0;        // worker mode: jobs waiting or running after this frame
    double worker_ms = 0;          // worker mode: work time of the results that arrived in this frame
};

class AuthPipeline {
public:
    AuthPipeline();
    ~AuthPipeline();  // stops the worker thread, if there is one
    AuthPipeline(const AuthPipeline&) = delete;
    AuthPipeline& operator=(const AuthPipeline&) = delete;

    // Create the detector and the extractor from the profile and open the database of
    // registered people: the file `store_path`, managed by FeatureStore (see feature_store.h) in
    // the format the USER chooses: CSV (the default) or SQLite. `db_format` Auto follows the file: an
    // existing file is recognised by its content, a new one by its extension (.db .sqlite .sqlite3 =
    // SQLite, anything else CSV); Csv or Sqlite is an explicit choice. A missing file is created
    // empty, a damaged one is refused, the file is locked against a second program, and every
    // registration, deletion and change is written to it at once (and undone when the write fails).
    // An empty path means no file: the people live in memory only. With async_worker = true a second
    // pose detector, a second extractor and a worker thread are created too (see "TWO MODES" above).
    // Returns false and prints the reason on failure.
    bool init(const ModelProfile& profile, const std::string& store_path, bool async_worker = false,
              DatabaseFormat db_format = DatabaseFormat::Auto);

    bool uses_worker() const { return worker_ != nullptr; }

    // True when every engine of this pipeline runs on the CPU: the detector, the pose detector, the extractor
    // and, in worker mode, the worker's own pose detector and extractor (the matcher is AVX2 code on the CPU).
    // This is always the case: the project uses no GPU.
    bool runs_on_cpu() const;

    // Process one camera frame. `time_ms` is the capture time in milliseconds.
    bool process(const Image& frame, double time_ms, FrameResult& result);

    // Worker mode: wait until the worker has finished everything queued and apply the
    // results (so a stored crop or a verification is visible at once). Does nothing in
    // synchronous mode.
    void flush();

    // ----- registration (requirement 2.4) -----
    // Register the person who is shown by track `track_id`, using the face crop
    // stored for that track (worker mode: after flush()). The feature is added to the
    // store, which writes the database file (the registration is undone if it cannot). `card_id` is the person's RF card (empty = none); it must not
    // belong to somebody else already.
    bool enroll_track(int track_id, const std::string& person_id, const std::string& name, const std::string& card_id = std::string());
    // Register the largest face found in a still picture (for example a portrait).
    bool enroll_image(const Image& photo, const std::string& person_id, const std::string& name, const std::string& card_id = std::string());
    // Compute the feature of the largest face in a picture without registering it.
    bool extract_from_image(const Image& photo, Feature& feature, Face* face = nullptr);

    bool remove_person(const std::string& person_id);
    bool rename_or_refresh(const std::string& person_id, const std::string& new_name, const Image* new_photo);

    // ----- card first: N is the set of features that RF cards stand for -----
    // One or more RF cards have been read (by hardware this program does not know about). The card
    // information is ALWAYS identified first: begin_card_check looks every card up in the database
    // and retrieves the feature stored with it. From then on the faces detected in the frames
    // that follow (M of them in a frame) are compared with exactly those N retrieved features and
    // with nothing else: M and N are independent (fewer faces than cards, as many, or more). A face
    // is verified as the owner of the card whose feature it is most like, when the similarity reaches
    // the threshold; so a registered person who shows somebody else's card is NOT verified, and a
    // stranger is not either. A card that is not in the database is a FAILED comparison: it is
    // reported as found = false in FrameResult::cards, nothing is compared for it, and there is
    // never a fallback to a search among all registered people while the check is active.
    // Faces that were verified before are checked again against the cards (their old verification
    // proves nothing for this check). The check stays active until end_card_check().
    // Returns true only if cards is not empty and every card was found; false (with the reason on
    // stderr) otherwise, but the check is active anyway unless the list is empty: all the given cards are kept, a card
    // that is not in the database shows as found = false in every FrameResult::cards and nothing is compared for it.
    bool begin_card_check(const std::vector<std::string>& card_ids);
    // Leave card-first mode: unverified faces are searched among all registered people again.
    // Faces that were verified by the check stay verified.
    void end_card_check();
    bool card_check_active() const { return card_check_; }

    // The registered persons (also used by the tests). Changes made through it are written to the
    // database file like the pipeline's own (the store is open); but registering a person this way
    // does not mark any track as verified, use enroll_track / enroll_image for that. External
    // store mutations invalidate cached identities and old worker results on the next process/flush.
    FeatureStore& store() { return store_; }
    const FaceTracker& tracker() const { return tracker_; }
    FaceDetector& detector() { return *detector_; }
    // The detector that does the pose check (step 3): the profile's pose.detector_profile, or the
    // main detector itself when the profile names none. Tests and tools that call refine_face()
    // the way the pipeline does must use this one.
    FaceDetector& pose_detector() { return pose_detector_ ? *pose_detector_ : *detector_; }
    FeatureExtractor& extractor() { return *extractor_; }
    float match_threshold() const { return threshold_; }
    void set_match_threshold(float t) { threshold_ = t; }

    // Forget all tracks (start of a new video). Results of jobs that are still running
    // for the old tracks are thrown away when they arrive.
    void reset_tracking();

private:
    struct TrackState {
        bool verified = false;
        std::string person_id, person_name;
        float last_score = 0;
        int missed_run = 0;           // frames in a row in which this track was not found
        bool has_crop = false;
        bool job_pending = false;     // worker mode: a job for this track is queued or running
        double last_crop_ms = -1e9;
        double last_check_ms = -1e9;  // last time the head pose was checked with the refined landmarks
        bool last_pose_ok = false;    // result of that check
        Pose last_pose;               // pose measured by that check
        FaceCrop crop;
    };
    struct Worker;  // the thread, its queue and its results (auth_pipeline.cpp)

    // Take over what a job found into the track's state. Returns false (and ignores the
    // result) when the track no longer exists or belongs to a time before reset_tracking().
    bool apply_result_state(GateResult& result);
    // Compare the features of all applied results with the registered people (one M:N call), or
    // with the features of the cards while a card-first check is active, and mark the matching
    // tracks verified. `out` (may be null) receives the counters and the per-face status.
    void match_results(std::vector<GateResult>& results, FrameResult* out);
    // Fill FrameResult::cards for the current frame (card-first check only).
    void fill_card_status(FrameResult& result) const;
    void sync_store_revision();

    std::unique_ptr<FaceDetector> detector_;
    std::unique_ptr<FaceDetector> pose_detector_;  // null = detector_ also does the pose check
    std::unique_ptr<FeatureExtractor> extractor_;
    FaceTracker tracker_;
    FeatureStore store_;  // the registered people; its database file is store_.path()
    uint64_t store_revision_ = 0;  // detect changes made through the public store accessor
    float threshold_ = 0.5f;  // replaced by the profile's recognizer.threshold in init()
    bool card_check_ = false;                 // a card-first check is active (begin_card_check)
    std::vector<std::string> active_cards_;   // its cards
    std::vector<float> card_best_;            // per card: the highest similarity any face has had to the owner
    std::map<int, TrackState> states_;   // by track ID
    std::vector<Face> faces_;            // reused between frames
    int generation_ = 0;                 // raised by reset_tracking(); jobs carry the number they were made in
    std::unique_ptr<Worker> worker_;     // null in synchronous mode
};

}  // namespace fa

#endif  // FA_AUTH_PIPELINE_H
