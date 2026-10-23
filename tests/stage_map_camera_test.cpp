#include "../src/gui/MapPinLayout.hpp"
#include "../src/render/StageMapView.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void boundsFit(glm::dvec4 bounds,double aspect,bool flipForBlueTeam=false){
    const auto camera=restoration::FitStageMapCamera(bounds,aspect,flipForBlueTeam);
    const auto vp=camera.projection*camera.view;
    constexpr double yMin=-.72,yMax=.74;
    for(const double y:{-1.2,6.0})for(const double x:{bounds.x,bounds.y})for(const double z:{bounds.z,bounds.w}){
        const glm::dvec4 clip=vp*glm::dvec4(x,y,z,1.0);
        check(clip.w>0.0,"A stage corner fell behind the diorama camera");
        const double nx=clip.x/clip.w,ny=clip.y/clip.w;
        check(nx>=-.921&&nx<=.921,"A stage corner escaped the source camera's horizontal fit");
        check(ny>=yMin-.001&&ny<=yMax+.001,"A stage corner escaped the source camera's vertical fit");
    }
}
}

int main(){
    try{
        const auto spread=restoration::SpreadMapPins({{100,100},{100,100},{110,105}},84);
        check(spread.size()==3&&glm::length(spread[0]-spread[1])>60,"Coincident map pins must separate deterministically");
        check(glm::length(restoration::MapArcPoint({0,0},{10,-10},{20,0},0)-glm::dvec2(0))<1e-9&&glm::length(restoration::MapArcPoint({0,0},{10,-10},{20,0},1)-glm::dvec2(20,0))<1e-9,"Map arc endpoints must stay attached to the world anchors");
        const glm::dvec4 arena{-42,42,-64,64};
        // The source full-screen camera keeps the same fit at 720p and 1080p.
        boundsFit(arena,1280.0/720.0);
        boundsFit(arena,1920.0/1080.0);
        boundsFit(arena,256.0/384.0);
        boundsFit({-25,57,-88,42},1280.0/720.0);
        boundsFit(arena,1920.0/1080.0,true);
        boundsFit({-25,57,-88,42},1280.0/720.0,true);
        const auto orange=restoration::FitStageMapCamera(arena,16.0/9.0,false);
        const auto blue=restoration::FitStageMapCamera(arena,16.0/9.0,true);
        check(std::abs(orange.eye.z-orange.target.z+blue.eye.z-blue.target.z)<1e-6,
              "The blue-team diorama did not mirror the camera toward the opposite stage end");
        check(std::abs(orange.zShift+blue.zShift)<1e-6,
              "The blue-team diorama did not invert the fitted source z shift");
        std::cout<<"STAGE_MAP_CAMERA projected arena corners within diorama fit at 720p/1080p/portrait and mirrored team orientation\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
