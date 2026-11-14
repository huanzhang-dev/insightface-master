// auth_pipeline.cpp - see auth_pipeline.h.
#include "auth_pipeline.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "image_ops.h"
#include "pose.h"
#include "timer.h"

namespace fa {

// ===========================================================================
// The unit of work for steps 3 and 4: "check the pose of this face and, if it is acceptable,
// store a crop and compute its feature". The same function runs inline (synchronous mode) or
// on the worker thread, so both modes behave identically.
// ===========================================================================

// Everything the job needs, copied out of the camera frame so the frame can be reused at once.
struct GateJob {
    int track_id = 0;
    int generation = 0;     // the pipeline's generation_ when the job was made
    double time_ms = 0;     // capture time of the frame the job was made from
    Image region;           // the part of the camera image around the face (box enlarged by one box size)
    Face coarse;            // the face as detected; its coordinates are inside `region`
};

struct GateResult {
    int track_id = 0;
    int generation = 0;
    double time_ms = 0;
    bool refined = false;       // the second look found the face again
    Pose pose;                  // the exact head rotation (valid when refined)
    bool pose_ok = false;       // inside the tolerance
    bool has_crop = false;      // a crop was cut out
    FaceCrop crop;
    bool has_feature = false;   // the feature of that crop was computed
    Feature feature;
    double refine_ms = 0, extract_ms = 0;  // how long each part took
    bool applied = false;       // set when the result was taken over into the track state
};

namespace {

// Run one job with the given pose detector and extractor (they must not be used by another thread
// at the same time).
GateResult run_gate_job(FaceDetector& pose_detector, FeatureExtractor& extractor, const GateJob& job) {
    GateResult r;
    r.track_id = job.track_id;
    r.generation = job.generation;
    r.time_ms = job.time_ms;

    const double start = now_ms();
    Face refined;
    if (refine_face(pose_detector, job.region, job.coarse, refined)) {  // exact landmarks -> exact pose
        r.refined = true;
        r.pose = refined.pose;
        r.pose_ok = pose_within_tolerance(refined.pose);
        if (r.pose_ok && make_face_crop(job.region, refined, r.crop)) r.has_crop = true;
    }
    r.refine_ms = now_ms() - start;

    if (r.has_crop) {
        const double extract_start = now_ms();
        r.has_feature = extractor.extract(r.crop, r.feature);
        r.extract_ms = now_ms() - extract_start;
    }
    return r;
}

// Copy the part of the frame around a face into a job. The region is the face box enlarged by one
// box width / height on every side (clipped to the frame): the pose check needs the box plus half
// a box of margin, the crop needs 30 percent. Returns false if the face is practically outside.
bool make_job(const Image& frame, const Track& track, double time_ms, int generation, GateJob& job) {
    const Rect& b = track.face.box;
    const int x0 = std::max(0, static_cast<int>(std::floor(b.x - b.w)));
    const int y0 = std::max(0, static_cast<int>(std::floor(b.y - b.h)));
    const int x1 = std::min(frame.width, static_cast<int>(std::ceil(b.right() + b.w)));
    const int y1 = std::min(frame.height, static_cast<int>(std::ceil(b.bottom() + b.h)));
    if (x1 - x0 < 8 || y1 - y0 < 8) return false;

    crop(frame, Rect{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0), static_cast<float>(y1 - y0)}, job.region);
    job.track_id = track.id;
    job.generation = generation;
    job.time_ms = time_ms;
    job.coarse = track.face;
    job.coarse.box.x -= x0;
    job.coarse.box.y -= y0;
    for (int k = 0; k < 5; ++k) {
        job.coarse.landmarks[k].x -= x0;
        job.coarse.landmarks[k].y -= y0;
    }
    return !job.region.empty();
}

}  // namespace

