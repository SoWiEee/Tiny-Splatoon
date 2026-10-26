#pragma once
#include <string>
namespace restoration {
void LimitFrame(double started,int cap);
void Capture(const std::string& path,int width,int height);
}
