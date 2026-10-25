#define GLM_ENABLE_EXPERIMENTAL
#include "ProjectileVisuals.hpp"
#include "TeamPalette.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace restoration {
std::vector<ProjectileBlob> BuildProjectileBlobs(const std::vector<ShooterProjectile>& shots) {
    std::vector<ProjectileBlob> result;
    BuildProjectileBlobs(shots,result);
    return result;
}
void BuildProjectileBlobs(const std::vector<ShooterProjectile>& shots,std::vector<ProjectileBlob>& result) {
    constexpr double pi=3.14159265358979323846;
    constexpr double satelliteSize[]={0.46,0.33,0.24,0.17};
    const auto& colors=InkColors;
    result.clear();
    result.reserve(std::min<std::size_t>(700,shots.size()*4));
    for(const auto& p:shots) {
        if(result.size()>=695)break;
        if(!p.owner||p.delay>0||p.kind=="arrow"||p.kind=="bladewave"||p.kind=="fist")continue;
        const double speed=glm::length(p.vel);
        const auto direction=p.vel/std::max(speed,1e-3);
        const auto rotation=glm::rotation(glm::dvec3(0,0,1),speed>1e-8?direction:glm::dvec3(0,0,1));
        const double grow=std::min(1.0,p.age*20);
        const double baseSize=p.look.size>0?p.look.size:p.size;
        double size=baseSize*grow*(1+0.3*std::sin(grow*pi));
        double tail=p.look.tail0+std::min(p.look.tailK,speed*0.04)*grow;
        double wobble=p.look.wobble,bright=1;
        if(p.kind=="blast") {
            double k=std::clamp((p.age/p.life-0.8)/0.2,0.0,1.0);k=k*k*(3-2*k);
            size*=1+0.34*k;wobble*=1+2.4*k;bright=1+0.9*k;tail*=1-0.55*k;
        }
        const auto color=colors[p.owner->team()==1?1:0];
        const auto add=[&](glm::dvec3 at,double radius,glm::dvec4 shape,glm::dvec3 tint) {
            const auto matrix=glm::translate(glm::dmat4(1),at)*glm::mat4_cast(rotation)*glm::scale(glm::dmat4(1),glm::dvec3(radius));
            result.push_back({glm::mat4(matrix),glm::vec4(shape),glm::vec3(tint)});
        };
        add(p.pos,size,{tail,wobble,p.seed*40+p.age*p.look.frequency,p.look.nose},color*bright);
        const int satellites=std::min(p.look.satellites,4);
        if(!satellites||speed<4)continue;
        const double travel=glm::length(p.start-p.pos);
        const glm::dvec3 side=std::abs(direction.y)<0.95?glm::normalize(glm::dvec3(-direction.z,0,direction.x)):glm::dvec3(1,0,0);
        const auto up=glm::cross(direction,side);
        const double speedK=0.55+0.45*std::min(1.0,speed/25);
        const double fade=1-0.45*std::min(1.0,p.age/p.life);
        for(int i=0;i<satellites&&result.size()<700;i++) {
            const double back=baseSize*(tail+1.15+i*1.8)*speedK;
            if(travel<back+baseSize*1.6)break;
            const double phase=p.seed*31+i*2.4+p.age*11,lat=baseSize*(0.16+i*0.16);
            const auto at=p.pos-direction*back+side*(std::sin(phase)*lat)+up*(std::cos(phase*1.3)*lat);
            const double radius=baseSize*satelliteSize[i]*fade*(1+0.14*std::sin(phase*2.1));
            add(at,radius,{1.3+0.25*speedK,0.05,phase*3,0},color);
        }
    }
}
}
