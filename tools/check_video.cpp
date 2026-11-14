// check_video.cpp - play a video through the whole authentication system and check the result.
//
//   check_video <profile.ini> <video.mp4 | folder of frames> [options]
//
//   --async          use the pipeline's worker thread and play the video in real time
//   --realtime       play in real time without the worker (to see what the delays are like)
//   --fps N          frame rate of a folder of frames (default 15); a video file brings its own times
//   --scenario FILE  who is registered (default: scenario.txt next to the video; written by gen_video)
//   --truth FILE     the true face boxes (default: truth.csv next to the video; written by gen_video)
//   --fillers N      register N more people who are not in the video, so the matcher has a real
//                    gallery to search (default 20)
//   --out FILE.mp4   write the annotated video here (default: <video folder>/annotated_<mode>.mp4)
//
// What it does
//   1. registers the people marked "registered" in scenario.txt from their portrait photo (and the fillers);
//   2. plays the video frame by frame through AuthPipeline, exactly as camera frames would arrive;
//   3. compares what the system found with the truth: was every face detected, did every person keep
//      one track ID, was the registered person recognized (and as the right person), was every stranger
//      refused, how long did recognition take, how long did each frame take;
//   4. writes an annotated video: green box = verified (name and score shown), yellow = not (yet)
//      verified, thin gray box = the true face position, text = track ID, head rotation, timings;
//   5. prints a report and returns 0 only if every check passed.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fs_compat.h"
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "auth_pipeline.h"
#include "draw_text.h"
#include "frame_source.h"
#include "image_io.h"
#include "image_ops.h"
#include "timer.h"
#include "video_writer.h"

namespace fs = fa::fs;
using namespace fa;

struct TruthRow {
    std::string label;
    Rect box;
    bool visible = false;
};

struct ActorReport {
    bool registered = false;
    int frames_visible = 0, frames_detected = 0;
    std::set<int> track_ids;
    double first_seen_ms = -1, first_verified_ms = -1;
    std::string verified_as;
    bool wrong_person = false;
    int verified_frames = 0, unverified_frames = 0;
    int previous_track = -1, id_changes = 0;
};

