#include "Car.hpp"
#include "Multilingual.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <opencv2/core/ocl.hpp>
namespace car {
std::string letters(const std::string& s){return characters(s);}
std::string typeName(int c){switch(c){case 1:return "bicycle";case 2:return "passenger_car";case 3:return "motorcycle";case 5:return "bus";case 7:return "truck";default:return "unknown";}}
static float iou(const cv::Rect2f& a,const cv::Rect2f& b){float area=(a&b).area();return area/std::max(1.f,a.area()+b.area()-area);}
std::vector<int> assign(const std::vector<std::vector<double> >& costs){
 int rows=(int)costs.size();if(!rows)return {};int cols=(int)costs[0].size();int n=std::max(rows,cols);
 std::vector<double> u(n+1),v(n+1);std::vector<int> p(n+1),way(n+1);
 for(int i=1;i<=n;++i){p[0]=i;int j0=0;std::vector<double> best(n+1,1e10);std::vector<bool> used(n+1,false);
  do{used[j0]=true;int i0=p[j0],j1=0;double delta=1e10;
   for(int j=1;j<=n;++j)if(!used[j]){double c=(i0<=rows&&j<=cols)?costs[i0-1][j-1]:.8;double cur=c-u[i0]-v[j];if(cur<best[j]){best[j]=cur;way[j]=j0;}if(best[j]<delta){delta=best[j];j1=j;}}
   for(int j=0;j<=n;++j)if(used[j]){u[p[j]]+=delta;v[j]-=delta;}else best[j]-=delta;j0=j1;
  }while(p[j0]!=0);
  do{int j1=way[j0];p[j0]=p[j1];j0=j1;}while(j0);
 }
 std::vector<int> result(rows,-1);for(int j=1;j<=n;++j)if(p[j]>0&&p[j]<=rows&&j<=cols)result[p[j]-1]=j-1;return result;
}
static cv::Rect2f boxFromState(const cv::Mat& s){float w=std::max(1.f,s.at<float>(2)),h=std::max(1.f,s.at<float>(3));return cv::Rect2f(s.at<float>(0)-w/2,s.at<float>(1)-h/2,w,h);}
std::vector<float> appearanceFeature(const cv::Mat& image){
 if(image.empty())return {};cv::Mat resized,hsv,gray;cv::resize(image,resized,cv::Size(32,32));cv::cvtColor(resized,hsv,cv::COLOR_BGR2HSV);cv::cvtColor(resized,gray,cv::COLOR_BGR2GRAY);
 std::vector<float> feature(128,0.f);for(int y=0;y<hsv.rows;++y)for(int x=0;x<hsv.cols;++x){cv::Vec3b c=hsv.at<cv::Vec3b>(y,x);int h=std::min(15,c[0]*16/180),sat=std::min(3,c[1]*4/256);feature[h*4+sat]+=1.f;}
 double histNorm=0;for(int i=0;i<64;++i)histNorm+=feature[i]*feature[i];histNorm=std::sqrt(histNorm);for(int i=0;i<64;++i)feature[i]=(float)(feature[i]/std::max(1.,histNorm)*.8);
 cv::resize(gray,gray,cv::Size(8,8));double grayNorm=cv::norm(gray,cv::NORM_L2);for(int i=0;i<64;++i)feature[64+i]=(float)(gray.at<unsigned char>(i/8,i%8)/std::max(1.,grayNorm)*.6);return feature;
}
static double appearanceSimilarity(const std::vector<float>& a,const std::vector<float>& b){if(a.empty()||a.size()!=b.size())return -1.;double dot=0,aa=0,bb=0;for(size_t i=0;i<a.size();++i){dot+=a[i]*b[i];aa+=a[i]*a[i];bb+=b[i]*b[i];}return aa>0&&bb>0?dot/std::sqrt(aa*bb):-1.;}
static double identityRecoveryCost(const Track& track,const Detection& detection,double now,bool active){
 // Appearance-only recovery must not merge different vehicle classes.
 if(track.category!=detection.category)return 2.;
 double similarity=appearanceSimilarity(track.appearance,detection.appearance);if(similarity<(active?.97:.93))return 2.;
 const cv::Rect2f& reference=active?track.box:track.lastObservedBox;
 double distance=cv::norm(cv::Point2f(reference.x+reference.width/2,reference.y+reference.height/2)-cv::Point2f(detection.box.x+detection.box.width/2,detection.box.y+detection.box.height/2))/std::max(30.f,std::max(detection.box.width,detection.box.height));
 double sizeRatio=detection.box.area()/std::max(1.f,reference.area());double age=std::max(0.,now-track.lastSeen);
 double distanceLimit=active?std::min(6.,1.+8.*age):std::min(8.,2.+age*.5);
 if(distance>=distanceLimit||sizeRatio<(active?.5:.4)||sizeRatio>(active?2.:2.5))return 2.;
 return 1.-similarity+.01*distance;
}
static std::vector<int> recoveryAssignment(const std::vector<std::vector<double> >& costs){
 if(costs.empty())return {};std::vector<std::vector<double> > filtered=costs;size_t columns=costs[0].size();
 // Near ties cannot justify selecting an old identity. Require a margin in both directions.
 for(size_t i=0;i<costs.size();++i)for(size_t j=0;j<columns;++j)if(costs[i][j]<.16){
  double competitor=2.;for(size_t k=0;k<columns;++k)if(k!=j)competitor=std::min(competitor,costs[i][k]);
  for(size_t k=0;k<costs.size();++k)if(k!=i)competitor=std::min(competitor,costs[k][j]);
  if(competitor-costs[i][j]<.02)filtered[i][j]=2.;
 }
 auto assignment=assign(filtered);for(size_t i=0;i<assignment.size();++i)if(assignment[i]>=0&&filtered[i][assignment[i]]>=.16)assignment[i]=-1;return assignment;
}
static void resetMotion(Track& t,const cv::Rect2f& box){t.filter.statePost=(cv::Mat_<float>(8,1)<<box.x+box.width/2,box.y+box.height/2,box.width,box.height,0,0,0,0);t.filter.statePre=t.filter.statePost.clone();cv::setIdentity(t.filter.errorCovPost,cv::Scalar(10));cv::setIdentity(t.filter.errorCovPre,cv::Scalar(10));t.box=box;}
static void observe(Track& t,const Detection& d,double now){
 t.observationAge=0;t.lastObservedBox=d.box;if(!d.appearance.empty()){if(t.appearance.size()!=d.appearance.size())t.appearance=d.appearance;else for(size_t i=0;i<t.appearance.size();++i)t.appearance[i]=.85f*t.appearance[i]+.15f*d.appearance[i];}
 cv::Mat z=(cv::Mat_<float>(4,1)<<d.box.x+d.box.width/2,d.box.y+d.box.height/2,d.box.width,d.box.height);
 t.box=boxFromState(t.filter.correct(z));t.lastSeen=now;++t.hits;t.confidence=d.confidence;t.typeVotes[d.category]+=d.confidence;
 double best=-1;for(auto vote:t.typeVotes)if(vote.second>best){best=vote.second;t.category=vote.first;}
 t.plateVisible=false;
 bool evidence=d.plateDetected&&d.plateBox.area()>0&&(d.plateSource=="detector"||d.plateSource=="heuristic_test");
 if(evidence&&!d.plate.text.empty()&&textLength(d.plate.text)>=4&&textLength(d.plate.text)<=12&&d.plate.confidence>=.5f&&now>t.lastPlateObservation){
  t.lastPlateObservation=now;
  // Bound accumulated hypotheses for long-lived tracks.
  if(t.plateVotes.size()>=32&&!t.plateVotes.count(d.plate.text)){auto lowest=std::min_element(t.plateVotes.begin(),t.plateVotes.end(),[](const std::pair<const std::string,double>& a,const std::pair<const std::string,double>& b){return a.second<b.second;});t.plateCounts.erase(lowest->first);t.plateVotes.erase(lowest);}
  t.plateVotes[d.plate.text]+=d.plate.confidence;++t.plateCounts[d.plate.text];double weight=-1;
  for(auto vote:t.plateVotes)if(vote.second>weight){weight=vote.second;t.plate.text=vote.first;t.plate.confidence=(float)(vote.second/t.plateCounts[vote.first]);t.plateStable=t.plateCounts[vote.first]>=(t.plate.confidence<.75f?3:2);t.plate.characterConfidence.clear();}
  // A different current reading does not make the historical winner visible.
  t.plateVisible=t.plate.text==d.plate.text;if(t.plateVisible){t.lastPlateSeen=now;t.plateSource=d.plateSource;}
 }

}
void Tracker::predict(double now){double dt=previous_<0?1./30:std::max(.001,now-previous_);previous_=now;
 for(auto& t:tracks_){for(int k=0;k<4;++k)t.filter.transitionMatrix.at<float>(k,k+4)=(float)dt;t.box=boxFromState(t.filter.predict());t.trail.push_back(cv::Point((int)(t.box.x+t.box.width/2),(int)(t.box.y+t.box.height/2)));if(t.trail.size()>30)t.trail.pop_front();}
 for(auto it=tracks_.begin();it!=tracks_.end();){if(now-it->lastSeen>2.){it->plateVisible=false;archived_.push_back(*it);it=tracks_.erase(it);}else ++it;}
}
void Tracker::update(const std::vector<Detection>& ds,double now){
 std::vector<std::vector<double> > costs(tracks_.size(),std::vector<double>(ds.size(),1.));
 for(size_t i=0;i<tracks_.size();++i)for(size_t j=0;j<ds.size();++j){float overlap=iou(tracks_[i].box,ds[j].box);float dx=tracks_[i].box.x+tracks_[i].box.width/2-ds[j].box.x-ds[j].box.width/2;float dy=tracks_[i].box.y+tracks_[i].box.height/2-ds[j].box.y-ds[j].box.height/2;float dist=std::sqrt(dx*dx+dy*dy)/std::max(20.f,std::max(ds[j].box.width,ds[j].box.height));double similarity=appearanceSimilarity(tracks_[i].appearance,ds[j].appearance);costs[i][j]=(overlap>.05f||dist<.7f)?1.-overlap+.1*std::min(1.f,dist)+.3*(similarity<0?0:1.-similarity):2.;if(similarity>=0&&similarity<.65)costs[i][j]=2.;}
 std::vector<int> matches=assign(costs);std::vector<bool> used(ds.size(),false),matched(tracks_.size(),false);
 for(size_t i=0;i<tracks_.size();++i)if(matches[i]>=0&&costs[i][matches[i]]<.95){used[matches[i]]=true;matched[i]=true;observe(tracks_[i],ds[matches[i]],now);}
 // Recover unmatched, still-active identities before creating duplicates after abrupt motion.
 std::vector<size_t> activeRows;std::vector<std::vector<double> > activeCosts;
 for(size_t i=0;i<tracks_.size();++i)if(!matched[i]){std::vector<double> row(ds.size(),2.);for(size_t j=0;j<ds.size();++j)if(!used[j])row[j]=identityRecoveryCost(tracks_[i],ds[j],now,true);if(!row.empty()&&*std::min_element(row.begin(),row.end())<.16){activeRows.push_back(i);activeCosts.push_back(row);}}
 auto activeMatches=recoveryAssignment(activeCosts);for(size_t row=0;row<activeRows.size();++row)if(activeMatches[row]>=0){size_t i=activeRows[row];int j=activeMatches[row];resetMotion(tracks_[i],ds[j].box);++tracks_[i].reacquisitions;observe(tracks_[i],ds[j],now);used[j]=true;matched[i]=true;}
 // Reacquire archived identities before creating new IDs. A missing descriptor never justifies identity reuse.
 std::vector<std::vector<double> > recovery(archived_.size(),std::vector<double>(ds.size(),2.));
 for(size_t i=0;i<archived_.size();++i)for(size_t j=0;j<ds.size();++j)if(!used[j]){
  recovery[i][j]=identityRecoveryCost(archived_[i],ds[j],now,false);
 }
 std::vector<size_t> eligible;std::vector<std::vector<double> > compact;for(size_t i=0;i<recovery.size();++i)if(!recovery[i].empty()&&*std::min_element(recovery[i].begin(),recovery[i].end())<.16){eligible.push_back(i);compact.push_back(recovery[i]);}
 std::vector<int> recovered=recoveryAssignment(compact);std::vector<size_t> remove;
 for(size_t row=0;row<eligible.size();++row){size_t i=eligible[row];if(recovered[row]>=0&&!used[recovered[row]]&&recovery[i][recovered[row]]<.16){int j=recovered[row];Track t=archived_[i];resetMotion(t,ds[j].box);++t.reacquisitions;observe(t,ds[j],now);tracks_.push_back(t);used[j]=true;remove.push_back(i);}}
 for(auto i=remove.rbegin();i!=remove.rend();++i)archived_.erase(archived_.begin()+*i);
 for(size_t j=0;j<ds.size();++j)if(!used[j]){Track t;t.id=nextId_++;t.filter.init(8,4,0);cv::setIdentity(t.filter.transitionMatrix);t.filter.measurementMatrix=cv::Mat::zeros(4,8,CV_32F);for(int k=0;k<4;++k)t.filter.measurementMatrix.at<float>(k,k)=1;cv::setIdentity(t.filter.processNoiseCov,cv::Scalar(.1));cv::setIdentity(t.filter.measurementNoiseCov,cv::Scalar(1));cv::setIdentity(t.filter.errorCovPost,cv::Scalar(10));t.filter.statePost=(cv::Mat_<float>(8,1)<<ds[j].box.x+ds[j].box.width/2,ds[j].box.y+ds[j].box.height/2,ds[j].box.width,ds[j].box.height,0,0,0,0);t.filter.statePre=t.filter.statePost.clone();observe(t,ds[j],now);tracks_.push_back(t);}
}
Tracker Tracker::project(double now) const {
 Tracker view;view.nextId_=nextId_;view.previous_=previous_;view.archived_=archived_;for(auto& t:view.archived_)t.observationAge=std::max(0.,now-t.lastSeen);double dt=std::max(0.,now-previous_);
 for(const auto& source:tracks_){if(now-source.lastSeen>2.)continue;Track t=source;t.observationAge=std::max(0.,now-t.lastSeen);const cv::Mat& state=source.filter.statePost;
  t.box=cv::Rect2f(state.at<float>(0)+state.at<float>(4)*(float)dt-t.box.width/2,state.at<float>(1)+state.at<float>(5)*(float)dt-t.box.height/2,t.box.width,t.box.height);
  if(now-t.lastPlateSeen>.5)t.plateVisible=false;view.tracks_.push_back(t);
 }return view;
}
std::string trackingStatus(const Track& track){return track.observationAge<=.25?"detected":track.observationAge<=2.?"coasting":"lost";}
std::string plateStatus(const Track& track){if(!track.plateVisible)return track.plate.text.empty()?"not_observed":"historical";return track.plateStable?"observed":"unconfirmed";}
static cv::Mat glyphFeature(const cv::Mat& binary){cv::Mat feature(32,24,CV_8U,cv::Scalar());std::vector<cv::Point> points;cv::findNonZero(binary,points);if(points.empty())return cv::Mat::zeros(1,768,CV_32F);cv::Rect box=cv::boundingRect(points);double scale=std::min(20./box.width,28./box.height);cv::Mat small;cv::resize(binary(box),small,cv::Size(std::max(1,(int)(box.width*scale)),std::max(1,(int)(box.height*scale))),0,0,cv::INTER_AREA);small.copyTo(feature(cv::Rect((24-small.cols)/2,(32-small.rows)/2,small.cols,small.rows)));cv::Mat out;feature.reshape(1,1).convertTo(out,CV_32F,1./255);return out;}
Recognizer::Recognizer(const std::string& path,bool numbersOnly,bool opencl,bool directml,int device):network_(path,opencl,directml,device),numbersOnly_(numbersOnly){cv::Mat samples,labels;const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
 for(const auto& font:std::vector<std::string>{"Arial","Consolas","Tahoma","Verdana"})for(int bold=0;bold<2;++bold)for(int glyphHeight:std::vector<int>{24,32,40,48,64})for(size_t c=0;c<alphabet.size();++c){cv::Mat patch=renderTextLine(alphabet.substr(c,1),64,glyphHeight,font,bold!=0),gray,binary;cv::cvtColor(patch,gray,cv::COLOR_BGR2GRAY);cv::threshold(gray,binary,0,255,cv::THRESH_BINARY_INV|cv::THRESH_OTSU);for(int variation=0;variation<3;++variation){cv::Mat variant;if(variation==1)cv::dilate(binary,variant,cv::Mat());else if(variation==2)cv::erode(binary,variant,cv::Mat());else variant=binary;samples.push_back(glyphFeature(variant));labels.push_back((float)c);}}
 glyphClassifier_=cv::ml::KNearest::create();glyphClassifier_->setDefaultK(3);glyphClassifier_->train(samples,cv::ml::ROW_SAMPLE,labels);
}
Reading Recognizer::readGlyphs(const cv::Mat& gray){
 Reading r;cv::Mat binary;cv::threshold(gray,binary,0,255,cv::THRESH_BINARY_INV|cv::THRESH_OTSU);std::vector<std::vector<cv::Point> > contours;cv::findContours(binary.clone(),contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);std::vector<cv::Rect> boxes;
 for(const auto& contour:contours){cv::Rect b=cv::boundingRect(contour);if(b.height>gray.rows*.35&&b.height<gray.rows*.95&&b.width>=2&&b.width<gray.cols*.3)boxes.push_back(b);}
 if(boxes.size()<4||boxes.size()>12)return r;std::sort(boxes.begin(),boxes.end(),[](const cv::Rect& a,const cv::Rect& b){return a.x<b.x;});float sum=0;const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
 for(const auto& b:boxes){cv::Mat feature=glyphFeature(binary(b)),result,neighbors,distances;int label=(int)glyphClassifier_->findNearest(feature,3,result,neighbors,distances);double distance=distances.at<float>(0,0)/768.;if(distance>.065)return Reading();char c=alphabet[label];if(numbersOnly_&&!(c>='0'&&c<='9'))return Reading();float confidence=(float)std::max(0.,1.-2*std::sqrt(distance));r.text+=c;r.characterConfidence.push_back(confidence);sum+=confidence;}
 r.confidence=sum/r.text.size();return r;
}
Reading Recognizer::read(const cv::Mat& patch){Reading r;if(patch.empty())return r;cv::Mat gray;if(patch.channels()==3)cv::cvtColor(patch,gray,cv::COLOR_BGR2GRAY);else gray=patch;Reading earlyGlyphs=readGlyphs(gray);if(earlyGlyphs.confidence>.9f)return earlyGlyphs;cv::Mat logits=network_.forward(cv::dnn::blobFromImage(gray,1./127.5,cv::Size(100,32),cv::Scalar(127.5)));if(logits.dims!=3||logits.size[2]!=37)throw std::runtime_error("Unexpected CRNN output; expected [time,1,37]");const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";int last=-1;float sum=0;
 for(int t=0;t<logits.size[0];++t){const float* row=logits.ptr<float>(t,0);int index=(int)(std::max_element(row,row+37)-row);double total=0;float top=row[index];for(int k=0;k<37;++k)total+=std::exp(row[k]-top);float conf=(float)(1./total);if(index&&index!=last){char c=alphabet[index-1];if(numbersOnly_&&!(c>='0'&&c<='9'))return Reading();r.text+=c;r.characterConfidence.push_back(conf);sum+=conf;}last=index;}
 if(!r.text.empty())r.confidence=sum/r.text.size();Reading glyphs=readGlyphs(gray);if(glyphs.confidence>.8f&&glyphs.confidence>r.confidence)return glyphs;return r;
}
Pipeline::Pipeline(const Options& o):lightingEnabled_(o.lighting),allowTextCandidates_(o.allowTextCandidates),detector_(o.models+"/yolox.onnx",o.opencl,o.directml,o.gpuDevice),plateDetector_(o.models+"/plate.onnx",o.opencl,o.directml,o.gpuDevice),recognizer_(o.models+"/ocr_latin.onnx",o.numbersOnly,o.opencl,o.directml,o.gpuDevice),confidence_(o.confidence){if(o.ocrLanguages!="latin")multilingual_.reset(new MultilingualOcr(o.models+"/tessdata",o.ocrLanguages,o.numbersOnly));}
Pipeline::~Pipeline(){}

std::vector<cv::Rect> Pipeline::plateCandidates(const cv::Mat& vehicle){
 std::vector<cv::Rect> out;if(vehicle.cols<40||vehicle.rows<30)return out;cv::Mat gray,mask;cv::cvtColor(vehicle,gray,cv::COLOR_BGR2GRAY);if(lightingEnabled_&&cv::mean(gray)[0]<130)cv::threshold(gray,mask,0,255,cv::THRESH_BINARY|cv::THRESH_OTSU);else cv::threshold(gray,mask,160,255,cv::THRESH_BINARY);std::vector<std::vector<cv::Point> > contours;cv::findContours(mask,contours,cv::RETR_LIST,cv::CHAIN_APPROX_SIMPLE);
 for(auto& contour:contours){cv::Rect b=cv::boundingRect(contour);double ratio=b.width/(double)std::max(1,b.height);if(b.width>=35&&b.height>=10&&ratio>2.0&&ratio<6.8&&b.area()<vehicle.total()*.35&&b.y>vehicle.rows*.2){int dx=std::max(1,b.width/30),dy=std::max(1,b.height/10);b=(cv::Rect(b.x+dx,b.y+dy,b.width-2*dx,b.height-2*dy)&cv::Rect(0,0,vehicle.cols,vehicle.rows));if(b.area()>0)out.push_back(b);}}
 std::sort(out.begin(),out.end(),[](const cv::Rect& a,const cv::Rect& b){return a.area()>b.area();});if(out.size()>4)out.resize(4);return out;
}
std::vector<Detection> Pipeline::detect(const cv::Mat& original){
 cv::Mat image=lightingEnabled_?lighting_.apply(original):original;cv::Mat alternate=lightingEnabled_?nightExposureView(original):cv::Mat();
 const int side=640;double ratio=std::min(side/(double)image.cols,side/(double)image.rows);cv::Mat resized,padded(side,side,CV_8UC3,cv::Scalar(114,114,114));cv::resize(image,resized,cv::Size((int)(image.cols*ratio),(int)(image.rows*ratio)));resized.copyTo(padded(cv::Rect(0,0,resized.cols,resized.rows)));
 std::vector<Detection> candidates;for(int exposure=0;exposure<(alternate.empty()?1:2);++exposure){if(exposure==1){image=alternate;cv::resize(image,resized,cv::Size((int)(image.cols*ratio),(int)(image.rows*ratio)));padded.setTo(cv::Scalar(114,114,114));resized.copyTo(padded(cv::Rect(0,0,resized.cols,resized.rows)));}
 cv::Mat output=detector_.forward(cv::dnn::blobFromImage(padded,1.,cv::Size(side,side),cv::Scalar(),true,false));if(output.total()!=8400*85)throw std::runtime_error("Unexpected YOLOX output; expected [1,8400,85]");cv::Mat rows=output.reshape(1,8400);int rowIndex=0;
 for(int stride:std::vector<int>{8,16,32})for(int y=0;y<side/stride;++y)for(int x=0;x<side/stride;++x,++rowIndex){const float* p=rows.ptr<float>(rowIndex);int category=(int)(std::max_element(p+5,p+85)-(p+5));if(category!=1&&category!=2&&category!=3&&category!=5&&category!=7)continue;float confidence=p[4]*p[5+category];if(confidence<confidence_)continue;float cx=(p[0]+x)*stride,cy=(p[1]+y)*stride,w=std::exp(p[2])*stride,h=std::exp(p[3])*stride;if(!std::isfinite(w)||!std::isfinite(h))continue;Detection d;d.box=cv::Rect2f((float)((cx-w/2)/ratio),(float)((cy-h/2)/ratio),(float)(w/ratio),(float)(h/ratio))&cv::Rect2f(0,0,(float)image.cols,(float)image.rows);d.confidence=confidence;d.category=category;if(d.box.area()>30)candidates.push_back(d);}
 }
 std::vector<Detection> result;std::vector<cv::Rect> vehicleBoxes;std::vector<float> vehicleScores;for(const auto& d:candidates){vehicleBoxes.push_back(d.box);vehicleScores.push_back(d.confidence);}std::vector<int> vehicleKeep;cv::dnn::NMSBoxes(vehicleBoxes,vehicleScores,confidence_,.45f,vehicleKeep);for(int i:vehicleKeep)result.push_back(candidates[i]);

 std::sort(result.begin(),result.end(),[](const Detection& a,const Detection& b){return a.confidence>b.confidence;});if(result.size()>30)result.resize(30);
 for(auto& vehicle:result){cv::Rect crop=vehicle.box;crop&=cv::Rect(0,0,original.cols,original.rows);if(crop.area()>0)vehicle.appearance=appearanceFeature(original(crop));}
 std::vector<cv::Rect> plateBoxes;
 if(!result.empty()){
  cv::Mat prediction=plateDetector_.forward(cv::dnn::blobFromImage(padded,1./255,cv::Size(side,side),cv::Scalar(),true,false));
  if(prediction.dims!=3||prediction.size[2]!=6)throw std::runtime_error("Unexpected plate model output; expected [1,N,6]");
  cv::Mat proposals=prediction.reshape(1,(int)(prediction.total()/6));std::vector<cv::Rect> boxes;std::vector<float> scores;
  for(int i=0;i<proposals.rows;++i){const float* p=proposals.ptr<float>(i);float confidence=p[4]*p[5];if(confidence<.3f)continue;cv::Rect box((int)((p[0]-p[2]/2)/ratio),(int)((p[1]-p[3]/2)/ratio),(int)(p[2]/ratio),(int)(p[3]/ratio));box&=cv::Rect(0,0,image.cols,image.rows);if(box.width>=25&&box.height>=8){boxes.push_back(box);scores.push_back(confidence);}}
  std::vector<int> keep;cv::dnn::NMSBoxes(boxes,scores,.3f,.45f,keep);for(int i:keep)plateBoxes.push_back(boxes[i]);
 }
 for(size_t i=0;i<result.size()&&i<8;++i){cv::Rect roi=result[i].box;roi&=cv::Rect(0,0,image.cols,image.rows);if(roi.empty())continue;std::vector<cv::Rect> patches;for(cv::Rect b:plateBoxes)if(roi.contains(cv::Point(b.x+b.width/2,b.y+b.height/2))){b=(b&roi)-roi.tl();if(b.area()>0)patches.push_back(b);}if(allowTextCandidates_)for(const auto& candidate:plateCandidates(image(roi))){bool duplicate=false;for(const auto& prior:patches)if(iou(candidate,prior)>.7f)duplicate=true;if(!duplicate)patches.push_back(candidate);}if(patches.size()>5)patches.resize(5);for(cv::Rect b:patches){cv::Mat crop=original(roi)(b);if(lightingEnabled_)crop=normalizePlateLighting(crop);Reading text=multilingual_?multilingual_->read(crop):recognizer_.read(crop);if(textLength(text.text)>=4&&textLength(text.text)<=12&&!numbers(text.text).empty()&&text.confidence>result[i].plate.confidence){result[i].plate=text;result[i].plateBox=b+roi.tl();result[i].plateImage=original(roi)(b).clone();result[i].plateDetected=true;result[i].plateSource="heuristic_test";for(const auto& detected:plateBoxes)if(iou(result[i].plateBox,detected)>.7f){result[i].plateSource="detector";break;}}}}
 return result;
}
Worker::Worker(const Options& o):pipeline_(o),dropped_(0){thread_=std::thread(&Worker::loop,this);}
Worker::~Worker(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_one();thread_.join();}
void Worker::submit(const cv::Mat& image,int frame,double timestamp){std::lock_guard<std::mutex> lock(mutex_);if(!pending_.empty())++dropped_;pending_=image.clone();frame_=frame;timestamp_=timestamp;wake_.notify_one();}
bool Worker::take(Result& result){std::lock_guard<std::mutex> lock(mutex_);if(!error_.empty())throw std::runtime_error(error_);if(!available_)return false;result=latest_;available_=false;return true;}
void Worker::loop(){try{while(true){cv::Mat image;Result result;{std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this]{return stop_||!pending_.empty();});if(stop_)break;image=pending_;pending_.release();result.frame=frame_;result.timestamp=timestamp_;}auto start=std::chrono::steady_clock::now();result.detections=pipeline_.detect(image);result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();{std::lock_guard<std::mutex> lock(mutex_);latest_=result;available_=true;}}}catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex_);error_=e.what();}}
void draw(cv::Mat& image,const Tracker& tracker,double fps){
 if(image.empty())return;
 for(const Track& t:tracker.tracks()){
  if(t.hits<2)continue;cv::Scalar color(60+(t.id*67)%180,60+(t.id*103)%180,60+(t.id*41)%180);cv::rectangle(image,t.box,color,2);
  for(size_t i=1;i<t.trail.size();++i)cv::line(image,t.trail[i-1],t.trail[i],color,1);
  if(image.cols<80||image.rows<60)continue;
  int width=std::min(260,image.cols),left=std::max(0,std::min(image.cols-width,(int)t.box.x));
  int top=std::max(0,std::min(image.rows-58,(int)t.box.y));
  cv::rectangle(image,cv::Rect(left,top,width,58),cv::Scalar(20,20,20),cv::FILLED);
  std::ostringstream label;label<<'#'<<t.id<<' '<<typeName(t.category)<<' '<<trackingStatus(t)<<" | "<<plateStatus(t);cv::putText(image,label.str(),cv::Point(left+2,top+12),cv::FONT_HERSHEY_SIMPLEX,.34,color,1,cv::LINE_AA);
  bool current=t.plateStable&&t.plateVisible;std::string characterText=current?characters(t.plate.text):"(not currently read)",numberText=current?numbers(t.plate.text):"(not currently read)";
  if(current&&characterText.empty())characterText="(none)";if(current&&numberText.empty())numberText="(none)";
  cv::Mat a=renderTextLine("Characters: "+characterText,width,20,"Segoe UI",false),b=renderTextLine("Numbers: "+numberText,width,20,"Segoe UI",false);
  a.copyTo(image(cv::Rect(left,top+16,width,20)));b.copyTo(image(cv::Rect(left,top+37,width,20)));
 }
 if(image.cols>=80&&image.rows>=30){std::ostringstream title;title<<"Vehicle / plate monitor | "<<std::fixed<<std::setprecision(1)<<fps<<" fps";cv::putText(image,title.str(),cv::Point(2,22),cv::FONT_HERSHEY_SIMPLEX,.45,cv::Scalar(255,240,0),1,cv::LINE_AA);}
}
}
