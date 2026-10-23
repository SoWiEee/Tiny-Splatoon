#include "../src/game/PlayerSimulation.hpp"
#include "../src/game/ArsenalData.hpp"
#include "fixtures/controller_fixtures.hpp"
#include "fixtures/brolly_source_fixtures.hpp"
#include "fixtures/main_kit_source_fixtures.hpp"
#include "fixtures/main_weapon_source_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <glm/geometric.hpp>

using namespace restoration;

namespace {
void check(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void runTracerContact(double sideOffset,double expectedDamage,double expectedMark,
                      bool ownerLocal=false,bool victimLocal=false) {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),victim(stage,geometry,paint,1,&world);
    owner.setLoadout("brolly");owner.state.pos={0,0,0};owner.state.invuln=0;owner.input.aimYaw=0;owner.input.aimPitch=0;
    victim.state.pos={sideOffset,0,4};victim.state.invuln=0;
    owner.setLocal(ownerLocal);victim.setLocal(victimLocal);

    // Source fixture: config.js SUBS.tracer directDamage=35/directMark=9 and
    // trailDamage=22/trailMark=3; tracer.js tests the direct capsule before its
    // one-second trail capsule and applies each mark at most once per bolt.
    world.deploy(owner,"tracer");
    for(int i=0;i<30 && victim.state.hp>100-expectedDamage;++i) world.tick(1.0/60.0);
    check(std::abs(victim.state.hp-(100-expectedDamage))<1e-8,"Tracer hit damage diverged from the source fixture");
    check(std::abs(victim.state.track-expectedMark)<1e-8 && victim.state.trackTeam==owner.team(),"Tracer mark duration/team diverged from the source fixture");
    const auto events=world.takeVisualEvents();
    const auto hitEvents=std::count_if(events.begin(),events.end(),[&](const auto& event) {
        return event.kind=="device_hit" && (event.source=="tracer" || event.source=="tracer_trail");
    });
    const auto hit=std::find_if(events.begin(),events.end(),[&](const auto& event) {
        return event.kind=="device_hit" && (event.source=="tracer" || event.source=="tracer_trail");
    });
    const std::string expectedSource=expectedDamage==35.0?"tracer":"tracer_trail";
    check(hitEvents==1 && hit!=events.end() && hit->source==expectedSource,
          "Tracer direct/trail hit must emit exactly one distinct source audio event per contact");
    if(hit==events.end())return;
    check(hit->sourceLocal==ownerLocal && hit->sourceVictimLocal==victimLocal,
          "Tracer hit audio must preserve the local owner/victim playback gates");
    check(glm::distance(hit->pos,victim.visualPos())<2.0,
          "Tracer contact FX must retain the world hit position even for a local victim; GameAudio applies nonspatial playback separately");
}

void checkBrollyConfig() {
    const auto& source=test::brollySourceFixture;
    const auto& native=GetEquipment(EquipmentCategory::Main,"brolly");
    const double values[]={source.canopyHp,source.openDelay,source.openTime,source.closeTime,source.launchHold,source.regrowTime,
        source.canopyRegen,source.canopyRegenDelay,source.launchSpeed,source.launchLife,source.launchPaint};
    const char* names[]={"canopyHp","openDelay","openTime","closeTime","launchHold","regrowTime","canopyRegen",
        "canopyRegenDelay","launchSpeed","launchLife","launchPaint"};
    for(std::size_t i=0;i<std::size(values);++i)
        check(std::abs(native.number(names[i])-values[i])<1e-12,"Native Brolly tuning diverged from the source-generated fixture");
}

void runBrollyShield() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation canopy(stage,geometry,paint,0,&world),ally(stage,geometry,paint,0,&world),attacker(stage,geometry,paint,1,&world);
    canopy.setLoadout("brolly");canopy.state.pos={0,0,0};canopy.state.grounded=true;canopy.state.invuln=0;canopy.input.aimYaw=0;canopy.input.aimPitch=0;
    ally.state.pos={0,0,-2};ally.state.invuln=0;
    attacker.state.pos={8,0,4};attacker.state.invuln=0;
    const double dt=1.0/60.0;
    const double fireStarted=world.time();
    canopy.input.fire=true;
    for(int i=0;i<30 && world.shields().empty();++i){canopy.tick(dt);world.tick(dt);}
    auto held=world.shields();
    check(held.size()==1 && held[0].held && held[0].live,"Source threshold did not expose the held Brolly shield");
    const auto shieldId=held[0].id;
    const auto& source=test::brollySourceFixture;
    const double pitch=std::clamp(canopy.input.aimPitch,source.pitchMin,source.pitchMax);
    const glm::dvec3 expectedNormal(std::sin(canopy.input.aimYaw)*std::cos(pitch),std::sin(pitch),std::cos(canopy.input.aimYaw)*std::cos(pitch));
    check(glm::length(held[0].N-expectedNormal)<1e-10 && std::abs(held[0].R-source.heldRadius)<1e-10,"Held shield pose or radius diverged from source geometry");
    const double heldHp=held[0].hp;
    check(world.blockShot(held[0].C+held[0].N*3.0,held[0].C-held[0].N*3.0,1,20),"Enemy shot crossed a held canopy");
    held=world.shields();
    check(held.size()==1 && held[0].id==shieldId && std::abs(held[0].hp-(heldHp-20))<1e-10,"Held canopy damage or identity diverged from source behavior");
    check(std::abs(canopy.combatPose().canopyFlash-31.0/70.0)<1e-10 &&
          std::abs(canopy.combatPose().canopyShake-5.0/12.0)<1e-10,
          "Held canopy did not forward the source 20-damage flash and shake to its pose");

    // The source bomb:explode hook guards teammates behind a friendly canopy and applies its quadratic blast damage.
    const auto blast=held[0].C+held[0].N*4.0;
    const double radius=8.0,dist=glm::length(held[0].C-blast),d=std::max(0.0,dist-held[0].R*0.5);
    const double k=1.0-std::clamp((d-0.8)/std::max(0.1,radius-0.8),0.0,1.0);
    const double shieldDamage=35.0+(180.0-35.0)*k*k;
    const double allyHp=ally.state.hp,canopyHp=canopy.state.hp;
    world.burst(attacker,blast,radius,100,30,1.5,"bomb");
    held=world.shields();
    check(std::abs(held[0].hp-(heldHp-20-shieldDamage))<1e-8,"Brolly blast soak differs from source formula");
    check(ally.state.hp==allyHp && canopy.state.hp==canopyHp,"Friendly canopy failed to guard a teammate from a blast");

