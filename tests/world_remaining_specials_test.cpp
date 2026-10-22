#include "../src/game/ArsenalData.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "remaining_specials_source_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace restoration;
constexpr double dt = 1.0 / 60.0;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void near(double actual, double expected, const char* message, double tolerance = 1e-6) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}

void near(const glm::dvec3& actual, const glm::dvec3& expected, const char* message, double tolerance = 1e-6) {
    near(actual.x, expected.x, message, tolerance);
    near(actual.y, expected.y, message, tolerance);
    near(actual.z, expected.z, message, tolerance);
}

glm::dvec3 vec(double x, double y, double z) { return {x, y, z}; }

StageData makeStage(bool zipWall = false) {
    StageData stage{};
    stage.id = "remaining-special-source";
    stage.name = "Remaining Special Source Acceptance";
    stage.bounds = {-30.0, 30.0, -30.0, 30.0};
    stage.spawnPads = {glm::dvec3(-20.0, 0.0, -20.0), glm::dvec3(20.0, 0.0, -20.0)};
    stage.spawnBarrier = 0.0;
    BlockData floor{};
    floor.center = {0.0, -0.5, 0.0};
    floor.half = {30.0, 0.5, 30.0};
    floor.axes = {glm::dvec3(1.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0), glm::dvec3(0.0, 0.0, 1.0)};
    floor.faces.fill(-1);
    floor.faces[2] = 0;
    floor.solid = true;
    stage.blocks.push_back(floor);
    FaceData top{};
    top.origin = {-30.0, 0.0, -30.0};
    top.u = {1.0, 0.0, 0.0};
    top.v = {0.0, 0.0, 1.0};
    top.normal = {0.0, 1.0, 0.0};
    top.width = top.height = 60.0;
    top.block = 0;
    top.paintable = top.turf = true;
    stage.faces.push_back(top);
    if (zipWall) {
        BlockData wall{};
        wall.center = {7.2, 2.0, 0.0};
        wall.half = {0.2, 2.0, 4.0};
        wall.axes = {glm::dvec3(1.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0), glm::dvec3(0.0, 0.0, 1.0)};
        wall.faces.fill(-1);
        wall.solid = true;
        stage.blocks.push_back(wall);
    }
    return stage;
}

struct Scene {
    StageData stage;
    StageGeometry geometry;
    FacePaint paint;
    CombatWorld world;
    explicit Scene(bool zipWall = false)
        : stage(makeStage(zipWall)), geometry(stage), paint(stage, geometry, 2048), world(stage, geometry, paint) {}
};

void ready(PlayerSimulation& actor, std::string special, glm::dvec3 pos = glm::dvec3(0.0)) {
    actor.setLoadout("shooter", "bomb", std::move(special));
    actor.spawnAt(pos, 0.0);
    actor.state.hp = 5000.0;
    actor.state.invuln = 0.0;
    actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
}

void activate(PlayerSimulation& actor) {
    actor.input.special = true;
    actor.tick(dt);
    actor.input.special = false;
}

void frame(Scene& scene, PlayerSimulation& actor) {
    actor.tick(dt);
    scene.world.tick(dt);
}

double damageTaken(const PlayerSimulation& actor) { return 5000.0 - actor.state.hp; }

void strikeOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation enemy(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation ally(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation outside(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(owner, "strike", {-8.0, 0.0, -8.0});
    ready(enemy, "strike", {test::strikeSourceFixture.targetX, 0.0, test::strikeSourceFixture.targetZ});
    ready(ally, "strike", {test::strikeSourceFixture.targetX, 0.0, test::strikeSourceFixture.targetZ});
    ready(outside, "strike", {test::strikeSourceFixture.targetX + test::strikeSourceFixture.radius + 1.5,
                               0.0, test::strikeSourceFixture.targetZ});
    activate(owner);
    check(owner.specialPose().active && owner.specialPose().mapTarget && owner.specialPose().body,
          "Strike did not enter the source cursor state");
    owner.input.hasMapTarget = true;
    owner.input.mapTarget = {test::strikeSourceFixture.targetX, 0.0, test::strikeSourceFixture.targetZ};
    for (int i = 0; i < test::strikeSourceFixture.boundaryFrames - 1; ++i) frame(scene, owner);
    owner.input.specialConfirm = true;
    frame(scene, owner);
    owner.input.specialConfirm = false;
    near(owner.specialPose().elapsed, test::strikeSourceFixture.boundaryFrames * dt,
         "Strike source confirm boundary elapsed");
    check(test::strikeSourceFixture.confirmRejected == 1 && owner.specialPose().active,
          "Strike did not remain active at the strict source confirm boundary");
    frame(scene, owner);
    owner.input.specialConfirm = true;
    frame(scene, owner);
    owner.input.specialConfirm = false;
    check(!owner.specialPose().active && owner.state.specialCharge == 0.0 && owner.state.ink == 100.0,
          "Strike confirmation did not launch and spend the special");

    int missileTicks = 0;
    while (damageTaken(enemy) == 0.0 && missileTicks < test::strikeSourceFixture.missileFrames + 5) {
        frame(scene, owner);
        ++missileTicks;
    }
    check(damageTaken(enemy) > 0.0, "Strike missile never became a damaging vortex");
    near(damageTaken(enemy), test::strikeSourceFixture.enemyTickDamage, "Strike first vortex tick damage");
    near(damageTaken(ally), test::strikeSourceFixture.allyTickDamage, "Strike friendly damage");
    near(damageTaken(outside), 0.0, "Strike outside-radius damage");
    check(missileTicks == test::strikeSourceFixture.missileFrames + 1,
          "Strike missile transition and first vortex tick differ from the source fixed-step oracle");

    for (int i = 0; i < test::strikeSourceFixture.cleanupFrames + 3; ++i) frame(scene, owner);
    const double afterCleanup = damageTaken(enemy);
    for (int i = 0; i < 4; ++i) frame(scene, owner);
    near(damageTaken(enemy), afterCleanup, "Strike vortex did not clean up at its source lifetime");
}

void blowerOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    const auto& source = test::blowerSourceFixture;
    const double blastRadius = test::blowerSourceFixture.blastRadius;
    ready(owner, "blower");
    activate(owner);
    check(owner.specialPose().active && owner.specialPose().noSquid,
          "Blower did not enter its source special state");
    owner.input.fire = true;
    for (int i = 0; i < 60; ++i) frame(scene, owner);
    owner.input.fire = false;
    frame(scene, owner);
    check(owner.specialPose().active && owner.state.specialCharge == 0.0,
          "Blower release ended the special or restored its gauge");

    // The source creates blast targets after release, so avoid letting the inflating bubble displace them.
    PlayerSimulation enemy(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation ally(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation outside(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(enemy, "blower", {source.bubbleX, 0.0, source.bubbleZ});
    ready(ally, "blower", {source.bubbleX, 0.0, source.bubbleZ});
    ready(outside, "blower", {source.bubbleX + blastRadius + 2.0, 0.0, source.bubbleZ});

    const double rayLength = scene.world.blockRay({source.bubbleX, source.bubbleY, source.bubbleZ - 6.0}, {0.0, 0.0, 1.0}, 12.0,
        owner.team(), test::blowerSourceFixture.popDamage, &owner);
    check(rayLength < 12.0, "A friendly shot did not pop the released source bubble");
    near(damageTaken(enemy), test::blowerSourceFixture.enemyDamage, "Blower pop blast enemy damage");
    near(damageTaken(ally), test::blowerSourceFixture.allyDamage, "Blower friendly fire");
    near(damageTaken(outside), test::blowerSourceFixture.outsideDamage, "Blower outside-radius damage");
    scene.world.tick(dt);
    const double secondRay = scene.world.blockRay({source.bubbleX, source.bubbleY, source.bubbleZ - 6.0}, {0.0, 0.0, 1.0}, 12.0,
        owner.team(), test::blowerSourceFixture.popDamage, &owner);
    near(secondRay, 12.0, "Blower removed its popped bubble");

    Scene chainScene;
    PlayerSimulation chainOwner(chainScene.stage, chainScene.geometry, chainScene.paint, 0, &chainScene.world);
    ready(chainOwner, "blower");
    activate(chainOwner);
    chainOwner.input.fire = true;
    for (int i = 0; i < 60; ++i) frame(chainScene, chainOwner);
    chainOwner.input.fire = false;
    frame(chainScene, chainOwner);
    chainOwner.input.fire = true;
    for (int i = 0; i < 60; ++i) frame(chainScene, chainOwner);
    chainOwner.input.fire = false;
    frame(chainScene, chainOwner);

    PlayerSimulation chainEnemy(chainScene.stage, chainScene.geometry, chainScene.paint, 1, &chainScene.world);
    PlayerSimulation chainOutside(chainScene.stage, chainScene.geometry, chainScene.paint, 1, &chainScene.world);
    ready(chainEnemy, "blower", {source.chainFirstX, 0.0, source.chainFirstZ});
    ready(chainOutside, "blower", {source.chainFirstX + blastRadius + 2.0, 0.0, source.chainFirstZ});
    chainEnemy.state.hp = 100.0;
    const auto chainRayFrom = glm::dvec3(source.chainFirstX, source.chainFirstY, source.chainFirstZ + 6.0);
    const double chainHit = chainScene.world.blockRay(chainRayFrom, {0.0, 0.0, -1.0}, 12.0,
        chainOwner.team(), source.popDamage, &chainOwner);
    check(chainHit < 12.0, "Blower chain source shot did not hit the first bubble");
    check(!chainEnemy.state.alive, "Blower chain did not kill its normal-HP target");
    near(100.0 - chainEnemy.state.hp, source.chainTargetDamage, "Blower chain source target damage");
    near(damageTaken(chainOutside), source.chainOutsideDamage, "Blower chain outside-radius damage");
    chainScene.world.tick(dt);
    const double afterChain = chainScene.world.blockRay(chainRayFrom, {0.0, 0.0, -1.0}, 12.0,
        chainOwner.team(), source.popDamage, &chainOwner);
    near(afterChain, 12.0, "Blower nested chain did not clean both bubbles safely");
    check(source.chainExplosions == 2 && source.chainObjectsAfter == 0 && source.chainTargetAliveAfter == 0,
          "Blower source chain fixture did not exercise both explosions and victim cleanup");
}

void jetpackOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    ready(owner, "jetpack");
    activate(owner);
    auto pose = owner.specialPose();
    check(pose.active && pose.body && pose.jumpBack, "Jetpack did not capture its return-home pose");
    near(pose.origin, {test::jetpackSourceFixture.originX, test::jetpackSourceFixture.originY,
                       test::jetpackSourceFixture.originZ}, "Jetpack source origin");

    int firstShotFrame = 0;
    for (int frameIndex = 1; frameIndex <= 60; ++frameIndex) {
        owner.input.move = frameIndex <= 40 ? glm::dvec3(1.0, 0.0, 0.0) : glm::dvec3(0.0);
        owner.input.fire = true;
        owner.input.jump = frameIndex == 22;
        frame(scene, owner);
        if (firstShotFrame == 0 && owner.specialPose().firing > 0.0) firstShotFrame = frameIndex;
        if (frameIndex == 22)
            near(owner.state.vel.y, test::jetpackSourceFixture.boostVelocityY, "Jetpack source boost velocity");
    }
    owner.input.move = glm::dvec3(0.0);
    owner.input.fire = false;
    owner.input.jump = false;
    near(firstShotFrame, test::jetpackSourceFixture.firstShotFrame, "Jetpack first firing frame", 0.0);
    near(owner.state.pos.x, test::jetpackSourceFixture.at60X, "Jetpack frame-60 x");
    near(owner.state.pos.y, test::jetpackSourceFixture.at60Y, "Jetpack frame-60 y");
    near(owner.state.pos.z, test::jetpackSourceFixture.at60Z, "Jetpack frame-60 z");
    near(std::hypot(owner.state.vel.x, owner.state.vel.z), test::jetpackSourceFixture.firstHorizontalSpeed,
         "Jetpack frame-60 horizontal speed");

    int expiryFrame = 60;
    while (owner.specialPose().active && expiryFrame < 500) {
        frame(scene, owner);
        ++expiryFrame;
    }
    near(expiryFrame, test::jetpackSourceFixture.expiryFrame, "Jetpack source expiry frame", 0.0);
    check(!owner.specialPose().active && owner.superJumpState().has_value()
          && owner.superJumpState()->home, "Jetpack did not start its source return-home jump");
    near(owner.superJumpState()->target, {test::jetpackSourceFixture.originX, test::jetpackSourceFixture.originY,
                                           test::jetpackSourceFixture.originZ}, "Jetpack return target");
}

void stampOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation enemy(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation ally(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation front(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation back(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(owner, "stamp");
    ready(enemy, "stamp", {0.0, 0.0, 1.6});
    ready(ally, "stamp", {0.0, 0.0, 1.6});
    ready(front, "stamp", {0.0, 0.0, 2.0});
    ready(back, "stamp", {0.0, 0.0, -3.0});
    activate(owner);
    check(owner.specialPose().active && owner.specialPose().noSquid,
          "Stamp did not enter its source pose state");
    owner.input.fire = true;
    int slamFrame = 0;
    for (int frameIndex = 1; frameIndex <= 30 && damageTaken(enemy) == 0.0; ++frameIndex) {
        frame(scene, owner);
        if (damageTaken(enemy) > 0.0) slamFrame = frameIndex;
    }
    owner.input.fire = false;
    near(slamFrame, test::stampSourceFixture.slamFrame, "Stamp delayed slam frame", 0.0);
    near(damageTaken(enemy), test::stampSourceFixture.enemyDamage, "Stamp slam enemy damage");
    near(damageTaken(ally), test::stampSourceFixture.allyDamage, "Stamp friendly fire");
    near(damageTaken(front), test::stampSourceFixture.slamFrontDamage, "Stamp slam front damage");
    check(owner.specialPose().active, "Grounded Stamp slam unexpectedly ended its source special");
    check(!scene.world.applyHit(front, owner, 20.0, "fixture"), "Stamp front guard splatted its owner");
    const double afterFront = owner.state.hp;
    check(!scene.world.applyHit(back, owner, 20.0, "fixture"), "Stamp rear hit splatted its owner");
    near(afterFront - owner.state.hp, test::stampSourceFixture.guardBackDamage, "Stamp directional guard damage");

    Scene throwScene;
    PlayerSimulation thrower(throwScene.stage, throwScene.geometry, throwScene.paint, 0, &throwScene.world);
    PlayerSimulation throwVictim(throwScene.stage, throwScene.geometry, throwScene.paint, 1, &throwScene.world);
    PlayerSimulation throwOutside(throwScene.stage, throwScene.geometry, throwScene.paint, 1, &throwScene.world);
    ready(thrower, "stamp");
    ready(throwVictim, "stamp", {test::stampSourceFixture.throwLandingX, 0.0, test::stampSourceFixture.throwLandingZ});
    ready(throwOutside, "stamp", {test::stampSourceFixture.throwLandingX + test::stampSourceFixture.throwBlastRadius + 2.0,
                                   0.0, test::stampSourceFixture.throwLandingZ});
    std::vector<std::string> triggers;
    thrower.setPoseTriggerSink([&](const CharacterTrigger& event) { triggers.push_back(event.name); });
    activate(thrower);
    thrower.input.sub = true;
    frame(throwScene, thrower);
    thrower.input.sub = false;
    check(!thrower.specialPose().active, "Stamp throw did not end its source special");
    check(std::find(triggers.begin(), triggers.end(), "throw") != triggers.end(),
          "Stamp throw did not emit its source pose event");
    int throwFrames = 0;
    while (damageTaken(throwVictim) == 0.0 && throwFrames < 60) {
        frame(throwScene, thrower);
        ++throwFrames;
    }
    check(damageTaken(throwVictim) > 0.0, "Thrown Stamp did not produce its source area impact");
    near(throwFrames, test::stampSourceFixture.throwTargetFrames, "Stamp throw impact source frame", 0.0);
    near(damageTaken(throwVictim), test::stampSourceFixture.throwVictimDamage, "Stamp throw impact damage");
    near(damageTaken(throwOutside), test::stampSourceFixture.throwOutsideDamage, "Stamp throw outside-radius damage");
}

void booyahOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation helper(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation enemy(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation ally(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation outside(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    const auto& source = test::booyahSourceFixture;
    ready(owner, "booyah");
    ready(helper, "booyah", {2.0, 0.0, 0.0});
    helper.state.specialCharge = 0.0;
    ready(enemy, "booyah", {source.landingX, 0.0, source.landingZ});
    ready(ally, "booyah", {source.landingX, 0.0, source.landingZ});
    ready(outside, "booyah", {source.landingX + source.radius + 2.0, 0.0, source.landingZ});
    activate(owner);
    helper.input.cheer = true;
    scene.world.tick(dt);
    near(owner.specialPose().charge, source.cheer, "Booyah teammate cheer charge");
    near(helper.state.specialCharge, source.helperGauge, "Booyah teammate gauge reward");
    helper.input.cheer = true;
    scene.world.tick(dt);
    near(owner.specialPose().charge, source.cheer, "Booyah repeated cheer cooldown");
    near(helper.state.specialCharge, source.helperGauge, "Booyah repeated helper gauge");
    helper.input.cheer = false;

    int chargedFrame = 0;
    while (owner.specialPose().charge < 1.0 && chargedFrame < 300) {
        frame(scene, owner);
        ++chargedFrame;
    }
    near(chargedFrame, source.chargedFrame, "Booyah source charge frame", 0.0);
    for (int i = 0; i < 5; ++i) frame(scene, owner);
    owner.input.fire = true;
    frame(scene, owner);
    owner.input.fire = false;
    check(!owner.specialPose().active, "Booyah throw did not end the source actor state");

    int orbFrames = 0;
    while (damageTaken(enemy) == 0.0 && orbFrames < source.orbLandingFrames + source.fuseFrames + 8) {
        frame(scene, owner);
        ++orbFrames;
    }
    check(damageTaken(enemy) > 0.0, "Booyah orb did not land and blast");
    near(damageTaken(enemy), source.enemyDamage, "Booyah source blast damage");
    near(damageTaken(ally), source.allyDamage, "Booyah friendly fire");
    near(damageTaken(outside), source.outsideDamage, "Booyah outside-radius damage");
    check(orbFrames == source.orbLandingFrames + source.fuseFrames,
          "Booyah flight/fuse differs from its source fixed-step lifetime");
    const auto visualEvents=scene.world.takeVisualEvents();
    check(std::count_if(visualEvents.begin(),visualEvents.end(),[&owner](const auto& event){return event.kind=="special_warning"&&event.source=="booyah"&&event.team==owner.team();})==1,
          "Booyah orb landing did not emit one source warning cue");
    check(std::count_if(visualEvents.begin(),visualEvents.end(),[&owner](const auto& event){return event.kind=="explosion"&&event.source=="booyah"&&event.team==owner.team();})==1,
          "Booyah orb detonation did not preserve its source ID on one explosion event");
}

void zipcasterOracle() {
    Scene scene(true);
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    const auto& source = test::zipcasterSourceFixture;
    const glm::dvec3 target(source.targetX, source.targetY, source.targetZ);
    PlayerSimulation enemy(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(owner, "zipcaster");
    ready(enemy, "zipcaster", target + glm::dvec3(0.0, -1.0, 1.5));
    owner.input.aimPoint = {14.0, 2.8, 0.0};
    owner.input.hasAimPoint = true;
    activate(owner);
    check(owner.specialPose().active && owner.specialPose().jumpBack,
          "Zipcaster did not capture its source return anchor");
    owner.input.sub = true;
    frame(scene, owner);
    owner.input.sub = false;
    check(owner.specialPose().active && owner.specialPose().body,
          "Zipcaster did not latch the controlled wall and begin its body zip");
    near(owner.specialPose().target, target, "Zipcaster source wall offset");
    int travelFrames = 0;
    while (owner.specialPose().body && travelFrames < 120) {
        frame(scene, owner);
        ++travelFrames;
    }
    check(!owner.specialPose().body && owner.specialPose().hang > 0.0,
          "Zipcaster did not stop at its wall and enter the source hang");
    near(owner.state.pos, target, "Zipcaster arrival point");
    near(owner.specialPose().hang, source.hang, "Zipcaster hang duration");
    near(damageTaken(enemy), source.impactSplash, "Zipcaster impact splash damage");
    near(travelFrames, source.travelFrames, "Zipcaster source travel frames", 0.0);
    int expiryFrame = 1 + travelFrames;
    while (owner.specialPose().active && expiryFrame < 600) {
        frame(scene, owner);
        ++expiryFrame;
    }
    near(expiryFrame, source.expiryFrame, "Zipcaster source expiry frame", 0.0);
    check(!owner.specialPose().active && owner.superJumpState().has_value()
          && owner.superJumpState()->home, "Zipcaster did not return to its source anchor");
    near(owner.superJumpState()->target, glm::dvec3(0.0), "Zipcaster home-jump target");
}

void crabOracle() {
    Scene scene;
    PlayerSimulation owner(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation front(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    PlayerSimulation rear(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(owner, "crab");
    ready(front, "crab", {0.0, 0.0, 3.0});
    ready(rear, "crab", {0.0, 0.0, -3.0});
    activate(owner);
    auto pose = owner.specialPose();
    const auto& source = test::crabSourceFixture;
    check(pose.active && pose.noSquid && pose.inkProof, "Crab did not enter its source armored pose");
    near(pose.hitPoints, source.hp, "Crab source hull health");
    near(pose.hitRadius, 1.0, "Crab source hit radius");
    near(pose.hitHeight, 2.2, "Crab source hit height");
    near(pose.seat, 0.0, "Crab initial rider seat before the first special tick");
    check(!scene.world.applyHit(front, owner, 100.0, "fixture"), "Crab front hull hit splatted its rider");
    near(owner.specialPose().hitPoints, source.hpAfterFront, "Crab front hull absorption");
    near(owner.state.hp, 5000.0, "Crab front rider health");
    check(!scene.world.applyHit(rear, owner, source.riderDamageFromRear, "fixture"), "Crab rear rider hit splatted its rider");
    near(damageTaken(owner), source.riderDamageFromRear, "Crab rear exposure damage");

    owner.input.squid = true;
    frame(scene, owner);
    owner.input.squid = false;
    check(owner.specialPose().roll, "Crab did not enter its source armored roll");
    near(owner.specialPose().moveSpeed, source.rollSpeed, "Crab roll speed");
    near(owner.specialPose().seat, 0.0, "Crab source seat while rolled");
    check(!scene.world.applyHit(front, owner, 100.0, "fixture"), "Crab roll hit splatted its rider");
    near(owner.specialPose().hitPoints, source.hpAfterRollHit, "Crab roll hull armor");
    near(damageTaken(owner), source.riderDamageFromRear, "Crab roll rider protection");

    const glm::dvec3 crabShotOwnerPos = owner.state.pos;
    owner.input.fire = true;
    frame(scene, owner);
    owner.input.fire = false;
    near(owner.specialPose().seat, source.seatY, "Crab rider seat after unrolling");
    check(!scene.world.projectiles().empty(), "Crab gatling did not create its source main-gun projectile");
    const auto shot = std::find_if(scene.world.projectiles().begin(), scene.world.projectiles().end(),
        [&](const ShooterProjectile& projectile) { return projectile.owner == &owner && projectile.weaponId == "crab"; });
    check(shot != scene.world.projectiles().end(), "Crab main-gun shot lost its owner or source weapon ID");
    near(shot->start, crabShotOwnerPos + glm::dvec3(source.gunMuzzleX, source.gunMuzzleY, source.gunMuzzleZ),
         "Crab source gun prop muzzle");
    near(glm::length(shot->vel), source.gunSpeed, "Crab main-gun projectile speed");
    near(shot->damage, source.gunDamage, "Crab main-gun damage");

    int expiryFrame = 2; // the roll and gun frames above have already advanced the active source timer
    while (owner.specialPose().active && expiryFrame < 600) {
        frame(scene, owner);
        ++expiryFrame;
    }
    near(expiryFrame, source.expiryFrames, "Crab source expiry frame", 0.0);
    check(!owner.specialPose().active && owner.specialPose().hitRadius < 0.0
          && owner.specialPose().hitHeight < 0.0,
          "Crab expiry did not restore the rider hit capsule");
}

void crabTracerCapsule() {
    Scene scene;
    PlayerSimulation attacker(scene.stage, scene.geometry, scene.paint, 0, &scene.world);
    PlayerSimulation rider(scene.stage, scene.geometry, scene.paint, 1, &scene.world);
    ready(attacker, "zooka");
    attacker.setLoadout("shooter", "tracer", "zooka");
    ready(rider, "crab", {10.0, 0.0, 10.0});
    activate(rider);
    attacker.input.aimYaw = glm::pi<double>();
    scene.world.deploy(attacker, "tracer");
    check(scene.world.devices().size() == 1, "Tracer capsule test did not deploy");
    const auto& tracer = scene.world.devices().front();
    const auto step = tracer.vel * dt;
    const glm::dvec3 side = glm::normalize(glm::dvec3(step.z, 0.0, -step.x));
    // The source Crab capsule extends beyond the ordinary rider's radius.
    rider.state.pos = tracer.pos + step * 0.5 + side * 0.8;
    rider.state.pos.y = 0.0;
    rider.state.yaw = std::atan2(-step.x, -step.z);
    const double before = rider.specialPose().hitPoints;
    scene.world.tick(dt);
    near(before - rider.specialPose().hitPoints,
         GetEquipment(EquipmentCategory::Sub, "tracer").number("directDamage"),
         "Tracer missed the source Crab hit capsule");
    near(damageTaken(rider), 0.0, "Crab tracer hit damaged its armored rider");
}

} // namespace

int main() {
    try {
        strikeOracle();
        blowerOracle();
        jetpackOracle();
        stampOracle();
        booyahOracle();
        zipcasterOracle();
        crabOracle();
        crabTracerCapsule();
        std::cout << "Strike, Blower, Jetpack, Stamp, Booyah, Zipcaster and Crab actual-source acceptance passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
