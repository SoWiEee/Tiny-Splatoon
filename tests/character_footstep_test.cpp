#include "CharacterData.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(){
    try{
        using namespace restoration;
        CharacterData data;CharacterPose pose(data,data.rig(0,0,0),"steps","shooter");
        CharacterAnimState actor;actor.speed=4;actor.localMove={0,1};
        CharacterSceneInput scene;scene.inWorld=true;scene.lifeLevel=2;
        scene.raycast=[](glm::dvec3 origin,glm::dvec3 direction,double distance){
            const double t=direction.y<0?-origin.y/direction.y:distance+1;
            return CharacterGroundHit{t>=0&&t<=distance,t,origin+direction*t,{0,1,0}};
        };
        int count=0;scene.footstep=[&](int side,glm::dvec3 position,double speed){
            if(side<0||side>1||!std::isfinite(position.y)||speed<=0)throw std::runtime_error("Invalid planted-foot audio event");
            ++count;
        };
        for(int frame=0;frame<180;++frame){actor.position.z+=4.0/60;pose.update(1.0/60,actor,scene);}
        if(count<8||count>30)throw std::runtime_error("Foot contacts did not emit a plausible gait cadence");
        const int running=count;actor.visible=false;
        for(int frame=0;frame<60;++frame){actor.position.z+=4.0/60;pose.update(1.0/60,actor,scene);}
        if(count!=running)throw std::runtime_error("Hidden actors emitted footsteps");
        actor.visible=true;actor.form=CharacterForm::Squid;
        for(int frame=0;frame<60;++frame){actor.position.z+=4.0/60;pose.update(1.0/60,actor,scene);}
        if(count!=running)throw std::runtime_error("Squid form emitted kid footsteps");
        std::cout<<"CHARACTER_FOOTSTEP contacts="<<count<<" hidden/squid=quiet\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