    auto hasLaunchedShield=[&] {const auto current=world.shields();return std::any_of(current.begin(),current.end(),[](const auto& s){return !s.held;});};
    for(int i=0;i<180 && !hasLaunchedShield();++i) {
        canopy.tick(dt);world.tick(dt);
    }
    auto launched=world.shields();
    const auto shield=std::find_if(launched.begin(),launched.end(),[](const auto& s){return !s.held;});
    check(shield!=launched.end() && shield->blocksActors && shield->live,"Held canopy did not launch into a blocking world shield");
    const auto launchedId=shield->id;
    const double launchTime=world.time()-fireStarted;
    check(std::abs(launchTime-(source.openDelay+source.openTime+source.launchHold))<2.5*dt,"Canopy launch timing diverged from source hold/open timings");
    check(std::abs(shield->planeOff-source.planeOffset)<1e-10 && std::abs(shield->R-source.launchR)<1e-10,"Launched shield plane/radius diverged from source model geometry");
    const double before=shield->hp;
    const double distance=world.blockRay(shield->C+shield->N*2.0,-shield->N,4.0,1,10.0);
    launched=world.shields();
    const auto after=std::find_if(launched.begin(),launched.end(),[&](const auto& s){return s.id==launchedId;});
    check(distance<4.0 && after!=launched.end() && std::abs(after->hp-(before-10.0))<1e-10,"Launched shield failed the source ray-blocking/damage contract");
}

void runPlacementOrder() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world);owner.setLoadout("shooter","mine","storm");owner.state.pos={0,0,0};owner.state.grounded=true;
    world.deploy(owner,"mine");
    check(world.devices().size()==1 && world.devices()[0].live,"First placed mine was not deployed on the source floor");
    const auto armed=world.takeVisualEvents();
    check(std::count_if(armed.begin(),armed.end(),[&owner](const auto& event){return event.kind=="device_arm"&&event.source=="mine"&&event.team==owner.team();})==1,
          "Placed mine did not emit exactly one source arm cue");
    owner.state.pos={20,20,20};
    world.deploy(owner,"mine");
    check(world.devices().size()==1 && world.devices()[0].live,"Invalid mine placement replaced the existing mine before surface validation");
    check(world.takeVisualEvents().empty(),"Rejected mine placement emitted an arm cue");
}

void runBombFuseDetonation(bool squid) {
    // Source contract: splat-ref/src/config.js SUBS.bomb fuse/radius/damage curve;
    // bomb.js arms on floor contact, then its own fuse tick triggers the blast.
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","bomb","storm");owner.state.pos={0,0,0};owner.state.grounded=true;
    owner.input.aimYaw=0;owner.input.aimPitch=0;owner.setRandomSource([]{return 0.5;});
    enemy.state.pos={20,0,20};enemy.state.hp=500;enemy.state.invuln=0;enemy.state.squid=squid;
    const auto& tuning=GetEquipment(EquipmentCategory::Sub,"bomb");
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"bomb");
    check(world.devices().size()==1&&world.devices()[0].kind=="bomb"&&world.devices()[0].fuse<0,
          "Splat Bomb did not begin in its source airborne/unarmed phase");
    const auto bombId=world.devices()[0].id;
    const auto bomb=[&]() -> const CombatDevice* {
        const auto it=std::find_if(world.devices().begin(),world.devices().end(),[&](const auto& device){return device.id==bombId;});
        return it==world.devices().end()?nullptr:&*it;
    };

    int frames=0;
    while(frames<120) {
        const auto* current=bomb();
        if(!current||current->fuse>=0)break;
        world.tick(dt);++frames;
    }
    const auto* armed=bomb();
    check(armed&&armed->fuse>=0&&armed->fuse<tuning.number("fuse"),
          "Splat Bomb did not arm and begin counting down on floor contact");
    if(!armed)return;
    check(std::abs((tuning.number("fuse")-dt)-armed->fuse)<1e-9,
          "Splat Bomb floor contact did not consume exactly its current source tick from the fuse");

    std::vector<WorldVisualEvent> detonationEvents;
    for(int i=0;i<240;++i) {
        const auto* current=bomb();
        if(!current)break;
        // Place the controlled target beyond the source's 0.8 m damage core, so
        // the final blast verifies its distance falloff as well as hit application.
        if(current->fuse<=3*dt)enemy.state.pos=current->pos+glm::dvec3(1.6,-0.4,0);
        world.tick(dt);
        auto events=world.takeVisualEvents();
        detonationEvents.insert(detonationEvents.end(),events.begin(),events.end());
    }
    check(!bomb(),"Splat Bomb did not detonate and clean up when its source fuse expired");
    const auto blast=std::find_if(detonationEvents.begin(),detonationEvents.end(),[](const auto& event){return event.kind=="explosion"&&event.source=="bomb";});
    check(std::count_if(detonationEvents.begin(),detonationEvents.end(),[](const auto& event){return event.kind=="explosion"&&event.source=="bomb";})==1,
          "Splat Bomb fuse expiration did not emit exactly one source-tagged blast");
    check(blast!=detonationEvents.end()&&std::abs(blast->radius-tuning.number("radius"))<1e-9,
          "Splat Bomb blast event radius diverged from its source tuning");
    if(blast==detonationEvents.end())return;
    const auto center=blast->pos;
    const auto target=enemy.state.pos+glm::dvec3(0,0.7,0);
    const double distance=glm::length(target-center),radius=tuning.number("radius");
    check(distance<=radius,"Splat Bomb target fixture drifted outside the source blast radius");
    const double k=1.0-std::clamp((distance-0.8)/(radius-0.8),0.0,1.0);
    const double expected=tuning.number("damageMin")+(tuning.number("damageMax")-tuning.number("damageMin"))*k*k;
    const double actual=500.0-enemy.state.hp;
    if(std::abs(actual-expected)>=1e-8)
        std::cerr<<"Splat Bomb damage diagnostic: source distance="<<distance<<", expected="<<expected<<", actual="<<actual
                 <<", blast=("<<blast->pos.x<<","<<blast->pos.y<<","<<blast->pos.z<<"), target=("
                 <<target.x<<","<<target.y<<","<<target.z<<")\n";
    check(std::abs(actual-expected)<1e-8,squid
          ?"Splat Bomb fuse blast did not apply the source quadratic damage curve to a squid target"
          :"Splat Bomb fuse blast did not apply the source quadratic damage curve to a standing target");
}

