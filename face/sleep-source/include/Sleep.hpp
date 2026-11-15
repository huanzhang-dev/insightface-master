#pragma once
#include "Model.hpp"
#include "Image.hpp"
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>
namespace drowse {
struct Settings {
 double earThreshold=.18,closureAlarm=1.5,window=30,minValid=10,perclosAlarm=.28,maxSampleGap=.25,calibrationSeconds=5;
 double yawLimit=40,pitchLimit=35,irBlack=0,irWhite=65535;int fps=30,frames=0,cameraWidth=640,cameraHeight=480;
 bool infrared=true,rawIR=false,directml=false,headless=false,offline=false,sound=false;
 std::string source="0",models="models",output="output",eyewear="auto",fourcc;Rect roi;
};
struct Features {
 double timestamp=0,leftEar=0,rightEar=0,mar=0,pitch=0,yaw=0,roll=0,faceConfidence=0,meshConfidence=0;
 bool facePresent=false,eyesVisible=false,mouthVisible=false,poseReliable=false,identityChanged=false;
 std::string reason="No face";Rect face;std::vector<Point> landmarks;
};
struct Decision {
 std::string state="UNABLE_TO_ASSESS",reason="Waiting for a usable observation";
 double perclos=0,coverage=0,validSeconds=0,closureSeconds=0,eyeThreshold=.18;int yawns=0,nods=0;
 bool calibrated=false,alarm=false,observationFault=false;
};
class Temporal {
 struct Segment {double start,end;bool valid,closed;};Settings settings_;std::deque<Segment> history_;
 std::vector<double> baselineEyes_,baselinePitch_;std::deque<double> yawns_,nods_;
 double previous_=-1,start_=-1,closure_=0,open_=0,calibration_=0,threshold_=.18,pitchBaseline_=0,unavailable_=-1,yawnStart_=-1,nodStart_=-1;
 bool previousValid_=false,previousClosed_=false,calibrated_=false,latched_=false,yawnCounted_=false,nodCounted_=false;
public:
 explicit Temporal(const Settings& settings=Settings());void reset();Decision update(const Features& feature);
};
class Vision {
 Model detector_,mesh_;Settings settings_;std::vector<Point> anchors_;Rect previousFace_;double previousFaceTime_=-1;
public:
 explicit Vision(const Settings& settings);Features analyze(const Image& image,double timestamp);
};
struct Analysis {Features feature;Image source;int index=-1;double milliseconds=0;};
class Worker {
 Vision vision_;std::thread thread_;std::mutex mutex_;std::condition_variable wake_;Image pending_;double timestamp_=0;int index_=-1;bool stop_=false,ready_=false;Analysis latest_;std::string error_;std::atomic<int> dropped_;void loop();
public:
 explicit Worker(const Settings& settings);~Worker();void submit(const Image& image,double timestamp,int index);bool take(Analysis& result);int dropped() const {return dropped_.load();}
};
bool estimatePose(const std::vector<Point>& landmarks,int width,int height,Rect face,double& pitch,double& yaw,double& roll);
std::vector<Point> poseFixture(double pitch,double yaw,double roll,int width,int height);
void validateSettings(const Settings& settings);
Image cameraPixels(const Image& raw,const Settings& settings);
double eyeRatio(const std::vector<Point>& points,const int indices[6]);
void overlay(Image& image,const Features& feature,const Decision& decision,double fps);
std::string jsonString(const std::string& text);
int run(const Settings& settings);int tests(const std::string& root);int demo(const std::string& root,bool directml);
}