static std::vector<std::string> split_line(const std::string& line, char sep) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, sep)) parts.push_back(item);
    return parts;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) {
        std::printf("usage: check_video <profile.ini> <video.mp4 | frames folder> [--async] [--realtime] [--fps N] [--scenario F] [--truth F] [--fillers N] [--out F.mp4] [--artifacts DIR] [--portraits DIR] [--portrait-file NAME]\n");
        return 2;
    }
    const std::string profile_path = argv[1], input = argv[2];
    bool async = false, realtime = false;
    double fps = 15;
    int fillers = 20;
    std::string scenario_path, truth_path, out_path;
    std::string portraits = "data/portraits", portrait_file = "117th.jpg", artifacts;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--portraits") && i + 1 < argc) portraits = argv[++i];
        else if (!std::strcmp(argv[i], "--portrait-file") && i + 1 < argc) portrait_file = argv[++i];
        else if (!std::strcmp(argv[i], "--artifacts") && i + 1 < argc) artifacts = argv[++i];
        else if (!std::strcmp(argv[i], "--async")) async = true;
        else if (!std::strcmp(argv[i], "--realtime")) realtime = true;
        else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) fps = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc) scenario_path = argv[++i];
        else if (!std::strcmp(argv[i], "--truth") && i + 1 < argc) truth_path = argv[++i];
        else if (!std::strcmp(argv[i], "--fillers") && i + 1 < argc) fillers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
    }
    const bool is_folder = fs::is_directory(input);
    const fs::path base = is_folder ? fs::path(input) : fs::path(input).parent_path();
    if (scenario_path.empty()) scenario_path = (base / "scenario.txt").string();
    if (truth_path.empty()) truth_path = (base / "truth.csv").string();
    if (out_path.empty()) out_path = (base / (async ? "annotated_worker.mp4" : "annotated_sync.mp4")).string();
    if (async) realtime = true;

    ModelProfile profile;
    if (!load_profile(profile_path, ".", profile)) return 2;
    if (!artifacts.empty()) fs::create_directories(artifacts);
    const std::string csv = artifacts.empty() ? "build\\check_video.csv" : (fs::path(artifacts) / "people.csv").string();
    const std::string frames_csv = artifacts.empty() ? "build\\check_video_frames.csv" : (fs::path(artifacts) / "frames.csv").string();
    const fs::path still_dir = artifacts.empty() ? base / "check" : fs::path(artifacts) / "stills";
    std::remove(csv.c_str());
    AuthPipeline pipeline;
    if (!pipeline.init(profile, csv, async)) return 2;
    std::printf("profile %s: detector %s, extractor %s (%d numbers, threshold %.2f), %s\n", profile_path.c_str(),
                pipeline.detector().name(), pipeline.extractor().name(), pipeline.extractor().dim(), pipeline.match_threshold(),
                async ? "worker thread, real time" : (realtime ? "synchronous, real time" : "synchronous, as fast as possible"));

    std::printf("all engines run on the CPU: %s\n", pipeline.runs_on_cpu() ? "yes (no GPU is used)" : "NO");

    // ---- 1. the scenario: who is in the video, who is registered ----
    std::map<std::string, ActorReport> actors;
    std::set<std::string> in_video;
    {
        std::ifstream f(scenario_path);
        if (!f) { std::printf("no scenario file %s (make one with gen_video)\n", scenario_path.c_str()); return 2; }
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string word, label, kind;
            ss >> word >> label >> kind;
            if (word != "person" || label.empty()) continue;
            actors[label].registered = (kind == "registered");
            in_video.insert(label);
        }
    }
    int enrolled = 0;
    for (auto& entry : actors) {
        if (!entry.second.registered) continue;
        Image photo;
        if (!load_image((fs::path(portraits) / entry.first / portrait_file).string(), photo) ||
            !pipeline.enroll_image(photo, entry.first, "Registered " + entry.first)) {
            std::printf("cannot register %s\n", entry.first.c_str());
            return 2;
        }
        ++enrolled;
    }
    int filler_count = 0;
    for (const auto& dir : fs::directory_iterator(portraits)) {
        if (filler_count >= fillers) break;
        const std::string label = dir.path().filename().string();
        if (!fs::is_directory(dir.path()) || in_video.count(label)) continue;
        Image photo;
        if (load_image((dir.path() / portrait_file).string(), photo) && pipeline.enroll_image(photo, label, "Filler " + label)) ++filler_count;
    }
    std::printf("registered: %d people from the scenario + %d fillers (the matcher searches %d people)\n", enrolled, filler_count,
                pipeline.store().count());

    // ---- 2. the true face positions ----
    std::map<int, std::vector<TruthRow>> truth;
    {
        std::ifstream f(truth_path);
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            const auto p = split_line(line, ',');
            if (p.size() < 9) continue;
            TruthRow r;
            r.label = p[2];
            r.box = {static_cast<float>(std::atof(p[3].c_str())), static_cast<float>(std::atof(p[4].c_str())),
                     static_cast<float>(std::atof(p[5].c_str())), static_cast<float>(std::atof(p[6].c_str()))};
            r.visible = std::atoi(p[8].c_str()) != 0;
            truth[std::atoi(p[0].c_str())].push_back(r);
        }
        if (truth.empty()) { std::printf("no truth in %s\n", truth_path.c_str()); return 2; }
    }

    // ---- 3. play ----
    std::unique_ptr<FrameSource> source = is_folder ? open_sequence_source(input, fps) : open_video_file_source(input);
    if (!source) { std::printf("cannot open %s\n", input.c_str()); return 2; }

    VideoWriter writer;
    std::ofstream log_csv(frames_csv);
    if (!log_csv) { std::printf("cannot write frame log\n"); return 2; }
    log_csv << "frame,time_ms,track,x,y,w,h,pitch,yaw,roll,verified,person,score,process_ms,jobs_in_flight\n";
    TimingStats frame_time;
    Image frame, shown;
    double t_ms = 0, previous_t = -1;
    const int expected_frames = static_cast<int>(truth.size());
    int index = 0, false_detections = 0, total_detections = 0, process_failures = 0, write_failures = 0, missing_truth_frames = 0;
    const double wall_start = now_ms();
    std::map<std::string, Image> stills;  // a few annotated pictures to look at
    std::set<std::string> still_keys;
    while (source->next(frame, t_ms)) {
        if (!truth.count(index)) ++missing_truth_frames;
        if (realtime) {
            const double wait = t_ms - (now_ms() - wall_start);
            if (wait > 1.0) Sleep(static_cast<DWORD>(wait));
        }
        FrameResult r;
        if (!pipeline.process(frame, t_ms, r)) { ++index; ++process_failures; continue; }
        frame_time.add(r.total_ms);

        // ---- compare with the truth of this frame ----
        std::vector<char> face_used(r.faces.size(), 0);
        for (const TruthRow& tr : truth[index]) {
            if (!tr.visible) continue;
            ActorReport& a = actors[tr.label];
            ++a.frames_visible;
            int best = -1;
            float best_iou = 0.4f;
            for (size_t k = 0; k < r.faces.size(); ++k) {
                if (face_used[k]) continue;
                const float v = iou(r.faces[k].box, tr.box);
                if (v > best_iou) { best_iou = v; best = static_cast<int>(k); }
            }
            if (best < 0) continue;
            face_used[best] = 1;
            const FaceStatus& s = r.faces[best];
            ++a.frames_detected;
            if (a.first_seen_ms < 0) a.first_seen_ms = t_ms;
            a.track_ids.insert(s.track_id);
            if (a.previous_track >= 0 && a.previous_track != s.track_id) ++a.id_changes;
            a.previous_track = s.track_id;
            if (s.verified) {
                ++a.verified_frames;
                if (a.first_verified_ms < 0) { a.first_verified_ms = t_ms; a.verified_as = s.person_id; }
                if (s.person_id != tr.label) a.wrong_person = true;
            } else {
                ++a.unverified_frames;
            }
        }
        for (size_t k = 0; k < r.faces.size(); ++k) { ++total_detections; if (!face_used[k]) ++false_detections; }

        // ---- the annotated picture ----
        std::vector<std::string> new_keys;  // stills to keep from this frame
        shown = frame;
        for (const TruthRow& tr : truth[index])
            if (tr.visible) draw_rect(shown, tr.box, 150, 150, 150, 1);
        for (const FaceStatus& s : r.faces) {
            const bool ok = s.verified;
            draw_rect(shown, s.box, ok ? 60 : 0, ok ? 220 : 220, ok ? 60 : 255, 3);  // BGR: green or yellow
            char text[160];
            if (ok) std::snprintf(text, sizeof text, "#%d  %s  %.2f", s.track_id, s.person_id.c_str(), s.score);
            else std::snprintf(text, sizeof text, "#%d  not verified%s", s.track_id, s.score > 0 ? "" : "");
            draw_text(shown, static_cast<int>(s.box.x), std::max(0, static_cast<int>(s.box.y) - 30), text, ok ? 60 : 0, ok ? 255 : 230, ok ? 60 : 255, 22);
            char pose[96];
            std::snprintf(pose, sizeof pose, "pitch %+.0f  yaw %+.0f  roll %+.0f%s", s.pose.pitch, s.pose.yaw, s.pose.roll, s.pose_ok ? "" : "  (outside gate)");
            draw_text(shown, static_cast<int>(s.box.x), static_cast<int>(s.box.bottom()) + 4, pose, 255, 255, 255, 18);
            log_csv << index << ',' << t_ms << ',' << s.track_id << ',' << s.box.x << ',' << s.box.y << ',' << s.box.w << ',' << s.box.h << ','
                    << s.pose.pitch << ',' << s.pose.yaw << ',' << s.pose.roll << ',' << (s.verified ? 1 : 0) << ',' << s.person_id << ','
                    << s.score << ',' << r.total_ms << ',' << r.jobs_in_flight << '\n';
            const std::string key = ok ? "verified_" + s.person_id : "unverified_first";
            if (!still_keys.count(key)) { still_keys.insert(key); new_keys.push_back(key); }  // saved after ALL faces are drawn
        }
        char head[200];
        std::snprintf(head, sizeof head, "frame %d  t=%.2fs  %s   process %.1f ms%s", index, t_ms / 1000.0, async ? "WORKER THREAD" : "synchronous",
                      r.total_ms, async ? "" : (r.gate_ms + r.extract_ms > 1 ? "  (pose check + extraction in this frame)" : ""));
        draw_text(shown, 12, 10, head, 255, 255, 255, 24);
        for (const std::string& key : new_keys) stills[key] = shown;  // the picture is complete now

        if (!writer.frames_written() && !writer.open(out_path, shown.width - shown.width % 2, shown.height - shown.height % 2,
                                                     previous_t >= 0 ? 1000.0 / std::max(1.0, t_ms - previous_t) : fps))
            std::printf("warning: the annotated video cannot be written\n");
        if (shown.width % 2 == 0 && shown.height % 2 == 0 && !writer.write(shown)) ++write_failures;
        previous_t = t_ms;
        ++index;
    }
    pipeline.flush();
    writer.close();
    const double wall_s = (now_ms() - wall_start) / 1000.0;

    // ---- 4. the report ----
    std::printf("\nplayed %d frames in %.1f s%s\n", index, wall_s, realtime ? " (real time)" : "");
    std::printf("time per frame in process(): median %.1f ms, p95 %.1f ms, p99 %.1f ms, max %.1f ms\n", frame_time.median(), frame_time.p95(),
                frame_time.percentile(99), frame_time.max());
    int failures = 0;
    auto check = [&](bool ok, const std::string& text) {
        std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", text.c_str());
        if (!ok) ++failures;
    };
    check(index > 0 && index == expected_frames, "decoded frame count equals ground-truth frame count");
    check(missing_truth_frames == 0, "every decoded frame has ground truth");
    check(process_failures == 0, "every decoded frame processed successfully");
    check(write_failures == 0 && writer.frames_written() == index, "every annotated frame written successfully");
    std::printf("\nper person:\n");
    for (auto& entry : actors) {
        const std::string& label = entry.first;
        const ActorReport& a = entry.second;
        std::printf("- %s (%s): visible in %d frames, detected in %d, track IDs:", label.c_str(), a.registered ? "REGISTERED" : "stranger",
                    a.frames_visible, a.frames_detected);
        for (int id : a.track_ids) std::printf(" %d", id);
        if (a.first_verified_ms >= 0)
            std::printf("; first verified as '%s' %.2f s after he was first seen (%d frames verified, %d not)\n", a.verified_as.c_str(),
                        (a.first_verified_ms - a.first_seen_ms) / 1000.0, a.verified_frames, a.unverified_frames);
        else
            std::printf("; never verified\n");
        check(a.frames_visible > 0 && a.frames_detected >= 0.9 * a.frames_visible, label + ": face detected in at least 90% of the frames where it is visible");
        std::printf("  ID changes: %d\n", a.id_changes);
        check(a.track_ids.size() == 1 && a.id_changes == 0, label + ": the person keeps one track ID");
        if (a.registered) {
            check(a.first_verified_ms >= 0 && a.verified_as == label, label + ": recognized as the right person");
            check(!a.wrong_person, label + ": never reported as somebody else");
            check(a.first_verified_ms >= 0 && a.first_verified_ms - a.first_seen_ms <= 4000, label + ": recognized within 4 s of first being seen");
        } else {
            check(a.first_verified_ms < 0, label + ": the stranger is never verified");
        }
    }
    std::printf("\ndetections that match no true face: %d of %d\n", false_detections, total_detections);
    check(false_detections <= total_detections / 50, "at most 2% of the detections are false");
    if (async) check(frame_time.max() < 60.0, "worker mode: no frame took 60 ms or more");

    // ---- 5. files ----
    fs::create_directories(still_dir);
    for (const auto& s : stills) save_image((still_dir / (s.first + ".png")).string(), s.second);
    std::printf("\nannotated video: %s\nper-frame log:   %s\nstill pictures:  %s\n", out_path.c_str(),
                frames_csv.c_str(), still_dir.string().c_str());
    std::printf("\n%s (%d failed checks)\n", failures ? "VIDEO CHECK FAILED" : "VIDEO CHECK PASSED", failures);
    return failures ? 1 : 0;
}
