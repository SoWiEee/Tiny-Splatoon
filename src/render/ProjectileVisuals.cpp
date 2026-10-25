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
#include <limits>

namespace restoration {
std::vector<ProjectileBlob> BuildProjectileBlobs(const std::vector<ShooterProjectile>& shots) {
    std::vector<ProjectileBlob> result;
    BuildProjectileBlobs(shots,result);
    return result;
}
void BuildProjectileBlobs(const std::vector<ShooterProjectile>& shots,std::vector<ProjectileBlob>& result,bool includeShooter) {
    constexpr double pi=3.14159265358979323846;
    constexpr double satelliteSize[]={0.46,0.33,0.24,0.17};
    const auto& colors=InkColors;
    result.clear();
    result.reserve(std::min<std::size_t>(700,shots.size()*4));
    for(const auto& p:shots) {
        if(result.size()>=695)break;
        if(!includeShooter&&p.kind=="shot"&&(p.weaponId=="shooter"||p.weaponId=="spinner"))continue;
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

void AppendShooterInkBlobs(const std::vector<ShooterProjectile>& shots,std::vector<ProjectileBlob>& output) {
    constexpr double pi=3.14159265358979323846;
    constexpr double twoPi=2*pi;
    const auto smooth=[](double a,double b,double x) {
        const double t=std::clamp((x-a)/(b-a),0.0,1.0);
        return t*t*(3-2*t);
    };
    const auto finiteVec=[](glm::dvec3 v) {
        constexpr double limit=std::numeric_limits<float>::max();
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
            std::abs(v.x)<=limit&&std::abs(v.y)<=limit&&std::abs(v.z)<=limit;
    };
    if(output.size()>=700)return;
    if(output.capacity()<700)output.reserve(700);
    for(const auto& p:shots) {
        if(output.size()>=700)break;
        if(p.kind!="shot"||p.weaponId!="shooter"||!p.owner||!std::isfinite(p.delay)||p.delay>0||
           !finiteVec(p.pos)||!finiteVec(p.start)||!finiteVec(p.vel)||!std::isfinite(p.age)||
           !std::isfinite(p.life)||p.life<=1e-6||!std::isfinite(p.seed))continue;
        const double base=p.look.size>0?p.look.size:p.size;
        if(!std::isfinite(base)||base<=0)continue;
        const double radius=std::clamp(base,0.04,0.32),age=std::clamp(p.age,0.0,p.life),life=age/p.life;
        const double seed=std::fmod(std::abs(p.seed),1.0),phase=seed*twoPi;
        const auto travelVector=p.pos-p.start;
        const double distance=glm::length(travelVector),travel=std::min(distance,1000.0),speed=glm::length(p.vel);
        const glm::dvec3 direction=speed>1e-8?p.vel/speed:distance>1e-8?travelVector/distance:glm::dvec3(0,0,1);
        const auto rotation=glm::rotation(glm::dvec3(0,0,1),direction);
        const auto color=InkColors[p.owner->team()==1?1:0];
        if(!std::isfinite(color.x)||!std::isfinite(color.y)||!std::isfinite(color.z))continue;
        const auto add=[&](glm::dvec3 at,glm::dvec3 scale,glm::dvec4 shape,glm::dvec3 tint) {
            const auto matrix=glm::translate(glm::dmat4(1),at)*glm::mat4_cast(rotation)*glm::scale(glm::dmat4(1),scale);
            output.push_back({glm::mat4(matrix),glm::vec4(shape),glm::vec3(tint)});
        };
        const double launchKick=std::exp(-age*14.0),speedK=std::clamp(speed/28.0,0.0,1.0);
        const double growth=0.60+0.40*smooth(0.0,0.035,age);
        const double bodyRadius=radius*1.16*growth*(1+0.10*launchKick+0.035*std::sin(phase+age*11));
        const double blend=0.38+0.10*seed;
        const double bodyLength=std::clamp(1.42+0.48*speedK+0.24*launchKick+0.10*std::sin(phase+age*9),1.4,2.2);
        add(p.pos,{bodyRadius,bodyRadius*0.94,bodyRadius*bodyLength},{blend,launchKick,phase,life},color);

        const glm::dvec3 reference=std::abs(direction.y)<0.92?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const glm::dvec3 side=glm::normalize(glm::cross(direction,reference)),up=glm::normalize(glm::cross(side,direction));
        const int drops=static_cast<int>(seed*4);
        for(int i=0;i<drops&&output.size()<700;i++) {
            const double release=0.025+0.020*i,dropAge=age-release,dropLife=std::clamp(dropAge/p.life,0.0,1.0);
            if(dropAge<=0||travel<radius*(2.9+0.8*i))continue;
            const double fade=1-smooth(0.78,1.0,life),grow=smooth(0.0,0.035,dropAge);
            const double dropSeed=std::fmod(seed+(i+1)*0.38196601125,1.0);
            const double dropRadius=radius*(0.15+0.20*dropSeed)*grow*fade;
            if(dropRadius<=1e-5)continue;
            const double dropPhase=phase+i*2.399963229728653,back=std::min(travel*0.65,radius*(3.2+1.2*i)+speed*0.12*dropAge);
            const double spread=smooth(0.0,0.045,dropAge),lateral=(radius*(1.1+0.24*i)+speed*0.05*dropAge)*spread;
            const auto at=p.pos-direction*back+side*(std::cos(dropPhase)*lateral)+up*(std::sin(dropPhase)*lateral*0.8);
            add(at,{dropRadius,dropRadius*0.9,dropRadius*(1.0+0.12*dropSeed)},{0,0,dropPhase,dropLife},color);
        }
    }
}
}
