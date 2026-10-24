#pragma once
#include "../game/LobbyLayout.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace restoration {
struct LobbyView { glm::dmat4 view{1},projection{1}; glm::dvec3 eye{0},target{0}; double fov=0; glm::dvec2 lensShift{0}; };

// showcase.js _lobFrame, fitted to the native room sidebar's free rectangle.
inline LobbyView FitLobbyView(const LobbyLayout& layout,bool hub,int width,int height) {
    const auto& camera=hub?layout.hubCamera:layout.lobbyCamera;
    LobbyView result;result.eye=camera.position;result.target=camera.target;
    result.view=glm::lookAt(camera.position,camera.target,glm::dvec3(0,1,0));
    const double w=std::max(1,width),h=std::max(1,height);
    const auto base=glm::perspective(glm::radians(camera.fov),w/h,camera.nearPlane,camera.farPlane)*result.view;
    std::array<double,4> bounds{1e9,-1e9,1e9,-1e9};
    const auto add=[&](glm::dvec3 point){
        const auto clip=base*glm::dvec4(point,1);const auto ndc=glm::dvec3(clip)/clip.w;
        const double x=(ndc.x+1)*w*.5,y=(1-ndc.y)*h*.5;
        bounds[0]=std::min(bounds[0],x);bounds[1]=std::max(bounds[1],x);
        bounds[2]=std::min(bounds[2],y);bounds[3]=std::max(bounds[3],y);
    };
    const double u=std::min(w*.01,h*.017778);
    const double sidebar=width<=1100?std::max(w*.36,340.0):std::max(w*.34,390.0);
    const double left=sidebar+1.2*u,right=w-2.4*u,top=.1*h,bottom=.91*h;
    double zoom,cx=(left+right)*.5,cy;
    if(hub){
        const auto p=layout.hubSpot.position;
        add(p+glm::dvec3(-.42,0,0));add(p+glm::dvec3(.42,0,0));
        add(p+glm::dvec3(0,1.62,0));add(p+glm::dvec3(0,-.05,.35));
        const double bh=bounds[3]-bounds[2],bw=bounds[1]-bounds[0];
        zoom=std::min(std::min(.5*h,.88*(bottom-top))/std::max(1.0,bh),.8*(right-left)/std::max(1.0,bw));
        cy=std::min((top+bottom)*.5+.03*h,bottom-.5*bh*zoom-.01*h);
    }else{
        for(const auto& spot:layout.lineupSpots){
            add(spot.position+glm::dvec3(-.42,-.05,.2));
            add(spot.position+glm::dvec3(.42,-.05,.2));
            add(spot.position+glm::dvec3(0,1.64,0));
        }
        const double nameTop=top+4.2*u;
        zoom=std::min((right-left)/std::max(1.0,bounds[1]-bounds[0]),(bottom-nameTop)/std::max(1.0,bounds[3]-bounds[2]));
        cy=(nameTop+bottom)*.5;
    }
    zoom=std::clamp(zoom,.55,2.6);
    const double fov=2*std::atan(std::tan(glm::radians(camera.fov)*.5)/zoom);
    result.fov=fov;result.projection=glm::perspective(fov,w/h,camera.nearPlane,camera.farPlane);
    const double sx=cx-(w*.5+((bounds[0]+bounds[1])*.5-w*.5)*zoom);
    const double sy=cy-(h*.5+((bounds[2]+bounds[3])*.5-h*.5)*zoom);
    result.lensShift={sx,sy};
    result.projection[2][0]-=2*sx/w;result.projection[2][1]+=2*sy/h;
    return result;
}
// Source 1.7-second hub/room glide, lens easing, countdown lean and launch tilt.
class LobbyCameraMotion {
public:
    LobbyView Update(const LobbyLayout& layout,bool hub,int width,int height,double dt,double time,
                     bool launching,int countdown,bool reduced){
        if(!initialized_){hub_=hub;initialized_=true;}
        if(hub_!=hub){previousHub_=hub_;hub_=hub;glide_=0;}
        glide_+=dt;
        if(launching&&!wasLaunching_)countdownAt_=time;
        if(!launching){countdownAt_=-1;goAt_=-1;}
        if(launching&&countdown==0&&goAt_<0)goAt_=time;
        wasLaunching_=launching;
        auto shot=FitLobbyView(layout,hub,width,height);
        const bool gliding=!reduced&&glide_<1.7;
        if(gliding){
            const auto from=FitLobbyView(layout,previousHub_,width,height);const double k=ease(glide_/1.7);
            shot.eye=glm::mix(from.eye,shot.eye,k);shot.target=glm::mix(from.target,shot.target,k);
            shot.fov=glm::mix(from.fov,shot.fov,k);shot.lensShift=glm::mix(from.lensShift,shot.lensShift,k);
        }
        const double k=gliding||reduced||!hasFit_?1:1-std::exp(-7*dt);
        fov_+=(shot.fov-fov_)*k;shift_+=(shot.lensShift-shift_)*k;hasFit_=true;
        shot.fov=fov_;shot.lensShift=shift_;
        if(!reduced){
            shot.target+=glm::dvec3(.05*std::sin(time*.21)+.015*std::sin(time*.53+1),.028*std::sin(time*.17+2),.03*std::sin(time*.13+.4));
            shot.fov*=1+.006*std::sin(time*.19+2.1);
            if(!hub){
                if(countdownAt_>=0)shot.fov*=1-.05*ease((time-countdownAt_)/3);
                if(goAt_>=0){const double lean=ease((time-goAt_-.2)/1.1);shot.target.y+=1.4*lean;shot.fov*=1-.03*lean;}
            }
        }
        const auto& camera=hub?layout.hubCamera:layout.lobbyCamera;
        shot.view=glm::lookAt(shot.eye,shot.target,glm::dvec3(0,1,0));
        shot.projection=glm::perspective(shot.fov,double(width)/height,camera.nearPlane,camera.farPlane);
        shot.projection[2][0]-=2*shot.lensShift.x/width;shot.projection[2][1]+=2*shot.lensShift.y/height;
        return shot;
    }
private:
    static double ease(double value){const double t=std::clamp(value,0.,1.);return t*t*t*(t*(t*6-15)+10);}
    bool initialized_=false,hub_=true,previousHub_=true,wasLaunching_=false,hasFit_=false;
    double glide_=9,fov_=0,countdownAt_=-1,goAt_=-1;
    glm::dvec2 shift_{0};
};

}
