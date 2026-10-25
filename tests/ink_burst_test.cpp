#include "WorldFxRecipes.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace restoration;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::vector<WorldFxParticle> build(const char* name,std::uint32_t seed=37){WorldFxRecipeContext c;c.speed=16;c.factor=1;return BuildWorldFxRecipe(name,{0,1,0},{0,1,0},{1,.2,.01},1,seed,-100,{0,2,4},{},c,{0,.1,1});}
int count(const std::vector<WorldFxParticle>& p,int flag){return int(std::count_if(p.begin(),p.end(),[&](auto x){return (int(x.flags)&flag)!=0;}));}
}
int main(){try{
    for(const char* name:{"inkSlosh","inkFlick","inkExplosion","inkImpact"}){
        auto particles=build(name),repeat=build(name);check(particles.size()==repeat.size(),"Seeded burst counts changed");
        check(count(particles,InkBurstHero)>=3&&count(particles,InkBurstHero)<=12,"Burst hero count must stay in 3..12");
        check(count(particles,InkBurstMid)>=10&&count(particles,InkBurstMid)<=40,"Burst mid-drop count outside budget");
        check(count(particles,InkBurstMicro)>=24&&count(particles,InkBurstMicro)<=96,"Burst micro-spray count outside budget");
        float minRadius=10,maxRadius=0,minSpeed=100,maxSpeed=0;
        for(std::size_t i=0;i<particles.size();i++){
            const auto& p=particles[i];const auto& r=repeat[i];
            check(p.pos==r.pos&&p.vel==r.vel&&p.radius==r.radius&&p.life==r.life&&p.seed==r.seed,"Burst is not replayable");
            check(std::isfinite(p.radius)&&p.radius>0&&std::isfinite(p.vel.x)&&std::isfinite(p.vel.y)&&std::isfinite(p.vel.z),"Burst emitted invalid data");
            if(int(p.flags)&InkBurstHero){minRadius=std::min(minRadius,p.radius);maxRadius=std::max(maxRadius,p.radius);const auto speed=glm::length(p.vel);minSpeed=std::min(minSpeed,speed);maxSpeed=std::max(maxSpeed,speed);}
            if(p.shape==WorldFxShape::Drop)check((int(p.flags)&128)!=0&&(int(p.flags)&1)==0,"Burst drops must never repaint or deal damage");
            if(int(p.flags)&InkBurstMicro)check(p.shape==WorldFxShape::Puff&&p.radius<.06f&&p.life<.25f,"Micro spray must remain small, short-lived billboards");
        }
        check(maxRadius>minRadius*1.02f&&maxSpeed>minSpeed*1.02f,"Hero lobes must have distinct scales and timing");
        int specks=0,ripples=0;std::uint32_t rng=37;
        const WorldFxCollider hit=[](glm::vec3,glm::vec3 next,WorldFxHit& out){out={next,{0,1,0}};return true;};
        UpdateWorldFxRecipe(particles,.016,rng,hit,-100,false,true,[&](const auto&,auto,float){++specks;},[&](auto,double,double,double,double){++ripples;});
        check(specks==0&&ripples==0,"Cosmetic landing changed paint/ripple callbacks");
        for(int i=0;i<80;i++)UpdateWorldFxRecipe(particles,1.0/60,rng,{},-100,false,false);
        check(particles.empty(),"Burst residue never expired");
    }
    auto slosh=build("inkSlosh"),flick=build("inkFlick");
    const auto spread=[](const auto& p,int axis){float lo=100,hi=-100;for(const auto& x:p)if(int(x.flags)&InkBurstHero){lo=std::min(lo,x.vel[axis]);hi=std::max(hi,x.vel[axis]);}return hi-lo;};
    check(spread(slosh,0)>spread(slosh,1)*1.5f&&spread(flick,0)>spread(slosh,0),"Slosh/flick must have a directional horizontal fan");
    auto anticipation=build("inkAnticipation");check(anticipation.size()==3&&anticipation.front().life<=.11f,"Anticipation must be a short muzzle cue");
    std::cout<<"INK_BURST hero/mid/micro, direction, replay, visual-only landing and expiry passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
