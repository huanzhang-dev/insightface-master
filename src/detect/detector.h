// detector.h - the face detection engine.
//
// A detector looks at one camera image and returns every face in it, each with
// a box, a score, 5 landmarks (when the method provides them) and the head
// rotation. Two families exist behind the same interface:
//   * deep learning:  YuNet or SCRFD neural networks   (detector_deep.cpp)
//   * classical:      Haar cascade (Viola-Jones)        (detector_haar.cpp)
#ifndef FA_DETECTOR_H
#define FA_DETECTOR_H

#include <memory>
#include <vector>

#include "profile.h"
#include "types.h"

namespace fa {

class FaceDetector {
public:
    virtual ~FaceDetector() {}

    // Find the faces in a BGR camera image of ANY size. The detector resizes the
    // image itself (step 1.1 of the requirements). The returned boxes and
    // landmarks are in the pixel coordinates of the image you passed in.
    //
    // accurate = true asks for the most exact result even if it is slower: a detector with a
    // fast small-image mode must then use its full-size network. Used by refine_face().
    virtual bool detect(const Image& frame, std::vector<Face>& faces, bool accurate = false) = 0;

    // Forget everything remembered from earlier frames. Only detectors that follow faces
    // from frame to frame (Haar) have something to forget; the default does nothing.
    virtual void reset() {}

    // Short name for logs, e.g. "yunet/onnxruntime".
    virtual const char* name() const = 0;

    // The detector type of the profile ("yunet", "scrfd" or "haar"). Selects the pose model.
    virtual const char* type() const = 0;

    // True when this detector runs on the CPU (every detector of this project does; no GPU is used anywhere).
    // The deep detectors ask their networks (Net::runs_on_cpu); the classical Haar detector is plain CPU code.
    virtual bool runs_on_cpu() const { return true; }
};

// Build the detector described by the profile (detector.type = yunet, scrfd or
// haar). Returns nullptr and prints the reason when a model cannot be loaded.
std::unique_ptr<FaceDetector> create_detector(const ModelProfile& profile);

// Build the detector that does the POSE CHECK (refine_face) for this profile: the one named by
// the profile's pose.detector_profile key, or (key absent) a second instance of the profile's own
// detector. The result has its own networks, so it may run on another thread than the detector of
// create_detector(). It gets the thread count of `profile`. Returns nullptr and prints the reason
// when the named profile cannot be read, names a further pose profile, or its models do not load.
std::unique_ptr<FaceDetector> create_pose_detector(const ModelProfile& profile);

// The individual builders (create_detector calls one of them).
std::unique_ptr<FaceDetector> create_deep_detector(const ModelProfile& profile);
std::unique_ptr<FaceDetector> create_haar_detector(const ModelProfile& profile);

// Second look at one face ("refinement"). A landmark network is least exact for
// SMALL faces (a face 120 pixels wide has only 60 pixels in the 640x640 network
// image). This cuts the face out of the camera image with a margin, enlarges it so
// the face is about FA_REFINE_FACE_WIDTH pixels wide, and runs the detector again
// on that crop. The landmarks (and so the pose and the alignment) are then much
// more exact. If the first result shows a head tilt of 3 degrees or more, the crop is
// also turned by that tilt and detected once more ("rotation compensation"), because
// a landmark network under-reports a strong tilt. Both detector calls use accurate =
// true (the full-size network). Returns false when the detector finds no face in the
// crop. `refined` holds the box, the landmarks and the pose in the camera image
// coordinates. For the Haar detector only the roll comes from the refined landmarks;
// its yaw and pitch stay the detector's own estimates (see detector_haar.cpp).
bool refine_face(FaceDetector& detector, const Image& frame, const Face& coarse, Face& refined);

// ---- helpers shared by the detectors and the tests ----

// Intersection-over-union of two boxes: 0 = no overlap, 1 = identical.
float iou(const Rect& a, const Rect& b);

// Keep the best-scoring box of every group of overlapping boxes.
void non_max_suppression(std::vector<Face>& faces, float iou_threshold);

}  // namespace fa

#endif  // FA_DETECTOR_H
