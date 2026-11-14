// tracker.h - follows faces from frame to frame and gives each one a track ID.
//
// The ID is what the rest of the system uses to remember "this is the same
// person as in the previous frame", for example to avoid verifying the same
// face again and again.
#ifndef FA_TRACKER_H
#define FA_TRACKER_H

#include <vector>

#include "types.h"

namespace fa {

class FaceTracker {
public:
    // Give the faces detected in the newest frame. Each detection is matched to
    // the existing track whose (movement-predicted) box overlaps it most.
    // Detections without a match start a new track; tracks that were missed in more than
    // FA_TRACK_MAX_MISSED_FRAMES frames in a row are removed.
    void update(const std::vector<Face>& detections);

    // All live tracks. A track with updated == true was seen in the newest frame.
    const std::vector<Track>& tracks() const { return tracks_; }

    // Forget everything (new IDs start again at 1).
    void reset();

private:
    std::vector<Track> tracks_;
    int next_id_ = 1;
};

}  // namespace fa

#endif  // FA_TRACKER_H
