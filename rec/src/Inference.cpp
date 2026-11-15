#include "Inference.hpp"
#include <windows.h>
#include <dxgi.h>
#include <onnxruntime_c_api.h>
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace car {
struct Runtime {
 HMODULE library=NULL;const OrtApi* api=NULL;OrtEnv* environment=NULL;
 Runtime(){wchar_t path[MAX_PATH];GetModuleFileNameW(NULL,path,MAX_PATH);std::wstring file(path);file=file.substr(0,file.find_last_of(L"\\/"))+L"\\onnxruntime.dll";library=LoadLibraryW(file.c_str());if(!library)throw std::runtime_error("Cannot load onnxruntime.dll; run tools/setup.ps1 or use CPU mode");
  typedef const OrtApiBase* (ORT_API_CALL *Getter)();Getter get=(Getter)GetProcAddress(library,"OrtGetApiBase");if(!get)throw std::runtime_error("ONNX Runtime API missing");api=get()->GetApi(ORT_API_VERSION);if(!api)throw std::runtime_error("ONNX Runtime version mismatch");check(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"Car",&environment));
 }
 void check(OrtStatus* status){if(status){std::string message=api->GetErrorMessage(status);api->ReleaseStatus(status);throw std::runtime_error(message);}}
 ~Runtime(){if(environment)api->ReleaseEnv(environment);if(library)FreeLibrary(library);}
};
static Runtime& runtime(){static Runtime instance;return instance;}
static int gpuIndex(int requested){if(requested>=0)return requested;IDXGIFactory1* factory=NULL;if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),(void**)&factory)))return 0;SIZE_T memory=0;int selected=0;for(UINT i=0;;++i){IDXGIAdapter1* adapter=NULL;if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc={};adapter->GetDesc1(&desc);if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&desc.DedicatedVideoMemory>memory){memory=desc.DedicatedVideoMemory;selected=(int)i;}adapter->Release();}factory->Release();return selected;}
struct Network::Impl {
 Runtime& rt;OrtSession* session=NULL;OrtMemoryInfo* memory=NULL;std::string input,output;
 Impl():rt(runtime()){}
 ~Impl(){if(session)rt.api->ReleaseSession(session);if(memory)rt.api->ReleaseMemoryInfo(memory);}
};
Network::Network(const std::string& path,bool opencl,bool directml,int device){
 if(!directml){cpu_=cv::dnn::readNetFromONNX(path);cpu_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);if(opencl){if(!cv::ocl::haveOpenCL())throw std::runtime_error("OpenCL is unavailable");cv::ocl::setUseOpenCL(true);cpu_.setPreferableTarget(cv::dnn::DNN_TARGET_OPENCL);}else cpu_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);return;}
 gpu_.reset(new Impl());Runtime& rt=gpu_->rt;const OrtApi* api=rt.api;OrtSessionOptions* options=NULL;rt.check(api->CreateSessionOptions(&options));
 try{rt.check(api->DisableMemPattern(options));rt.check(api->SetSessionExecutionMode(options,ORT_SEQUENTIAL));rt.check(api->SetSessionGraphOptimizationLevel(options,ORT_ENABLE_ALL));rt.check(api->SetIntraOpNumThreads(options,2));
  typedef OrtStatus* (ORT_API_CALL *Append)(OrtSessionOptions*,int);Append append=(Append)GetProcAddress(rt.library,"OrtSessionOptionsAppendExecutionProvider_DML");if(!append)throw std::runtime_error("DirectML execution provider is missing");int selected=gpuIndex(device);rt.check(append(options,selected));
  int count=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,NULL,0);std::vector<wchar_t> wide(count);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide.data(),count);rt.check(api->CreateSession(rt.environment,wide.data(),options,&gpu_->session));api->ReleaseSessionOptions(options);options=NULL;
  OrtAllocator* allocator=NULL;rt.check(api->GetAllocatorWithDefaultOptions(&allocator));char* name=NULL;rt.check(api->SessionGetInputName(gpu_->session,0,allocator,&name));gpu_->input=name;allocator->Free(allocator,name);name=NULL;rt.check(api->SessionGetOutputName(gpu_->session,0,allocator,&name));gpu_->output=name;allocator->Free(allocator,name);rt.check(api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&gpu_->memory));std::cout<<"DirectML adapter "<<selected<<": "<<path<<'\n';
 }catch(...){if(options)api->ReleaseSessionOptions(options);throw;}
}
Network::~Network(){}
cv::Mat Network::forward(const cv::Mat& blob){if(!gpu_){cpu_.setInput(blob);return cpu_.forward();}Runtime& rt=gpu_->rt;const OrtApi* api=rt.api;OrtValue *input=NULL,*output=NULL;OrtTensorTypeAndShapeInfo* info=NULL;
 try{std::vector<int64_t> shape;for(int i=0;i<blob.dims;++i)shape.push_back(blob.size[i]);rt.check(api->CreateTensorWithDataAsOrtValue(gpu_->memory,(void*)blob.data,blob.total()*sizeof(float),shape.data(),shape.size(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&input));const char* in=gpu_->input.c_str();const char* out=gpu_->output.c_str();rt.check(api->Run(gpu_->session,NULL,&in,(const OrtValue* const*)&input,1,&out,1,&output));rt.check(api->GetTensorTypeAndShape(output,&info));size_t dimensions=0;rt.check(api->GetDimensionsCount(info,&dimensions));std::vector<int64_t> dims(dimensions);rt.check(api->GetDimensions(info,dims.data(),dims.size()));std::vector<int> sizes;for(auto d:dims){if(d<=0||d>2147483647)throw std::runtime_error("Invalid inference output dimensions");sizes.push_back((int)d);}void* data=NULL;rt.check(api->GetTensorMutableData(output,&data));cv::Mat result((int)sizes.size(),sizes.data(),CV_32F,data);result=result.clone();api->ReleaseTensorTypeAndShapeInfo(info);api->ReleaseValue(input);api->ReleaseValue(output);return result;
 }catch(...){if(info)api->ReleaseTensorTypeAndShapeInfo(info);if(input)api->ReleaseValue(input);if(output)api->ReleaseValue(output);throw;}
}
}
