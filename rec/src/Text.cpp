#include "Text.hpp"
#include <windows.h>
#include <stdexcept>
namespace car {
std::wstring wideText(const std::string& text) {
 if(text.empty())return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),NULL,0);
 if(!n)throw std::runtime_error("Invalid UTF-8 text");
 std::wstring result(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),&result[0],n);return result;
}
static bool numericCodepoint(unsigned cp) {
 static const unsigned ranges[][2]={
#include "UnicodeNumbers.inc"
 };
 size_t lo=0,hi=sizeof(ranges)/sizeof(ranges[0]);
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(cp<ranges[mid][0])hi=mid;else if(cp>ranges[mid][1])lo=mid+1;else return true;}return false;
}
static bool whitespaceCodepoint(unsigned cp) {
 return (cp>=9&&cp<=13)||cp==32||cp==0x85||cp==0xA0||cp==0x1680||(cp>=0x2000&&cp<=0x200A)||cp==0x2028||cp==0x2029||cp==0x202F||cp==0x205F||cp==0x3000;
}
std::vector<TextSymbol> textSymbols(const std::string& text) {
 wideText(text); // Validate before walking UTF-8 byte boundaries.
 std::vector<TextSymbol> out;
 for(size_t i=0;i<text.size();){unsigned char first=(unsigned char)text[i];size_t count=first<128?1:first<224?2:first<240?3:4;unsigned cp=first& (count==1?127:count==2?31:count==3?15:7);
  for(size_t j=1;j<count;++j)cp=(cp<<6)|((unsigned char)text[i+j]&63);
  if(!whitespaceCodepoint(cp))out.push_back({text.substr(i,count),numericCodepoint(cp)});i+=count;
 }return out;
}
std::string characters(const std::string& text){std::string out;for(const auto& s:textSymbols(text))if(!s.number)out+=s.value;return out;}
std::string numbers(const std::string& text){std::string out;for(const auto& s:textSymbols(text))if(s.number)out+=s.value;return out;}
std::string compactText(const std::string& text){std::string out;for(const auto& s:textSymbols(text))out+=s.value;return out;}
size_t textLength(const std::string& text){return textSymbols(text).size();}
std::string jsonText(const std::string& text){wideText(text);std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=(char)c;}return out+'"';}
std::string csvText(const std::string& text){wideText(text);std::string out="\"";for(char c:text){if(c=='"')out+='"';out+=c;}return out+'"';}
}
