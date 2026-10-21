#include "MatchSimulation.hpp"
#include "FacePaint.hpp"
#include "StageGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(){try{using namespace restoration;
    int completed=0;
    for(const auto& stage:ReferenceStages()){
        if(stage.mode!="zones"||stage.onlineOnly)continue;
        StageGeometry geometry(stage);NavGraph nav(stage,geometry);
        for(const char* difficulty:{"easy","normal","hard"}){
            if(stage.id!="tidewater"&&std::string_view(difficulty)!="normal")continue;
            FacePaint paint(stage,geometry);MatchOptions options;options.mode="zones";options.difficulty=difficulty;options.autopilot=true;options.seed=2026;
            MatchSimulation match(stage,geometry,paint,nav,options);match.startPlaying();
            bool captured=false;double zoneInk=0;int frames=0;
            constexpr double dt=1./60;
            for(;frames<36600&&match.phase()!=MatchPhase::Judge;++frames){
                match.tick(dt);const auto* zones=match.zones();if(zones->owner()>=0)captured=true;
                for(const auto& actor:match.actors()){
                    const auto& state=actor->state;
                    if(!std::isfinite(state.pos.x+state.pos.y+state.pos.z+state.vel.x+state.vel.y+state.vel.z+state.yaw+state.smoothY+state.hp+state.ink)){
                        std::cerr<<stage.id<<'/'<<difficulty<<" frame="<<frames<<" weapon="<<actor->mainId()<<" alive="<<state.alive<<" pos="<<state.pos.x<<','<<state.pos.y<<','<<state.pos.z<<" vel="<<state.vel.x<<','<<state.vel.y<<','<<state.vel.z<<" hp="<<state.hp<<" ink="<<state.ink<<" input="<<actor->input.move.x<<','<<actor->input.move.y<<','<<actor->input.move.z<<" aim="<<actor->input.aimYaw<<','<<actor->input.aimPitch<<'\n';
                        throw std::runtime_error("Non-finite Zone actor: "+std::string(stage.id));
                    }
                    zoneInk=std::max(zoneInk,actor->zoneTurfPainted());
                }
            }
            if(match.phase()!=MatchPhase::Judge||!match.result()||!match.result()->zones||!match.zones()->ended())throw std::runtime_error("Zone match did not finish: "+std::string(stage.id)+"/"+difficulty);
            if(!captured||zoneInk<=0)throw std::runtime_error("Bots did not capture and paint active objectives: "+std::string(stage.id)+"/"+difficulty);
            const auto& result=*match.result()->zones;
            std::cout<<stage.id<<'/'<<difficulty<<" seconds="<<frames*dt<<" count="<<result.counts[0]<<','<<result.counts[1]<<" reason="<<result.reason<<" zoneInk="<<zoneInk<<'\n';
            match.finishJudging();if(match.phase()!=MatchPhase::Results)throw std::runtime_error("Zone judging did not expose results");++completed;
        }
    }
    if(completed!=9)throw std::runtime_error("Expected seven normal stages and two additional difficulty runs");
    std::cout<<"Nine complete native Zone matches passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
