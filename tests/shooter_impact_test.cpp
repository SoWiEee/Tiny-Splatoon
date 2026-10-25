#include "WorldFxRecipes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace restoration;

namespace {
constexpr int VisualOnly=128, Matte=16;

void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

int count(const std::vector<WorldFxParticle>& items,WorldFxShape shape){
    return static_cast<int>(std::count_if(items.begin(),items.end(),[&](const auto& item){return item.shape==shape;}));
}

bool same(glm::vec3 a,glm::vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}

bool same(const WorldFxParticle& a,const WorldFxParticle& b){
    return a.shape==b.shape&&same(a.pos,b.pos)&&same(a.vel,b.vel)&&same(a.normal,b.normal)&&same(a.color,b.color)
        &&a.radius==b.radius&&a.life==b.life&&a.gravity==b.gravity&&a.stretch==b.stretch&&a.flags==b.flags
        &&a.radius1==b.radius1&&a.expand==b.expand&&a.alpha==b.alpha&&a.kind==b.kind&&a.seed==b.seed
        &&a.wobble==b.wobble&&a.crown==b.crown;
}

void finite(const WorldFxParticle& p){
    const std::array<float,34> values={
        p.pos.x,p.pos.y,p.pos.z,p.vel.x,p.vel.y,p.vel.z,p.normal.x,p.normal.y,p.normal.z,p.color.x,p.color.y,p.color.z,
        p.radius,p.length,p.age,p.life,p.alpha,p.style,p.seed,p.radius1,p.gravity,p.stretch,p.flags,p.drag,p.buoyancy,
        p.kind,p.fadeIn,p.fadeOut,p.rotation,p.rotationVelocity,p.wobble,p.expand,p.crown,p.renderRadius};
    check(std::all_of(values.begin(),values.end(),[](float value){return std::isfinite(value);}),"Impact particle contains a non-finite value");
    check(std::isfinite(p.renderLength)&&std::isfinite(p.renderAlpha)&&std::isfinite(p.sourceGroundY)
          &&std::isfinite(p.renderScale.x)&&std::isfinite(p.renderScale.y)&&std::isfinite(p.renderScale.z),
          "Impact render attributes contain a non-finite value");
}

std::vector<WorldFxParticle> impact(double radius,std::uint32_t seed){
    const glm::dvec3 pos(2.3,-1.2,4.7),normal=glm::normalize(glm::dvec3(0.37,0.81,-0.45));
    const glm::dvec3 color(1.0,0.2541520943200296,0.0069954101845983935);
    return BuildWorldFxRecipe("shooterImpact",pos,normal,color,radius,seed);
}

const WorldFxParticle& shell(const std::vector<WorldFxParticle>& items){
    const auto found=std::find_if(items.begin(),items.end(),[](const auto& item){return item.shape==WorldFxShape::Shell;});
    check(found!=items.end(),"Impact crown shell missing");
    return *found;
}

void checkLegacyBurst(){
    const glm::dvec3 color(1.0,0.2541520943200296,0.0069954101845983935);
    std::uint32_t rng=0x6d2b79f5u;
    auto items=BuildWorldFxRecipe("burst",glm::dvec3(0),glm::dvec3(0,1,0),color,1.0,rng);
    check(count(items,WorldFxShape::Drop)==15&&count(items,WorldFxShape::Shell)==1,
          "Legacy burst golden pool counts changed");
    const auto first=std::find_if(items.begin(),items.end(),[](const auto& item){return item.shape==WorldFxShape::Drop;});
    check(first!=items.end()&&std::abs(first->vel.x-(-1.03568804f))<1e-6f
          &&std::abs(first->vel.y-3.10870290f)<1e-6f&&std::abs(first->vel.z-(-2.20724773f))<1e-6f,
          "Legacy burst golden projectile-independent velocity changed");

    int specks=0,ripples=0;
    const WorldFxCollider collide=[](glm::vec3,glm::vec3 next,WorldFxHit& hit){hit={next,{0,1,0}};return true;};
    const WorldFxSpeckSink speck=[&](const WorldFxHit&,glm::vec3,float){++specks;};
    const WorldFxRippleSink ripple=[&](glm::dvec3,double,double,double,double){++ripples;};
    UpdateWorldFxRecipe(items,1.0/60.0,rng,collide,-100.0f,false,true,speck,ripple);
    check(specks>0&&ripples>0,"Legacy burst collision callbacks changed");
}
}

