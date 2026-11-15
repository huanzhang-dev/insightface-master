#pragma once
#include <memory>
#include "Car.hpp"
namespace car {
class MultilingualOcr {
 struct Impl; std::unique_ptr<Impl> impl_;
public:
 MultilingualOcr(const std::string& data,const std::string& languages,bool numbersOnly);
 ~MultilingualOcr();
 Reading read(const cv::Mat& patch);
};
}
