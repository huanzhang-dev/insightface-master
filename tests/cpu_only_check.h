// cpu_only_check.h - the proof that nothing ran on a GPU (used by test_common, test_feature and test_pipeline).
//
// Every engine of this project runs on the CPU (the target PC has no GPU): ncnn is told not to use Vulkan, ONNX
// Runtime gets no execution provider but its CPU one, the classical engines and the matcher are plain CPU code
// (AVX2). Besides asking the engines (runs_on_cpu), the tests look at the process itself: a network that had used a
// GPU would have loaded a GPU library, so after the engines have run NONE of the libraries below may be loaded.
//
//   vulkan-1       Vulkan (what ncnn would load for GPU computing)
//   d3d12          Direct3D 12 (DirectML, hardware video processing)
//   nvcuda, cudart, cublas, cudnn, nvinfer   NVIDIA CUDA / TensorRT
//   OpenCL         OpenCL
//   DirectML       DirectML
//   onnxruntime_providers_cuda / _tensorrt / _dml / _openvino   the GPU execution providers of ONNX Runtime
//
// (Direct3D 11 and DXGI are NOT in the list: Windows loads them for many ordinary reasons. They are only reported.)
#ifndef FA_CPU_ONLY_CHECK_H
#define FA_CPU_ONLY_CHECK_H

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

// The file names (without folder) of all libraries that this process has loaded, in lower case.
static std::vector<std::string> loaded_library_names() {
    std::vector<std::string> names;
    HMODULE modules[2048];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return names;
    const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), 2048);
    for (DWORD i = 0; i < count; ++i) {
        char path[MAX_PATH] = {0};
        if (!GetModuleFileNameA(modules[i], path, MAX_PATH)) continue;
        std::string name = path;
        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        std::transform(name.begin(), name.end(), name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        names.push_back(name);
    }
    return names;
}

// The GPU libraries (list above) that are loaded right now. Empty = nothing used a GPU.
static std::vector<std::string> loaded_gpu_libraries() {
    static const char* const kGpu[] = {"vulkan-1", "d3d12", "nvcuda", "cudart", "cublas", "cudnn", "nvinfer", "opencl", "directml",
                                       "onnxruntime_providers_cuda", "onnxruntime_providers_tensorrt", "onnxruntime_providers_dml",
                                       "onnxruntime_providers_openvino"};
    std::vector<std::string> found;
    for (const std::string& name : loaded_library_names())
        for (const char* prefix : kGpu)
            if (name.compare(0, std::char_traits<char>::length(prefix), prefix) == 0) found.push_back(name);
    return found;
}

// Direct3D 11 / DXGI libraries that are loaded (only for information; see above).
static std::vector<std::string> loaded_graphics_libraries() {
    std::vector<std::string> found;
    for (const std::string& name : loaded_library_names())
        if (name.compare(0, 5, "d3d11") == 0 || name.compare(0, 4, "dxgi") == 0 || name.compare(0, 5, "dxva2") == 0) found.push_back(name);
    return found;
}

#endif  // FA_CPU_ONLY_CHECK_H
