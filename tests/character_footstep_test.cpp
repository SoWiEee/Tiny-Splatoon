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
        CharacterAnimState aim;aim.aimPitch=.45;
        CharacterPose shooter(data,data.rig(0,0,0),"aim-test","shooter");
        for(int frame=0;frame<120;++frame)shooter.update(1.0/60,aim,scene);
        const auto idle=shooter.frame();
        if(idle.aimReady>=.02||glm::distance(idle.aimMuzzle,idle.muzzle)<=.03)
            throw std::runtime_error("Unready shooter must expose a distinct predictive aim socket");
        aim.firing=true;
        for(int frame=0;frame<120;++frame)shooter.update(1.0/60,aim,scene);
        if(shooter.frame().aimReady<=.98)throw std::runtime_error("Firing shooter must reach its live muzzle socket");
        CharacterPose roller(data,data.rig(0,0,0),"roller-aim-test","roller");
        aim.firing=false;roller.update(1.0/60,aim,scene);
        if(roller.frame().aimReady!=1.0||glm::distance(roller.frame().aimMuzzle,roller.frame().muzzle)>1e-9)
            throw std::runtime_error("Roller must bypass predictive muzzle blending");
        CharacterPose twins(data,data.rig(0,0,0),"left-aim-test","twins");
        twins.update(1.0/60,aim,scene);
        if(glm::distance(twins.frame().shotMuzzle(1),twins.frame().muzzleLeft)>1e-9)
            throw std::runtime_error("Dualies must use the left-hand live muzzle without predictive blending");
        std::cout<<"CHARACTER_FOOTSTEP contacts="<<count<<" hidden/squid=quiet\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