void runDeviceBlastTargetHeight(const std::string& kind,bool squid) {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(-10,0,-10),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    if(kind=="torpedo")stage.blocks.push_back(controller_fixtures::syntheticBlocks[2]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter",kind,"storm");owner.state.pos={-10,0,-10};owner.state.grounded=true;
    enemy.state.pos=kind=="torpedo"?glm::dvec3(2.8,0.5,1.0):glm::dvec3(0,0,2);
    enemy.state.hp=500;enemy.state.invuln=0;enemy.state.squid=squid;
    if(kind=="torpedo") {enemy.state.submerged=squid;enemy.state.smoothY=0.2;}
    const auto& tuning=GetEquipment(EquipmentCategory::Sub,kind);
    constexpr double dt=1.0/60.0;
    world.deploy(owner,kind);
    check(world.devices().size()==1&&world.devices()[0].kind==kind,"Blast target fixture did not deploy its requested sub");
    const auto deviceId=world.devices()[0].id;
    const auto device=[&]() -> const CombatDevice* {
        const auto it=std::find_if(world.devices().begin(),world.devices().end(),[&](const auto& d){return d.id==deviceId;});
        return it==world.devices().end()?nullptr:&*it;
    };
    auto& active=const_cast<CombatDevice&>(world.devices()[0]);
    active.pos=kind=="torpedo"?glm::dvec3(0,2,0):glm::dvec3(0,0.8,0);active.vel={0,0,0};active.age=0;
    if(kind=="sticky") {
        active.state="stuck";active.normal={0,1,0};active.timer=dt;
    } else if(kind=="seeker") {
        active.state="run";active.age=tuning.number("life");active.target=&enemy;active.heading=0;
    } else if(kind=="torpedo") {
        // Hit the tall fixture wall before armTime so the torpedo bursts on
        // contact instead of locking onto the standing target and unfolding.
        active.state="fly";active.vel={300,0,0};
    } else if(kind=="boomerang") {
        active.state="armed";active.timer=dt;active.walkTime=1.0;active.target=&enemy;
    } else {
        check(false,"Blast target fixture requested an unsupported sub kind");return;
    }
    world.tick(dt);
    const auto events=world.takeVisualEvents();
    const auto blast=std::find_if(events.begin(),events.end(),[&](const auto& e){return e.kind=="explosion"&&e.source==kind;});
    const auto fixtureName=kind+std::string(squid?" squid":" standing");
    check(!device(),"Blast target fixture ("+fixtureName+") remained after the detonation tick");
    check(blast!=events.end(),"Blast target fixture ("+fixtureName+") did not emit an explosion on its tick");
    check(std::count_if(events.begin(),events.end(),[&](const auto& e){return e.kind=="explosion"&&e.source==kind;})==1,
          "Blast target fixture emitted other than one source-tagged explosion");
    if(blast==events.end())return;

    double radius=tuning.number("radius"),maxDamage=tuning.number("damageMax"),minDamage=tuning.number("damageMin");
    if(kind=="boomerang") {
        radius=tuning.number("hitRadius");maxDamage=tuning.number("hitDamageMax");minDamage=tuning.number("hitDamageMin");
    }
    const glm::dvec3 sourceCenter=kind=="seeker"?blast->pos+glm::dvec3(0,0.2,0):blast->pos;
    double targetHeight=squid?0.3:0.7;
    if(kind=="sticky"||kind=="seeker")targetHeight=0.7;
    else if(kind=="torpedo")targetHeight=enemy.state.smoothY+(squid?0.3:0.8);
    else if(kind=="boomerang")targetHeight=squid?0.3:0.75;
    const auto target=enemy.state.pos+glm::dvec3(0,targetHeight,0);
    const double distance=glm::length(target-sourceCenter);
    check(distance<=radius,"Blast target fixture ("+fixtureName+") drifted outside the source radius");
    double expected=0.0;
    if(kind=="torpedo") {
        const double core=tuning.number("coreRadius");
        expected=distance<=core?maxDamage:maxDamage+(minDamage-maxDamage)*std::clamp((distance-core)/(radius-core),0.0,1.0);
    } else {
        const double k=1.0-std::clamp((distance-0.8)/(radius-0.8),0.0,1.0);
        expected=minDamage+(maxDamage-minDamage)*k*k;
    }
    const double actual=500.0-enemy.state.hp;
    if(std::abs(actual-expected)>=1e-8)
        std::cerr<<"Sub blast diagnostic ("<<fixtureName<<"): distance="<<distance<<", expected="<<expected<<", actual="<<actual<<"\n";
    check(std::abs(actual-expected)<1e-8,"Sub blast target height/center differed from its source hit fixture ("+fixtureName+")");
}

void runWaddleTickDetonation() {
    // Source contract: splat-ref/src/config.js SUBS.waddle and bomb.js's
    // landed sense → wake → walk → trigger-distance explosion state machine.
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("bow","waddle","strike");owner.state.pos={0,0,0};owner.state.grounded=true;
    owner.input.aimYaw=0;owner.input.aimPitch=0;owner.setRandomSource([]{return 0.5;});
    enemy.state.pos={20,0,20};enemy.state.hp=500;enemy.state.invuln=0;
    const auto& tuning=GetEquipment(EquipmentCategory::Sub,"waddle");
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"waddle");
    check(world.devices().size()==1&&world.devices()[0].state=="fly",
          "Waddle Bomb did not begin in its source thrown phase");
    const auto waddleId=world.devices()[0].id;
    const auto waddle=[&]() -> const CombatDevice* {
        const auto it=std::find_if(world.devices().begin(),world.devices().end(),[&](const auto& device){return device.id==waddleId;});
        return it==world.devices().end()?nullptr:&*it;
    };
    int frames=0;
    while(frames<120) {
        const auto* current=waddle();
        if(!current||current->state=="sense")break;
        world.tick(dt);++frames;
    }
    const auto* sensed=waddle();
    check(sensed&&sensed->state=="sense"&&sensed->grounded,
          "Waddle Bomb did not reach its grounded sensing phase through simulation ticks");
    if(!sensed)return;

    const double scanDistance=std::max(tuning.number("triggerDist")+0.5,2.0);
    check(scanDistance<tuning.number("senseRadius"),"Waddle source tuning leaves no separation between scan and detonation fixture");
    enemy.state.pos=sensed->pos+glm::dvec3(0,0,scanDistance);
    for(frames=0;frames<180;++frames) {
        const auto* current=waddle();
        if(!current||current->state=="wake")break;
        world.tick(dt);
    }
    const auto* waking=waddle();
    check(waking&&waking->state=="wake"&&waking->target==&enemy,
          "Waddle Bomb failed to acquire an in-range enemy during its source sensing ticks");
    if(!waking)return;
    for(frames=0;frames<40;++frames) {
        const auto* current=waddle();
        if(!current||current->state=="walk")break;
        world.tick(dt);
    }
    const auto* walking=waddle();
    check(walking&&walking->state=="walk"&&walking->target==&enemy,
          "Waddle Bomb did not complete its source wake delay and enter the walk phase");
    if(!walking)return;

    // Match the actor chest to the source proximity trigger without bypassing any
    // device state: the following world tick must execute the real detonation.
    enemy.state.pos=walking->pos-glm::dvec3(0,0.45,0);
    world.tick(dt);
    const auto events=world.takeVisualEvents();
    const auto blast=std::find_if(events.begin(),events.end(),[](const auto& event){return event.kind=="explosion"&&event.source=="waddle";});
    check(!waddle()&&blast!=events.end(),"Waddle Bomb proximity trigger did not detonate through its simulation tick");
    check(std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="explosion"&&event.source=="waddle";})==1,
          "Waddle Bomb proximity trigger emitted other than one source explosion");
    check(blast!=events.end()&&std::abs(blast->radius-tuning.number("radius"))<1e-9,
          "Waddle Bomb blast event radius diverged from its source tuning");
    check(std::abs(enemy.state.hp-(500.0-tuning.number("damageMax")))<1e-8,
          "Waddle Bomb proximity blast did not apply source maximum damage at contact range");
}

