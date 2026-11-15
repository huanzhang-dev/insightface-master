#pragma once
#include <opencv2/opencv.hpp>
namespace car {
class Lighting {
 double gamma_=1.; bool initialized_=false;
public:
 cv::Mat apply(const cv::Mat& frame);
};
cv::Mat nightExposureView(const cv::Mat& frame);
cv::Mat normalizePlateLighting(const cv::Mat& patch);
int generateLightingVideos(const std::string& directory);
}
