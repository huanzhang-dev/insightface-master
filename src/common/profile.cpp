// profile.cpp - see profile.h.
#include "profile.h"

#include <cstdio>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fa {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t end = s.find(separator, start);
        if (end == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// A path is "absolute" if it has a drive letter ("C:\...") or starts with a slash.
static bool is_absolute(const std::string& p) {
    return (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\'));
}

static std::string make_path(const std::string& root, const std::string& p) {
    if (p.empty() || is_absolute(p)) return p;
    return root.empty() ? p : root + "/" + p;
}

static bool parse_number(const std::string& text, int& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}

static bool parse_number(const std::string& text, float& value) {
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

bool load_profile(const std::string& path, const std::string& root, ModelProfile& profile) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "profile: cannot open " << path << "\n";
        return false;
    }
    profile.root = root;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line_number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // UTF-8 byte order mark from some editors
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::cerr << "profile: " << path << ":" << line_number << ": expected key = value\n";
            return false;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        bool valid_number = true;

        if (key == "detector.type") profile.detector_type = value;
        else if (key == "detector.backend") profile.detector_backend = value;
        else if (key == "detector.model") profile.detector_model = make_path(root, value);
        else if (key == "detector.model_small") profile.detector_model_small = make_path(root, value);
        else if (key == "detector.small_size") valid_number = parse_number(value, profile.detector_small_size);
        else if (key == "detector.small_max_side") valid_number = parse_number(value, profile.detector_small_max_side);
        else if (key == "detector.input") profile.detector_input = value;
        else if (key == "detector.layout") profile.detector_layout = value;
        else if (key == "detector.outputs") {
            profile.detector_outputs = split(value, ',');
            for (std::string& name : profile.detector_outputs) name = trim(name);  // "out0, out1" is fine
        }
        else if (key == "detector.score") valid_number = parse_number(value, profile.detector_score);
        else if (key == "detector.haar_face") profile.haar_face_model = make_path(root, value);
        else if (key == "detector.haar_eye") profile.haar_eye_model = make_path(root, value);
        else if (key == "pose.detector_profile") profile.pose_detector_profile = make_path(root, value);
        else if (key == "recognizer.type") profile.recognizer_type = value;
        else if (key == "recognizer.backend") profile.recognizer_backend = value;
        else if (key == "recognizer.model") profile.recognizer_model = make_path(root, value);
        else if (key == "recognizer.input") profile.recognizer_input = value;
        else if (key == "recognizer.output") profile.recognizer_output = value;
        else if (key == "recognizer.dim") valid_number = parse_number(value, profile.feature_dim);
        else if (key == "recognizer.threshold") valid_number = parse_number(value, profile.match_threshold);
        else if (key == "runtime.threads") valid_number = parse_number(value, profile.threads);
        else {
            std::cerr << "profile: " << path << ":" << line_number << ": unknown key '" << key << "'\n";
            return false;
        }
        if (!valid_number) {
            std::cerr << "profile: " << path << ":" << line_number << ": invalid number for " << key << "\n";
            return false;
        }
    }
    if (!std::isfinite(profile.detector_score) || profile.detector_score < 0 || profile.detector_score > 1 ||
        !std::isfinite(profile.match_threshold) || profile.match_threshold < -1 || profile.match_threshold > 1) {
        std::cerr << "profile: " << path << ": detector.score must be 0..1 and recognizer.threshold -1..1\n";
        return false;
    }
    if (profile.threads < 1 || profile.threads > 64) {
        std::cerr << "profile: " << path << ": runtime.threads must be 1..64\n";
        return false;
    }
    if (profile.feature_dim < 1 || profile.feature_dim > FA_MAX_FEATURE_DIM) {
        std::cerr << "profile: " << path << ": recognizer.dim must be 1.." << FA_MAX_FEATURE_DIM << "\n";
        return false;
    }
    if (!profile.detector_model_small.empty() &&
        (profile.detector_small_size < 32 || profile.detector_small_size % 32 != 0 || profile.detector_small_max_side < 1)) {
        std::cerr << "profile: " << path << ": detector.small_size must be a multiple of 32 (at least 32) and "
                  << "detector.small_max_side at least 1\n";
        return false;
    }
    if (profile.detector_layout != "flat" && profile.detector_layout != "chw") {
        std::cerr << "profile: " << path << ": detector.layout must be 'flat' or 'chw', not '" << profile.detector_layout << "'\n";
        return false;
    }
    if (profile.detector_layout == "chw" && (profile.detector_type != "scrfd" || profile.detector_outputs.size() != 9)) {
        std::cerr << "profile: " << path << ": detector.layout = chw is for SCRFD and needs nine detector.outputs (scores, boxes, landmarks of the three strides)\n";
        return false;
    }
    // A misspelled backend would otherwise silently select ONNX Runtime.
    for (const std::string* backend : {&profile.detector_backend, &profile.recognizer_backend}) {
        if (*backend != "onnx" && *backend != "ncnn") {
            std::cerr << "profile: " << path << ": backend must be 'onnx' or 'ncnn', not '" << *backend << "'\n";
            return false;
        }
    }
    return true;
}

}  // namespace fa
