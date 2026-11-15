#include "Multilingual.hpp"
#include <windows.h>
#include <stdexcept>
namespace car {
// Stable C entry points from the pinned Tesseract 5.2.0 capi.h.
struct MultilingualOcr::Impl {
 HMODULE library=NULL,leptonica=NULL;void* api=NULL;bool numbersOnly;
 void* (__cdecl *create)();void (__cdecl *destroy)(void*);
 int (__cdecl *init)(void*,const char*,const char*);
 void (__cdecl *image)(void*,const unsigned char*,int,int,int,int);
 void (__cdecl *pageMode)(void*,int);char* (__cdecl *text)(void*);
 int (__cdecl *confidence)(void*);void (__cdecl *freeText)(const char*);
 Impl(const std::string& data,const std::string& languages,bool numeric):numbersOnly(numeric) {
  wchar_t executable[MAX_PATH];GetModuleFileNameW(NULL,executable,MAX_PATH);std::wstring base(executable);base=base.substr(0,base.find_last_of(L"\\/")+1);
  leptonica=LoadLibraryW((base+L"leptonica-1.82.0.dll").c_str());library=LoadLibraryW((base+L"tesseract50.dll").c_str());
  try {
   if(!leptonica||!library)throw std::runtime_error("Cannot load native multilingual OCR DLLs; rebuild after tools/setup.ps1");
#define TESS_BIND(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(library,name));if(!member)throw std::runtime_error("Missing Tesseract C API: " name)
   TESS_BIND(create,"TessBaseAPICreate");TESS_BIND(destroy,"TessBaseAPIDelete");TESS_BIND(init,"TessBaseAPIInit3");TESS_BIND(image,"TessBaseAPISetImage");TESS_BIND(pageMode,"TessBaseAPISetPageSegMode");TESS_BIND(text,"TessBaseAPIGetUTF8Text");TESS_BIND(confidence,"TessBaseAPIMeanTextConf");TESS_BIND(freeText,"TessDeleteText");
#undef TESS_BIND
   api=create();if(!api||init(api,data.c_str(),languages.c_str())!=0)throw std::runtime_error("Cannot initialize requested OCR languages: "+languages+" in "+data);
   pageMode(api,7); // One text line; preserve the model's full alphabet, no ASCII whitelist.
  }catch(...){if(api&&destroy)destroy(api);if(library)FreeLibrary(library);if(leptonica)FreeLibrary(leptonica);throw;}
 }
 ~Impl(){if(api)destroy(api);if(library)FreeLibrary(library);if(leptonica)FreeLibrary(leptonica);}
};
MultilingualOcr::MultilingualOcr(const std::string& data,const std::string& languages,bool numbersOnly):impl_(new Impl(data,languages,numbersOnly)){}
MultilingualOcr::~MultilingualOcr(){}
Reading MultilingualOcr::read(const cv::Mat& patch) {
 Reading out;if(patch.empty())return out;cv::Mat gray;if(patch.channels()==3)cv::cvtColor(patch,gray,cv::COLOR_BGR2GRAY);else gray=patch.clone();
 double scale=std::max(1.,64./gray.rows);cv::resize(gray,gray,cv::Size(),scale,scale,cv::INTER_CUBIC);cv::copyMakeBorder(gray,gray,8,8,8,8,cv::BORDER_CONSTANT,cv::Scalar(255));
 impl_->image(impl_->api,gray.data,gray.cols,gray.rows,1,(int)gray.step);char* raw=impl_->text(impl_->api);if(!raw)return out;
 std::string value(raw);impl_->freeText(raw);out.text=compactText(value);
 if(impl_->numbersOnly&&!characters(out.text).empty())return Reading();
 out.confidence=std::max(0.f,std::min(1.f,impl_->confidence(impl_->api)/100.f));
 // This backend exposes line confidence; repeated values are explicitly marked in exports.
 out.characterConfidence.assign(textLength(out.text),out.confidence);return out;
}
}