// ===========================================================================
// The worker thread: a queue of jobs in, a list of results out.
// ===========================================================================
struct AuthPipeline::Worker {
    std::unique_ptr<FaceDetector> pose_detector;  // the worker's own pose detector and extractor: the ones of
    std::unique_ptr<FeatureExtractor> extractor;  // the main thread keep internal buffers and are not thread-safe
                                                  // (the worker never detects faces in whole frames, only refines)
    std::thread thread;
    std::mutex mutex;                 // protects everything below
    std::condition_variable wake;     // "there is a job" or "stop"
    std::condition_variable idle;     // "the worker has nothing left to do"
    std::deque<GateJob> jobs;
    std::vector<GateResult> done;
    bool stop = false;
    bool busy = false;                // a job is running right now

    ~Worker() { shutdown(); }

    void start() { thread = std::thread([this] { loop(); }); }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }

    // Add a job. Returns false (and does nothing) when FA_WORKER_QUEUE_SIZE jobs are already waiting.
    bool enqueue(GateJob&& job) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (static_cast<int>(jobs.size()) >= FA_WORKER_QUEUE_SIZE) return false;
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
        return true;
    }

    // Move all finished results to `out` (does not wait).
    void take_finished(std::vector<GateResult>& out) {
        std::lock_guard<std::mutex> lock(mutex);
        for (GateResult& r : done) out.push_back(std::move(r));
        done.clear();
    }

    int in_flight() {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<int>(jobs.size()) + (busy ? 1 : 0);
    }

    void wait_idle() {
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock, [this] { return jobs.empty() && !busy; });
    }

    void loop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (true) {
            wake.wait(lock, [this] { return stop || !jobs.empty(); });
            if (stop) break;  // jobs that are still queued are dropped
            GateJob job = std::move(jobs.front());
            jobs.pop_front();
            busy = true;
            lock.unlock();
            GateResult result = run_gate_job(*pose_detector, *extractor, job);  // the slow part, without the lock
            lock.lock();
            done.push_back(std::move(result));
            busy = false;
            if (jobs.empty()) idle.notify_all();
        }
        busy = false;
        idle.notify_all();
    }
};

// ===========================================================================
// AuthPipeline
// ===========================================================================
AuthPipeline::AuthPipeline() = default;
AuthPipeline::~AuthPipeline() = default;  // ~Worker joins the thread

bool AuthPipeline::runs_on_cpu() const {
    bool cpu = detector_ && extractor_ && detector_->runs_on_cpu() && extractor_->runs_on_cpu();
    if (pose_detector_) cpu = cpu && pose_detector_->runs_on_cpu();
    if (worker_) cpu = cpu && worker_->pose_detector && worker_->pose_detector->runs_on_cpu() && worker_->extractor && worker_->extractor->runs_on_cpu();
    return cpu;
}

bool AuthPipeline::init(const ModelProfile& profile, const std::string& store_path, bool async_worker, DatabaseFormat db_format) {
    worker_.reset();
    // A new initialization must never inherit identities or jobs from the old database,
    // even if loading the replacement models/database fails.
    tracker_.reset();
    states_.clear();
    ++generation_;
    card_check_ = false;
    active_cards_.clear();
    card_best_.clear();
    pose_detector_.reset();
    detector_ = create_detector(profile);
    extractor_ = create_extractor(profile);
    if (!detector_ || !extractor_) return false;
    if (!profile.pose_detector_profile.empty()) {  // another detector measures the head pose
        pose_detector_ = create_pose_detector(profile);
        if (!pose_detector_) return false;
    }
    threshold_ = profile.match_threshold;

    // The database: the file (CSV or SQLite, as the user chose) is opened as a managed database
    // (feature_store.h): locked against a second program, created when missing, and from now on every
    // registration / deletion / change is written to it at once. An empty path means "no file": the
    // people live in memory only.
    if (!store_path.empty()) {
        if (!store_.open(store_path, db_format)) {
            std::cerr << "pipeline: " << store_.last_error() << "\n";
            return false;
        }
        if (store_.count() > 0 && store_.records()[0].feature.dim() != extractor_->dim()) {
            std::cerr << "pipeline: the database holds features of length " << store_.records()[0].feature.dim()
                      << " but the extractor makes " << extractor_->dim() << " (wrong profile?)\n";
            store_.close();  // release the lock again
            return false;
        }
    } else {
        store_.close();
    }

    if (async_worker) {
        // The worker gets its own copies of the networks (FA_WORKER_NETWORK_THREADS CPU threads each),
        // so it never touches the objects the main thread is using.
        ModelProfile worker_profile = profile;
        worker_profile.threads = FA_WORKER_NETWORK_THREADS;
        auto worker = std::make_unique<Worker>();
        worker->pose_detector = create_pose_detector(worker_profile);
        worker->extractor = create_extractor(worker_profile);
        if (!worker->pose_detector || !worker->extractor) {
            store_.close();  // init failed: do not keep the database locked
            return false;
        }
        worker->start();
        worker_ = std::move(worker);
    }
    store_revision_ = store_.revision();
    return true;
}

