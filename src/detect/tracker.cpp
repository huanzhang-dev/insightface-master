// tracker.cpp - see tracker.h.
#include "tracker.h"

#include <algorithm>

#include "detector.h"  // iou()

namespace fa {
namespace {

// Where do we expect the track's box in the new frame? Move it by its speed.
Rect predicted_box(const Track& t) {
    Rect r = t.face.box;
    const float frames = static_cast<float>(t.missed_frames + 1);
    r.x += t.vx * frames;
    r.y += t.vy * frames;
    return r;
}

struct Candidate {
    float overlap;
    int track;
    int detection;
};

}  // namespace

void FaceTracker::reset() {
    tracks_.clear();
    next_id_ = 1;
}

void FaceTracker::update(const std::vector<Face>& detections) {
    // 1. score every (track, detection) pair by box overlap
    std::vector<Candidate> pairs;
    for (int t = 0; t < static_cast<int>(tracks_.size()); ++t) {
        const Rect expected = predicted_box(tracks_[t]);
        for (int d = 0; d < static_cast<int>(detections.size()); ++d) {
            const float overlap = iou(expected, detections[d].box);
            if (overlap >= FA_TRACK_MATCH_IOU) pairs.push_back({overlap, t, d});
        }
    }
    // 2. greedy matching: best overlap first, each track/detection used once
    std::sort(pairs.begin(), pairs.end(), [](const Candidate& a, const Candidate& b) { return a.overlap > b.overlap; });
    std::vector<int> track_taken(tracks_.size(), 0), detection_taken(detections.size(), 0);

    for (const Candidate& c : pairs) {
        if (track_taken[c.track] || detection_taken[c.detection]) continue;
        track_taken[c.track] = detection_taken[c.detection] = 1;

        Track& t = tracks_[c.track];
        const Face& d = detections[c.detection];
        // speed = how far the box centre moved, per frame (smoothed)
        const float frames = static_cast<float>(t.missed_frames + 1);
        const float dx = ((d.box.x + d.box.w / 2) - (t.face.box.x + t.face.box.w / 2)) / frames;
        const float dy = ((d.box.y + d.box.h / 2) - (t.face.box.y + t.face.box.h / 2)) / frames;
        t.vx = 0.5f * t.vx + 0.5f * dx;
        t.vy = 0.5f * t.vy + 0.5f * dy;
        t.face = d;
        t.missed_frames = 0;
        t.updated = true;
        t.age_frames++;
    }

    // 3. tracks without a detection: count the miss, delete the old ones
    for (size_t t = 0; t < tracks_.size(); ++t) {
        if (track_taken[t]) continue;
        tracks_[t].missed_frames++;
        tracks_[t].updated = false;
        tracks_[t].age_frames++;
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [](const Track& t) { return t.missed_frames > FA_TRACK_MAX_MISSED_FRAMES; }),
                  tracks_.end());

    // 4. detections without a track start a new one
    for (size_t d = 0; d < detections.size(); ++d) {
        if (detection_taken[d]) continue;
        Track t;
        t.id = next_id_++;
        t.face = detections[d];
        t.updated = true;
        t.age_frames = 1;
        tracks_.push_back(t);
    }
}

}  // namespace fa
