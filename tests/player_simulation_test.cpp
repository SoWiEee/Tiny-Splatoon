#include "../src/game/PlayerSimulation.hpp"
#include "../src/world/FacePaint.hpp"
#include "fixtures/player_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <memory>
#include <limits>

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
            // Dive oracles sample dry ground even when the native charger beam would paint it.
            if(scenario=="wall" || scenario=="chargerDive" || scenario=="spinnerDive") oracleStage->faces[0].paintable=false;
            else oracleStage->faces[1].paintable=false;
            oracleGeometry=std::make_unique<StageGeometry>(*oracleStage);
            oraclePaint=std::make_unique<FacePaint>(*oracleStage,*oracleGeometry,2048);
            if(scenario=="swim" || scenario=="enemy" || scenario=="enemyRevealed") oraclePaint->splat({0,0,0},60,scenario=="swim"?0:1,0.2);
            if(scenario=="wall") oraclePaint->splat({0,1,7.8},10,0,0.2);
            oracle=std::make_unique<PlayerSimulation>(*oracleStage,*oracleGeometry,*oraclePaint);
            oracle->setLoadout(scenario=="chargerDive"?"charger":scenario=="spinnerDive"?"spinner":scenario=="rollerPop"?"roller":"shooter");
            oracle->spawnAt({scenario=="rail"?0.15:0.0,scenario=="rail"?0.56:0.0,scenario=="wall"?7.0:0.0},0);
            oracle->state.invuln=0;
            if(scenario=="enemyRevealed") oracle->state.reveal=8;
            if(scenario.rfind("returnJump",0)==0)
                check(oracle->superJump({6,0,4},scenario=="returnJumpInstant",true),"Native return jump did not start");
        }
        while(oracleFrame<f.frame) {
            auto& in=oracle->input;in={};
            if(scenario=="chargerDive") in.aimYaw=0.6;
            if(scenario=="run" || scenario=="squidDry" || scenario=="swim" || scenario=="enemy" || scenario=="fireMove" || scenario=="wall" || scenario=="airFire" || scenario=="step" || scenario=="rail" || scenario=="barrier") in.move={0,0,1};
            if(scenario=="fireRelease") in.move={1,0,0};
            if(scenario=="enemyRevealed") in.move={0,0,1};
            if(scenario=="brake") in.move={0,0,oracleFrame<60?1.0:0.0};
            if(scenario=="reverse") in.move={0,0,oracleFrame<60?1.0:-1.0};
            if(scenario=="turn") in.move=oracleFrame<60?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
            if(scenario=="air") in.move=oracleFrame<30?glm::dvec3(0,0,1):glm::dvec3(1,0,0);
            in.jump=(scenario=="jump" || scenario=="air" || scenario=="airFire") && oracleFrame==0;
            in.squid=scenario=="squidDry" || scenario=="swim" || scenario=="wall" || scenario=="grate";
            in.fire=scenario=="fireMove" || scenario=="airFire" || (scenario=="fireRelease" && oracleFrame<30);
            if(scenario=="chargerDive" || scenario=="spinnerDive") {in.fire=oracleFrame<30;in.squid=oracleFrame>=30;}
            if(scenario=="rollerPop") {in.squid=oracleFrame<6;in.fire=oracleFrame==6;}
            oracle->tick(f.dt);++oracleFrame;
        }
        const auto& s=oracle->state;
        const test::PlayerFixture* expected = &f;
        if (scenario == "squidDry") {
            const auto run = std::find_if(test::playerFixtures().begin(), test::playerFixtures().end(), [&](const auto& candidate) {
                return std::string(candidate.scenario) == "run" && candidate.frame == f.frame && candidate.dt == f.dt;
            });
            check(run != test::playerFixtures().end(), "dry Shift override is missing its matching kid-run oracle frame");
            if (run != test::playerFixtures().end()) expected = &*run;
        }
        auto close=[](double a,double b){return std::abs(a-b)<1e-6;};
        const auto& jump=oracle->superJumpState();
        const bool jumpMatches=(jump?int(jump->flight):-1)==expected->jumpPhase&&
            close(jump?jump->elapsed:0.0,expected->jumpTime)&&close(jump?jump->duration:0.0,expected->jumpDuration)&&
            bool(jump&&jump->home)==bool(expected->jumpHome);
        bool equal = false;
        if (scenario == "wall") {
            // The source trace dives from dry ground before reaching its wall,
            // then stays squid after landing. Native Shift now waits for the
            // own-ink wall and surfaces on dry ground; the focused check below
            // covers that full native trajectory.
            equal=close(s.yaw,f.yaw)&&close(s.hp,f.hp)&&close(s.ink,f.ink)&&jumpMatches;
        } else if (scenario == "grate") {
            // Dry Shift must not enter squid form to drop through a grate.
            equal=close(s.pos.x,f.x)&&close(s.pos.y,0.0)&&close(s.pos.z,f.z)&&
                close(s.vel.x,0.0)&&close(s.vel.y,0.0)&&close(s.vel.z,0.0)&&close(s.yaw,f.yaw)&&
                close(s.hp,f.hp)&&close(s.ink,f.ink)&&s.grounded&&!s.squid&&!s.submerged&&!s.climbing&&jumpMatches;
        } else {
            equal=close(s.pos.x,expected->x)&&close(s.pos.y,expected->y)&&close(s.pos.z,expected->z)&&
                close(s.vel.x,expected->vx)&&close(s.vel.y,expected->vy)&&close(s.vel.z,expected->vz)&&close(s.yaw,expected->yaw)&&
                close(s.hp,expected->hp)&&close(s.ink,expected->ink)&&s.grounded==bool(expected->ground)&&s.squid==bool(expected->squid)&&
                s.submerged==bool(expected->submerged)&&s.climbing==bool(expected->climbing)&&jumpMatches;
        }
        if(!equal) {
            if (oracleFailures++ < 12) {
                if (scenario == "wall")
                    std::cerr << "Actor wall frame " << f.frame << ": native wall override changed yaw/hp/ink or jump state\n";
                else if (scenario == "grate")
                    std::cerr << "Actor grate frame " << f.frame << ": expected grounded kid at y=0; got y=" << s.pos.y
                              << " grounded=" << s.grounded << " squid=" << s.squid << '\n';
                else
                    std::cerr<<"Actor "<<scenario<<" frame "<<f.frame<<": pos "<<s.pos.x<<','<<s.pos.y<<','<<s.pos.z<<" expected "<<expected->x<<','<<expected->y<<','<<expected->z
                        <<" vel "<<s.vel.x<<','<<s.vel.y<<','<<s.vel.z<<" expected "<<expected->vx<<','<<expected->vy<<','<<expected->vz
                        <<" yaw/hp/ink "<<s.yaw<<'/'<<s.hp<<'/'<<s.ink<<" expected "<<expected->yaw<<'/'<<expected->hp<<'/'<<expected->ink<<'\n';
            }
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
    check(!player.state.squid && player.state.groundTeam == 0 &&
          near(std::hypot(player.state.vel.x, player.state.vel.z), 6.0),
          "holding Shift on dry ground must keep the actor in kid form and at kid run speed");
    player.spawnAt({0, 0, 0}, 0);
    paint.splat({0, 0.1, 0}, 2.0, 0, 0.3);
    player.input.squid = true;
    player.input.move = {0, 0, 1};
    for (int i = 0; i < 25; ++i) player.tick(dt);
    check(player.state.submerged && player.state.vel.z > 8.0, "own ink enables swim acceleration");

    paint.clear();
    paint.splat({0, 0.1, 0}, 2.0, 1, 0.3);
    player.spawnAt({0, 0, 0}, 0);
    player.input.squid = true;
    player.input.move = {0, 0, 1};
    for (int i = 0; i < 120; ++i) player.tick(dt);
    check(!player.state.squid && !player.state.submerged && player.state.groundTeam == 2 &&
          std::hypot(player.state.vel.x, player.state.vel.z) <= 1.91,
          "holding Shift on enemy ink must stay kid while preserving enemy-ink movement slowdown");

    StageData wallStage = makeStage();
    wallStage.faces[0].paintable = false;
    StageGeometry wallGeometry(wallStage);
    FacePaint wallPaint(wallStage, wallGeometry, 2048);
    wallPaint.splat({0, 1.0, 7.8}, 10.0, 0, 0.2);
    PlayerSimulation climber(wallStage, wallGeometry, wallPaint);
    climber.spawnAt({0, 0, 7.0}, 0);
    climber.input.squid = true;
    climber.input.move = {0, 0, 1};
    bool startedAsKidOnDryGround = false, sawInkWallClimb = false;
    bool carriedSquidAcrossTheClimbJump = false, surfacedOnDryLanding = false;
    for (int i = 0; i < 120; ++i) {
        climber.tick(dt);
        if (i == 0) startedAsKidOnDryGround = !climber.state.squid && climber.state.grounded && climber.state.groundTeam == 0;
        sawInkWallClimb = sawInkWallClimb || climber.state.climbing;
        if (sawInkWallClimb && !climber.state.grounded && climber.state.squid)
            carriedSquidAcrossTheClimbJump = true;
        if (sawInkWallClimb && climber.state.grounded && climber.state.groundTeam == 0 && !climber.state.squid)
            surfacedOnDryLanding = true;
    }
    check(startedAsKidOnDryGround && sawInkWallClimb && carriedSquidAcrossTheClimbJump && surfacedOnDryLanding,
          "dry Shift waits for the own-ink wall, climbs it, then surfaces on dry ground after landing");

    StageData grateStage = makeStage();
    grateStage.blocks[0].grate = true;
    StageGeometry grateGeometry(grateStage);
    FacePaint gratePaint(grateStage, grateGeometry, 2048);
    PlayerSimulation grateWalker(grateStage, grateGeometry, gratePaint);
    grateWalker.input.squid = true;
    bool stayedKidOnGrate = true;
    for (int i = 0; i < 120; ++i) {
        grateWalker.tick(dt);
        stayedKidOnGrate = stayedKidOnGrate && grateWalker.state.grounded && !grateWalker.state.squid &&
                           near(grateWalker.state.pos.y, 0.0);
    }
    check(stayedKidOnGrate, "dry Shift on a grate must stay kid and must not fall through");

    paint.clear();
    paint.splat({0, 0.1, 0}, 2.0, 0, 0.3);

    // Source actor.js records input press times, lets the most recent Swim/Fire
    // press win, and carries the fire buffer across the kid emerge delay.
    player.spawnAt({0, 0, 0}, 0);
    player.input.squid = true;
    player.tick(dt);
    check(player.state.squid && player.state.submerged,"Swim press did not enter submerged state on own ink");
    player.input.fire = true;
    player.tick(dt);
    check(!player.state.squid && !player.state.submerged,"Later Fire press did not win over held Swim");
    for (int i = 0; i < 12; ++i) player.tick(dt);
    check(player.shotsFired() > 0,"Fire press during emergence was not buffered into a shot");

    player.spawnAt({0, 0, 0}, 0);
    player.input.fire = true;
    player.tick(dt);
    player.input.squid = true;
    player.tick(dt);
    check(player.state.squid && player.state.submerged,"Later Swim press did not win while Fire stayed held");

    paint.clear();
    player.spawnAt({0, 0, 0}, 0);
    player.input.squid = true;
    player.input.jump = true;
    player.tick(dt);
    check(!player.state.grounded && !player.state.squid && player.state.vel.y > 8.0,
          "dry Shift stays kid without suppressing the source 8.4 m/s jump impulse");
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
    player.state.invuln=0;const double beforeInvalidDamage=player.state.hp;
    check(!player.damage(std::numeric_limits<double>::quiet_NaN())&&player.state.hp==beforeInvalidDamage,
          "source damage filter rejects NaN without poisoning health");
    player.state.smoothY=std::numeric_limits<double>::quiet_NaN();player.tick(dt);
    check(std::isfinite(player.state.pos.y+player.state.smoothY+player.state.vel.y)&&player.state.alive,
          "source safety net recovers invalid visual height before weapon updates");
    if (failures) return 1;
    std::cout << "Player movement, vitals and Spritzer paint passed\n";
}
