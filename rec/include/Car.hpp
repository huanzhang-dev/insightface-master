#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include "Inference.hpp"
#include "Text.hpp"
#include "Lighting.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace car {
class MultilingualOcr;
struct Reading { std::string text; float confidence=0; std::vector<float> characterConfidence; };
struct Detection { cv::Rect2f box; int category=2; float confidence=0; std::vector<float> appearance; Reading plate; cv::Rect plateBox; cv::Mat plateImage; bool plateDetected=false; std::string plateSource="none"; };
struct Track { std::vector<float> appearance; cv::Rect2f lastObservedBox; int reacquisitions=0; int id=0; cv::Rect2f box; cv::KalmanFilter filter; double lastSeen=0,observationAge=0; int hits=0; int category=2; float confidence=0; std::map<int,double> typeVotes; std::map<std::string,double> plateVotes; std::map<std::string,int> plateCounts; Reading plate; bool plateStable=false,plateVisible=false; double lastPlateSeen=-1,lastPlateObservation=-1; std::string plateSource="none"; std::deque<cv::Point> trail; };
struct Options { std::string ocrLanguages="eng+chi_sim+kor+jpn",source="0",models="models",output="output",record; bool allowTextCandidates=false,lighting=true,headless=false,offline=false,numbersOnly=false,opencl=false,directml=false; int maxFrames=0,detectorEvery=1,threads=4,fps=30,gpuDevice=-1; float confidence=.3f; double maxAge=.5; };
std::string letters(const std::string& text); std::string numbers(const std::string& text); std::string typeName(int category);
std::vector<int> assign(const std::vector<std::vector<double> >& costs);
struct IdSwitchCounter { int previousId=0,switches=0; void observe(int id); };
std::vector<float> appearanceFeature(const cv::Mat& image);
class Tracker { std::vector<Track> tracks_,archived_; int nextId_=1; double previous_=-1; public: const std::vector<Track>& archivedTracks() const {return archived_;} Tracker project(double now) const; void predict(double now); void update(const std::vector<Detection>& detections,double now); const std::vector<Track>& tracks() const {return tracks_;} };
class Recognizer { Network network_; cv::Ptr<cv::ml::KNearest> glyphClassifier_; bool numbersOnly_; Reading readGlyphs(const cv::Mat& gray); public: Recognizer(const std::string& path,bool numbersOnly=false,bool opencl=false,bool directml=false,int device=-1); Reading read(const cv::Mat& patch); };
class Pipeline { Lighting lighting_; bool lightingEnabled_,allowTextCandidates_; std::unique_ptr<MultilingualOcr> multilingual_; Network detector_,plateDetector_; Recognizer recognizer_; float confidence_; public: Pipeline(const Options& options); ~Pipeline(); std::vector<Detection> detect(const cv::Mat& image); std::vector<cv::Rect> plateCandidates(const cv::Mat& vehicle); };
struct Result { std::vector<Detection> detections; int frame=-1; double timestamp=0,seconds=0; };
// A one-slot latest-frame queue: overload drops work instead of accumulating latency.
class Worker { Pipeline pipeline_; std::thread thread_; std::mutex mutex_; std::condition_variable wake_; cv::Mat pending_; int frame_=-1; double timestamp_=0; bool stop_=false,available_=false; Result latest_; std::string error_; std::atomic<int> dropped_; void loop(); public: Worker(const Options& options); ~Worker(); void submit(const cv::Mat& image,int frame,double timestamp); bool take(Result& result); int dropped() const {return dropped_.load();} };
cv::Mat renderTextLine(const std::string& text,int width,int height,const std::string& font="Arial",bool bold=true);
std::string trackingStatus(const Track& track);
std::string plateStatus(const Track& track);
void draw(cv::Mat& image,const Tracker& tracker,double fps); int generateEvidenceVideos(const std::string& directory);
int trackingReview(const std::string& directory,const std::string& models);
int generateVideos(const std::string& directory,const std::string& models="models"); int runTests(const std::string& models); int run(const Options& options);
}