void AuthPipeline::sync_store_revision() {
    if (store_revision_ == store_.revision()) return;
    // External edits invalidate old identities and any worker results from the old gallery.
    reset_tracking();
    card_best_.assign(active_cards_.size(), kNoScore);
    store_revision_ = store_.revision();
}

void AuthPipeline::reset_tracking() {
    tracker_.reset();
    states_.clear();
    ++generation_;       // results of jobs made before this moment are thrown away when they arrive
    detector_->reset();  // detectors that follow faces between frames (Haar) forget them too
}

bool AuthPipeline::apply_result_state(GateResult& r) {
    if (r.generation != generation_) return false;     // made for tracks from before reset_tracking()
    auto it = states_.find(r.track_id);
    if (it == states_.end()) return false;             // the track is gone
    TrackState& state = it->second;

    state.job_pending = false;
    state.last_pose_ok = r.refined && r.pose_ok;
    if (r.refined) state.last_pose = r.pose;
    if (r.has_crop) {
        state.crop = r.crop;
        state.crop.track_id = r.track_id;
        state.crop.time_ms = r.time_ms;
        state.has_crop = true;
        state.last_crop_ms = r.time_ms;
    }
    r.applied = true;
    return true;
}

void AuthPipeline::match_results(std::vector<GateResult>& results, FrameResult* out) {
    std::vector<Feature> queries;   // the M features of this frame (faces that are not verified yet)
    std::vector<int> query_track;
    for (GateResult& r : results) {
        if (!r.applied) continue;
        if (out) {
            if (r.has_crop)
                for (FaceStatus& s : out->faces)
                    if (s.track_id == r.track_id) {
                        s.crop_stored = true;
                        s.crop_time_ms = r.time_ms;
                    }
            if (r.has_feature) ++out->extractions;
        }
        const TrackState& state = states_[r.track_id];
        if (r.has_feature && !state.verified) {  // a verified track needs no new check
            queries.push_back(std::move(r.feature));
            query_track.push_back(r.track_id);
        }
    }

    // Copy the new state of the tracks into the status list of the frame.
    auto publish = [this, out]() {
        if (!out) return;
        for (FaceStatus& s : out->faces) {
            const TrackState& state = states_[s.track_id];
            s.verified = state.verified;
            s.person_id = state.person_id;
            s.person_name = state.person_name;
            s.score = state.last_score;
        }
    };

    if (card_check_) {
        // CARD FIRST. The cards are looked up in the database first, and the M faces are compared with
        // exactly the N features that were retrieved for them (never with the whole database). A card
        // that is not in the database is a failed comparison; nothing falls back to a full search.
        if (queries.empty()) return;
        CardMatch compared;
        store_.match_cards(queries, active_cards_, compared);  // ONE M:N call
        if (out) out->matched_queries = static_cast<int>(queries.size());
        for (size_t q = 0; q < queries.size(); ++q) {
            TrackState& state = states_[query_track[q]];
            const CardMatch::Query& face = compared.queries[q];
            state.last_score = face.score;
            if (face.best_card >= 0 && face.score >= threshold_) {
                const Record& owner = store_.records()[compared.cards[face.best_card].record];
                state.verified = true;
                state.person_id = owner.id;
                state.person_name = owner.name;
            }
        }
        for (size_t c = 0; c < compared.cards.size() && c < card_best_.size(); ++c)
            card_best_[c] = std::max(card_best_[c], compared.cards[c].best_score);
        publish();
        return;
    }

    // ONE M:N call: the M new features of this frame against all N registered people
    if (queries.empty() || store_.count() == 0) return;
    std::vector<MatchResult> matches;
    store_.matcher().match(queries, matches);
    if (out) out->matched_queries = static_cast<int>(queries.size());
    for (size_t q = 0; q < queries.size(); ++q) {
        TrackState& state = states_[query_track[q]];
        state.last_score = matches[q].score;
        if (matches[q].index >= 0 && matches[q].score >= threshold_) {
            const Record& who = store_.records()[matches[q].index];
            state.verified = true;
            state.person_id = who.id;
            state.person_name = who.name;
        }
    }
    publish();
}

