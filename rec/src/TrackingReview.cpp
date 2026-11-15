#include "Car.hpp"
#include <direct.h>
#include <cmath>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
namespace car {
void IdSwitchCounter::observe(int id){if(id<=0)return;if(previousId>0&&previousId!=id)++switches;previousId=id;}
static double reviewOverlap(const cv::Rect2f& a,const cv::Rect2f& b){return (a&b).area()/std::max(1.f,a.area()+b.area()-(a&b).area());}
int trackingReview(const std::string& directory,const std::string& models){
 _mkdir(directory.c_str());std::string data=models+"/../testdata";
 cv::Mat car=cv::imread(data+"/car_photo.png"),bus=cv::imread(data+"/bus_photo.jpg"),truck=cv::imread(data+"/truck_modern.jpg");if(car.empty()||bus.empty()||truck.empty())throw std::runtime_error("Tracking review source photographs missing");
 std::vector<cv::Mat> photos={car,bus,truck(cv::Rect(65,215,785,205)).clone()};
 cv::VideoWriter normal(directory+"/tracking_review.avi",cv::VideoWriter::fourcc('M','J','P','G'),30,cv::Size(1920,720)),slow(directory+"/tracking_review_slow.avi",cv::VideoWriter::fourcc('M','J','P','G'),15,cv::Size(1920,720)),input(directory+"/tracking_review_input.avi",cv::VideoWriter::fourcc('M','J','P','G'),30,cv::Size(960,540));
 if(!normal.isOpened()||!slow.isOpened()||!input.isOpened())throw std::runtime_error("Cannot create tracking review videos");
 Options options;options.models=models;options.directml=true;Pipeline pipeline(options);Tracker tracker;
 std::ofstream audit(directory+"/tracking_audit.csv");audit<<"frame,time_s,vehicle,assigned_id,visible_track_id,state,detector_gap,source_observation_s,reacquisitions,plate_status\n";
 IdSwitchCounter switchCounters[3];std::set<int> identities[3];int original[3]={0,0,0},switches[3]={0,0,0},recoveries[3]={0,0,0},measured[3]={0,0,0};
 for(int f=0;f<360;++f){double now=f/30.;bool gap=f>=90&&f<210;cv::Mat source(540,960,CV_8UC3,cv::Scalar(75,75,75));std::vector<cv::Rect> truth;
  for(int v=0;v<3;++v){int w=v==2?270:250,h=v==1?333:v==2?115:170;int x=35+v*315+(int)(8*std::sin(f*.025+v)),y=190+(285-h)/2+(int)(8*std::sin(f*.035));cv::Rect box(x,y,w,h);truth.push_back(box);cv::Mat photo;cv::resize(photos[v],photo,box.size());photo.copyTo(source(box));}
  input.write(source);
  if(f%6==0){auto detections=pipeline.detect(source);if(gap)detections.erase(std::remove_if(detections.begin(),detections.end(),[&truth](const Detection& d){return reviewOverlap(d.box,truth[0])>.2;}),detections.end());tracker.predict(now);tracker.update(detections,now);}
  Tracker view=tracker.project(now);cv::Mat annotated=source.clone();draw(annotated,view,30);
  cv::Mat canvas(720,1920,CV_8UC3,cv::Scalar(22,22,22));source.copyTo(canvas(cv::Rect(0,80,960,540)));annotated.copyTo(canvas(cv::Rect(960,80,960,540)));
  std::ostringstream title;title<<"TRACK ID CONTINUITY REVIEW | "<<std::fixed<<std::setprecision(2)<<now<<" s | COMPOSITE PHOTOGRAPHS - NOT REAL CAMERA FOOTAGE";cv::putText(canvas,title.str(),cv::Point(12,27),cv::FONT_HERSHEY_SIMPLEX,.66,cv::Scalar(255,255,255),1,cv::LINE_AA);
  cv::putText(canvas,"LEFT: original photographs; no inserted plates      RIGHT: actual engine tracks / current plate status",cv::Point(12,57),cv::FONT_HERSHEY_SIMPLEX,.62,cv::Scalar(200,200,200),1,cv::LINE_AA);
  if(gap)cv::putText(canvas,"CONTROLLED TEST: car detections suppressed from 3.0 to 7.0 seconds; no IDs or detections injected",cv::Point(12,644),cv::FONT_HERSHEY_SIMPLEX,.63,cv::Scalar(0,200,255),1,cv::LINE_AA);
  else cv::putText(canvas,"Appearance + motion association; license plate text is not used for identity assignment",cv::Point(12,644),cv::FONT_HERSHEY_SIMPLEX,.63,cv::Scalar(160,230,160),1,cv::LINE_AA);
  for(int v=0;v<3;++v){const Track* match=NULL;double best=.2;for(const auto& t:view.tracks()){double overlap=reviewOverlap(t.box,truth[v]);if(overlap>best){match=&t;best=overlap;}}
   std::string state="not_detected",plate="not_observed";int id=0,reacquired=0;double last=-1;
   if(match){id=match->id;if(!original[v])original[v]=id;identities[v].insert(id);state=now-match->lastSeen<.201?"detected":"coasting";last=match->lastSeen;plate=plateStatus(*match);reacquired=match->reacquisitions;recoveries[v]=std::max(recoveries[v],reacquired);if(f%6==0&&now-match->lastSeen<.001)++measured[v];}
   else if(original[v]){state="lost_id_retained";for(const auto& t:tracker.archivedTracks())if(t.id==original[v]){last=t.lastSeen;reacquired=t.reacquisitions;break;}}
   switchCounters[v].observe(id);switches[v]=switchCounters[v].switches;
   audit<<f<<','<<now<<','<<typeName(v==0?2:v==1?5:7)<<','<<original[v]<<','<<id<<','<<state<<','<<(gap&&v==0?1:0)<<','<<last<<','<<reacquired<<','<<plate<<'\n';
   std::ostringstream label;label<<typeName(v==0?2:v==1?5:7)<<" | ID #"<<original[v]<<" | "<<state<<" | recoveries "<<reacquired;cv::putText(canvas,label.str(),cv::Point(12+v*640,674),cv::FONT_HERSHEY_SIMPLEX,.52,cv::Scalar(220,240,220),1,cv::LINE_AA);
   std::ostringstream detail;detail<<"IDs seen: "<<identities[v].size()<<" | ID changes: "<<switches[v]<<" | plate: "<<plate;cv::putText(canvas,detail.str(),cv::Point(12+v*640,702),cv::FONT_HERSHEY_SIMPLEX,.49,cv::Scalar(210,210,210),1,cv::LINE_AA);
  }
  normal.write(canvas);slow.write(canvas);if(f==60||f==180||f==270)cv::imwrite(directory+"/review_frame_"+std::to_string(f)+".jpg",canvas);
 }
 normal.release();slow.release();input.release();bool passed=true;for(int v=0;v<3;++v)passed=passed&&identities[v].size()==1&&measured[v]>=3;passed=passed&&recoveries[0]>=1;
 std::ofstream report(directory+"/tracking_review.json");report<<"{\n\"status\":\""<<(passed?"passed":"failed")<<"\",\"frames\":360,\"fps\":30,\"slow_fps\":15,\"seconds\":12,\"simulated_photographic_composite\":true,\"physical_camera_tested\":false,\"ground_truth_used_only_for_evaluation_and_test_dropout\":true,\"car_detector_dropout_seconds\":[3,7],\"vehicles\":[";
 for(int v=0;v<3;++v){if(v)report<<',';report<<"{\"type\":"<<jsonText(typeName(v==0?2:v==1?5:7))<<",\"original_id\":"<<original[v]<<",\"unique_ids\":"<<identities[v].size()<<",\"id_changes\":"<<switches[v]<<",\"reacquisitions\":"<<recoveries[v]<<",\"measured_observations\":"<<measured[v]<<'}';}report<<"]}\n";report.close();
 std::cout<<"Tracking review "<<(passed?"PASS":"FAIL")<<": "<<directory<<"/tracking_review.avi\n";return passed?0:1;
}
}