void runDeviceWarningLifecycle() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","mine","storm");owner.state.pos={0,0,0};owner.state.grounded=true;
    enemy.state.pos={20,0,20};enemy.state.invuln=0;
    const auto& mine=GetEquipment(EquipmentCategory::Sub,"mine");
    const double dt=1.0/60.0;
    world.deploy(owner,"mine");
    const auto armEvents=world.takeVisualEvents();
    check(armEvents.size()==1 && armEvents[0].kind=="device_arm"&&armEvents[0].source=="mine",
          "Mine lifecycle did not start with one arm cue");
    const auto mineId=world.devices()[0].id;
    const auto mineDevice=[&]() -> const CombatDevice* {
        const auto it=std::find_if(world.devices().begin(),world.devices().end(),[&](const auto& device){return device.id==mineId;});
        return it==world.devices().end()?nullptr:&*it;
    };
    world.tick(mine.number("armTime")+dt);
    const auto* activeMine=mineDevice();
    check(activeMine&&activeMine->state=="mine"&&activeMine->age>=mine.number("armTime"),
          "Mine did not become active after its source arm delay");
    if(!activeMine)return;
    check(world.takeVisualEvents().empty(),"Untriggered active mine emitted a warning");

    const double ageBeforeTrigger=activeMine->age;
    const double hpBeforeMine=enemy.state.hp;
    enemy.state.pos=activeMine->pos;
    world.tick(dt);
    const auto warningEvents=world.takeVisualEvents();
    check(warningEvents.size()==1 && warningEvents[0].kind=="device_warning"&&warningEvents[0].source=="mine"
          && warningEvents[0].team==owner.team(),"Triggered mine did not emit one source warning cue");
    const auto* triggeredMine=mineDevice();
    check(triggeredMine&&triggeredMine->fuse==mine.number("delay")&&triggeredMine->age>ageBeforeTrigger,
          "Mine warning reset its source lifetime clock or failed to enter its delay state");

    std::vector<WorldVisualEvent> detonationEvents;
    for(int i=0;i<static_cast<int>(std::ceil(mine.number("delay")/dt))+3;++i) {
        if(!mineDevice())break;
        world.tick(dt);
        auto events=world.takeVisualEvents();
        detonationEvents.insert(detonationEvents.end(),events.begin(),events.end());
    }
    check(!mineDevice(),"Triggered mine did not clean up after its source fuse");
    check(std::count_if(detonationEvents.begin(),detonationEvents.end(),[](const auto& event){return event.kind=="explosion"&&event.source=="mine";})==1,
          "Mine detonation did not emit exactly one source explosion cue");
    check(std::abs(enemy.state.hp-(hpBeforeMine-mine.number("damage")))<1e-8&&
          enemy.state.track==mine.number("trackTime")&&enemy.state.trackTeam==owner.team(),
          "Mine tick detonation did not apply its source damage and enemy tracking effect");

    PlayerSimulation stickyOwner(stage,geometry,paint,0,&world);
    stickyOwner.setLoadout("shooter","sticky","storm");stickyOwner.state.pos={-5,0,0};stickyOwner.state.grounded=true;
    world.deploy(stickyOwner,"sticky");
    std::vector<WorldVisualEvent> stickyEvents;
    int ticks=0;
    while(ticks<240 && (world.devices().empty() || world.devices().back().state!="stuck")) {
        world.tick(dt);++ticks;
        auto events=world.takeVisualEvents();
        stickyEvents.insert(stickyEvents.end(),events.begin(),events.end());
    }
    check(!world.devices().empty()&&world.devices().back().state=="stuck","Sticky charge did not reach its source stuck phase");
    check(std::count_if(stickyEvents.begin(),stickyEvents.end(),[](const auto& event){return event.kind=="device_arm"&&event.source=="sticky";})==1,
          "Stuck sticky charge did not emit one arm cue");
    for(int i=0;i<90 && std::count_if(stickyEvents.begin(),stickyEvents.end(),[](const auto& event){return event.kind=="device_warning"&&event.source=="sticky";})<2;++i) {
        world.tick(dt);
        auto events=world.takeVisualEvents();
        stickyEvents.insert(stickyEvents.end(),events.begin(),events.end());
    }
    std::vector<double> warningProgress;
    for(const auto& event:stickyEvents)if(event.kind=="device_warning"&&event.source=="sticky")warningProgress.push_back(event.charge);
    check(warningProgress.size()>=2&&warningProgress[0]<warningProgress[1]&&warningProgress.back()<=1.0,
          "Sticky fuse warnings did not report increasing source fuse progress");
}

void runBoomerangContactFuse() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),victim(stage,geometry,paint,1,&world);
    owner.setLoadout("mitts","boomerang","slam");owner.state.pos={0,0,0};owner.state.invuln=0;owner.input.aimYaw=0;
    victim.state.pos={0,0,3};victim.state.invuln=0;
    const auto& source=GetEquipment(EquipmentCategory::Sub,"boomerang");
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"boomerang");
    for(int i=0;i<60&&world.devices().front().state!="armed";++i)world.tick(dt);
    const auto& device=world.devices().front();
    check(device.state=="armed"&&device.target==&victim&&std::abs(device.timer-source.number("hitFuse"))<1e-10,
          "Boomerang contact did not enter the source armed fuse state");
    check(victim.state.hp<100.0,"Boomerang contact did not apply the source arm hit");
    std::vector<WorldVisualEvent> events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_arm"&&e.source=="boomerang";})==1,
          "Boomerang contact did not emit one visual arm transition");
    const auto initialWarning=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_warning"&&e.source=="boomerang";});
    check(initialWarning!=events.end()&&std::abs(initialWarning->charge)<1e-12,
          "Boomerang arm beep did not report its initial fuse progress");
    for(int i=0;i<static_cast<int>(std::ceil(source.number("hitFuse")/dt))+3&&device.live;++i) {
        world.tick(dt);auto more=world.takeVisualEvents();events.insert(events.end(),more.begin(),more.end());
    }
    const auto warnings=std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_warning"&&e.source=="boomerang";});
    const auto blast=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind=="explosion"&&e.source=="boomerang";});
    check(!device.live&&warnings>=2&&blast!=events.end()&&std::abs(blast->charge-1.0)<1e-12
          &&std::abs(blast->radius-source.number("hitRadius"))<1e-10,
          "Boomerang did not warn through its source fuse and finish with the full hit blast");
}

void runDeviceRayDestroyCue() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world);owner.setLoadout("shooter","torpedo","storm");owner.state.pos={0,0,0};
    world.deploy(owner,"torpedo");
    const auto device=world.devices().front();
    world.blockRay(device.pos-glm::dvec3(0,0,1),{0,0,1},2.0,1,1000.0);
    const auto events=world.takeVisualEvents();
    check(!world.devices().front().live&&events.size()==1&&events[0].kind=="device_destroy"
          &&events[0].source=="torpedo"&&events[0].team==owner.team(),
          "Ray-destroyed torpedo did not emit one source-tagged device destroy event");
}

