#include "Sleep.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace drowse {
void validateSettings(const Settings& s){
 const double values[]={s.earThreshold,s.closureAlarm,s.window,s.minValid,s.perclosAlarm,s.maxSampleGap,s.calibrationSeconds,s.yawLimit,s.pitchLimit,s.irBlack,s.irWhite};for(double v:values)if(!std::isfinite(v))throw std::invalid_argument("Configuration values must be finite");
 if(s.earThreshold<.08||s.earThreshold>.35||s.closureAlarm<.5||s.window<5||s.minValid<1||s.minValid>s.window||s.perclosAlarm<=0||s.perclosAlarm>=1||s.maxSampleGap<=0||s.maxSampleGap>1||s.calibrationSeconds<1||s.fps<1||s.fps>120||s.frames<0||s.yawLimit<10||s.yawLimit>70||s.pitchLimit<10||s.pitchLimit>60||s.irBlack<0||s.irWhite>65535||s.irWhite<=s.irBlack||s.cameraWidth<80||s.cameraHeight<60)throw std::invalid_argument("Invalid configuration range");
 if(s.eyewear!="auto"&&s.eyewear!="opaque")throw std::invalid_argument("Eyewear must be auto or opaque");if(!s.fourcc.empty()&&s.fourcc.size()!=4)throw std::invalid_argument("FOURCC must have four characters");
}
Temporal::Temporal(const Settings& settings):settings_(settings){validateSettings(settings_);reset();}
void Temporal::reset(){history_.clear();baselineEyes_.clear();baselinePitch_.clear();yawns_.clear();nods_.clear();previous_=-1;start_=-1;closure_=0;open_=0;calibration_=0;threshold_=settings_.earThreshold;pitchBaseline_=0;unavailable_=-1;yawnStart_=-1;nodStart_=-1;previousValid_=false;previousClosed_=false;calibrated_=false;latched_=false;yawnCounted_=false;nodCounted_=false;}
Decision Temporal::update(const Features& f){
 if(!std::isfinite(f.timestamp)||f.timestamp<0||(previous_>=0&&f.timestamp<=previous_))throw std::invalid_argument("Observation times must be finite, nonnegative and strictly increasing");
 if(f.identityChanged)reset();double dt=previous_<0?0:f.timestamp-previous_;if(start_<0)start_=f.timestamp;
 bool measurements=std::isfinite(f.leftEar)&&std::isfinite(f.rightEar)&&std::isfinite(f.mar)&&std::isfinite(f.pitch)&&std::isfinite(f.yaw);
 bool valid=f.facePresent&&f.eyesVisible&&f.poseReliable&&measurements&&std::abs(f.yaw)<=settings_.yawLimit&&std::abs(f.pitch)<=settings_.pitchLimit&&f.leftEar>=0&&f.rightEar>=0&&f.leftEar<1&&f.rightEar<1;
 bool closed=valid&&f.leftEar<threshold_&&f.rightEar<threshold_;bool continuous=dt<=settings_.maxSampleGap;
 if(previous_>=0)history_.push_back({previous_,f.timestamp,continuous&&previousValid_&&valid,previousClosed_});
 double cutoff=f.timestamp-settings_.window;while(!history_.empty()&&history_.front().end<=cutoff)history_.pop_front();if(!history_.empty())history_.front().start=std::max(history_.front().start,cutoff);
 if(closed)closure_=previousClosed_&&continuous?closure_+dt:0;else closure_=0;
 if(valid&&!closed)open_=previousValid_&&!previousClosed_&&continuous?open_+dt:0;else open_=0;
 if(!valid){if(unavailable_<0)unavailable_=f.timestamp;}else unavailable_=-1;
 if(!calibrated_&&valid&&!closed&&std::min(f.leftEar,f.rightEar)>.20&&std::max(f.leftEar,f.rightEar)<.55&&std::abs(f.yaw)<30&&std::abs(f.pitch)<30){if(continuous)calibration_+=dt;baselineEyes_.push_back((f.leftEar+f.rightEar)/2);baselinePitch_.push_back(f.pitch);if(calibration_>=settings_.calibrationSeconds&&baselineEyes_.size()>=30){std::sort(baselineEyes_.begin(),baselineEyes_.end());std::sort(baselinePitch_.begin(),baselinePitch_.end());threshold_=std::max(.12,std::min(.24,baselineEyes_[baselineEyes_.size()/2]*.65));pitchBaseline_=baselinePitch_[baselinePitch_.size()/2];calibrated_=true;}}
 bool mouthValid=f.facePresent&&f.mouthVisible&&f.poseReliable&&measurements&&std::abs(f.yaw)<=settings_.yawLimit;
 if(mouthValid&&f.mar>.55&&continuous){if(yawnStart_<0)yawnStart_=f.timestamp;if(!yawnCounted_&&f.timestamp-yawnStart_>=1.2){yawns_.push_back(f.timestamp);yawnCounted_=true;}}else if(!mouthValid||!continuous||f.mar<.35){yawnStart_=-1;yawnCounted_=false;}
 if(calibrated_&&f.facePresent&&f.poseReliable&&measurements&&f.pitch-pitchBaseline_>15&&std::abs(f.yaw)<settings_.yawLimit&&continuous){if(nodStart_<0)nodStart_=f.timestamp;if(!nodCounted_&&f.timestamp-nodStart_>=.6){nods_.push_back(f.timestamp);nodCounted_=true;}}else if(!f.poseReliable||!continuous||f.pitch-pitchBaseline_<8){nodStart_=-1;nodCounted_=false;}
 while(!yawns_.empty()&&yawns_.front()<cutoff)yawns_.pop_front();while(!nods_.empty()&&nods_.front()<cutoff)nods_.pop_front();
 Decision d;double closedTime=0;for(const auto& interval:history_)if(interval.valid){d.validSeconds+=interval.end-interval.start;if(interval.closed)closedTime+=interval.end-interval.start;}
 d.coverage=d.validSeconds/std::max(.001,std::min(settings_.window,f.timestamp-start_));d.coverage=std::min(1.,d.coverage);d.perclos=d.validSeconds>0?closedTime/d.validSeconds:0;d.closureSeconds=closure_;d.calibrated=calibrated_;d.eyeThreshold=threshold_;d.yawns=(int)yawns_.size();d.nods=(int)nods_.size();
 bool prolonged=closure_>=settings_.closureAlarm,perclos=valid&&d.validSeconds>=settings_.minValid&&d.coverage>=.6&&d.perclos>=settings_.perclosAlarm,multiple=d.yawns>=2&&d.nods>=2;
 if(prolonged||perclos||multiple)latched_=true;else if(valid&&open_>=2&&d.perclos<settings_.perclosAlarm)latched_=false;
 d.alarm=latched_;d.observationFault=unavailable_>=0&&f.timestamp-unavailable_>=3;
 if(latched_){d.state="DROWSINESS_ALERT";d.reason=prolonged?"Prolonged bilateral eye closure":perclos?"High time-weighted eye-closure fraction":multiple?"Repeated yawning with head nodding":valid?"Previous drowsiness alert; waiting for sustained recovery":"Previous drowsiness alert; current view cannot be assessed";}
 else if(!valid){d.state="UNABLE_TO_ASSESS";d.reason=f.reason.empty()?"Eyes or pose cannot be measured reliably":f.reason;}
 else if(closed){d.state="EYES_CLOSED";d.reason="Timing eye closure";}
 else if(!calibrated_){d.state="CALIBRATING";d.reason="Look forward with eyes open for personal calibration";}
 else{d.state="MONITORING";d.reason="No current drowsiness cue detected; not a sleep-state diagnosis";}
 previous_=f.timestamp;previousValid_=valid;previousClosed_=closed;return d;
}
}