void AuthPipeline::fill_card_status(FrameResult& result) const {
    result.cards.clear();
    if (!card_check_) return;
    for (size_t c = 0; c < active_cards_.size(); ++c) {
        CardStatus status;
        status.card_id = active_cards_[c];
        status.score = c < card_best_.size() ? card_best_[c] : kNoScore;
        const Record* owner = store_.find_by_card(active_cards_[c]);  // looked up again every frame: the database may have changed
        status.found = owner != nullptr;
        if (owner) {
            status.person_id = owner->id;
            status.person_name = owner->name;
            for (const FaceStatus& face : result.faces)
                if (face.verified && face.person_id == owner->id) {
                    status.matched = true;
                    status.track_id = face.track_id;
                    break;
                }
        }
        result.cards.push_back(status);
    }
}

bool AuthPipeline::begin_card_check(const std::vector<std::string>& card_ids) {
    if (card_ids.empty()) {
        std::cerr << "card: no card given\n";
        return false;
    }
    flush();  // worker mode: take over what is in flight before the verifications are reset
    card_check_ = true;
    active_cards_ = card_ids;
    card_best_.assign(card_ids.size(), kNoScore);
    // The card information is identified first: look every card up now (match_cards does it again for every comparison).
    CardMatch probe;
    const bool all_found = store_.match_cards(std::vector<Feature>(), card_ids, probe);
    for (const CardMatch::Card& c : probe.cards)
        if (!c.found) std::cerr << "card: '" << c.card_id << "' is not in the database: the comparison for it failed\n";
    // Earlier verifications do not count for this check: every face has to match a card's owner now.
    for (auto& entry : states_) {
        TrackState& state = entry.second;
        state.verified = false;
        state.person_id.clear();
        state.person_name.clear();
        state.last_score = 0;
        state.last_crop_ms = -1e9;  // take a fresh crop at once
    }
    return all_found;
}