void runBrollyMainShot() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("brolly");owner.state.pos={0,0,0};owner.state.grounded=true;owner.input.aimYaw=0;owner.input.aimPitch=0;
    owner.setRandomSource([]{return 0.5;});enemy.state.pos={0,0,4};enemy.state.grounded=true;enemy.state.invuln=0;
    constexpr double dt=1.0/60.0;
    for(int i=0;i<24;++i){owner.tick(dt);world.tick(dt);}
    owner.input.fire=true;owner.tick(dt);
    const auto pellets=std::count_if(world.projectiles().begin(),world.projectiles().end(),[](const auto& p){return p.weaponId=="brolly"&&p.kind=="shot";});
    check(GetEquipment(EquipmentCategory::Main,"brolly").number("pellets")==11.0&&owner.shotsFired()==1&&pellets==11,
          "Brolly main fire did not create one 11-pellet source volley");
    check(std::abs(owner.state.ink-(100.0-GetEquipment(EquipmentCategory::Main,"brolly").number("inkPerShot")))<1e-8,
          "Brolly main volley did not spend its source ink cost exactly once");
    owner.input.fire=false;
    for(int i=0;i<20&&enemy.state.hp>=100.0;++i)world.tick(dt);
    check(enemy.state.hp<100.0&&std::any_of(world.hudEvents().begin(),world.hudEvents().end(),[](const auto& e){return e.kind==CombatHudEvent::Kind::Hit&&e.source=="brolly";}),
          "Brolly source volley never connected with its centered target");
}

void runScanCloudLifecycle() {
    // Source contract: splat-ref/src/config.js SUBS.scan (fuse 1.1, radius 4.2, cloudTime 0.9, trackTime 8).
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","scan","storm");owner.state.pos={0,0,0};owner.input.aimYaw=0;owner.input.aimPitch=0;
    enemy.state.hp=100;enemy.state.invuln=0;
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"scan");
    int frames=0;
    while(frames<120&&!world.devices().empty()&&world.devices()[0].state!="cloud"){world.tick(dt);++frames;}
    check(!world.devices().empty()&&world.devices()[0].state=="cloud","Echo Orb did not transition from its throw into a sensing cloud");
    check(frames>0&&frames<70,"Echo Orb cloud started outside its source fuse/landing window");
    const auto cloudCenter=world.devices()[0].pos;
    enemy.state.pos=cloudCenter-glm::dvec3(0,0.7,0);
    for(int i=0;i<6;++i)world.tick(dt);
    check(enemy.state.track==8.0&&enemy.state.trackTeam==owner.team()&&enemy.state.hp==100,
          "Echo Orb cloud did not source-track an enemy without dealing damage");
    const auto events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_arm"&&e.source=="scan";})==1,
          "Echo Orb cloud transition did not emit exactly one arm cue");
    for(int i=0;i<90&& !world.devices().empty();++i)world.tick(dt);
    check(world.devices().empty(),"Echo Orb cloud survived beyond its source lifetime");
}

void runMistDirectPoisonLifecycle() {
    // Source contract: splat-ref/src/config.js SUBS.mist (direct poison 4.5 s; 12 ink/s; no HP damage).
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","mist","storm");owner.state.pos={0,0,0};owner.input.aimYaw=0;owner.input.aimPitch=0;
    enemy.state.pos={0,0,2};enemy.state.grounded=true;enemy.state.invuln=0;enemy.state.ink=50;
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"mist");
    int frames=0;
    while(frames<50&&!world.devices().empty()&&world.devices()[0].state!="mist"){world.tick(dt);++frames;}
    check(!world.devices().empty()&&world.devices()[0].state=="mist","Murk Bomb direct contact did not open its poison cloud");
    check(enemy.state.poison==4.5&&enemy.state.hp==100,"Murk Bomb direct hit must poison for the full source cloud lifetime without HP damage");
    enemy.tick(dt);
    check(std::abs(enemy.state.ink-(50.0-12.0*dt))<1e-8&&enemy.state.hp==100,
          "Murk Bomb poison did not drain ink at its source rate while preserving HP");
    enemy.state.pos={8,0,8};
    const auto events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_arm"&&e.source=="mist";})==1,
          "Murk Bomb direct contact did not emit exactly one cloud arm cue");
    for(int i=0;i<280&&!world.devices().empty();++i)world.tick(dt);
    check(world.devices().empty(),"Murk Bomb cloud did not clean up at its source lifetime");
}

void runCurtainBlockingLifecycle() {
    // Source contract: splat-ref/src/config.js SUBS.curtain (3.4 x 2.7, 170 HP, 19 HP/s, half damage per shot).
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),enemy(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","curtain","storm");owner.state.pos={0,0,0};owner.input.aimYaw=0;owner.input.aimPitch=0;
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"curtain");
    int frames=0;
    while(frames<120&&!world.devices().empty()&&world.devices()[0].state!="curtain"){world.tick(dt);++frames;}
    check(!world.devices().empty()&&world.devices()[0].state=="curtain"&&world.devices()[0].blocksActors,
          "Drip Curtain throw did not settle into its blocking wall state");
    auto& curtain=const_cast<CombatDevice&>(world.devices()[0]);
    const auto& source=GetEquipment(EquipmentCategory::Sub,"curtain");
    check(std::abs(curtain.halfW-source.number("width")*0.5)<1e-10&&std::abs(curtain.R-source.number("height"))<1e-10,
          "Drip Curtain collision dimensions differ from source tuning");
    auto events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_arm"&&e.source=="curtain";})==1,
          "Drip Curtain landing did not emit one source arm cue");
    world.tick(dt);
    check(std::abs(curtain.hp-(170.0-19.0*dt))<1e-8,"Drip Curtain did not fade at its source health decay rate");
    const auto from=curtain.pos-curtain.N+glm::dvec3(0,1,0),to=curtain.pos+curtain.N+glm::dvec3(0,1,0);
    check(world.blockShot(from,to,1,50.0),"Enemy shot crossed the Drip Curtain plane");
    check(std::abs(curtain.hp-(170.0-19.0*dt-25.0))<1e-8,
          "Drip Curtain shot damage did not use the source half-damage multiplier");
    enemy.state.pos=curtain.pos;enemy.state.vel={0,0,0};world.blockActor(enemy);
    check(glm::length(enemy.state.pos-curtain.pos)>0.4,"Enemy actor crossed the Drip Curtain collision plane");
    check(world.blockShot(from,to,1,300.0)&&!curtain.live,"Drip Curtain did not break when source-scaled shot damage depleted its HP");
    events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_destroy"&&e.source=="curtain";})==1,
          "Drip Curtain break did not emit one source-tagged destroy cue");
}

