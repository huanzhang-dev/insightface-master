#include <cmath>
#include <stdexcept>
#include "Car.hpp"
#include <windows.h>
#include <direct.h>
#include <iostream>
namespace car {
cv::Mat renderTextLine(const std::string& text,int width,int height,const std::string& fontName,bool bold){
 HDC dc=CreateCompatibleDC(NULL);BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
 if(!dc||!bitmap||!pixels)throw std::runtime_error("Cannot create text bitmap");HGDIOBJ oldBitmap=SelectObject(dc,bitmap);RECT rect={0,0,width,height};FillRect(dc,&rect,(HBRUSH)GetStockObject(WHITE_BRUSH));std::wstring unicode=wideText(text),face=wideText(fontName);HFONT font=CreateFontW(-(int)(height*.75),0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face.c_str());HGDIOBJ oldFont=SelectObject(dc,font);SetTextColor(dc,RGB(0,0,0));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,unicode.c_str(),(int)unicode.size(),&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);cv::Mat bgra(height,width,CV_8UC4,pixels),bgr;cv::cvtColor(bgra,bgr,cv::COLOR_BGRA2BGR);SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);return bgr;
}
static void plate(cv::Mat& image,const cv::Rect& r,const std::string& text){cv::Mat patch=renderTextLine(text,r.width,r.height);patch.copyTo(image(r));cv::rectangle(image,r,cv::Scalar(50,50,50),1);}

