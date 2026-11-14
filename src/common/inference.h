// inference.h - one simple interface for running a neural network.
//
// The project can run networks with two different libraries:
//   * ncnn         (reads "model.param" + "model.bin")
//   * ONNX Runtime (reads "model.onnx")
// The engines (detector, extractor) only talk to the `Net` interface below, so
// switching the library is a one-line change in the model settings.
#ifndef FA_INFERENCE_H
#define FA_INFERENCE_H

#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace fa {

// The result of one network output: a flat list of numbers.
// (The shape is not needed: each engine knows what its model produces.)
struct Tensor {
    std::vector<float> data;
};

// How an 8-bit image is turned into the network input:
//     value = (pixel - mean[c]) * norm[c]
// swap_rb = true gives the network RGB instead of BGR order.
struct InputPrep {
    float mean[3] = {0, 0, 0};
    float norm[3] = {1, 1, 1};
    bool swap_rb = false;
};

class Net {
public:
    virtual ~Net() {}

    // Run the network on one 3-channel BGR image and return all outputs, in model order.
    // Returns false when the network fails.
    virtual bool run(const Image& image, const InputPrep& prep, std::vector<Tensor>& outputs) = 0;

    // Name of the library behind this object ("ncnn" or "onnxruntime").
    virtual const char* backend() const = 0;

    // True when this network runs on the CPU. EVERY network of this project does: no GPU is used anywhere
    // (the target PC has none). ncnn is told not to use Vulkan; ONNX Runtime gets no execution provider other than
    // its built-in CPU one. The tests assert this for every network and also check that no GPU library
    // (Vulkan, CUDA, DirectML, OpenCL, Direct3D 12) was loaded into the process (tests/cpu_only_check.h).
    virtual bool runs_on_cpu() const = 0;
};

// The execution providers that the ONNX Runtime library offers ("CPUExecutionProvider" and the remote "AzureExecutionProvider", in the CPU build this
// project ships). Used by the tests to prove that the library in use cannot run a network on a GPU.
std::vector<std::string> onnxruntime_providers();

// ncnn model. `input_name` / `output_names` are the blob names inside the .param file.
std::unique_ptr<Net> create_ncnn_net(const std::string& param_path, const std::string& bin_path,
                                     const std::string& input_name,
                                     const std::vector<std::string>& output_names, int threads);

// ONNX Runtime model. Input and output names are read from the .onnx file.
std::unique_ptr<Net> create_onnx_net(const std::string& onnx_path, int threads);

}  // namespace fa

#endif  // FA_INFERENCE_H
