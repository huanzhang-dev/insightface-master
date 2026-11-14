// NOMINMAX is supplied by the compile flags.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include "auth_pipeline.h"
#include "image_io.h"
#include "sqlite3.h"
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// A minimal ONNX Constant graph encoded as protobuf, so these fixtures need no Python package.
static std::string proto_uint(unsigned long long value) {
    std::string bytes;
    while (value > 127) { bytes += static_cast<char>((value & 127) | 128); value >>= 7; }
    bytes += static_cast<char>(value);
    return bytes;
}
static std::string proto_integer(unsigned field, unsigned long long value) {
    return proto_uint(field << 3) + proto_uint(value);
}
static std::string proto_bytes(unsigned field, const std::string& bytes) {
    return proto_uint((field << 3) | 2) + proto_uint(bytes.size()) + bytes;
}
static std::string onnx_value(const std::string& name, unsigned type, const std::vector<int>& dimensions) {
    std::string shape;
    for (int dimension : dimensions) shape += proto_bytes(1, proto_integer(1, dimension));
    return proto_bytes(1, name) + proto_bytes(2, proto_bytes(1, proto_integer(1, type) + proto_bytes(2, shape)));
}
static bool write_constant_onnx(const std::string& path, unsigned type, const std::string& scalar) {
    std::string raw;
    for (int i = 0; i < 128; ++i) raw += scalar;
    const std::string tensor = proto_integer(1, 1) + proto_integer(1, 128) + proto_integer(2, type) + proto_bytes(9, raw);
    const std::string attribute = proto_bytes(1, "value") + proto_integer(20, 4) + proto_bytes(5, tensor);
    const std::string node = proto_bytes(2, "out0") + proto_bytes(4, "Constant") + proto_bytes(5, attribute);
    const std::string graph = proto_bytes(1, node) + proto_bytes(2, "output_type_boundary") +
        proto_bytes(11, onnx_value("in0", 1, {1, 3, 112, 112})) + proto_bytes(12, onnx_value("out0", type, {1, 128}));
    const std::string model = proto_integer(1, 8) + proto_bytes(8, proto_integer(2, 11)) + proto_bytes(7, graph);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(model.data(), model.size());
    return static_cast<bool>(file);
}

