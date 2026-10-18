#include "../restoration/PlayerSimulation.hpp"
#include "../restoration/FacePaint.hpp"
#include "player_fixtures.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <memory>

using namespace restoration;

namespace {
StageData makeStage() {
    StageData stage{};
    stage.id = "player-test";
    stage.bounds = {-20.0, 20.0, -20.0, 20.0};
    stage.spawnPads = {glm::dvec3(0.0), glm::dvec3(0.0)};
    BlockData floor{};
    floor.center = {0.0, -0.5, 0.0}; floor.half = {20.0, 0.5, 20.0};
    floor.axes = {glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)};
    floor.faces.fill(-1); floor.faces[2] = 0; floor.solid = true;
    stage.blocks.push_back(floor);
    BlockData wall{};
    wall.center = {0.0, 1.0, 8.0}; wall.half = {4.0, 1.0, 0.2};
    wall.axes = floor.axes;
    wall.faces.fill(-1); wall.faces[5] = 1; wall.solid = true;
    stage.blocks.push_back(wall);
    FaceData ground{};
    ground.origin = {-20.0, 0.0, -20.0}; ground.u = {1, 0, 0}; ground.v = {0, 0, 1};
    ground.normal = {0, 1, 0}; ground.width = ground.height = 40.0;
    ground.block = 0; ground.paintable = ground.turf = true;
    stage.faces.push_back(ground);
    FaceData front{};
    front.origin = {-4.0, 0.0, 7.8}; front.u = {1, 0, 0}; front.v = {0, 1, 0};
    front.normal = {0, 0, -1}; front.width = 8.0; front.height = 2.0;
    front.block = 1; front.paintable = front.wall = true;
    stage.faces.push_back(front);
    return stage;
}
bool near(double a, double b, double tolerance = 0.04) { return std::abs(a - b) <= tolerance; }
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; ++failures; } };
    std::string scenario;
    std::unique_ptr<StageData> oracleStage;
    std::unique_ptr<StageGeometry> oracleGeometry;
    std::unique_ptr<FacePaint> oraclePaint;
    std::unique_ptr<PlayerSimulation> oracle;
    int oracleFrame=0, oracleFailures=0;double oracleDt=0;
    for(const auto& f:test::playerFixtures()) {
        if(scenario!=f.scenario || oracleDt!=f.dt) {
            scenario=f.scenario;oracleFrame=0;oracleDt=f.dt;
            oracle.reset();oraclePaint.reset();oracleGeometry.reset();
            oracleStage=std::make_unique<StageData>(makeStage());
            oracleStage->blocks[0].roof=scenario=="roof";oracleStage->blocks[0].grate=scenario=="grate";
            if(scenario=="step" || scenario=="rail") {
                BlockData b{};b.axes=oracleStage->blocks[0].axes;b.faces.fill(-1);b.solid=true;
                if(scenario=="step") {b.center={0,0.1,2};b.half={2,0.1,1};}
                else {b.center={0,0.5,0};b.half={0.035,0.06,4};b.rail=true;}
                oracleStage->blocks.push_back(b);
            }
            oracleStage->spawnPads[1]={0,0,4};oracleStage->spawnBarrier=scenario=="barrier"?2.0:0.0;
            if(scenario=="wall") oracleStage->faces[0].paintable=false;
            else oracleStage->faces[1].paintable=false;
            oracleGeometry=std::make_unique<StageGeometry>(*oracleStage);
            oraclePaint=std::make_unique<FacePaint>(*oracleStage,*oracleGeometry,2048);
            if(scenario=="swim" || scenario=="enemy") oraclePaint->splat({0,0,0},60,scenario=="swim"?0:1,0.2);
            if(scenario=="wall") oraclePaint->splat({0,1,7.8},10,0,0.2);
            oracle=std::make_unique<PlayerSimulation>(*oracleStage,*oracleGeometry,*oraclePaint);
            oracle->spawnAt({scenario=="rail"?0.15:0.0,scenario=="rail"?0.56:0.0,scenario=="wall"?7.0:0.0},0);
            oracle->state.invuln=0;
        }
        while(oracleFrame<f.frame) {
            auto& in=oracle->input;in={};
            if(scenario=="run" || scenario=="squidDry" || scenario=="swim" || scenario=="enemy" || scenario=="fireMove" || scenario=="wall" || scenario=="airFire" || scenario=="step" || scenario=="rail" || scenario=="barrier") in.move={0,0,1};
            if(scenario=="fireRelease") in.move={1,0,0};
            if(scenario=="brake") in.move={0,0,oracleFrame<60?1.0:0.0};
            if(scenario=="reverse") in.move={0,0,oracleFrame<60?1.0:-1.0};
            if(scenario=="turn") in.move=oracleFrame<60?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
            if(scenario=="air") in.move=oracleFrame<30?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
            in.jump=(scenario=="jump" || scenario=="air" || scenario=="airFire") && oracleFrame==0;
            in.squid=scenario=="squidDry" || scenario=="swim" || scenario=="wall" || scenario=="grate";
            in.fire=scenario=="fireMove" || scenario=="airFire" || (scenario=="fireRelease" && oracleFrame<30);
            oracle->tick(f.dt);++oracleFrame;
        }
        const auto& s=oracle->state;
        auto close=[](double a,double b){return std::abs(a-b)<1e-6;};
        const bool equal=close(s.pos.x,f.x)&&close(s.pos.y,f.y)&&close(s.pos.z,f.z)&&
            close(s.vel.x,f.vx)&&close(s.vel.y,f.vy)&&close(s.vel.z,f.vz)&&close(s.yaw,f.yaw)&&
            close(s.hp,f.hp)&&close(s.ink,f.ink)&&s.grounded==bool(f.ground)&&s.squid==bool(f.squid)&&
            s.submerged==bool(f.submerged)&&s.climbing==bool(f.climbing);
        if(!equal) {
            if(oracleFailures++<12) std::cerr<<"Actor "<<scenario<<" frame "<<f.frame<<": pos "<<s.pos.x<<','<<s.pos.y<<','<<s.pos.z<<" expected "<<f.x<<','<<f.y<<','<<f.z
                <<" vel "<<s.vel.x<<','<<s.vel.y<<','<<s.vel.z<<" expected "<<f.vx<<','<<f.vy<<','<<f.vz
                <<" yaw/hp/ink "<<s.yaw<<'/'<<s.hp<<'/'<<s.ink<<" expected "<<f.yaw<<'/'<<f.hp<<'/'<<f.ink<<'\n';
        }
    }
    check(oracleFailures==0,"reference Actor trace mismatch");
    const StageData stage = makeStage();
    const StageGeometry geometry(stage);
    FacePaint paint(stage, geometry, 2048);
    PlayerSimulation player(stage, geometry, paint);
    constexpr double dt = 1.0 / 120.0;
    check(player.state.grounded && near(player.state.pos.y, 0.0), "spawn settles onto floor");
    player.input.move = {0, 0, 1};
    for (int i = 0; i < 120; ++i) player.tick(dt);
    check(player.state.grounded && near(std::hypot(player.state.vel.x, player.state.vel.z), 6.0),
          "kid reaches source 6 m/s run speed");
    player.spawnAt({0, 0, 0}, 0);
    player.input.squid = true;
    player.input.move = {0, 0, 1};
    for (int i = 0; i < 120; ++i) player.tick(dt);
    check(player.state.squid && near(std::hypot(player.state.vel.x, player.state.vel.z), 2.9),
          "dry squid reaches source 2.9 m/s speed");
    player.spawnAt({0, 0, 0}, 0);
    paint.splat({0, 0.1, 0}, 2.0, 0, 0.3);
    player.input.squid = true;
    player.input.move = {0, 0, 1};
    for (int i = 0; i < 25; ++i) player.tick(dt);
    check(player.state.submerged && player.state.vel.z > 8.0, "own ink enables swim acceleration");
    player.spawnAt({0, 0, 0}, 0);
    player.input.jump = true;
    player.tick(dt);
    check(!player.state.grounded && player.state.vel.y > 8.0, "jump uses source 8.4 m/s impulse");
    player.input.jump = false;
    for (int i = 0; i < 150; ++i) player.tick(dt);
    check(player.state.grounded && near(player.state.pos.y, 0.0), "jump lands on floor");
    player.spawnAt({0, 0, 0}, 0);
    player.input.fire = true;
    player.input.aimYaw = 0;
    player.input.aimPitch = 0;
    player.input.aimPoint = {0, 1, 8};
    player.input.hasAimPoint = true;
    for (int i = 0; i < 60; ++i) player.tick(dt);
    check(player.shotsFired() >= 5 && player.state.ink < 96.0, "shooter cadence and ink cost");
    check(player.turfPainted() > 0.0 && player.state.specialCharge > 0.0, "shooter impacts paint and charge special");
    player.spawnAt({0, 0, 0}, 0);
    for (int i = 0; i < 200; ++i) player.tick(dt);
    check(!player.damage(30.0) && near(player.state.hp, 70.0), "damage after spawn invulnerability");
    for (int i = 0; i < 180; ++i) player.tick(dt);
    check(player.state.hp > 70.0, "health regenerates after source delay");
    paint.clear();
    paint.splat({0, 0.1, 0}, 2.0, 1, 0.4);
    player.spawnAt({0, 0, 0}, 0);
    for (int i = 0; i < 620; ++i) player.tick(dt);
    check(player.state.groundTeam == 2 && near(player.state.hp, 60.0, 0.2),
          "enemy ink damage is capped at 40 hp and blocks regeneration");
    paint.clear();
    player.spawnAt({0, 0, 0}, 0);
    for (int i = 0; i < 200; ++i) player.tick(dt);
    check(player.damage(110.0) && !player.state.alive, "lethal damage splats player");
    for (int i = 0; i < 660; ++i) player.tick(dt);
    check(player.state.alive && near(player.state.hp, 100.0) && player.state.pos.y > 0.0,
          "splat respawns after 5.5 seconds above spawn pad");
    if (failures) return 1;
    std::cout << "Player movement, vitals and Spritzer paint passed\n";
}