void AuthPipeline::end_card_check() {
    card_check_ = false;
    active_cards_.clear();
    card_best_.clear();
}
bool AuthPipeline::process(const Image& frame, double time_ms, FrameResult& result) {
    sync_store_revision();
    result = FrameResult();
    const double t_start = now_ms();

    // ---- 1. detect --------------------------------------------------------
    double t = now_ms();
    if (!detector_->detect(frame, faces_)) return false;
    result.detect_ms = now_ms() - t;

    // ---- 2. track ---------------------------------------------------------
    t = now_ms();
    tracker_.update(faces_);
    result.track_ms = now_ms() - t;

    // Forget the state of tracks that no longer exist.
    for (auto it = states_.begin(); it != states_.end();) {
        bool alive = false;
        for (const Track& tr : tracker_.tracks())
            if (tr.id == it->first) { alive = true; break; }
        it = alive ? std::next(it) : states_.erase(it);
    }

    // ---- 3a. results of jobs that finished since the last frame (worker mode) ----
    // Their state is taken over now, so the gate below knows about the new crop / pending status.
    std::vector<GateResult> results;
    if (worker_) {
        worker_->take_finished(results);
        for (GateResult& r : results)
            if (apply_result_state(r)) result.worker_ms += r.refine_ms + r.extract_ms;
    }

    // ---- 3b. gate: pose check and crop for unverified tracks ---------------
    for (const Track& tr : tracker_.tracks()) {
        TrackState& state = states_[tr.id];
        if (!tr.updated) {  // not seen in this frame: nothing to store, but count the miss
            ++state.missed_run;
            continue;
        }
        // A verified track that was lost for a few frames and is now found again may belong to
        // somebody else (another person stepped into the same spot). Verification is a statement
        // about one continuously followed face, so it is dropped and has to be earned again.
        if (state.verified && state.missed_run >= FA_REVERIFY_AFTER_MISSED_FRAMES) {
            state.verified = false;
            state.person_id.clear();
            state.person_name.clear();
            state.last_score = 0;
            state.last_crop_ms = -1e9;
        }
        state.missed_run = 0;
        // Timestamps normally only grow. If they jump back (a new recording), do not wait for
        // the old time to come round again before storing a crop.
        if (time_ms < state.last_crop_ms) state.last_crop_ms = -1e9;
        if (time_ms < state.last_check_ms) state.last_check_ms = -1e9;

        FaceStatus status;
        status.track_id = tr.id;
        status.box = tr.face.box;

        // A crop is due when the track is unverified, the face is big enough, and the
        // last crop is at least FA_CROP_STORE_INTERVAL_MS old.
        const bool crop_due = !state.verified && tr.face.box.w >= FA_MIN_CROP_FACE_PIXELS &&
                              time_ms - state.last_crop_ms >= FA_CROP_STORE_INTERVAL_MS;
        if (crop_due && !state.job_pending && time_ms - state.last_check_ms >= FA_POSE_CHECK_INTERVAL_MS) {
            GateJob job;
            if (make_job(frame, tr, time_ms, generation_, job)) {
                state.last_check_ms = time_ms;
                if (worker_) {
                    // hand the work to the worker; the answer arrives in a later frame
                    if (worker_->enqueue(std::move(job))) state.job_pending = true;
                    else state.last_check_ms = -1e9;  // queue full: try again in the next frame
                } else {
                    // do it right here
                    const double a = now_ms();
                    GateResult r = run_gate_job(pose_detector(), *extractor_, job);
                    result.gate_ms += (now_ms() - a) - r.extract_ms;
                    result.extract_ms += r.extract_ms;
                    apply_result_state(r);
                    results.push_back(std::move(r));
                }
            }
        }
        // For display: the most exact pose we have (the refined one if the track was checked).
        status.pose = state.last_check_ms > -1e8 ? state.last_pose : tr.face.pose;
        status.pose_ok = state.last_pose_ok;
        status.verified = state.verified;
        status.person_id = state.person_id;
        status.person_name = state.person_name;
        status.score = state.last_score;
        result.faces.push_back(status);
    }

    // ---- 4/5. the new features (made inline or by the worker) -> one M:N match ---
    t = now_ms();
    match_results(results, &result);
    fill_card_status(result);  // (card-first check only)
    result.match_ms = now_ms() - t;
    if (worker_) result.jobs_in_flight = worker_->in_flight();
    result.total_ms = now_ms() - t_start;
    return true;
}

