#include "Lighting.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
namespace car {
static int percentile(const cv::Mat& gray,double fraction) {
 int histogram[256]={};for(int y=0;y<gray.rows;++y){const unsigned char* p=gray.ptr<unsigned char>(y);for(int x=0;x<gray.cols;++x)++histogram[p[x]];}
 int target=std::max(1,(int)std::ceil(gray.total()*fraction)),sum=0;
 for(int i=0;i<256;++i){sum+=histogram[i];if(sum>=target)return i;}return 255;
}
cv::Mat Lighting::apply(const cv::Mat& frame) {
 if(frame.empty())return frame;
 if(frame.type()!=CV_8UC3)throw std::invalid_argument("Lighting expects 8-bit BGR frames");
 cv::Mat probe,gray;cv::resize(frame,probe,cv::Size(160,90),0,0,cv::INTER_AREA);cv::cvtColor(probe,gray,cv::COLOR_BGR2GRAY);
 int mid=percentile(gray,.5),upper=percentile(gray,.95),lower=percentile(gray,.15);
 // A nearly black signal contains no reliable scene detail: do not amplify sensor noise.
 if(upper<=5){gamma_=1.;initialized_=false;return frame;}
 bool dark=mid<50;double target=dark?std::max(.4,std::min(1.,std::log(.44)/std::log(std::max(6,mid)/255.))):1.;
 // Smooth ordinary exposure changes; reset on abrupt day/night transitions.
 if(!initialized_||std::abs(target-gamma_)>.35)gamma_=target;else gamma_=.75*gamma_+.25*target;initialized_=true;
 bool uneven=lower<25&&upper>100&&mid<65;
 if(gamma_>.99&&!uneven)return frame;
 cv::Mat table(1,256,CV_8U);for(int i=0;i<256;++i)table.at<unsigned char>(i)=cv::saturate_cast<unsigned char>(255*std::pow(i/255.,gamma_));
 cv::Mat corrected;cv::LUT(frame,table,corrected);
 // Limit noise enhancement and retain colour: local contrast only on luminance.
 cv::Mat lab;cv::cvtColor(corrected,lab,cv::COLOR_BGR2Lab);std::vector<cv::Mat> channels;cv::split(lab,channels);cv::Mat smooth,local;
 if(mid<40){cv::GaussianBlur(channels[0],smooth,cv::Size(3,3),.6);cv::addWeighted(channels[0],.75,smooth,.25,0,channels[0]);}
 cv::Ptr<cv::CLAHE> clahe=cv::createCLAHE(2.,cv::Size(8,8));clahe->apply(channels[0],local);cv::addWeighted(channels[0],.65,local,.35,0,channels[0]);cv::merge(channels,lab);cv::cvtColor(lab,corrected,cv::COLOR_Lab2BGR);return corrected;
}
cv::Mat nightExposureView(const cv::Mat& frame) {
 if(frame.empty())return {};cv::Mat probe,gray;cv::resize(frame,probe,cv::Size(160,90));cv::cvtColor(probe,gray,cv::COLOR_BGR2GRAY);
 int mid=percentile(gray,.5),upper=percentile(gray,.95);if(mid>=50||upper<=5)return {};
 double gain=std::max(1.,std::min(7.,220./std::max(10,upper)));cv::Mat corrected;frame.convertTo(corrected,CV_8U,gain,-2*gain);return corrected;
}
cv::Mat normalizePlateLighting(const cv::Mat& patch) {
 if(patch.empty())return patch;cv::Mat gray;if(patch.channels()==3)cv::cvtColor(patch,gray,cv::COLOR_BGR2GRAY);else gray=patch.clone();
 if(gray.type()!=CV_8UC1)throw std::invalid_argument("Plate lighting expects 8-bit pixels");
 int low=percentile(gray,.05),high=percentile(gray,.95);
 if(high-low<12)return gray; // Flat regions and noise are not characters.
 cv::Mat result;gray.convertTo(result,CV_8U,255./(high-low),-low*255./(high-low));
 // Remove slow spatial illumination gradients, retaining thin character strokes.
 if(low>45&&high>160){cv::Mat background;cv::GaussianBlur(result,background,cv::Size(0,0),std::max(3.,gray.rows*.3));cv::divide(result+20,background+20,result,255.,CV_8U);}
 return result;
}
int generateLightingVideos(const std::string& directory) {
 for(int mode=0;mode<2;++mode){cv::VideoCapture source(directory+"/composite_plates.avi");if(!source.isOpened())throw std::runtime_error("Generate composite_plates.avi first");
  std::string name=mode==0?"lighting_night":"lighting_uneven";cv::VideoWriter out;cv::Mat frame;int index=0;cv::RNG random(20261115);
  while(source.read(frame)){
   if(!out.isOpened()){out.open(directory+"/"+name+".avi",cv::VideoWriter::fourcc('M','J','P','G'),30,frame.size());if(!out.isOpened())throw std::runtime_error("Cannot write lighting test video");}
   cv::Mat values;frame.convertTo(values,CV_32FC3);
   for(int y=0;y<values.rows;++y){cv::Vec3f* row=values.ptr<cv::Vec3f>(y);for(int x=0;x<values.cols;++x){float exposure=mode==0?.14f:(.12f+.85f*x/values.cols);row[x]=row[x]*exposure+cv::Vec3f(2,2,2);}}
   cv::Mat noise(values.size(),values.type());random.fill(noise,cv::RNG::NORMAL,0,1.2);values+=noise;values.convertTo(frame,CV_8UC3);
   cv::putText(frame,"SIMULATED LIGHTING - NOT REAL NIGHT FOOTAGE",cv::Point(12,25),cv::FONT_HERSHEY_SIMPLEX,.55,cv::Scalar(180,180,180),1);out.write(frame);if(index++==60)cv::imwrite(directory+"/"+name+".jpg",frame);
  }std::cout<<"Created "<<name<<".avi ("<<index<<" frames)\n";
 }return 0;
}
}
