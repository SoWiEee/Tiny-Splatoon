#include "../src/game/PlayerSimulation.hpp"
#include "fixtures/combat_runner_fixtures.hpp"
#include "fixtures/controller_fixtures.hpp"
#include <cmath>
#include <exception>
#include <iostream>
#include <algorithm>
#include <memory>
#include <string_view>
using namespace restoration;
int main() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);
    std::unique_ptr<PlayerSimulation> actor;std::unique_ptr<WeaponRunner> runner;
    std::string id;double dt=0,hold=0;int frame=0,failures=0;
    auto require=[&](bool condition,const char* message) {
        if(!condition) { std::cerr<<message<<'\n'; ++failures; }
    };
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

    for(const auto* main : {"shooter","charger","spinner","bow","blade","mitts"}) {
        PlayerSimulation empty(stage,geometry,paint);empty.setLoadout(main);empty.state.ink=0;empty.state.grounded=true;
        WeaponRunner dry(empty);dry.tick(1./60,true,true);
        const auto events=empty.combatWorld().takeVisualEvents();
        require(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind=="lowink";}),"An empty weapon must emit its dry-fire visual trigger");
        require(empty.state.ink==0&&empty.shotsFired()==0,"Dry-fire visuals must not spend ink or fire gameplay shots");
    }

    PlayerSimulation shaker(stage,geometry,paint);
    shaker.setLoadout("shooter","shaker");
    shaker.state.grounded=true;
    WeaponRunner shakerRunner(shaker);
    constexpr double shakerStep=1.0/60.0;
    for(int frame=0;frame<240 && shakerRunner.pose().shakerCharge<1.0;++frame)
        shakerRunner.tick(shakerStep,false,false,true,false);
    const auto chargedPose=shakerRunner.pose();
    require(chargedPose.shakerCharging && chargedPose.shakerCharge>=1.0,
          "Shaker held-charge pose must expose its fully charged gameplay state");
    auto shakerEvents=shaker.combatWorld().takeVisualEvents();
    const auto countSource=[&](std::string_view source) {
        return std::count_if(shakerEvents.begin(),shakerEvents.end(),[&](const auto& event) {
            return event.kind=="special_audio" && event.source==source;
        });
    };
    require(countSource("shaker_clink")==1 && countSource("shaker_clink_max")==1,
          "Shaker charge must emit one clink at each source level transition, without replaying while held");
    shakerRunner.tick(shakerStep,false,false,false,true);
    require(!shakerRunner.pose().shakerCharging && shakerRunner.pose().shakerCharge==0.0,
          "Shaker release must stop exposing the held-charge loop state");

    StageData shakerDeathStage=stage;
    shakerDeathStage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry shakerDeathGeometry(shakerDeathStage);
    FacePaint shakerDeathPaint(shakerDeathStage,shakerDeathGeometry,2048);
    PlayerSimulation dyingShaker(shakerDeathStage,shakerDeathGeometry,shakerDeathPaint);
    dyingShaker.setLoadout("shooter","shaker");
    dyingShaker.spawnAt({0,0,0},0);
    dyingShaker.state.invuln=0.0;
    dyingShaker.input.sub=true;
    for(int frame=0;frame<240 && dyingShaker.combatPose().shakerCharge<0.5;++frame)dyingShaker.tick(shakerStep);
    require(dyingShaker.combatPose().shakerCharging,
          "Shaker death regression needs a held gameplay charge before any audio frame observes it");
    require(dyingShaker.damage(1000.0),"A lethal hit must splat an actively charging Shaker actor");
    const auto deathEvents=dyingShaker.combatWorld().takeVisualEvents();
    require(std::count_if(deathEvents.begin(),deathEvents.end(),[](const auto& event) {
              return event.kind=="special_audio" && event.source=="shaker_fizzle";
          })==1,
          "Shaker death must emit exactly one fizzle from the gameplay splat hook");
    if(failures)return 1;
    std::cout<<"Shaker charge cues and death fizzle follow gameplay transitions\n";
}
