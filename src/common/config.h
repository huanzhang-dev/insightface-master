// config.h - every tunable number of the project lives here.
//
// Each setting is a macro with a default value. To change one, either edit this file
// or pass /DNAME=value to the compiler (see build.bat). Exceptions: the speed settings of
// the Haar detector (FA_HAAR_*) are at the top of detector_haar.cpp, and a few
// implementation details (for example the 3 degree limit of the rotation compensation in
// refine_face, or the matching IoU inside it) are fixed numbers in the code.
#ifndef FA_CONFIG_H
#define FA_CONFIG_H

// ---------------------------------------------------------------------------
// Head rotation tolerances (degrees). A face crop is stored for feature
// extraction only while |pitch|, |yaw| and |roll| are all inside these limits.
//   pitch = nodding (looking up / down)
//   yaw   = turning the head left / right
//   roll  = tilting the head toward a shoulder
// ---------------------------------------------------------------------------
#ifndef FA_POSE_PITCH_TOL_DEG
#define FA_POSE_PITCH_TOL_DEG 5.0f
#endif
#ifndef FA_POSE_YAW_TOL_DEG
#define FA_POSE_YAW_TOL_DEG 5.0f
#endif
#ifndef FA_POSE_ROLL_TOL_DEG
#define FA_POSE_ROLL_TOL_DEG 8.0f
#endif

// ---------------------------------------------------------------------------
// Face detection
// ---------------------------------------------------------------------------
// The deep detectors take a square image of this size (pixels).
#ifndef FA_DETECT_INPUT_SIZE
#define FA_DETECT_INPUT_SIZE 640
#endif
// Default minimum score of a detection (ModelProfile::detector_score starts with this
// value; the profile files override it; the Haar detector does not use it).
#ifndef FA_DETECT_SCORE_THRESHOLD
#define FA_DETECT_SCORE_THRESHOLD 0.6f
#endif
// Two boxes that overlap more than this (IoU) are treated as the same face (NMS).
#ifndef FA_DETECT_NMS_IOU
#define FA_DETECT_NMS_IOU 0.3f
#endif
// Faces smaller than this many pixels (in the ORIGINAL camera image) are ignored by the
// deep detectors. The Haar detector cannot see faces below 24 working pixels, which is
// 96 camera pixels for a 1280 pixel wide image (see FA_HAAR_WORK_WIDTH).
#ifndef FA_MIN_FACE_PIXELS
#define FA_MIN_FACE_PIXELS 40
#endif
// Most faces the detectors return per frame (the best ones; the rest are dropped).
#ifndef FA_MAX_FACES
#define FA_MAX_FACES 32
#endif

// ---------------------------------------------------------------------------
// Camera picture size
// ---------------------------------------------------------------------------
// The picture size that open_camera_source asks the camera for when the caller gives none. The camera need
// not offer exactly this: the camera mode that is closest is used (pick_camera_mode in frame_source.h), and
// the whole system works with any picture size, for example 640x480, 1280x720, 1920x1080 or 2560x1440.
#ifndef FA_CAMERA_DEFAULT_WIDTH
#define FA_CAMERA_DEFAULT_WIDTH 1920
#endif
#ifndef FA_CAMERA_DEFAULT_HEIGHT
#define FA_CAMERA_DEFAULT_HEIGHT 1080
#endif
// Camera modes that deliver fewer frames per second than this are only used when the camera has no faster
// mode (large uncompressed pictures often come at 5 or 10 frames per second through USB).
#ifndef FA_CAMERA_MIN_FPS
#define FA_CAMERA_MIN_FPS 15
#endif

// The refinement step (detector.h, refine_face) enlarges a face until it is about
// this wide before detecting its landmarks again.
#ifndef FA_REFINE_FACE_WIDTH
#define FA_REFINE_FACE_WIDTH 320
#endif
// Faces narrower than this (in the camera image) are never stored for feature
// extraction: too few pixels for a reliable feature.
#ifndef FA_MIN_CROP_FACE_PIXELS
#define FA_MIN_CROP_FACE_PIXELS 80
#endif
// A track that is still waiting for an acceptable head pose is re-checked at most
// once per this many milliseconds (the check costs one extra detector run, two when the
// head is tilted by 3 degrees or more: see refine_face).
#ifndef FA_POSE_CHECK_INTERVAL_MS
#define FA_POSE_CHECK_INTERVAL_MS 100
#endif

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------
// A detection joins an existing track when their IoU is at least this value.
#ifndef FA_TRACK_MATCH_IOU
#define FA_TRACK_MATCH_IOU 0.3f
#endif
// A track is deleted when it has been missed in MORE than this many frames in a row
// (so it survives exactly this many missed frames).
#ifndef FA_TRACK_MAX_MISSED_FRAMES
#define FA_TRACK_MAX_MISSED_FRAMES 15
#endif

// ---------------------------------------------------------------------------
// Face crop storing (authentication pipeline)
// ---------------------------------------------------------------------------
// While a track is NOT verified, its crop is refreshed at most once per this
// many milliseconds (the older crop is overwritten).
#ifndef FA_CROP_STORE_INTERVAL_MS
#define FA_CROP_STORE_INTERVAL_MS 1000
#endif
// A VERIFIED track that was not found for this many frames in a row loses its verification
// when it is found again: it may now be another person standing in the same place. (A
// shorter gap, for example one missed detection, keeps the verification.)
#ifndef FA_REVERIFY_AFTER_MISSED_FRAMES
#define FA_REVERIFY_AFTER_MISSED_FRAMES 3
#endif
// Worker thread mode of the authentication pipeline (init(..., async_worker = true)):
// the worker's own networks use this many CPU threads. 1 leaves the other core(s) to the
// detection in the main thread; more threads make the pose check and extraction finish
// sooner but compete with the detection for the CPU.
#ifndef FA_WORKER_NETWORK_THREADS
#define FA_WORKER_NETWORK_THREADS 1
#endif
// At most this many pose-check jobs wait for the worker. When it is full, the track simply
// tries again in the next frame.
#ifndef FA_WORKER_QUEUE_SIZE
#define FA_WORKER_QUEUE_SIZE 4
#endif

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------
// Largest feature length the classical extractor, the synthetic generator and the model
// profile reader (recognizer.dim) accept (the SFace model gives 128, the ArcFace model gives
// 512, the classical extractor 256), so do not lower it below 512 while an ArcFace profile
// is in use. The deep extractors themselves, the matcher and the store do not check it.
#ifndef FA_MAX_FEATURE_DIM
#define FA_MAX_FEATURE_DIM 512
#endif
// Aligned face image size given to the recognizers.
#ifndef FA_ALIGNED_FACE_SIZE
#define FA_ALIGNED_FACE_SIZE 112
#endif

#endif  // FA_CONFIG_H