void AuthPipeline::flush() {
    sync_store_revision();
    if (!worker_) return;
    worker_->wait_idle();
    std::vector<GateResult> results;
    worker_->take_finished(results);
    for (GateResult& r : results) apply_result_state(r);
    match_results(results, nullptr);
}

bool AuthPipeline::enroll_track(int track_id, const std::string& person_id, const std::string& name, const std::string& card_id) {
    flush();  // worker mode: make sure the newest crop of the track has been taken over
    auto it = states_.find(track_id);
    if (it == states_.end() || !it->second.has_crop) {
        std::cerr << "enroll: track " << track_id << " has no stored face crop\n";
        return false;
    }
    Record r;
    r.id = person_id;
    r.name = name;
    r.card_id = card_id;
    if (!extractor_->extract(it->second.crop, r.feature)) return false;
    if (!store_.add(r)) {
        std::cerr << "enroll: " << store_.last_error() << "\n";
        return false;
    }
    store_revision_ = store_.revision();
    // (the store has written the database file; if that had failed, add() would have undone the
    // registration and returned false, and nothing below would have happened)
    it->second.verified = true;  // the person is known from now on
    it->second.person_id = person_id;
    it->second.person_name = name;
    return true;
}

bool AuthPipeline::extract_from_image(const Image& photo, Feature& feature, Face* face_out) {
    std::vector<Face> faces;
    if (!detector_->detect(photo, faces) || faces.empty()) return false;
    const Face* best = &faces[0];  // the largest face wins
    for (const Face& f : faces)
        if (f.box.area() > best->box.area()) best = &f;
    if (!best->has_landmarks) return false;
    FaceCrop crop;
    if (!make_face_crop(photo, *best, crop)) return false;
    if (face_out) *face_out = *best;
    return extractor_->extract(crop, feature);
}

bool AuthPipeline::enroll_image(const Image& photo, const std::string& person_id, const std::string& name, const std::string& card_id) {
    sync_store_revision();
    Record r;
    r.id = person_id;
    r.name = name;
    r.card_id = card_id;
    if (!extract_from_image(photo, r.feature)) return false;
    if (!store_.add(r)) {
        std::cerr << "enroll: " << store_.last_error() << "\n";
        return false;
    }
    store_revision_ = store_.revision();
    return true;
}

bool AuthPipeline::remove_person(const std::string& person_id) {
    sync_store_revision();
    if (!store_.remove(person_id)) {  // unknown id, or the database file could not be written (nothing was deleted)
        std::cerr << "remove: " << store_.last_error() << "\n";
        return false;
    }
    store_revision_ = store_.revision();
    // Tracks that were verified as this person must be checked again.
    for (auto& entry : states_) {
        if (entry.second.person_id == person_id) {
            entry.second.verified = false;
            entry.second.person_id.clear();
            entry.second.person_name.clear();
            entry.second.last_crop_ms = -1e9;
        }
    }
    return true;
}

bool AuthPipeline::rename_or_refresh(const std::string& person_id, const std::string& new_name, const Image* new_photo) {
    sync_store_revision();
    Feature fresh;
    if (new_photo && !extract_from_image(*new_photo, fresh)) return false;
    if (!store_.update(person_id, new_name, fresh)) {  // unknown id, or the database file could not be written (nothing changed)
        std::cerr << "modify: " << store_.last_error() << "\n";
        return false;
    }
    store_revision_ = store_.revision();
    // Tracks that are verified as this person must follow the change: a new name is shown
    // from now on, and after a new feature the old verification no longer proves anything.
    for (auto& entry : states_) {
        TrackState& state = entry.second;
        if (!state.verified || state.person_id != person_id) continue;
        if (!new_name.empty()) state.person_name = new_name;
        if (fresh.dim() > 0) {
            state.verified = false;
            state.person_id.clear();
            state.person_name.clear();
            state.last_crop_ms = -1e9;
        }
    }
    return true;
}

}  // namespace fa
