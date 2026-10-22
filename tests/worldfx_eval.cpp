#include "../src/render/WorldFxRecipes.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace restoration;
namespace {
void arr(const std::vector<float>& v){std::cout<<'[';for(std::size_t i=0;i<v.size();++i){if(i)std::cout<<',';std::cout<<v[i];}std::cout<<']';}
template<class F>std::vector<float> flatten(const std::vector<WorldFxParticle>& p,WorldFxShape shape,int stride,F get){std::vector<float> out;for(const auto& x:p)if(x.shape==shape){for(int i=0;i<stride;++i)out.push_back(get(x,i));}return out;}
void vec3(std::vector<float>& out,glm::vec3 v){out.insert(out.end(),{v.x,v.y,v.z});}
void pool(const std::vector<WorldFxParticle>& p,WorldFxShape shape,const char* name,bool render){
    const auto items=std::count_if(p.begin(),p.end(),[&](const auto& x){return x.shape==shape;});
    std::cout<<'"'<<name<<"\":{\"count\":"<<items;
    std::vector<float> pos,vel,color,normal,attrs,axis;
    for(const auto& x:p)if(x.shape==shape){
        vec3(pos,x.pos);vec3(vel,x.vel);vec3(color,x.color);
        if(shape==WorldFxShape::Drop){attrs.insert(attrs.end(),{x.radius,x.age,x.life,x.gravity,x.stretch,x.seed,0,x.flags});}
        else if(shape==WorldFxShape::Puff||shape==WorldFxShape::Glow){attrs.insert(attrs.end(),{x.radius,x.radius1,x.age,x.life,x.alpha,x.rotation,x.rotationVelocity,x.drag,x.buoyancy,x.kind,x.fadeIn,x.seed,x.wobble,x.fadeOut});}
        else if(shape==WorldFxShape::Ring){vec3(normal,x.normal);attrs.insert(attrs.end(),{x.radius,x.age,x.life,x.alpha,x.style,x.length,x.seed,x.expand});}
        else if(shape==WorldFxShape::Shell){attrs.insert(attrs.end(),{x.radius,x.radius1,x.age,x.life,x.expand,x.alpha,x.kind,x.seed,x.wobble});vec3(axis,x.normal);axis.push_back(x.crown);}
        else if(shape==WorldFxShape::Beam){attrs.insert(attrs.end(),{x.radius,x.length,x.age,x.life,x.seed});}
    }
    std::cout<<",\"pos\":";arr(pos);if(shape!=WorldFxShape::Beam){std::cout<<",\"vel\":";arr(vel);}if(shape==WorldFxShape::Ring){std::cout<<",\"normal\":";arr(normal);}else if(shape==WorldFxShape::Shell){std::cout<<",\"axis\":";arr(axis);}std::cout<<",\"color\":";arr(color);std::cout<<",\"attrs\":";arr(attrs);
    if(render){
        std::vector<float> rpos,rvel,rcolor,rmisc,rnormal,raxis;
        for(const auto& x:p)if(x.shape==shape){
            rpos.insert(rpos.end(),{x.pos.x,x.pos.y,x.pos.z,x.renderRadius});
            if(shape==WorldFxShape::Drop){rvel.insert(rvel.end(),{x.vel.x,x.vel.y,x.vel.z,x.renderLength});rcolor.insert(rcolor.end(),{x.color.r,x.color.g,x.color.b,(int(x.flags)&16)?0.f:1.f});}
            else if(shape==WorldFxShape::Puff||shape==WorldFxShape::Glow){rcolor.insert(rcolor.end(),{x.color.r,x.color.g,x.color.b,x.renderAlpha});rmisc.insert(rmisc.end(),{x.rotation,x.seed,x.life>0?x.age/x.life:0,x.kind});}
            else if(shape==WorldFxShape::Ring){rnormal.insert(rnormal.end(),{x.normal.x,x.normal.y,x.normal.z,x.life>0?x.age/x.life:0});rcolor.insert(rcolor.end(),{x.color.r,x.color.g,x.color.b,x.alpha});rmisc.insert(rmisc.end(),{x.seed,x.style,x.length,0});}
            else if(shape==WorldFxShape::Shell){rcolor.insert(rcolor.end(),{x.color.r,x.color.g,x.color.b,x.alpha});rmisc.insert(rmisc.end(),{x.life>0?x.age/x.life:0,x.seed,x.kind,x.renderLength});raxis.insert(raxis.end(),{x.normal.x,x.normal.y,x.normal.z,x.crown});}
            else if(shape==WorldFxShape::Beam){rcolor.insert(rcolor.end(),{x.color.r,x.color.g,x.color.b,x.renderAlpha});rmisc.insert(rmisc.end(),{x.life>0?x.age/x.life:0,x.renderLength,x.seed,0});}
        }
        std::cout<<",\"render\":{"<<((shape==WorldFxShape::Puff||shape==WorldFxShape::Glow)?"\"aPosSize\":" : "\"aPosR\":");arr(rpos);
        if(shape==WorldFxShape::Drop){std::cout<<",\"aVelS\":";arr(rvel);std::cout<<",\"aColA\":";arr(rcolor);}
        else if(shape==WorldFxShape::Puff||shape==WorldFxShape::Glow){std::cout<<",\"aColA\":";arr(rcolor);std::cout<<",\"aMisc\":";arr(rmisc);}
        else if(shape==WorldFxShape::Ring){std::cout<<",\"aNrmT\":";arr(rnormal);std::cout<<",\"aColA\":";arr(rcolor);std::cout<<",\"aMisc\":";arr(rmisc);}
        else if(shape==WorldFxShape::Shell){std::cout<<",\"aColA\":";arr(rcolor);std::cout<<",\"aMisc\":";arr(rmisc);std::cout<<",\"aAxis\":";arr(raxis);}
        else {std::cout<<",\"aColA\":";arr(rcolor);std::cout<<",\"aMisc\":";arr(rmisc);}
        std::cout<<'}';
    }
    std::cout<<'}';
}
void pools(const std::vector<WorldFxParticle>& p,bool render){
    std::cout<<'{';pool(p,WorldFxShape::Drop,"drops",render);std::cout<<',';pool(p,WorldFxShape::Puff,"puffs",render);std::cout<<',';pool(p,WorldFxShape::Glow,"glows",render);std::cout<<',';pool(p,WorldFxShape::Ring,"rings",render);std::cout<<',';pool(p,WorldFxShape::Shell,"shells",render);std::cout<<',';pool(p,WorldFxShape::Beam,"beams",render);std::cout<<'}';
}
glm::dvec3 orange(){return {1.0,0.2541520943200296,0.0069954101845983935};}
}
int main(){
    std::cout<<std::setprecision(9);std::string name;
    while(std::getline(std::cin,name)){
        if(name.empty())continue;std::uint32_t rng=0x6d2b79f5u;glm::dvec3 pos(0),normal(0,1,0),color=orange();double radius=1;
        if(name=="explosion")radius=3;if(name=="slamWave")radius=5.2f;if(name=="stormStart")radius=3.4f;if(name=="rainSheet"){pos.y=4.6f;radius=3.4f;}
        if(name=="formPop")radius=1;if(name=="dive"||name=="emerge")radius=4;if(name=="wake"){normal={0,0,1};radius=8;}
        if(name.rfind("footstep",0)==0){normal={0,0,1};radius=5;}
        auto items=BuildWorldFxRecipe(name,pos,normal,color,radius,rng);
        std::cout<<"{\"recipe\":\""<<name<<"\",\"immediatePools\":";pools(items,false);
        UpdateWorldFxRecipe(items,1.0/60.0,rng);
        std::cout<<",\"firstFramePools\":";pools(items,true);
        for(int i=0;i<180;i++)UpdateWorldFxRecipe(items,1.0/60.0,rng);
        std::cout<<",\"expired\":{";
        std::cout<<"\"drops\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Drop;});
        std::cout<<",\"puffs\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Puff;});
        std::cout<<",\"glows\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Glow;});
        std::cout<<",\"rings\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Ring;});
        std::cout<<",\"shells\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Shell;});
        std::cout<<",\"beams\":"<<std::count_if(items.begin(),items.end(),[](const auto& x){return x.shape==WorldFxShape::Beam;});
        std::cout<<",\"checks\":0,\"sched\":0,\"caps\":{\"drops\":2600,\"puffs\":520,\"glows\":300,\"rings\":300}";
        std::cout<<"}}\n";
    }
}
