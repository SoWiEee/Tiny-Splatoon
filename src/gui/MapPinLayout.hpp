#pragma once
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <cmath>
#include <algorithm>
#include <vector>
namespace restoration {
// Source hud.js: six symmetric relaxation passes, including deterministic coincident pins.
inline std::vector<glm::dvec2> SpreadMapPins(const std::vector<glm::dvec2>& anchors,double minimum) {
    auto points=anchors;
    for(int pass=0;pass<6;++pass)for(std::size_t i=0;i<points.size();++i)for(std::size_t j=i+1;j<points.size();++j){
        auto delta=points[j]-points[i];double distance=glm::length(delta);
        if(distance>=minimum)continue;
        if(distance<.5){const double angle=i*2.1+j*1.3;delta={std::cos(angle),std::sin(angle)};distance=1;}
        const auto push=delta*((minimum-distance)/2/distance);points[i]-=push;points[j]+=push;
    }
    return points;
}
inline glm::dvec2 MapArcControl(glm::dvec2 from,glm::dvec2 to){
    auto delta=to-from;const double length=std::max(1.,glm::length(delta));glm::dvec2 normal(-delta.y/length,delta.x/length);
    if(normal.y>0||(normal.y==0&&normal.x<0))normal=-normal;
    const double lift=std::min(170.,length*.4+30);return (from+to)*.5+normal*lift-glm::dvec2(0,lift*.25);
}
inline glm::dvec2 MapArcPoint(glm::dvec2 from,glm::dvec2 control,glm::dvec2 to,double t){return (1-t)*(1-t)*from+2*t*(1-t)*control+t*t*to;}
}
