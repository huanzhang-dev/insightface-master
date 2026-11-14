// profile.h - a "model profile" tells the engines WHICH models to use.
//
// A profile is a small text file with lines like   key = value
// (lines starting with # are comments). Five ready-made profiles ship with the
// project:
//   models/permissive.ini      YuNet + SFace      (MIT / Apache-2.0 licensed models, 128-d features)
//   models/dev_noncommercial.ini  SCRFD + ArcFace (non-commercial licence, 512-d features); SCRFD runs on the smallest
//                              rectangle that holds the picture (640x384 for 16:9), see detector.layout below
//   models/dev_noncommercial_square.ini  the same, with SCRFD on a 640x640 square as before (fallback, for comparison)
//   models/dev_noncommercial_onnx.ini  the same SCRFD + ArcFace on ONNX Runtime instead of ncnn
//   models/classical.ini       Haar cascade + LBP histograms (no neural network, 256-d features)
//
// Switching models never needs a code change: pass another profile file.
#ifndef FA_PROFILE_H
#define FA_PROFILE_H

#include <string>
#include <vector>

#include "config.h"

namespace fa {

struct ModelProfile {
    // ---- detector ----
    std::string detector_type = "yunet";      // "yunet", "scrfd" (deep) or "haar" (classical)
    std::string detector_backend = "onnx";    // "onnx" or "ncnn"
    std::string detector_model;               // .onnx file, or the .param file for ncnn
    // Optional faster model for SMALL camera images (YuNet on ONNX Runtime only). It is the
    // same network with a smaller input square. Used when the longer side of the camera
    // image is at most detector_small_max_side pixels. Leave empty to always use the big one.
    std::string detector_model_small;
    int detector_small_size = 320;
    int detector_small_max_side = 640;
    std::string detector_input = "in0";       // ncnn only: name of the input blob
    std::vector<std::string> detector_outputs;  // ncnn only: names of the output blobs
    // How the SCRFD outputs are laid out (the key detector.layout):
    //   "flat" (default) the nine outputs of the original conversion: scores (after the sigmoid), boxes and landmarks
    //          as lists with one row per anchor; the network input must be a 640x640 square.
    //   "chw"  the nine raw outputs of the last convolutions (channels x rows x columns, scores before the sigmoid):
    //          the network accepts ANY input whose sides are multiples of 32, so the picture is shrunk to
    //          the smallest such rectangle (640x384 for a 16:9 camera image) instead of a 640x640 square:
    //          the same scale and the same result with about 40% less work for 16:9. Needs a model whose
    //          upsampling layers scale instead of using fixed sizes (models/dev_noncommercial/det_dyn.param).
    std::string detector_layout = "flat";
    std::string haar_face_model;              // cascade xml used by the classical detector
    std::string haar_eye_model;               // eye cascade xml (classical roll/yaw/pitch)
    float detector_score = FA_DETECT_SCORE_THRESHOLD;

    // Optional: path of ANOTHER profile file whose detector does the pose check (the "second look",
    // refine_face) instead of this profile's detector. Empty (the default) = this detector does it.
    // Why: the head rotation is read from the 5 landmarks, and SCRFD's landmarks give a much noisier
    // pitch than YuNet's (docs/RESULTS.md section 4), so models/dev_noncommercial.ini lets YuNet
    // measure the pose. Only the detector.* keys of the named profile are used; it may not name
    // a further profile itself.
    std::string pose_detector_profile;

    // ---- recognizer (feature extractor) ----
    std::string recognizer_type = "sface";    // "sface", "arcface" (deep) or "lbp" (classical)
    std::string recognizer_backend = "onnx";
    std::string recognizer_model;
    std::string recognizer_input = "in0";     // ncnn only
    std::string recognizer_output = "out0";   // ncnn only
    int feature_dim = 128;                    // length of the feature vector
    float match_threshold = 0.363f;           // cosine similarity needed to accept a match

    // ---- runtime ----
    int threads = 2;                          // CPU threads used by each network

    // Where relative paths in the file start (the folder that holds the profile's
    // project root). Filled in by load_profile().
    std::string root;
};

// Read a profile file. Returns false (and prints why) when the file is missing
// or a line is malformed or has an unknown key or an unknown backend name, or a
// numeric value is malformed/non-finite or outside its supported range. Relative
// model paths get `root + "/"` put in front of them.
bool load_profile(const std::string& path, const std::string& root, ModelProfile& profile);

// Small text helpers (also used by feature_store.cpp). split() does not trim the pieces.
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char separator);

}  // namespace fa

#endif  // FA_PROFILE_H
