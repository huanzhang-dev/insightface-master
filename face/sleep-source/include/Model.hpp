#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace drowse {
struct Tensor {std::vector<int64_t> shape;std::vector<float> values;};
class Model {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 Model(const std::string& path,bool directml=false);~Model();
 std::vector<Tensor> forward(const Tensor& blob);
 Model(const Model&)=delete;Model& operator=(const Model&)=delete;
};
}
