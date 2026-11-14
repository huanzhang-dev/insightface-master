// inference.cpp - the two network backends behind fa::Net. See inference.h.
#include "inference.h"

#include <windows.h>

#include <cstring>
#include <iostream>

#include "net.h"                 // ncnn
#include "onnxruntime_c_api.h"   // ONNX Runtime: the C interface on purpose, see OnnxNet

namespace fa {
namespace {

// ===========================================================================
// ncnn backend
// ===========================================================================
class NcnnNet : public Net {
public:
    bool load(const std::string& param, const std::string& bin, const std::string& input,
              const std::vector<std::string>& outputs, int threads) {
        input_name_ = input;
        output_names_ = outputs;
        net_.opt.num_threads = threads;
        net_.opt.use_vulkan_compute = false;  // CPU only: the target PC has no GPU
        if (net_.load_param(param.c_str()) != 0) {
            std::cerr << "ncnn: cannot read " << param << "\n";
            return false;
        }
        if (net_.load_model(bin.c_str()) != 0) {
            std::cerr << "ncnn: cannot read " << bin << "\n";
            return false;
        }
        return true;
    }

    bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) override {
        outputs.clear();
        if (image.empty() || image.channels != 3) return false;
        // from_pixels converts BGR bytes to a float image (optionally swapping to RGB).
        ncnn::Mat in = ncnn::Mat::from_pixels(image.data.data(),
                                              prep.swap_rb ? ncnn::Mat::PIXEL_BGR2RGB : ncnn::Mat::PIXEL_BGR,
                                              image.width, image.height, image.stride);
        in.substract_mean_normalize(prep.mean, prep.norm);

        ncnn::Extractor ex = net_.create_extractor();
        if (ex.input(input_name_.c_str(), in) != 0) return false;
        outputs.assign(output_names_.size(), Tensor());
        for (size_t i = 0; i < output_names_.size(); ++i) {
            ncnn::Mat out;
            if (ex.extract(output_names_[i].c_str(), out) != 0) return false;
            // An ncnn Mat can have several channels, each padded in memory; copy them one by one.
            const size_t per_channel = static_cast<size_t>(out.w) * out.h * out.d;
            outputs[i].data.resize(per_channel * out.c);
            for (int c = 0; c < out.c; ++c)
                std::memcpy(&outputs[i].data[c * per_channel], out.channel(c), per_channel * sizeof(float));
        }
        return true;
    }

    const char* backend() const override { return "ncnn"; }
    // (the library was built with its Vulkan code, but use_vulkan_compute is switched off in load(), so no GPU is ever opened)
    bool runs_on_cpu() const override { return !net_.opt.use_vulkan_compute; }

private:
    ncnn::Net net_;
    std::string input_name_;
    std::vector<std::string> output_names_;
};

// ===========================================================================
// ONNX Runtime backend
// ===========================================================================
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n - 1);
    return w;
}

// ONNX Runtime is used through its C interface (onnxruntime_c_api.h), not through the C++ wrapper header
// (onnxruntime_cxx_api.h): that header of version 1.17 needs C++14 "relaxed constexpr", which the compiler of
// Visual Studio 2015 does not have (it stops with error C3249), whereas the C header compiles everywhere.
// Every call returns an OrtStatus*: null = success. ok() turns that into a bool and prints the message.
class OnnxNet : public Net {
public:
    ~OnnxNet() {
        if (api_) {
            if (session_) api_->ReleaseSession(session_);
            if (env_) api_->ReleaseEnv(env_);
        }
    }