int main(){
    try{
        constexpr std::uint32_t seed=0x6d2b79f5u;
        const auto a=impact(0.24,seed),b=impact(0.24,seed);
        check(a.size()>=5&&a.size()<=8&&a.size()==b.size(),"Impact particle count is outside the bounded recipe");
        for(std::size_t i=0;i<a.size();++i)check(same(a[i],b[i]),"Impact recipe is not deterministic for a fixed seed");
        check(count(a,WorldFxShape::Drop)>=4&&count(a,WorldFxShape::Drop)<=7&&count(a,WorldFxShape::Shell)==1,
              "Impact should contain four to seven drops and one crown shell");

        const glm::dvec3 normal=glm::normalize(glm::dvec3(0.37,0.81,-0.45));
        const float infinity=std::numeric_limits<float>::infinity();
        std::array<float,3> minimum={infinity,infinity,infinity},maximum={-infinity,-infinity,-infinity};
        for(const auto& item:a){
            finite(item);
            if(item.shape==WorldFxShape::Drop){
                const glm::dvec3 velocity(item.vel);
                check(glm::dot(velocity,normal)>0.0,"Impact drop did not eject away from the surface");
                check(item.life>=0.10f&&item.life<=0.22f,"Impact drop lifetime is outside the short splash window");
                check((static_cast<int>(item.flags)&VisualOnly)!=0&&(static_cast<int>(item.flags)&Matte)==0,
                      "Impact drops must be visual-only and retain the glossy material flag");
                for(int axis=0;axis<3;++axis){minimum[axis]=std::min(minimum[axis],item.vel[axis]);maximum[axis]=std::max(maximum[axis],item.vel[axis]);}
            }
        }
        for(int axis=0;axis<3;++axis)check(maximum[axis]-minimum[axis]>0.05f,"Impact drops lack variation in 3D");
        const auto& crown=shell(a);
        check(crown.life>=0.10f&&crown.life<=0.22f&&crown.crown>0.0f
              &&glm::dot(glm::dvec3(crown.normal),normal)>0.999,
              "Impact crown shell must be short-lived and aligned to the hit surface");

        check(std::abs(shell(impact(0.01,seed)).radius1-0.12f*1.05f)<1e-6f
              &&std::abs(shell(impact(0.24,seed)).radius1-0.24f*1.05f)<1e-6f
              &&std::abs(shell(impact(1.0,seed)).radius1-0.32f*1.05f)<1e-6f,
              "Projectile hit radius did not scale the impact within the 0.12–0.32 clamp");

        auto expiring=impact(0.24,seed);std::uint32_t updateRng=seed;
        for(int i=0;i<5;++i)UpdateWorldFxRecipe(expiring,0.05,updateRng);
        check(expiring.empty(),"Impact particles did not expire within the bounded lifetime");

        auto landing=impact(0.24,seed);const int landingDrops=count(landing,WorldFxShape::Drop);int collisions=0,specks=0,ripples=0;updateRng=seed;
        const WorldFxCollider collide=[&](glm::vec3,glm::vec3 next,WorldFxHit& hit){++collisions;hit={next,{0,1,0}};return true;};
        const WorldFxSpeckSink speck=[&](const WorldFxHit&,glm::vec3,float){++specks;};
        const WorldFxRippleSink ripple=[&](glm::dvec3,double,double,double,double){++ripples;};
        UpdateWorldFxRecipe(landing,1.0/60.0,updateRng,collide,-100.0f,false,true,speck,ripple);
        check(collisions==landingDrops,"Impact drops did not pass through the collider");
        check(specks==0&&ripples==0&&count(landing,WorldFxShape::Drop)==0&&count(landing,WorldFxShape::Ring)==0,
              "Impact landing created secondary cosmetic callbacks or particles");

        WorldFxParticle probe;probe.shape=WorldFxShape::Drop;probe.radius=0.1f;probe.life=1.0f;probe.flags=VisualOnly;
        std::vector<WorldFxParticle> bounceProbe{probe};updateRng=1;
        UpdateWorldFxRecipe(bounceProbe,1.0/60.0,updateRng,collide,-100.0f,false,true,speck,ripple);
        check(count(bounceProbe,WorldFxShape::Drop)==0,"Visual-only collision generated a secondary bounce particle");

        checkLegacyBurst();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
