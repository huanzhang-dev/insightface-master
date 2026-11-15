#pragma once
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <memory>
#include <string>
namespace car {
// Uses the stable ONNX Runtime C ABI for VS2015 compatibility; no C++ DLL ABI crosses runtimes.
class Network {
 struct Impl; std::unique_ptr<Impl> gpu_; cv::dnn::Net cpu_;
 public: Network(const std::string& path,bool opencl=false,bool directml=false,int device=-1); ~Network();
 cv::Mat forward(const cv::Mat& blob);
 Network(const Network&)=delete;Network& operator=(const Network&)=delete;
};
}