void runSprinklerPulseAndReplacement() {
    // Source contract: splat-ref/src/config.js SUBS.sprinkler (six drops every 0.3 s, one active per owner).
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world);owner.setLoadout("shooter","sprinkler","storm");
    owner.state.pos={0,0,0};owner.input.aimYaw=0;owner.input.aimPitch=0;owner.setRandomSource([]{return 0.5;});
    constexpr double dt=1.0/60.0;
    world.deploy(owner,"sprinkler");
    int frames=0;
    while(frames<120&&!world.devices().empty()&&world.devices()[0].state!="spray"){world.tick(dt);++frames;}
    check(!world.devices().empty()&&world.devices()[0].state=="spray","Twirl Sprinkler did not stick and enter its spraying state");
    auto events=world.takeVisualEvents();
    check(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="device_arm"&&e.source=="sprinkler";})==1,
          "Twirl Sprinkler landing did not emit one arm cue");
    std::set<std::uint64_t> dropIds;
    const auto captureDrops=[&]{
        for(const auto& p:world.projectiles())
            if(p.owner==&owner&&p.kind=="drop"&&p.weaponId=="sprinkler")dropIds.insert(p.id);
    };
    world.tick(dt);captureDrops();
    check(dropIds.size()==6,"Twirl Sprinkler first source pulse did not emit six owned drop projectiles");
    for(int i=0;i<16;++i){world.tick(dt);captureDrops();}
    check(dropIds.size()==6,"Twirl Sprinkler emitted its second pulse before the source interval");
    for(int i=0;i<10&&dropIds.size()==6;++i){world.tick(dt);captureDrops();}
    check(dropIds.size()==12,"Twirl Sprinkler failed to emit its second source-timed pulse");

    const auto firstId=world.devices().front().id;
    world.deploy(owner,"sprinkler");
    const auto secondSpraying=[&]{return std::find_if(world.devices().begin(),world.devices().end(),[&](const auto& d){return d.live&&d.id!=firstId&&d.kind=="sprinkler"&&d.state=="spray";});};
    for(int i=0;i<120&&secondSpraying()==world.devices().end();++i)world.tick(dt);
    check(secondSpraying()!=world.devices().end(),"Replacement Twirl Sprinkler did not land and start spraying");
    check(std::count_if(world.devices().begin(),world.devices().end(),[](const auto& d){return d.live&&d.kind=="sprinkler";})==1,
          "A newly landed Twirl Sprinkler did not replace its owner's existing one");
    world.cleanup(owner);
    check(std::none_of(world.devices().begin(),world.devices().end(),[](const auto& d){return d.kind=="sprinkler"&&d.live;}),
          "Owner cleanup left a Twirl Sprinkler alive after its source lifecycle ended");
}

void runSeekerCommit() {
    StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    stage.blocks.push_back(controller_fixtures::syntheticBlocks[0]);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation owner(stage,geometry,paint,0,&world),target(stage,geometry,paint,1,&world);
    owner.setLoadout("shooter","seeker","storm");owner.state.pos={0,0,0};owner.input.aimYaw=0;owner.input.aimPitch=0;
    target.state.pos={0,0,5};target.state.invuln=0;
    const auto& tuning=GetEquipment(EquipmentCategory::Sub,"seeker");
    world.deploy(owner,"seeker");
    const double dt=1.0/60.0;
    const CombatDevice* seeker=nullptr;
    for(int i=0;i<100;++i) {
        world.tick(dt);
        if(!world.devices().empty() && world.devices()[0].state=="run") {
            seeker=&world.devices()[0];
            if(seeker->dash)break;
        }
    }
    check(seeker && seeker->state=="run" && seeker->dash,"Seeker failed to commit once close and aligned with its target");
    check(std::abs(seeker->commitDist-tuning.number("commitDist"))<1e-12 && std::abs(seeker->creep-tuning.number("creep"))<1e-12,
          "Seeker runtime omitted its source commitment/creep tuning");

    // A committed seeker keeps its heading through a sidestep below 90 degrees,
    // then resumes bounded steering when the target crosses behind it.
    const double committedHeading=seeker->heading;
    target.state.pos={2,0,5};
    world.tick(dt);
    check(world.devices()[0].dash && std::abs(world.devices()[0].heading-committedHeading)<1e-12,
          "Committed Seeker steered before its target passed the source 90-degree break angle");
    target.state.pos={2,0,-2};
    const double beforeTurn=world.devices()[0].heading;
    world.tick(dt);
    check(!world.devices()[0].dash && std::abs(world.devices()[0].heading-beforeTurn)>1e-6,
          "Seeker did not resume steering after its target passed behind it");
}

struct BowRun {
    StageData stage;
    std::unique_ptr<StageGeometry> geometry;
    std::unique_ptr<FacePaint> paint;
    std::unique_ptr<CombatWorld> world;
    std::unique_ptr<PlayerSimulation> owner;
    std::unique_ptr<PlayerSimulation> target;
    BowRun(bool withTarget=false) {
        stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        auto floor=controller_fixtures::syntheticBlocks[0];floor.half={30,0.5,30};stage.blocks.push_back(floor);
        geometry=std::make_unique<StageGeometry>(stage);paint=std::make_unique<FacePaint>(stage,*geometry,2048);
        world=std::make_unique<CombatWorld>(stage,*geometry,*paint);
        owner=std::make_unique<PlayerSimulation>(stage,*geometry,*paint,0,world.get());owner->setLoadout("bow");
        owner->state.pos={0,0,0};owner->state.grounded=true;owner->state.invuln=0;owner->input.aimYaw=0;owner->input.aimPitch=0;
        if(withTarget) {
            target=std::make_unique<PlayerSimulation>(stage,*geometry,*paint,1,world.get());
            target->state.pos={0,0,10};target->state.invuln=0;
        }
    }
};

double chargeBow(BowRun& run,int frames) {
    constexpr double dt=1.0/60.0;
    run.owner->input.fire=true;
    for(int i=0;i<frames;++i) {run.owner->tick(dt);run.world->tick(dt);}
    const double chargeBeforeRelease=run.owner->combatPose().charge;
    check(run.owner->combatPose().charging,"Bow failed to keep its draw active while fire is held");
    run.owner->input.fire=false;
    run.owner->tick(dt);run.world->tick(dt);
    run.owner->tick(dt);run.world->tick(dt);
    check(run.owner->shotsFired()==1 && run.world->projectiles().size()==3,"Bow release did not loose exactly three arrows");
    const double expectedCharge=chargeBeforeRelease;
    const double inkCost=GetEquipment(EquipmentCategory::Main,"bow").number("inkFull")*
        std::max(GetEquipment(EquipmentCategory::Main,"bow").number("inkMin"),expectedCharge);
    check(std::abs(run.owner->state.ink-(100.0-inkCost))<1e-9,"Bow release ink cost diverged from source charge scaling");
    return expectedCharge;
}