int generateEvidenceVideos(const std::string& directory){
 cv::VideoCapture source(directory+"/composite_plates.avi");if(!source.isOpened())throw std::runtime_error("Composite source missing");
 cv::Mat car=cv::imread(directory+"/car_photo.png"),bus=cv::imread(directory+"/bus_photo.jpg"),truck=cv::imread(directory+"/truck_modern.jpg");if(car.empty()||bus.empty()||truck.empty())throw std::runtime_error("Source photos missing");
 std::vector<cv::Mat> photos={car,bus,truck(cv::Rect(65,215,785,205)).clone()};cv::VideoWriter output(directory+"/composite_no_labels.avi",cv::VideoWriter::fourcc('M','J','P','G'),30,cv::Size(960,540));if(!output.isOpened())throw std::runtime_error("Cannot create no-label negative video");
 cv::Mat image;int frame=0;while(source.read(image)){
  for(int v=0;v<3;++v){int w=v==2?270:250,h=v==1?333:v==2?115:170;int x=35+v*315+(int)(8*std::sin(frame*.025+v)),y=190+(285-h)/2+(int)(8*std::sin(frame*.035));cv::Mat photo;cv::resize(photos[v],photo,cv::Size(w,h));photo.copyTo(image(cv::Rect(x,y,w,h)));}
  cv::rectangle(image,cv::Rect(0,0,image.cols,90),cv::Scalar(75,75,75),cv::FILLED);cv::putText(image,"NEGATIVE TEST - NO INSERTED PLATE TEXT",cv::Point(12,50),cv::FONT_HERSHEY_SIMPLEX,.65,cv::Scalar(255,255,255),1);output.write(image);if(frame++==60)cv::imwrite(directory+"/composite_no_labels.jpg",image);
 }return 0;
}
int generateVideos(const std::string& directory,const std::string& models){_mkdir(directory.c_str());const int width=960,height=540,frames=150;const char* texts[]={"AB1234","789012","TR4567"};
 for(int mode=0;mode<3;++mode){std::string name=mode==0?"synthetic_day":mode==1?"synthetic_occlusion":"synthetic_night";cv::VideoWriter out(directory+"/"+name+".avi",cv::VideoWriter::fourcc('M','J','P','G'),25,cv::Size(width,height));if(!out.isOpened())throw std::runtime_error("Cannot create test footage");std::ofstream truth(directory+"/"+name+"_truth.csv");truth<<"frame,vehicle_id,vehicle_type,plate,vehicle_x,vehicle_y,vehicle_w,vehicle_h,plate_x,plate_y,plate_w,plate_h,visible\n";
 for(int frame=0;frame<frames;++frame){cv::Mat image(height,width,CV_8UC3,cv::Scalar(65,70,68));cv::rectangle(image,cv::Rect(0,0,width,100),cv::Scalar(210,175,130),cv::FILLED);for(int lane=1;lane<3;++lane)for(int y=110;y<height;y+=90)cv::line(image,cv::Point(lane*320,y+(frame*3)%90),cv::Point(lane*320,y+40+(frame*3)%90),cv::Scalar(210,210,210),4);
 for(int v=0;v<3;++v){int w=v==0?190:220,h=v==1?220:170;int x=45+v*315+(int)(12*std::sin(frame*.03+v)),y=150+(int)(frame*.45)-v*20;cv::Rect body(x,y,w,h),glass(x+18,y+20,w-36,v==1?70:50);cv::Scalar color=v==0?cv::Scalar(175,60,35):v==1?cv::Scalar(30,185,225):cv::Scalar(90,160,65);cv::rectangle(image,body,color,cv::FILLED);cv::rectangle(image,body,cv::Scalar(20,20,20),2);cv::rectangle(image,glass,cv::Scalar(45,45,35),cv::FILLED);cv::line(image,cv::Point(x+w/2,y+20),cv::Point(x+w/2,y+glass.height+20),cv::Scalar(160,160,160),2);for(int side=0;side<2;++side){cv::rectangle(image,cv::Rect(x+8+side*(w-38),y+h-58,30,14),cv::Scalar(30,40,240),cv::FILLED);cv::rectangle(image,cv::Rect(x+12+side*(w-32),y+h-8,20,15),cv::Scalar(10,10,10),cv::FILLED);}cv::Rect pr(x+(w-122)/2,y+h-44,122,31);plate(image,pr,texts[v]);bool visible=!(mode==1&&v==0&&frame>=60&&frame<90);if(!visible)cv::rectangle(image,cv::Rect(x-8,y+70,w+16,90),cv::Scalar(55,55,55),cv::FILLED);truth<<frame<<','<<v+1<<','<<typeName(v==0?2:v==1?5:7)<<','<<texts[v]<<','<<x<<','<<y<<','<<w<<','<<h<<','<<pr.x<<','<<pr.y<<','<<pr.width<<','<<pr.height<<','<<visible<<'\n';}
 if(mode==2)image.convertTo(image,-1,.38,8);cv::putText(image,"SYNTHETIC TEST - NOT REAL CAMERA FOOTAGE",cv::Point(12,25),cv::FONT_HERSHEY_SIMPLEX,.6,cv::Scalar(255,255,255),1);out.write(image);if(frame==50)cv::imwrite(directory+"/"+name+".jpg",image);}
 std::cout<<"Created "<<name<<".avi (150 frames, 6 seconds)\n";}
 cv::Mat board(90,360,CV_8UC3,cv::Scalar(255,255,255));plate(board,cv::Rect(2,2,356,86),"AB1234");cv::imwrite(directory+"/plate_letters_numbers.png",board);board.setTo(cv::Scalar(255,255,255));plate(board,cv::Rect(2,2,356,86),"789012");cv::imwrite(directory+"/plate_numbers.png",board);
 cv::Mat bus=cv::imread(directory+"/bus_photo.jpg"),truckSource=cv::imread(directory+"/truck_modern.jpg"),carPhoto;
 if(!bus.empty()&&!truckSource.empty()){
  Options options;options.models=models;Pipeline pipeline(options);cv::VideoCapture source(directory+"/traffic_real.mp4");
  for(int position:std::vector<int>{100,120,90,150}){source.set(cv::CAP_PROP_POS_FRAMES,position);cv::Mat frame;if(!source.read(frame))continue;auto ds=pipeline.detect(frame);for(const auto& d:ds)if(d.category==2&&d.box.area()>12000){cv::Rect roi=d.box;carPhoto=frame(roi).clone();break;}if(!carPhoto.empty())break;}
  if(!carPhoto.empty()){
   cv::imwrite(directory+"/car_photo.png",carPhoto);std::vector<cv::Mat> photos={carPhoto,bus.clone(),truckSource(cv::Rect(65,215,785,205)).clone()};
   cv::VideoWriter clip(directory+"/composite_plates.avi",cv::VideoWriter::fourcc('M','J','P','G'),30,cv::Size(960,540));if(!clip.isOpened())throw std::runtime_error("Cannot create composite test video");std::ofstream truth(directory+"/composite_plates_truth.csv");truth<<"frame,vehicle_id,vehicle_type,plate,vehicle_x,vehicle_y,vehicle_w,vehicle_h,plate_x,plate_y,plate_w,plate_h,visible\n";
   for(int f=0;f<180;++f){cv::Mat scene(540,960,CV_8UC3,cv::Scalar(75,75,75));for(int lane=1;lane<=2;++lane)cv::line(scene,cv::Point(320*lane,100),cv::Point(320*lane,540),cv::Scalar(220,220,220),3);for(int v=0;v<3;++v){int w=v==2?270:250,h=v==1?333:v==2?115:170;int x=35+v*315+(int)(8*std::sin(f*.025+v)),y=190+(285-h)/2+(int)(8*std::sin(f*.035));cv::Rect box(x,y,w,h);cv::Mat photo;cv::resize(photos[v],photo,box.size());photo.copyTo(scene(box));cv::Rect pr(x+(w-136)/2,y+(int)(h*(v==1?.55:v==2?.55:.77)),136,32);plate(scene,pr,texts[v]);truth<<f<<','<<v+1<<','<<typeName(v==0?2:v==1?5:7)<<','<<texts[v]<<','<<x<<','<<y<<','<<w<<','<<h<<','<<pr.x<<','<<pr.y<<','<<pr.width<<','<<pr.height<<",1\n";}cv::putText(scene,"COMPOSITE TEST - ARTIFICIAL PLATES",cv::Point(15,65),cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar(255,255,255),1);clip.write(scene);if(f==60)cv::imwrite(directory+"/composite_plates.jpg",scene);}
   std::cout<<"Created composite_plates.avi (180 frames, 30 FPS)\n";
  }
 }
 return 0;
}
}