// Flat SCRFD, CHW SCRFD and YuNet graphs with one valid candidate, or a bad score/box/landmark.
static bool write_detector_onnx(const std::string& path, int layout, int bad_group, float bad_value) {
    const bool yunet = layout == 2;
    std::string nodes, outputs;
    for (int group = 0; group < (yunet ? 4 : 3); ++group) {
        for (int level = 0; level < 3; ++level) {
            const int stride = 8 << level, cols = 640 / stride, cells = cols * cols;
            const int anchors = yunet ? cells : cells * 2;
            const int components = yunet ? (group < 2 ? 1 : group == 2 ? 4 : 10) : (group == 0 ? 1 : group == 1 ? 4 : 10);
            const int cell = 5 * cols + 5;
            const int candidate = yunet || layout == 1 ? cell : 2 * cell;
            const int box_group = yunet ? 2 : 1, landmark_group = yunet ? 3 : 2;
            std::vector<float> values(anchors * components, group == box_group && !yunet ? 3.0f : 0.0f);
            if (layout == 1 && group == 0) values.assign(values.size(), -100.0f);
            if (level == 0) {
                if (group == 0 || (yunet && group == 1)) values[candidate] = layout == 1 ? 3.0f : 0.9f;
                if (yunet && group == box_group) {
                    values[4 * candidate + 2] = std::log(6.0f);
                    values[4 * candidate + 3] = std::log(6.0f);
                }
                const int selected_group = bad_group == 0 ? 0 : bad_group == 1 ? box_group : landmark_group;
                if (bad_group >= 0 && group == selected_group) {
                    int index = layout == 1 ? cell : components * candidate;
                    if (yunet && group == box_group) index += 2;  // width exponent also exercises decoded overflow
                    values[index] = bad_value;
                }
            }
            const std::string name = "out" + std::to_string(group * 3 + level);
            const std::string raw(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
            const std::string tensor = proto_integer(1, values.size()) + proto_integer(2, 1) + proto_bytes(9, raw);
            const std::string attribute = proto_bytes(1, "value") + proto_integer(20, 4) + proto_bytes(5, tensor);
            nodes += proto_bytes(1, proto_bytes(2, name) + proto_bytes(4, "Constant") + proto_bytes(5, attribute));
            outputs += proto_bytes(12, onnx_value(name, 1, {static_cast<int>(values.size())}));
        }
    }
    const std::string graph = nodes + proto_bytes(2, "detector_finite_boundary") +
        proto_bytes(11, onnx_value("in0", 1, {1, 3, 640, 640})) + outputs;
    const std::string model = proto_integer(1, 8) + proto_bytes(8, proto_integer(2, 11)) + proto_bytes(7, graph);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(model.data(), model.size());
    return static_cast<bool>(file);
}

int main() {
    const char* bad_settings[] = {
        "recognizer.threshold=nonsense", "recognizer.threshold=nan", "recognizer.threshold=inf",
        "recognizer.threshold=1.01", "recognizer.threshold=-1.01", "detector.score=nan",
        "detector.score=-0.1", "detector.score=1.1", "runtime.threads=2junk",
        "runtime.threads=999999999999999999999", "recognizer.dim=128junk",
        "detector.small_size=320junk", "detector.small_max_side=640junk", "detector.score="
    };
    for (const char* setting : bad_settings) {
        std::ofstream file("build/reinspect_bad.ini"); file << setting << "\n"; file.close();
        fa::ModelProfile p; CHECK(!fa::load_profile("build/reinspect_bad.ini", ".", p));
    }
    for (const char* setting : {"recognizer.threshold=-1", "recognizer.threshold=1", "detector.score=0", "detector.score=1", "runtime.threads=64"}) {
        std::ofstream file("build/reinspect_bad.ini"); file << setting << "\n"; file.close();
        fa::ModelProfile p; CHECK(fa::load_profile("build/reinspect_bad.ini", ".", p));
    }
    {
        std::ofstream file("build/reinspect_bad.ini");
        file << "recognizer.threshold = nonsense\ndetector.score = nan\nruntime.threads = 2junk\n";
        file.close();fa::ModelProfile p;
        bool ok=fa::load_profile("build/reinspect_bad.ini",".",p);
        CHECK(!ok);
        std::printf("PROFILE: loaded=%d, threshold=%g, detector_score_finite=%d, threads=%d\n",ok,p.match_threshold,std::isfinite(p.detector_score),p.threads);
    }
    fa::Image photo;
    if(!fa::load_image("data/portraits/adam_kinzinger/117th.jpg",photo))return 2;
    {
        fa::ModelProfile p;
        if(!fa::load_profile("models/dev_noncommercial_square.ini",".",p))return 3;
        p.detector_outputs.assign(9,p.detector_outputs[2]);
        auto detector=fa::create_detector(p);
        if(!detector)return 4;
        std::vector<fa::Face> faces;
        bool ok=detector->detect(photo,faces);
        CHECK(!ok && faces.empty());
        std::printf("DETECTOR WRONG OUTPUTS: returned_success=%d, faces=%zu\n",ok,faces.size());
    }
    {
        std::ofstream param("build/reinspect_zero.param");
        param << "7767517\n2 2\nInput input 0 1 in0\nReduction sum 1 1 in0 out0 0=0 1=1\n";
        param.close();std::ofstream bin("build/reinspect_zero.bin",std::ios::binary);bin.close();
        fa::ModelProfile p;p.recognizer_type="sface";p.recognizer_backend="ncnn";
        p.recognizer_model="build/reinspect_zero.param";p.recognizer_output="out0";p.feature_dim=1;
        auto extractor=fa::create_extractor(p);
        fa::Image aligned;aligned.create(112,112,3);
        fa::Feature feature;
        bool ok=extractor && extractor->extract_aligned(aligned,feature);
        double norm=0;for(float v:feature.values)norm+=double(v)*v;
        CHECK(!ok && feature.values.empty());
        std::printf("ZERO EXTRACTOR: returned_success=%d, dimensions=%d, squared_norm=%g\n",ok,feature.dim(),norm);
        p.recognizer_input = "missing_input";
        auto wrong_input = fa::create_extractor(p);
        CHECK(wrong_input && !wrong_input->extract_aligned(aligned, feature) && feature.values.empty());

    }
    {
        fa::ModelProfile p;if(!fa::load_profile("models/permissive.ini",".",p))return 5;
        const std::string path="build/reinspect_reinit_"+std::to_string(GetCurrentProcessId())+".csv";
        {
            fa::AuthPipeline pipeline;
            if(!pipeline.init(p,"",false)||!pipeline.enroll_image(photo,"AUDIT-OLD","Old person"))return 6;
            fa::FrameResult before;
            bool verified=false;
            for(int i=0;i<8 && !verified;++i){pipeline.process(photo,i*1000.0,before);for(const auto& f:before.faces)verified=verified||f.verified;}
            bool reinitialized=pipeline.init(p,path,false);
            fa::FrameResult after;pipeline.process(photo,9000,after);
            bool retained=false;for(const auto& f:after.faces)retained=retained||f.verified;
            CHECK(verified && reinitialized && pipeline.store().count() == 0 && !retained);
            std::printf("REINIT: before_verified=%d, init_success=%d, new_database_count=%d, after_verified=%d\n",verified,reinitialized,pipeline.store().count(),retained);
        }
        DeleteFileA(path.c_str());DeleteFileA((path+".lock").c_str());
    }

    // A calibration may be malformed, or valid but center this exact input to zero.
    const std::string calibration_root = "build/boundary_lbp_" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(calibration_root.c_str(), nullptr);
    CreateDirectoryA((calibration_root + "/models").c_str(), nullptr);
    fa::Image aligned;
    aligned.create(112, 112, 3);
    for (size_t i = 0; i < aligned.data.size(); ++i) aligned.data[i] = static_cast<unsigned char>(i % 251);
    for (int mode = 0; mode < 3; ++mode) {
        std::vector<float> mean(fa::lbp_raw_size(), 0);
        std::vector<float> spread(fa::lbp_raw_size(), mode == 0 ? 0 : std::numeric_limits<float>::quiet_NaN());
        if (mode == 2) {
            CHECK(fa::lbp_raw_histogram(aligned, mean));
            spread.assign(fa::lbp_raw_size(), 1);
        }
        CHECK(fa::lbp_save_calibration(calibration_root + "/models/lbp_calibration.bin", mean, spread));
        fa::ModelProfile p;
        p.root = calibration_root;
        p.recognizer_type = "lbp";
        p.feature_dim = 256;
        auto extractor = fa::create_extractor(p);
        fa::Feature feature;
        if (mode < 2) CHECK(!extractor);
        else CHECK(extractor && !extractor->extract_aligned(aligned, feature) && feature.values.empty());
    }
    DeleteFileA((calibration_root + "/models/lbp_calibration.bin").c_str());
    RemoveDirectoryA((calibration_root + "/models").c_str());
    RemoveDirectoryA(calibration_root.c_str());

    // Exercise public store edits in both inline and worker modes.
    for (bool async : {false, true}) {
        fa::ModelProfile p;
        CHECK(fa::load_profile("models/permissive.ini", ".", p));
        fa::AuthPipeline pipeline;
        CHECK(pipeline.init(p, "", async));
        CHECK(pipeline.enroll_image(photo, "BOUNDARY", "Original", "CARD-OLD"));
        fa::FrameResult frame;
        bool verified = false;
        for (int i = 0; i < 8 && !verified; ++i) {
            CHECK(pipeline.process(photo, i * 1000.0, frame));
            pipeline.flush();
            CHECK(pipeline.process(photo, i * 1000.0 + 1, frame));
            for (const auto& face : frame.faces) verified = verified || face.verified;
        }
        CHECK(verified);
        const auto revision = pipeline.store().revision();
        CHECK(!pipeline.store().remove("unknown"));
        CHECK(pipeline.store().revision() == revision);
        CHECK(pipeline.store().update("BOUNDARY", "Updated", fa::Feature()));
        CHECK(pipeline.process(photo, 9000, frame));
        pipeline.flush();
        CHECK(pipeline.process(photo, 9001, frame));
        for (const auto& face : frame.faces) CHECK(!face.verified || face.person_name == "Updated");
        const fa::Feature original_feature = pipeline.store().records()[0].feature;
        fa::Feature replaced_feature = original_feature;
        for (float& value : replaced_feature.values) value = -value;
        CHECK(pipeline.store().update("BOUNDARY", "", replaced_feature));
        CHECK(pipeline.process(photo, 9500, frame));
        pipeline.flush();
        CHECK(pipeline.process(photo, 9501, frame));
        for (const auto& face : frame.faces) CHECK(!face.verified);
        CHECK(pipeline.store().update("BOUNDARY", "", original_feature));
        CHECK(pipeline.store().set_card_id("BOUNDARY", "CARD-NEW"));
        CHECK(pipeline.begin_card_check({"CARD-OLD"}) == false);
        CHECK(pipeline.process(photo, 10000, frame));
        pipeline.flush();
        CHECK(pipeline.process(photo, 10001, frame));
        for (const auto& face : frame.faces) CHECK(!face.verified);
        pipeline.end_card_check();
        verified = false;
        for (int i = 0; i < 8 && !verified; ++i) {
            CHECK(pipeline.process(photo, 11000 + i * 1000.0, frame));
            pipeline.flush();
            CHECK(pipeline.process(photo, 11001 + i * 1000.0, frame));
            for (const auto& face : frame.faces) verified = verified || face.verified;
        }
        CHECK(verified);
        CHECK(pipeline.store().remove("BOUNDARY"));
        CHECK(pipeline.process(photo, 20000, frame));
        pipeline.flush();
        CHECK(pipeline.process(photo, 20001, frame));
        CHECK(pipeline.store().count() == 0);
        for (const auto& face : frame.faces) CHECK(!face.verified);
    }


    // FLOAT outputs retain their values; non-FLOAT tensors must not be reinterpreted as floats.
    for (unsigned type : {1u, 11u, 7u, 2u}) {  // FLOAT, DOUBLE, INT64, UINT8
        const std::string path = "build/boundary_output_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(type) + ".onnx";
        const float float_one = 1;
        const double double_one = 1;
        const int64_t integer_one = 1;
        std::string scalar(1, '\1');
        if (type == 1) scalar.assign(reinterpret_cast<const char*>(&float_one), sizeof(float_one));
        if (type == 11) scalar.assign(reinterpret_cast<const char*>(&double_one), sizeof(double_one));
        if (type == 7) scalar.assign(reinterpret_cast<const char*>(&integer_one), sizeof(integer_one));
        CHECK(write_constant_onnx(path, type, scalar));
        fa::ModelProfile p;
        p.recognizer_type = "sface";
        p.recognizer_backend = "onnx";
        p.recognizer_model = path;
        p.feature_dim = 128;
        auto extractor = fa::create_extractor(p);
        CHECK(extractor);
        fa::Feature feature;
        if (extractor) {
            const bool extracted = extractor->extract_aligned(aligned, feature);
            if (type == 1) {
                CHECK(extracted && feature.dim() == 128);
                for (float value : feature.values) CHECK(std::fabs(value - 1.0f / std::sqrt(128.0f)) < 1e-6f);
            } else CHECK(!extracted && feature.values.empty());
        }
        extractor.reset();
        DeleteFileA(path.c_str());
    }

    const std::string csv_path = "build/boundary_csv_" + std::to_string(GetCurrentProcessId()) + ".csv";
    fa::FeatureStore csv_store;
    fa::Record baseline;
    baseline.id = "BASE";
    baseline.name = "Original";
    baseline.feature.values = {1, 0};
    CHECK(csv_store.add(baseline));
    const auto initial_revision = csv_store.revision();
    const char* bad_rows[] = {
        "\"P\"junk,Person,,2,1,0", "P\"Q\",Person,,2,1,0", "P,\"Name\"junk,,2,1,0",
        "P,Person,,2junk,1,0", "P,Person,,999999999999999999999,1,0", "P,Person,,,1,0",
        "P,Person,,2,1,nonsense", "P,Person,,2,1,0junk", "P,Person,,2,1,", "P,Person,,2,1,nan",
        "P,Person,,2,1,inf", "P,Person,,2,1,1e999", "P,Person,,2,1,1e-999", ",Person,,2,1,0"
    };
    for (const char* row : bad_rows) {
        std::ofstream file(csv_path, std::ios::binary | std::ios::trunc);
        file << "id,name,card_id,dim,values\r\n" << row << "\r\n";
        file.close();
        CHECK(!csv_store.load(csv_path));
        CHECK(csv_store.count() == 1 && csv_store.find("BASE") != nullptr && csv_store.revision() == initial_revision);
        CHECK(!csv_store.open(csv_path));
        CHECK(!csv_store.is_open() && csv_store.find("BASE") != nullptr && csv_store.revision() == initial_revision);
    }

    const char* bad_headers[] = {"wrong,fields,card_id\n", "id,name,card_id,dim,wrong\n",
        "id,name,card_id,dim,values,extra\n", "id,name,wrong,dim,values\n"};
    const std::string bound_path = csv_path + ".bound";
    CHECK(csv_store.save(bound_path));
    fa::FeatureStore managed_store;
    CHECK(managed_store.open(bound_path));
    const auto managed_revision = managed_store.revision();
    for (const char* header : bad_headers) {
        std::ofstream file(csv_path, std::ios::binary | std::ios::trunc);
        file << header;
        file.close();
        CHECK(!csv_store.load(csv_path));
        CHECK(csv_store.find("BASE") != nullptr && csv_store.revision() == initial_revision);
        CHECK(!managed_store.open(csv_path));
        CHECK(managed_store.path() == bound_path && managed_store.find("BASE") != nullptr &&
              managed_store.revision() == managed_revision);
    }
    managed_store.close();
    DeleteFileA(bound_path.c_str());

    // Valid legacy/current CSV, spaces around numbers, CRLF and quoted identifiers remain supported.
    for (const char* text : {"LEGACY,Person,2,1,0\r\n", "\"ID\"\"quote\",\"Name\"\"quote\",2,1,0\r\n",
        "id,name,card_id,dim,values\r\n\"ID,quoted\",\"Name, quoted\",CARD, 2 , 1e0 , -0 \r\n"}) {
        std::ofstream file(csv_path, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        CHECK(csv_store.load(csv_path));
        CHECK(csv_store.count() == 1 && csv_store.records()[0].feature.dim() == 2);
    }
    DeleteFileA(csv_path.c_str());


    fa::Image detector_input;
    detector_input.create(640, 640, 3);
    for (int layout = 0; layout < 3; ++layout) {
        // Valid baseline; NaN/Inf in each tensor family; finite values whose decoded coordinates overflow.
        for (int test = 0; test < 8; ++test) {
            const int group = test == 0 ? -1 : test == 7 ? 1 : (test - 1) / 2;
            const float value = test == 7 ? std::numeric_limits<float>::max() :
                test % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            const std::string path = "build/boundary_detector_" + std::to_string(GetCurrentProcessId()) + ".onnx";
            CHECK(write_detector_onnx(path, layout, group, value));
            fa::ModelProfile p;
            p.detector_type = layout == 2 ? "yunet" : "scrfd";
            p.detector_backend = "onnx";
            p.detector_layout = layout == 1 ? "chw" : "flat";
            p.detector_model = path;
            auto detector = fa::create_detector(p);
            CHECK(detector);
            std::vector<fa::Face> faces(1);
            if (detector) {
                const bool detected = detector->detect(detector_input, faces);
                if (test == 0) CHECK(detected && !faces.empty());
                else CHECK(!detected && faces.empty());
            }
            detector.reset();
            DeleteFileA(path.c_str());
        }
    }

    // CHW must compare the actual sigmoid score at both supported threshold endpoints.
    for (int test = 0; test < 3; ++test) {
        const std::string path = "build/boundary_threshold_" + std::to_string(GetCurrentProcessId()) + ".onnx";
        CHECK(write_detector_onnx(path, 1, 0, test == 0 ? -20.0f : test == 1 ? 14.0f : 100.0f));
        fa::ModelProfile p;
        p.detector_type = "scrfd"; p.detector_backend = "onnx"; p.detector_layout = "chw";
        p.detector_model = path; p.detector_score = test == 0 ? 0.0f : 1.0f;
        auto detector = fa::create_detector(p);
        CHECK(detector);
        std::vector<fa::Face> faces;
        if (detector) {
            CHECK(detector->detect(detector_input, faces));
            if (test == 1) CHECK(faces.empty());
            else CHECK(!faces.empty());
            for (const auto& face : faces) CHECK(face.score >= p.detector_score);
        }
        detector.reset(); DeleteFileA(path.c_str());
    }

    // A malformed SQLite dimension must not truncate or overflow into a valid blob size.
    const std::string sqlite_path = "build/boundary_dimension_" + std::to_string(GetCurrentProcessId()) + ".db";
    DeleteFileA(sqlite_path.c_str());
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(sqlite_path.c_str(), &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE people(id TEXT,name TEXT,card_id TEXT,dim INTEGER,feature BLOB);"
        "PRAGMA user_version=1; INSERT INTO people VALUES('A','A',NULL,2,x'0000803f00000000');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    fa::FeatureStore sqlite_store;
    CHECK(sqlite_store.load(sqlite_path));
    const auto sqlite_revision = sqlite_store.revision();
    for (const char* dim : {"4294967298", "1073741826", "9223372036854775807", "-1", "0", "2.5"}) {
        const std::string sql = std::string("UPDATE people SET dim=") + dim;
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        CHECK(!sqlite_store.load(sqlite_path));
        CHECK(sqlite_store.find("A") != nullptr && sqlite_store.revision() == sqlite_revision);
    }
    sqlite3_close(db); DeleteFileA(sqlite_path.c_str());

    std::printf("Engine boundary regressions: %d failures\n", failures);
    return failures ? 1 : 0;
}
