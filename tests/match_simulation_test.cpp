#include "../src/game/MatchSimulation.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <glm/geometric.hpp>

int main(int argc,char** argv){
    using namespace restoration;
    int failures=0,runs=0;
    const std::string selected=argc>1?argv[1]:"";
    const double duration=argc>2?std::stod(argv[2]):30.0;
    const auto seedFilter=argc>3?static_cast<std::uint32_t>(std::stoul(argv[3])):0;
    if(!std::isfinite(duration)||duration<1||duration>30)return 1;
    try{
        for(const auto& stage:ReferenceStages()){
            if(stage.mode!="turf"||stage.onlineOnly||(!selected.empty()&&stage.id!=selected))continue;
            StageGeometry geometry(stage);FacePaint paint(stage,geometry);NavGraph nav(stage,geometry);
            for(std::uint32_t seed:{1u,7u,2026u}){
                if(seedFilter&&seedFilter!=seed)continue;
                MatchOptions options;options.seed=seed;
                MatchSimulation match(stage,geometry,paint,nav,options);match.startPlaying();
                if(match.actors().size()!=8||match.bots().size()!=7)throw std::runtime_error("Turf roster must be one human and seven bots");
                std::array<int,2> teams{};for(const auto& a:match.actors())++teams[a->team()];
                if(teams[0]!=4||teams[1]!=4)throw std::runtime_error("Turf teams must be 4 vs 4");
                struct Travel {glm::dvec3 anchor,start;double still=0,distance=0;bool alive=true;int stalls=0,deaths=0;};
                std::vector<Travel> travel;
                for(const auto& bot:match.bots())travel.push_back({bot->actor().state.pos,bot->actor().state.pos});
                std::array<int,4> modes{};
                for(int frame=0;frame<static_cast<int>(std::lround(duration*60));++frame){
                    match.tick(1.0/60);
                    for(std::size_t b=0;b<match.bots().size();++b){
                        const auto& bot=*match.bots()[b];const auto& a=bot.actor();auto& t=travel[b];
                        ++modes[static_cast<int>(bot.mode())];
                        if(!std::isfinite(a.state.pos.x+a.state.pos.y+a.state.pos.z+a.state.ink+a.state.hp))throw std::runtime_error("Non-finite bot state");
                        if(t.alive&&!a.state.alive)++t.deaths;t.alive=a.state.alive;
                        t.distance=std::max(t.distance,std::hypot(a.state.pos.x-t.start.x,a.state.pos.z-t.start.z));
                        if(a.state.alive&&bot.mode()==BotMode::Paint&&glm::length(a.input.move)>0.1){
                            if(std::hypot(a.state.pos.x-t.anchor.x,a.state.pos.z-t.anchor.z)>=0.25){t.still=0;t.anchor=a.state.pos;}
                            else t.still+=1.0/60;
                            if(t.still>=5.0){++t.stalls;t.still=0;std::cerr<<stage.id<<" seed="<<seed<<" bot="<<b<<" seconds="<<(frame+1)/60.0<<" sustained move without 0.25m progress at "<<a.state.pos.x<<','<<a.state.pos.y<<','<<a.state.pos.z<<" main="<<a.mainId()<<" goal="<<bot.goal()<<" stuck="<<bot.stuckTime()<<" anchor="<<t.anchor.x<<','<<t.anchor.y<<','<<t.anchor.z<<" input="<<a.input.move.x<<','<<a.input.move.z<<" vel="<<a.state.vel.x<<','<<a.state.vel.y<<','<<a.state.vel.z<<" grounded="<<a.state.grounded<<" climbing="<<a.state.climbing<<" ink="<<a.state.ink<<'\n';}
                        }else {t.still=0;t.anchor=a.state.pos;}
                    }
                }
                int stalls=0,deaths=0;std::array<double,2> moved{};
                for(std::size_t b=0;b<travel.size();++b){stalls+=travel[b].stalls;deaths+=travel[b].deaths;moved[match.bots()[b]->actor().team()]+=travel[b].distance;}
                const auto counts=paint.counts();
                if(stalls||moved[0]<1||moved[1]<1||counts[0]<=0||counts[1]<=0){++failures;std::cerr<<stage.id<<" seed="<<seed<<" failed movement/paint/stall acceptance\n";}
                std::cout<<stage.id<<" seed="<<seed<<" seconds="<<duration<<" paint="<<counts[0]<<','<<counts[1]<<" moved="<<moved[0]<<','<<moved[1]<<" modes="<<modes[0]<<','<<modes[1]<<','<<modes[2]<<','<<modes[3]<<" stalls="<<stalls<<" deaths="<<deaths<<'\n';
                match.restart();if(!match.world().projectiles().empty()||!match.world().devices().empty())throw std::runtime_error("Restart retained prior attacks");
                ++runs;
            }
            MatchOptions practice;practice.practice=true;MatchSimulation solo(stage,geometry,paint,nav,practice);
            if(solo.actors().size()!=1||!solo.bots().empty())throw std::runtime_error("Practice created bots");
            if(solo.phase()!=MatchPhase::Playing)throw std::runtime_error("Practice must start playing without Turf intro");
            const double clock=solo.remaining();solo.local().input.move={0,0,1};
            solo.paused=true;const auto pausedPosition=solo.local().state.pos;solo.tick(0.1);
            if(solo.local().state.pos!=pausedPosition)throw std::runtime_error("Paused match moved the player");
            solo.paused=false;solo.tick(0.1);
            if(solo.remaining()!=clock)throw std::runtime_error("Practice consumed the match clock");
            solo.restart();if(solo.phase()!=MatchPhase::Playing||solo.paused)throw std::runtime_error("Practice restart did not restore play");
        }
        if(!runs)throw std::runtime_error("No matching offline Turf stage");
        std::cout<<runs<<" fixed-seed Turf runs, "<<failures<<" failures\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return failures?1:0;
}
