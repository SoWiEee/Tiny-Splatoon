#include "TeamPalette.hpp"
#include <stdexcept>
#include <iostream>
using namespace restoration;
int main(){
    SetInkPalette({"#ffd21a","#2a52ff"});
    if(InkColors[0].r<.99||InkColors[0].g<.6||InkColors[0].b>.02||InkColors[1].b<.99)
        throw std::runtime_error("Colorblind palette must retain yellow and blue pigments");
    const auto previous=InkColors;
    SetInkPalette({"#zzzzzz","bad"});
    if(InkColors!=previous)throw std::runtime_error("Invalid color must not replace valid ink");
    SetInkPalette({"#ff8a14","#2f5bff"});
    for(int i=0;i<2;++i)if(glm::length(InkColors[i]-SourceInkColors[i])>1e-9)
        throw std::runtime_error("Default palette must match source linear colors");
    std::cout<<"Native palette conversion and invalid input passed\n";
}
