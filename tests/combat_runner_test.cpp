#include "../restoration/PlayerSimulation.hpp"
#include "combat_runner_fixtures.hpp"
#include <cmath>
#include <exception>
#include <iostream>
#include <algorithm>
#include <memory>
using namespace restoration;
int main() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);
    std::unique_ptr<PlayerSimulation> actor;std::unique_ptr<WeaponRunner> runner;
    std::string id;double dt=0,hold=0;int frame=0,failures=0;
    for(const auto& f:test::combatRunnerFixtures) {
        if(id!=f.id || dt!=f.dt || hold!=f.hold) {
            id=f.id;dt=f.dt;hold=f.hold;frame=0;runner.reset();actor=std::make_unique<PlayerSimulation>(stage,geometry,paint);actor->setLoadout(id);actor->state.grounded=true;runner=std::make_unique<WeaponRunner>(*actor);
        }
        while(frame<f.frame){bool fire=frame<std::lround(hold/dt);try{runner->tick(dt,fire,frame==0);}catch(const std::exception& e){std::cerr<<id<<" frame="<<frame<<" threw: "<<e.what()<<"\n";return 1;}++frame;}
        auto pose=runner->pose();auto close=[](double a,double b){return std::abs(a-b)<1e-8;};
        bool ok=close(actor->state.ink,f.ink)&&close(pose.charge,f.charge)&&close(pose.burstTime,f.burst)&&close(runner->moveSpeed(),f.speed)&&actor->shotsFired()==f.shots&&pose.charging==bool(f.charging)&&pose.rolling==bool(f.rolling)&&pose.firing==bool(f.firing);
        ok=ok&&runner->busy()==bool(f.busy);
        if(!ok && failures++<20)std::cerr<<id<<" dt="<<dt<<" hold="<<hold<<" frame="<<frame<<" ink "<<actor->state.ink<<"/"<<f.ink<<" charge "<<pose.charge<<"/"<<f.charge<<" burst "<<pose.burstTime<<"/"<<f.burst<<" speed "<<runner->moveSpeed()<<"/"<<f.speed<<" shots "<<actor->shotsFired()<<"/"<<f.shots<<" busy "<<runner->busy()<<"/"<<f.busy<<"\n";
    }
    if(failures)return 1;
    std::cout<<test::combatRunnerFixtures.size()<<" source WeaponRunner snapshots passed\n";

}