    bool load(const std::string& path, int threads) {
        api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!api_) {
            std::cerr << "onnxruntime: the library does not offer API version " << ORT_API_VERSION << "\n";
            return false;
        }
        if (!ok(api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "face_auth", &env_))) return false;

        OrtSessionOptions* options = nullptr;
        if (!ok(api_->CreateSessionOptions(&options))) return false;
        const bool created = ok(api_->SetIntraOpNumThreads(options, threads)) && ok(api_->SetInterOpNumThreads(options, 1)) &&
                             ok(api_->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL)) &&
                             ok(api_->CreateSession(env_, widen(path).c_str(), options, &session_));
        api_->ReleaseSessionOptions(options);
        if (!created) return false;

        OrtAllocator* allocator = nullptr;  // the default allocator; owned by the library
        if (!ok(api_->GetAllocatorWithDefaultOptions(&allocator))) return false;
        char* name = nullptr;
        if (!ok(api_->SessionGetInputName(session_, 0, allocator, &name))) return false;
        input_name_ = name;
        allocator->Free(allocator, name);
        size_t outputs = 0;
        if (!ok(api_->SessionGetOutputCount(session_, &outputs))) return false;
        for (size_t i = 0; i < outputs; ++i) {
            if (!ok(api_->SessionGetOutputName(session_, i, allocator, &name))) return false;
            output_names_.push_back(name);
            allocator->Free(allocator, name);
        }
        return true;
    }

    bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) override {
        // Build the input: planar float RGB/BGR (channel-first), value = (pixel - mean) * norm.
        outputs.clear();
        if (image.empty() || image.channels != 3) return false;
        const int w = image.width, h = image.height;
        input_.resize(static_cast<size_t>(3) * w * h);
        for (int c = 0; c < 3; ++c) {
            const int source_channel = prep.swap_rb ? 2 - c : c;
            float* plane = &input_[static_cast<size_t>(c) * w * h];
            for (int y = 0; y < h; ++y) {
                const uint8_t* p = image.row(y) + source_channel;
                float* q = plane + static_cast<size_t>(y) * w;
                for (int x = 0; x < w; ++x, p += 3) q[x] = (*p - prep.mean[c]) * prep.norm[c];
            }
        }
        // The tensor wraps input_ (no copy); the library gives back one OrtValue per output, which must be released.
        const int64_t shape[4] = {1, 3, h, w};
        OrtMemoryInfo* memory = nullptr;
        if (!ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory))) return false;
        OrtValue* input = nullptr;
        bool good = ok(api_->CreateTensorWithDataAsOrtValue(memory, input_.data(), input_.size() * sizeof(float), shape, 4,
                                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
        api_->ReleaseMemoryInfo(memory);
        if (!good) return false;

        const char* in_names[1] = {input_name_.c_str()};
        std::vector<const char*> out_names;
        for (auto& n : output_names_) out_names.push_back(n.c_str());
        std::vector<OrtValue*> results(out_names.size(), nullptr);
        good = ok(api_->Run(session_, nullptr, in_names, &input, 1, out_names.data(), out_names.size(), results.data()));
        api_->ReleaseValue(input);

        if (good) {
            outputs.assign(results.size(), Tensor());
            for (size_t i = 0; i < results.size() && good; ++i) {
                OrtTensorTypeAndShapeInfo* info = nullptr;
                size_t count = 0;
                float* values = nullptr;
                good = ok(api_->GetTensorTypeAndShape(results[i], &info));
                if (good) {
                    ONNXTensorElementDataType element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
                    good = ok(api_->GetTensorElementType(info, &element_type));
                    if (good && element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                        std::cerr << "onnxruntime: output '" << output_names_[i]
                                  << "' must contain float32 values (type " << element_type << " is unsupported)\n";
                        good = false;
                    }
                    if (good) good = ok(api_->GetTensorShapeElementCount(info, &count));
                    api_->ReleaseTensorTypeAndShapeInfo(info);
                }
                if (good) good = ok(api_->GetTensorMutableData(results[i], reinterpret_cast<void**>(&values)));
                if (good && count > 0) outputs[i].data.assign(values, values + count);
            }
        }
        for (OrtValue* r : results)
            if (r) api_->ReleaseValue(r);
        if (!good) outputs.clear();
        return good;
    }

    const char* backend() const override { return "onnxruntime"; }
    // The session options in load() set only the thread counts and the optimization level: no execution provider is
    // appended, so ONNX Runtime uses its built-in CPU provider and nothing else.
    bool runs_on_cpu() const override { return true; }

private:
    // true when the call succeeded; otherwise prints the library's message and frees the status
    bool ok(OrtStatus* status) {
        if (!status) return true;
        std::cerr << "onnxruntime: " << api_->GetErrorMessage(status) << "\n";
        api_->ReleaseStatus(status);
        return false;
    }

    const OrtApi* api_ = nullptr;
    OrtEnv* env_ = nullptr;
    OrtSession* session_ = nullptr;
    std::string input_name_;
    std::vector<std::string> output_names_;
    std::vector<float> input_;  // reused between calls to avoid allocating every frame
};

}  // namespace

std::unique_ptr<Net> create_ncnn_net(const std::string& param_path, const std::string& bin_path,
                                     const std::string& input_name,
                                     const std::vector<std::string>& output_names, int threads) {
    auto net = std::make_unique<NcnnNet>();
    if (!net->load(param_path, bin_path, input_name, output_names, threads)) return nullptr;
    return net;
}

std::unique_ptr<Net> create_onnx_net(const std::string& onnx_path, int threads) {
    auto net = std::make_unique<OnnxNet>();
    if (!net->load(onnx_path, threads)) return nullptr;
    return net;
}

std::vector<std::string> onnxruntime_providers() {
    std::vector<std::string> providers;
    const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    char** names = nullptr;
    int count = 0;
    if (!api) return providers;
    OrtStatus* status = api->GetAvailableProviders(&names, &count);
    if (status) {
        api->ReleaseStatus(status);
        return providers;
    }
    for (int i = 0; i < count; ++i) providers.push_back(names[i]);
    api->ReleaseAvailableProviders(names, count);
    return providers;
}

}  // namespace fa
