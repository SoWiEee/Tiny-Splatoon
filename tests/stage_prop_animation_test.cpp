#include "StagePropAnimation.hpp"
#include <iostream>
#include <stdexcept>
using namespace restoration;
int main(){
    try{
        const auto close=[](double a,double b){if(!std::isfinite(a)||std::abs(a-b)>2e-6)throw std::runtime_error("Source prop animation mismatch");};
        StagePropAnimation p;p.kind="y";p.speed=2;p.phase=.3;p.base=glm::translate(glm::dmat4(1),glm::dvec3(4,2,-3));
        for(const double t:{0.,.5,17.}){const auto m=p.matrix(t);close(m[0][0],std::cos(t*2+.3));close(m[0][2],-std::sin(t*2+.3));close(m[3][0],4);close(m[3][2],-3);}
        p.kind="z";const auto z=p.matrix(.5);close(z[0][1],std::sin(1.3));
        p.kind="blink";p.rate=1;p.phase=0;p.lo=.2;p.hi=2;close(p.brightness(.25),2);close(p.brightness(.75),.2);close(p.brightness(0),.2+1.8*(.5/.6*.25));
        p.kind="cloth";glm::vec3 position(.4f,.6f,0),normal(0,0,1),original=position;
        p.cloth(1,0,{2,3,.2f},position,normal);if(position!=original)throw std::runtime_error("Pinned cloth moved");
        p.cloth(1,1,{2,3,.2f},position,normal);if(position==original)throw std::runtime_error("Free cloth stayed still");close(glm::length(normal),1);
        std::cout<<"STAGE_PROP rotations/blink/pinned-and-free-cloth passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
