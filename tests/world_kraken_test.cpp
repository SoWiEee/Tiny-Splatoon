#include "../restoration/PlayerSimulation.hpp"
#include "../restoration/WorldSpecials.hpp"
#include "../restoration/ArsenalData.hpp"
#include "kraken_source_fixtures.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <memory>

using namespace restoration;
namespace {
constexpr double dt = 1.0 / 60.0;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double actual, double expected, const char* label) {
    if (std::abs(actual - expected) > 1e-6) {
        std::cerr << label << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(label);
    }
}
void near(glm::dvec3 actual, const std::array<double, 3>& expected, const char* label) {
    for (int i = 0; i < 3; ++i) near(actual[i], expected[i], label);
}
StageData makeStage() {
    StageData s{}; s.id = "kraken-source"; s.bounds = {-20,20,-20,20};
    s.spawnPads = {glm::dvec3(0),glm::dvec3(0)};
    BlockData b{}; b.center={0,-0.5,0}; b.half={20,0.5,20};
    b.axes={glm::dvec3(1,0,0),glm::dvec3(0,1,0),glm::dvec3(0,0,1)};
    b.faces.fill(-1); b.faces[2]=0; b.solid=true; s.blocks.push_back(b);
    FaceData f{}; f.origin={-20,0,-20}; f.u={1,0,0}; f.v={0,0,1}; f.normal={0,1,0};
    f.width=f.height=40; f.block=0; f.paintable=f.turf=true; s.faces.push_back(f);
    return s;
}
struct Scene {
    StageData stage=makeStage(); StageGeometry geometry{stage}; FacePaint paint{stage,geometry,2048};
    CombatWorld world{stage,geometry,paint}; PlayerSimulation actor{stage,geometry,paint,0,&world};
    Scene() {
        actor.setLoadout("shooter","bomb","kraken"); actor.spawnAt({0,0,0},0);
        actor.state.invuln=0; actor.state.specialCharge=actor.weapon().number("specialCost",190);
    }
    void activate() { actor.input.special=true; actor.tick(dt); actor.input.special=false; }
    void enemyFloor() { paint.splat({0,0,0},60,1,0.2); }
};
void movement() {
    Scene s; s.activate(); const auto p=s.actor.specialPose();
    check(p.active && !p.body && p.noSquid && p.inkProof && p.faceMove,"Kraken source activation flags");
    near(p.moveSpeed,test::krakenSourceFixture.speed,"Kraken speed"); near(p.accel,45,"Kraken acceleration");
    s.actor.input.move={1,0,0}; s.actor.input.squid=true;
    int frame=0;
    for (const auto& f:test::krakenMotion) {
        while(frame<f.frame) { s.enemyFloor(); s.actor.tick(dt); ++frame; }
        near(s.actor.state.pos,f.pos,"Kraken movement position"); near(s.actor.state.vel,f.vel,"Kraken movement velocity");
        near(s.actor.state.yaw,f.yaw,"Kraken movement facing");
        check(!s.actor.state.squid && s.actor.state.groundTeam==2 && s.actor.state.hp==100,"Kraken enemy ink immunity/form");
    }
}
void attack(bool airborne) {
    Scene s; s.activate();
    if(airborne) { s.actor.state.pos.y=3; s.actor.state.grounded=false; }
    s.actor.input.fire=true; s.actor.tick(dt); s.actor.input.fire=false;
    const auto& f=airborne?test::krakenAirDrop:test::krakenGroundAttack;
    near(s.actor.state.pos,f.triggerPos,"Kraken attack trigger position");
    near(s.actor.state.vel,f.triggerVel,"Kraken attack trigger velocity");
    int frames=1;
    while(!s.actor.state.grounded && frames<180) { s.actor.tick(dt); ++frames; }
    check(frames==f.frames,"Kraken attack landing frame");
    near(s.actor.state.pos,f.finalPos,"Kraken attack final position");
}
void hopAndDamage() {
    Scene s; s.activate(); s.actor.state.pos={0,1,0}; s.actor.state.vel={0,4,0}; s.actor.state.grounded=false;
    s.actor.input.jump=true; s.actor.tick(dt);
    near(s.actor.state.pos,test::krakenHopPos,"Kraken hop position"); near(s.actor.state.vel,test::krakenHopVel,"Kraken hop velocity");
    s.actor.state.grounded=true; s.actor.state.vel={0,0,0}; s.actor.input.jump=false;
    PlayerSimulation enemy(s.stage,s.geometry,s.paint,1,&s.world); enemy.state.pos={0,0,-2};
    s.world.applyHit(enemy,s.actor,200,"shooter");
    check(s.actor.state.hp==100,"Kraken damage immunity");
    near(s.actor.state.vel.z,4,"Kraken damage knock cap"); near(s.actor.state.vel.y,2.12,"Kraken ground knock hop");
}
void expiryAndDeath() {
    Scene s; s.activate(); int frames=0;
    while(s.actor.specialPose().active && frames<500) { s.actor.tick(dt); ++frames; }
    check(frames==test::krakenSourceFixture.expiryFrames,"Kraken exact source expiry frame");
    Scene dying; dying.activate(); dying.world.cleanup(dying.actor);
    check(!dying.actor.specialPose().active,"Kraken owner cleanup");
}
void impactTargets() {
    auto stage=makeStage();
    BlockData wall=stage.blocks.front();wall.faces.fill(-1);wall.center={0.225,0.775,0};wall.half={0.02,0.005,0.05};
    stage.blocks.push_back(wall);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    WorldSpecials specials(world);PlayerSimulation owner(stage,geometry,paint,0,&world);
    owner.setLoadout("shooter","bomb","kraken");owner.state.specialCharge=190;owner.state.invuln=0;
    std::vector<std::unique_ptr<PlayerSimulation>> victims;
    for(int i=0;i<6;++i) {
        auto victim=std::make_unique<PlayerSimulation>(stage,geometry,paint,i==4?0:1,&world);
        victim->state.pos={i==1?2.0:i==2?0.45:i==3?4.0:0.0,0,0};
        victim->state.hp=10000;victim->state.invuln=0;if(i==5)victim->state.alive=false;
        victims.push_back(std::move(victim));
    }
    check(specials.activate(owner,"kraken"),"Kraken impact activation");
    bool sub=false,released=false;specials.weapon(owner,dt,true,true,sub,released);specials.tickActor(owner,dt);
    owner.state.pos={0,0,0};owner.state.grounded=true;specials.tickActor(owner,dt);
    near(10000-victims[0]->state.hp,200,"Kraken close damage");
    near((10000-victims[0]->state.hp)+(10000-victims[1]->state.hp),test::krakenSourceFixture.acceptedDamageTotal,"Kraken source accepted damage total");
    for(int i=2;i<6;++i) near(victims[i]->state.hp,10000,"Kraken blocked/outside/team/dead filtering");
    near(owner.state.specialCharge,0,"Kraken paint must not refill special gauge");
}
}
int main() {
    try { movement(); attack(false); attack(true); hopAndDamage(); expiryAndDeath(); impactTargets();
        std::cout<<"Kraken actual-source activation, movement, attack, hop, immunity and lifetime passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
