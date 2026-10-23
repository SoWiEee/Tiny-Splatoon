#include "StageMapView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace restoration {
namespace {
constexpr double pitch=1.1;
constexpr double fov=30.0;
constexpr double nearPlane=1.0;
constexpr double farPlane=6000.0;
constexpr double xLimit=.92;
constexpr double yMin=-.72;
constexpr double yMax=.74;
constexpr double yCenter=(yMin+yMax)*.5;

std::array<glm::dvec3,8> corners(glm::dvec4 bounds) {
    std::array<glm::dvec3,8> result{};
    std::size_t i=0;
    for(const double y:{-1.2,6.0})
        for(const double x:{bounds.x,bounds.y})
            for(const double z:{bounds.z,bounds.w}) result[i++]={x,y,z};
    return result;
}

StageMapCamera makeCamera(glm::dvec4 bounds,double aspect,double distance,double zShift,bool flipForBlueTeam) {
    const glm::dvec3 target((bounds.x+bounds.y)*.5,0,(bounds.z+bounds.w)*.5+zShift);
    const double yaw=flipForBlueTeam?glm::pi<double>():0.0;
    const double horizontal=std::cos(pitch)*distance;
    const glm::dvec3 eye=target+glm::dvec3(-std::sin(yaw)*horizontal,std::sin(pitch)*distance,-std::cos(yaw)*horizontal);
    StageMapCamera camera;
    camera.eye=eye;camera.target=target;camera.distance=distance;camera.zShift=zShift;
    camera.view=glm::lookAt(eye,target,glm::dvec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(fov),aspect,nearPlane,farPlane);
    return camera;
}

std::array<double,4> projectedBounds(const StageMapCamera& camera,const std::array<glm::dvec3,8>& points) {
    double minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;
    const auto vp=camera.projection*camera.view;
    for(const auto& point:points) {
        const glm::dvec4 clip=vp*glm::dvec4(point,1.0);
        if(clip.w<=0.0)return {-1e9,1e9,-1e9,1e9};
        const double x=clip.x/clip.w,y=clip.y/clip.w;
        minX=std::min(minX,x);maxX=std::max(maxX,x);
        minY=std::min(minY,y);maxY=std::max(maxY,y);
    }
    return {minX,maxX,minY,maxY};
}

bool fits(const StageMapCamera& camera,const std::array<glm::dvec3,8>& points) {
    const auto box=projectedBounds(camera,points);
    return box[0]>=-xLimit&&box[1]<=xLimit&&box[2]>=yMin&&box[3]<=yMax;
}
}

StageMapCamera FitStageMapCamera(glm::dvec4 bounds,double aspect,bool flipForBlueTeam) {
    if(!std::isfinite(bounds.x)||!std::isfinite(bounds.y)||!std::isfinite(bounds.z)||!std::isfinite(bounds.w)||
       bounds.y<=bounds.x||bounds.w<=bounds.z||!std::isfinite(aspect)||aspect<=0.0)
        throw std::invalid_argument("Invalid diorama bounds or aspect ratio");

    const auto points=corners(bounds);
    double distance=150.0,zShift=0.0;
    for(int pass=0;pass<4;++pass) {
        double low=20.0,high=1500.0;
        for(int step=0;step<28;++step) {
            const double candidate=(low+high)*.5;
            // The source fit is always solved from yaw zero. Team 1 mirrors
            // that fitted pose afterward; feeding yaw PI into this correction
            // changes the sign of the vertical response and diverges.
            if(fits(makeCamera(bounds,aspect,candidate,zShift,false),points))high=candidate;
            else low=candidate;
        }
        distance=high;
        const auto box=projectedBounds(makeCamera(bounds,aspect,distance,zShift,false),points);
        const double offset=yCenter-(box[2]+box[3])*.5;
        zShift-=offset*distance*std::tan(glm::radians(fov)*.5)/std::sin(pitch);
    }
    // cameraRig.js::_diorama mirrors Bravo's view by rotating 180 degrees and
    // negating the fitted z offset; keeping those operations paired preserves
    // the original fit window at the opposite end of the stage.
    return makeCamera(bounds,aspect,distance,flipForBlueTeam?-zShift:zShift,flipForBlueTeam);
}
} // namespace restoration