void runBowTierOracle(int frames,int tier) {
    BowRun run;const double charge=chargeBow(run,frames);
    const auto& source=test::bowSourceFixture;const auto& native=GetEquipment(EquipmentCategory::Main,"bow");
    const auto& arrows=run.world->projectiles();
    double expectedRange=0,expectedSpeed=0,expectedDamage=0,expectedSide=0,expectedFan=0;
    bool lodge=false;
    if(tier==2) {expectedRange=source.flightFull;expectedSpeed=source.speedFull;expectedDamage=source.damageFull;expectedSide=source.sideFull;expectedFan=source.fanFull;lodge=true;}
    else if(tier==1) {
        const double u=(charge-source.ring1)/(1.0-source.ring1);
        expectedRange=source.flightRing0+(source.flightRing1-source.flightRing0)*u;
        expectedSpeed=source.speedRing0+(source.speedRing1-source.speedRing0)*u;
        expectedDamage=source.damageRing0+(source.damageRing1-source.damageRing0)*u;expectedSide=expectedDamage*source.sideMul;
        expectedFan=source.fanRing;lodge=true;
    } else {
        const double u=charge/source.ring1;
        expectedRange=source.flightTap0+(source.flightTap1-source.flightTap0)*u;expectedSpeed=source.speedTap;
        expectedDamage=source.damageTap0+(source.damageTap1-source.damageTap0)*u;expectedSide=expectedDamage*source.sideMul;expectedFan=source.fanTap;
    }
    check(std::abs(native.number("flightTap.0")-source.flightTap0)<1e-12,"Native Bow tap range differs from source-generated tuning");
    check(arrows.size()==3,"Bow source tier did not create a three-arrow volley");
    for(const auto& arrow:arrows) {
        check(arrow.kind=="arrow" && arrow.weaponId=="bow" && arrow.lodge==lodge,"Bow tier selected the wrong arrow lifecycle");
        check(std::abs(glm::length(arrow.vel)-expectedSpeed)<1e-8 && std::abs(arrow.range-expectedRange)<1e-8,
              "Bow tier speed or straight-flight range differs from the source shot function");
        const bool center=arrow.centerArrow;
        check(std::abs(arrow.damage-(center?expectedDamage:expectedSide))<1e-8,"Bow centre/side arrow damage differs from source tier");
        check(std::abs(std::acos(std::clamp(arrow.vel.z/glm::length(arrow.vel),-1.0,1.0))*180.0/3.14159265358979323846-(center?0.0:expectedFan))<0.05,
              "Grounded Bow fan angle differs from source tier");
    }
}

void runBowDirectAndLodgedBurst() {
    constexpr double dt=1.0/60.0;
    BowRun direct(true);chargeBow(direct,70);
    for(int i=0;i<30 && direct.target->state.hp==100.0;++i)direct.world->tick(dt);
    check(std::abs(direct.target->state.hp-(100.0-test::bowSourceFixture.damageFull))<1e-9,
          "Full-draw centre arrow did not apply the source direct damage once");

    BowRun burst;chargeBow(burst,38);
    auto lodgedCount=[&] {return std::count_if(burst.world->projectiles().begin(),burst.world->projectiles().end(),[](const auto& p){return p.kind=="arrow" && p.state=="lodged";});};
    for(int i=0;i<240 && lodgedCount()<3;++i)burst.world->tick(dt);
    check(lodgedCount()==3,"Ring-draw arrows failed to lodge on the source floor after the dive");
    auto chosen=std::min_element(burst.world->projectiles().begin(),burst.world->projectiles().end(),[](const auto& a,const auto& b){return a.hitPoint.x<b.hitPoint.x;});
    check(chosen!=burst.world->projectiles().end(),"No lodged Bow arrow was available for the burst oracle");
    burst.target=std::make_unique<PlayerSimulation>(burst.stage,*burst.geometry,*burst.paint,1,burst.world.get());
    const auto chosenCenter=chosen->hitPoint+chosen->hitNormal*0.18;
    burst.target->state.pos={chosenCenter.x-1.3,0,chosenCenter.z};burst.target->state.hp=100;burst.target->state.invuln=0;
    double expectedDamage=0;
    for(const auto& arrow:burst.world->projectiles())if(arrow.kind=="arrow" && arrow.state=="lodged") {
        const auto center=arrow.hitPoint+arrow.hitNormal*0.18;
        const glm::dvec3 targetPoint(burst.target->state.pos.x,0.25,burst.target->state.pos.z);
        const double d=std::max(0.0,glm::length(targetPoint-center)-0.38*0.6);
        if(d<=arrow.burstRadius)expectedDamage+=d<=arrow.burstInner?arrow.burstDamage:
            arrow.burstDamage+(arrow.burstEdge-arrow.burstDamage)*(d-arrow.burstInner)/(arrow.burstRadius-arrow.burstInner);
    }
    check(expectedDamage>0,"Bow burst scenario placed its target outside every source radius");
    for(int i=0;i<60 && burst.target->state.hp==100.0;++i)burst.world->tick(dt);
    check(std::abs(burst.target->state.hp-(100.0-expectedDamage))<1e-8,"Lodged Bow blast damage diverged from source inner/edge falloff");
}

const test::MainKitTraceFixture& kitTrace(const char* weapon,const char* action) {
    const auto it=std::find_if(test::mainKitTraceFixtures.begin(),test::mainKitTraceFixtures.end(),[&](const auto& row){return std::string(row.weapon)==weapon && std::string(row.action)==action;});
    if(it==test::mainKitTraceFixtures.end())throw std::runtime_error("Missing generated source main-kit action fixture");
    return *it;
}

