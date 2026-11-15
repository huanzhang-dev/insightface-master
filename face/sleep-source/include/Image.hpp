#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
namespace drowse {
struct Point { double x=0,y=0; Point(){} Point(double a,double b):x(a),y(b){} };
struct Rect { int x=0,y=0,width=0,height=0; Rect(){} Rect(int a,int b,int w,int h):x(a),y(b),width(w),height(h){} int area() const {return width*height;} };
Rect intersection(Rect a,Rect b);
double distance(Point a,Point b);
struct Image {
 int width=0,height=0,stride=0;std::vector<unsigned char> data;
 Image(){} Image(int w,int h,unsigned char gray=0);
 bool empty() const {return data.empty();}
 unsigned char* row(int y){return data.data()+y*stride;}
 const unsigned char* row(int y) const {return data.data()+y*stride;}
};
Image loadImage(const std::string& path);
void saveImage(const std::string& path,const Image& image);
std::vector<unsigned char> encodeJPEG(const Image& image);
Image decodeImage(const std::vector<unsigned char>& bytes);
Image resizeImage(const Image& image,int width,int height);
Image cropImage(const Image& image,Rect roi);
Image grayscale(const Image& image);
Image contrastImage(const Image& image);
Image dimImage(const Image& image,double scale,double offset);
Image rotateImage(const Image& image,double degrees);
Image blurImage(const Image& image,int radius);
Image fromY16(const uint16_t* input,int width,int height,int strideBytes,double black,double white);
void samplePixel(const Image& image,double x,double y,double out[3]);
void fillRect(Image& image,Rect rect,unsigned char b,unsigned char g,unsigned char r);
void drawRect(Image& image,Rect rect,unsigned char b,unsigned char g,unsigned char r);
void drawPoint(Image& image,Point point,unsigned char b,unsigned char g,unsigned char r);
void text(Image& image,int x,int y,const std::string& value,unsigned int rgb=0xeeeeee,int size=18);
void paste(Image& destination,const Image& source,int x,int y);
struct Quality {double mean=0,sd=0,sharpness=0,clipped=0,dark=0;};
Quality quality(const Image& image,Rect roi);
class Platform {public:Platform();~Platform();Platform(const Platform&)=delete;Platform& operator=(const Platform&)=delete;};
class VideoSource {struct Impl;std::unique_ptr<Impl> impl_;public:VideoSource(const std::string& source,int width,int height,int fps,bool rawIR,const std::string& fourcc,double black,double white,bool preferNativeAvi=true);~VideoSource();bool read(Image& image);double fps() const;bool camera() const;};
class VideoWriter {struct Impl;std::unique_ptr<Impl> impl_;public:VideoWriter(const std::string& path,int width,int height,int fps);~VideoWriter();void write(const Image& image);void close();};
int preview(const Image& image);
void closePreview();
}
