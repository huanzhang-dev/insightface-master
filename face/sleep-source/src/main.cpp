#include "Sleep.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <sstream>
int main(int argc,char** argv){try{ drowse::Platform platform;
 wchar_t path[MAX_PATH];GetModuleFileNameW(NULL,path,MAX_PATH);std::wstring executable(path);std::wstring wideRoot=executable.substr(0,executable.find_last_of(L"\\/")+1)+L"../../";wchar_t full[MAX_PATH];GetFullPathNameW(wideRoot.c_str(),MAX_PATH,full,NULL);int n=WideCharToMultiByte(CP_UTF8,0,full,-1,NULL,0,NULL,NULL);std::string root(n,' ');WideCharToMultiByte(CP_UTF8,0,full,-1,&root[0],n,NULL,NULL);root.pop_back();

#ifdef SLEEP_DATA_ROOT
 root=SLEEP_DATA_ROOT;
#endif
 // Installed applications use the sibling data directory; developer builds use CMake's external data root.
 std::wstring sibling=executable.substr(0,executable.find_last_of(L"\\/")+1)+L"../data/";
 if(GetFileAttributesW((sibling+L"models/face_detector.onnx").c_str())!=INVALID_FILE_ATTRIBUTES){int count=WideCharToMultiByte(CP_UTF8,0,sibling.c_str(),-1,NULL,0,NULL,NULL);root.assign(count,' ');WideCharToMultiByte(CP_UTF8,0,sibling.c_str(),-1,&root[0],count,NULL,NULL);root.pop_back();}
 drowse::Settings s;s.models=root+"models";s.output=root+"output";std::string action="run";
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--help"){std::cout<<"Sleep.exe [--source CAMERA_INDEX|VIDEO|STREAM] [--infrared|--visible] [--raw-ir] [--fourcc FOURCC]\n [--ir-black 0] [--ir-white 65535] [--eyewear auto|opaque] [--roi x,y,w,h] [--fps 30]\n [--models PATH] [--output PATH] [--directml|--cpu] [--headless] [--offline] [--sound]\n [--frames N] [--width 640] [--height 480] [--ear-threshold .18] [--closure-seconds 1.5]\n [--perclos .28] [--window 30] [--min-valid 10] [--yaw-limit 40] [--pitch-limit 35]\nSleep.exe --self-test\nSleep.exe --demo [--directml]\n";return 0;}
 if(a=="--infrared")s.infrared=true;else if(a=="--visible")s.infrared=false;else if(a=="--raw-ir")s.rawIR=true;else if(a=="--directml")s.directml=true;else if(a=="--cpu")s.directml=false;else if(a=="--headless")s.headless=true;else if(a=="--offline")s.offline=true;else if(a=="--sound")s.sound=true;else if(a=="--self-test")action="tests";else if(a=="--demo")action="demo";
 else{if(++i==argc)throw std::invalid_argument("Missing value for "+a);std::string v=argv[i];if(a=="--source")s.source=v;else if(a=="--models")s.models=v;else if(a=="--output")s.output=v;else if(a=="--eyewear")s.eyewear=v;else if(a=="--fps")s.fps=std::stoi(v);else if(a=="--frames")s.frames=std::stoi(v);else if(a=="--width")s.cameraWidth=std::stoi(v);else if(a=="--height")s.cameraHeight=std::stoi(v);else if(a=="--fourcc")s.fourcc=v;else if(a=="--ir-black")s.irBlack=std::stod(v);else if(a=="--ir-white")s.irWhite=std::stod(v);else if(a=="--ear-threshold")s.earThreshold=std::stod(v);else if(a=="--closure-seconds")s.closureAlarm=std::stod(v);else if(a=="--perclos")s.perclosAlarm=std::stod(v);else if(a=="--window")s.window=std::stod(v);else if(a=="--min-valid")s.minValid=std::stod(v);else if(a=="--yaw-limit")s.yawLimit=std::stod(v);else if(a=="--pitch-limit")s.pitchLimit=std::stod(v);else if(a=="--roi"){for(char& c:v)if(c==',')c=' ';std::istringstream in(v);if(!(in>>s.roi.x>>s.roi.y>>s.roi.width>>s.roi.height)||s.roi.x<0||s.roi.y<0||s.roi.width<=0||s.roi.height<=0)throw std::invalid_argument("ROI must be x,y,width,height");}else throw std::invalid_argument("Unknown option: "+a);}
 }
 drowse::validateSettings(s);if(action=="tests")return drowse::tests(root);if(action=="demo")return drowse::demo(root,s.directml);return drowse::run(s);
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