void runBladeKitSourceTrace() {
    constexpr double dt=1.0/60.0;StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    auto floor=controller_fixtures::syntheticBlocks[0];floor.half={30,0.5,30};stage.blocks.push_back(floor);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation blade(stage,geometry,paint,0,&world);blade.setLoadout("blade");blade.state.pos={0,0,0};blade.state.grounded=true;blade.state.invuln=0;
    blade.input.fire=true;blade.tick(dt);world.tick(dt);
    for(int frame=2;frame<=3;++frame){blade.input.fire=false;blade.tick(dt);world.tick(dt);}
    const auto& tap=kitTrace("blade","tap");
    check(tap.frame==3 && tap.projectileCount==7,"Blade source action fixture lost tap timing or seven-drop event");
    check(std::abs(blade.state.ink-tap.ink)<1e-9 && world.projectiles().size()==static_cast<std::size_t>(tap.projectileCount),
          "Native Blade tap event/ink differs from actual source WeaponRunner trace");
    const auto& first=world.projectiles().front();
    check(first.kind=="drop" && first.weaponId=="blade" && first.volley==world.projectiles().back().volley &&
          first.delay==0.0 && std::abs(first.damage-tap.damage)<1e-9 && std::abs(first.damageFar-tap.sideDamage)<1e-9,
          "Native Blade crescent does not preserve the source drop command and one-hit volley");
    const double speed=glm::length(first.vel);
    check(speed>=test::bladeSourceFixture.tapSpeed*0.76 && speed<=test::bladeSourceFixture.tapSpeed*0.88,
          "Native Blade edge drop speed is outside the source sweep formula");

    PlayerSimulation heavy(stage,geometry,paint,0,&world);heavy.setLoadout("blade");heavy.state.pos={0,0,0};heavy.state.grounded=true;heavy.state.invuln=0;
    for(int frame=1;frame<=60;++frame){heavy.input.fire=true;heavy.tick(dt);world.tick(dt);}
    const double charge=heavy.combatPose().charge;
    heavy.input.fire=false;
    for(int frame=61;frame<=64;++frame){heavy.tick(dt);world.tick(dt);}
    const auto& heavySource=kitTrace("blade","heavy");
    const auto wave=std::find_if(world.projectiles().begin(),world.projectiles().end(),[&](const auto& p){return p.owner==&heavy && p.kind=="bladewave";});
    check(heavySource.frame==64 && charge>=1.0 && wave!=world.projectiles().end(),"Blade full-hold source trace did not release its charged wave on frame 64");
    check(std::abs(heavy.state.ink-heavySource.ink)<1e-8 && std::abs(glm::length(wave->vel)-heavySource.speed)<1e-8 &&
          std::abs(wave->damage-heavySource.damage)<1e-8 && std::abs(wave->damageFar-heavySource.sideDamage)<1e-8,
          "Native Blade heavy state/wave command differs from the source-generated action fixture");
}

void runBowKitSourceTrace() {
    BowRun run;const double charge=chargeBow(run,12);const auto sourceIt=std::find_if(test::mainKitTraceFixtures.begin(),test::mainKitTraceFixtures.end(),[](const auto& row){return std::string(row.weapon)=="bow" && std::string(row.action)=="arrow" && row.ordinal==0;});
    check(sourceIt!=test::mainKitTraceFixtures.end(),"Missing generated Bow center-arrow source trace");const auto& source=*sourceIt;
    const auto center=std::find_if(run.world->projectiles().begin(),run.world->projectiles().end(),[](const auto& p){return p.kind=="arrow" && p.centerArrow;});
    check(source.frame==13 && source.projectileCount==3 && std::abs(charge-source.charge)<1e-9,
          "Bow release timing/draw state diverged from the actual source kit trace");
    check(center!=run.world->projectiles().end() && std::abs(run.owner->state.ink-source.ink)<1e-8 &&
          std::abs(glm::length(center->vel)-source.speed)<1e-8 && std::abs(center->range-source.range)<1e-8 &&
          std::abs(center->damage-source.damage)<1e-8,
          "Native Bow command differs from the source-generated arrow release fixture");
}

void runMittsKitSourceTrace() {
    constexpr double dt=1.0/60.0;StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
    auto floor=controller_fixtures::syntheticBlocks[0];floor.half={30,0.5,30};stage.blocks.push_back(floor);
    StageGeometry geometry(stage);FacePaint paint(stage,geometry,2048);CombatWorld world(stage,geometry,paint);
    PlayerSimulation mitts(stage,geometry,paint,0,&world);mitts.setLoadout("mitts");mitts.state.pos={0,0,0};mitts.state.grounded=true;mitts.state.invuln=0;
    mitts.input.fire=true;
    for(int frame=1;frame<=15;++frame){mitts.tick(dt);world.tick(dt);
        if(frame==1)check(mitts.shotsFired()==1,"Mitts first source punch did not fire on frame 1");
        if(frame==8)check(mitts.shotsFired()==2,"Mitts second source punch did not fire on frame 8");
    }
    const auto& punch=kitTrace("mitts","punch");
    std::vector<const ShooterProjectile*> fists;
    for(const auto& p:world.projectiles())if(p.owner==&mitts && p.kind=="fist")fists.push_back(&p);
    check(punch.frame==1 && mitts.shotsFired()==3 && !fists.empty() && std::abs(mitts.state.ink-95.2)<1e-8,
          "Native Mitts punch cadence/ink does not match the source-generated frames 1, 8, and 15");
    check(std::abs(glm::length(fists.front()->vel)-punch.speed)<1e-9 &&
          std::abs(fists.front()->damage-punch.damage)<1e-9 &&
          std::abs(fists.front()->burstRadius-punch.delay)<1e-9,
          "Native Mitts fist command differs from the source-generated punch fixture");

    PlayerSimulation leap(stage,geometry,paint,0,&world);leap.setLoadout("mitts");leap.state.pos={0,0,0};leap.state.grounded=true;leap.state.invuln=0;
    leap.input.fire=true;leap.input.jump=true;
    for(int frame=1;frame<=27;++frame){leap.tick(dt);world.tick(dt);}
    const double charge=leap.combatPose().charge;
    leap.input.jump=false;leap.tick(dt);world.tick(dt);
    const auto& leapSource=kitTrace("mitts","leap");
    check(leapSource.frame==28 && charge>=0.999 && !leap.combatPose().charging,
          "Mitts charge/release timing differs from the actual source jump hook");
    check(std::abs(leap.state.ink-leapSource.ink)<1e-8 && glm::length(leap.state.vel-glm::dvec3(leapSource.x,leapSource.y,leapSource.z))<1e-7,
          "Native Mitts leap velocity/ink differs from the source-generated launch event");
}
}

int main() {
    try {
        checkBrollyConfig();
        runTracerContact(0.0,35.0,9.0);
        runTracerContact(0.68,22.0,3.0);
        runTracerContact(0.68,22.0,3.0,true,false);
        runTracerContact(0.68,22.0,3.0,false,true);
        runBrollyShield();
        runPlacementOrder();
        runBombFuseDetonation(false);
        runBombFuseDetonation(true);
        runDeviceBlastTargetHeight("sticky",true);
        runDeviceBlastTargetHeight("seeker",true);
        runDeviceBlastTargetHeight("torpedo",false);
        runDeviceBlastTargetHeight("torpedo",true);
        runDeviceBlastTargetHeight("boomerang",false);
        runDeviceBlastTargetHeight("boomerang",true);
        runWaddleTickDetonation();
        runDeviceWarningLifecycle();
        runBoomerangContactFuse();
        runDeviceRayDestroyCue();
        runBrollyMainShot();
        runScanCloudLifecycle();
        runMistDirectPoisonLifecycle();
        runCurtainBlockingLifecycle();
        runSprinklerPulseAndReplacement();
        runSeekerCommit();
        runBowTierOracle(12,0);
        runBowTierOracle(35,1);
        runBowTierOracle(70,2);
        runBowDirectAndLodgedBurst();
        runBowKitSourceTrace();
        runBladeKitSourceTrace();
        runMittsKitSourceTrace();
        std::cout<<"Source Tracer/Brolly combat, Bomb/Waddle/Mine device ticks, sub lifecycle, Seeker commitment, and Bow/Blade/Mitts source actions passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
