#pragma once
#include <string>
#include <vector>
namespace car {
struct TextSymbol { std::string value; bool number; };
std::wstring wideText(const std::string& text);
std::vector<TextSymbol> textSymbols(const std::string& text);
std::string characters(const std::string& text);
std::string numbers(const std::string& text);
std::string compactText(const std::string& text);
size_t textLength(const std::string& text);
std::string jsonText(const std::string& text);
std::string csvText(const std::string& text);
}
