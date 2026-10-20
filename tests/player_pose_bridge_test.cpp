#include "../src/game/PlayerSimulation.hpp"
#include "../src/game/ArsenalData.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace restoration;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
int main() {
    try {
        const auto& stage=ReferenceStages().front();StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);
        PlayerSimulation actor(stage,geometry,paint);
        std::vector<CharacterTrigger> events;std::vector<std::string> order;
        actor.setPoseTriggerSink([&](const CharacterTrigger& e){events.push_back(e);order.push_back(e.name);});
        actor.setPoseFrameSink([&](double dt){check(dt>0,"Pose frame has invalid timestep");order.push_back("frame");});
        actor.spawnAt(stage.spawnPads[0],0);
        check(events.size()==1 && events[0].name=="spawn","Respawn did not forward its source pose event");
        events.clear();order.clear();actor.state.grounded=true;actor.input.jump=true;actor.tick(1.0/60.0);
        check(order.size()>=2 && order.front()=="jump" && order.back()=="frame","Jump trigger must precede same-frame pose update");
        events.clear();order.clear();actor.input={};actor.state.invuln=0;actor.state.yaw=0;
        actor.damage(30);
        check(events.size()==1 && events[0].name=="hit" && std::abs(events[0].amount.value()-0.5)<1e-6,
              "Damage did not forward source flinch amplitude");
        check(std::abs(actor.state.hurtFlash-0.5)<1e-6,"Damage flash did not retain accepted damage");
        actor.damage(1000);check(!actor.state.alive,"Fixture did not splat actor");
        order.clear();actor.tick(1.0/60.0);
        check(order.size()==1 && order[0]=="frame","Dead actor did not refresh pose visibility exactly once");
        actor.spawnAt(stage.spawnPads[0],0);actor.setLoadout("shooter");actor.input.fire=true;
        events.clear();order.clear();const auto shots=actor.shotsFired();actor.tick(1.0/60.0);
        const auto shotEvents=std::count_if(events.begin(),events.end(),[](const CharacterTrigger& e){return e.name=="shoot";});
        check(shotEvents>0 && shotEvents==actor.shotsFired()-shots && order.back()=="frame",
              "Source firing events were dropped or delivered after pose evaluation");
        actor.spawnAt(stage.spawnPads[0],0);actor.setLoadout("twins");actor.input.fire=true;
        events.clear();actor.tick(1.0/60.0);
        const auto handShot=std::find_if(events.begin(),events.end(),[](const CharacterTrigger& e){return e.name=="shoot";});
        check(handShot!=events.end(),"Twins did not emit a hand-specific shot");
        const auto hand=static_cast<std::size_t>(handShot->hand);
        const auto clocks=actor.combatPose().sinceHand;
        check(clocks[hand]==0.0 && std::abs(clocks[1-hand]-(99.0+1.0/60.0))<1e-9,
              "Runner hand clocks did not reset only the firing hand before pose evaluation");
        actor.input.fire=false;actor.tick(1.0/60.0);
        check(std::abs(actor.combatPose().sinceHand[hand]-1.0/60.0)<1e-9,
              "Runner shot clock did not advance after release");
        std::cout<<"Actor pose bridge source trigger ordering and lifecycle passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
