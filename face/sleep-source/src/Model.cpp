#include "Model.hpp"
#include <windows.h>
#include <onnxruntime_c_api.h>
#include <stdexcept>
namespace drowse {
struct Runtime {
 HMODULE library=NULL;const OrtApi* api=NULL;OrtEnv* env=NULL;
 void check(OrtStatus* status){if(status){std::string message=api->GetErrorMessage(status);api->ReleaseStatus(status);throw std::runtime_error(message);}}
 Runtime(){wchar_t path[MAX_PATH];GetModuleFileNameW(NULL,path,MAX_PATH);std::wstring file(path);file=file.substr(0,file.find_last_of(L"\\/")+1)+L"onnxruntime.dll";
  library=LoadLibraryW(file.c_str());if(!library)throw std::runtime_error("Cannot load application-local onnxruntime.dll");
  try{typedef const OrtApiBase* (ORT_API_CALL *Getter)();Getter get=reinterpret_cast<Getter>(GetProcAddress(library,"OrtGetApiBase"));if(!get)throw std::runtime_error("ORT entry point missing");api=get()->GetApi(ORT_API_VERSION);if(!api)throw std::runtime_error("ORT version mismatch");check(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"Sleep",&env));}catch(...){FreeLibrary(library);throw;}
 }
 ~Runtime(){if(env)api->ReleaseEnv(env);if(library)FreeLibrary(library);}
};
static Runtime& runtime(){static Runtime instance;return instance;}
struct Model::Impl {
 Runtime& rt;OrtSession* session=NULL;OrtMemoryInfo* memory=NULL;std::string input;std::vector<std::string> outputs;
 Impl():rt(runtime()){}
 ~Impl(){if(session)rt.api->ReleaseSession(session);if(memory)rt.api->ReleaseMemoryInfo(memory);}
};
Model::Model(const std::string& path,bool directml):impl_(new Impl()){
 Runtime& rt=impl_->rt;const OrtApi* api=rt.api;OrtSessionOptions* options=NULL;rt.check(api->CreateSessionOptions(&options));
 try{
  rt.check(api->SetIntraOpNumThreads(options,2));rt.check(api->SetInterOpNumThreads(options,1));rt.check(api->SetSessionGraphOptimizationLevel(options,ORT_ENABLE_ALL));
  if(directml){rt.check(api->DisableMemPattern(options));rt.check(api->SetSessionExecutionMode(options,ORT_SEQUENTIAL));typedef OrtStatus* (ORT_API_CALL *Append)(OrtSessionOptions*,int);Append append=reinterpret_cast<Append>(GetProcAddress(rt.library,"OrtSessionOptionsAppendExecutionProvider_DML"));if(!append)throw std::runtime_error("DirectML provider missing");rt.check(append(options,0));}
  int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,NULL,0);if(!count)throw std::runtime_error("Invalid model path UTF-8");std::vector<wchar_t> wide(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,wide.data(),count);rt.check(api->CreateSession(rt.env,wide.data(),options,&impl_->session));api->ReleaseSessionOptions(options);options=NULL;
  OrtAllocator* allocator=NULL;rt.check(api->GetAllocatorWithDefaultOptions(&allocator));size_t inputs=0;rt.check(api->SessionGetInputCount(impl_->session,&inputs));if(inputs!=1)throw std::runtime_error("Expected one model input");char* name=NULL;rt.check(api->SessionGetInputName(impl_->session,0,allocator,&name));impl_->input=name;allocator->Free(allocator,name);
  size_t outputs=0;rt.check(api->SessionGetOutputCount(impl_->session,&outputs));for(size_t i=0;i<outputs;++i){name=NULL;rt.check(api->SessionGetOutputName(impl_->session,i,allocator,&name));impl_->outputs.push_back(name);allocator->Free(allocator,name);}rt.check(api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&impl_->memory));
 }catch(...){if(options)api->ReleaseSessionOptions(options);throw;}
}
Model::~Model(){}
std::vector<Tensor> Model::forward(const Tensor& blob){
 size_t elements=1;for(auto d:blob.shape){if(d<1||d>100000000||elements>100000000/(size_t)d)throw std::invalid_argument("Invalid input tensor shape");elements*=(size_t)d;}if(blob.shape.empty()||elements!=blob.values.size())throw std::invalid_argument("Tensor shape/data mismatch");
 Runtime& rt=impl_->rt;const OrtApi* api=rt.api;OrtValue* input=NULL;std::vector<OrtValue*> values(impl_->outputs.size(),NULL);OrtTensorTypeAndShapeInfo* info=NULL;
 try{
  const std::vector<int64_t>& shape=blob.shape;rt.check(api->CreateTensorWithDataAsOrtValue(impl_->memory,(void*)blob.values.data(),blob.values.size()*sizeof(float),shape.data(),shape.size(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&input));
  std::vector<const char*> outputs;for(const auto& name:impl_->outputs)outputs.push_back(name.c_str());const char* name=impl_->input.c_str();rt.check(api->Run(impl_->session,NULL,&name,(const OrtValue* const*)&input,1,outputs.data(),outputs.size(),values.data()));std::vector<Tensor> result;
  for(auto value:values){rt.check(api->GetTensorTypeAndShape(value,&info));ONNXTensorElementDataType type;rt.check(api->GetTensorElementType(info,&type));if(type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)throw std::runtime_error("Expected float32 output");size_t rank=0;rt.check(api->GetDimensionsCount(info,&rank));if(rank==0||rank>8)throw std::runtime_error("Unsupported output rank");std::vector<int64_t> dims(rank);rt.check(api->GetDimensions(info,dims.data(),rank));std::vector<int> sizes;size_t total=1;for(auto dimension:dims){if(dimension<1||dimension>100000000||total>100000000/(size_t)dimension)throw std::runtime_error("Invalid output shape");sizes.push_back((int)dimension);total*=(size_t)dimension;}
   void* data=NULL;rt.check(api->GetTensorMutableData(value,&data));Tensor output;output.shape=dims;output.values.assign((float*)data,(float*)data+total);result.push_back(std::move(output));api->ReleaseTensorTypeAndShapeInfo(info);info=NULL;
  }
  api->ReleaseValue(input);for(auto value:values)api->ReleaseValue(value);return result;
 }catch(...){if(info)api->ReleaseTensorTypeAndShapeInfo(info);if(input)api->ReleaseValue(input);for(auto value:values)if(value)api->ReleaseValue(value);throw;}
}
}
