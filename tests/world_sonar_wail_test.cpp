#include "../restoration/PlayerSimulation.hpp"
#include "../restoration/WorldSpecials.hpp"
#include "../restoration/ArsenalData.hpp"
#include "sonar_wail_source_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace restoration;
constexpr double dt = 1.0 / 60.0;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

StageData makeStage() {
    const auto& stages = ReferenceStages();
    const auto source = std::find_if(stages.begin(), stages.end(), [](const StageData& stage) {
        return stage.id == "tidewater" && stage.mode == "turf";
    });
    if (source == stages.end()) throw std::runtime_error("Missing source Turf stage");
    StageData stage = *source;
    stage.id = "sonar-wail-test";
    stage.name = "Sonar and Wail source fixture test";
    stage.bounds = {-25.0, 25.0, -25.0, 25.0};
    stage.spawnPads = {glm::dvec3(-12.0, 0.0, -10.0), glm::dvec3(12.0, 0.0, -10.0)};
    stage.spawnBarrier = 0.0;
    stage.blocks.clear();
    stage.faces.clear();

    const auto addBox = [&](glm::dvec3 center, glm::dvec3 half) {
        BlockData block{};
        block.center = center;
        block.half = half;
        block.axes = {glm::dvec3(1.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0), glm::dvec3(0.0, 0.0, 1.0)};
        block.faces.fill(-1);
        block.solid = true;
        stage.blocks.push_back(block);
    };
    addBox({0.0, -0.5, 0.0}, {30.0, 0.5, 30.0});
    // This wall cuts the captured Wail line, while leaving its floor-placement ray clear.
    addBox({0.0, 2.0, 6.0}, {4.0, 2.0, 0.2});
    return stage;
}

void prepare(PlayerSimulation& actor, std::string_view special, glm::dvec3 pos) {
    actor.setLoadout("shooter", "bomb", std::string(special));
    actor.state.pos = pos;
    actor.state.vel = glm::dvec3(0.0);
    actor.state.invuln = 0.0;
    actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
}

double horizontalSpeed(const PlayerSimulation& actor) {
    return std::hypot(actor.state.vel.x, actor.state.vel.z);
}

void testSonar(const StageData& stage) {
    using namespace restoration;
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    CombatWorld world(stage, geometry, paint);
    WorldSpecials specials(world);
    auto owner = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    auto enemyA = std::make_unique<PlayerSimulation>(stage, geometry, paint, 1, &world);
    auto enemyB = std::make_unique<PlayerSimulation>(stage, geometry, paint, 1, &world);
    auto deadEnemy = std::make_unique<PlayerSimulation>(stage, geometry, paint, 1, &world);
    auto ally = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    prepare(*owner, "sonar", {4.0, 0.0, -3.0});
    prepare(*enemyA, "sonar", {-2.0, 0.0, 4.0});
    prepare(*enemyB, "sonar", {9.0, 0.0, 8.0});
    prepare(*deadEnemy, "sonar", {-7.0, 0.0, 2.0});
    prepare(*ally, "sonar", {10.0, 0.0, 10.0});
    owner->state.vel = {2.5, 0.0, -1.25};
    owner->state.grounded = true;
    deadEnemy->state.alive = false;

    check(specials.activate(*owner, "sonar"), "Charged Sonar did not activate");
    check(specials.pose(*owner).active && specials.pose(*owner).body && specials.pose(*owner).casting,
          "Sonar did not enter its source body-cast state");
    for (const auto* enemy : {enemyA.get(), enemyB.get(), deadEnemy.get()}) {
        check(close(enemy->state.reveal, test::sonarSourceFixture.reveal)
              && enemy->state.revealTeam == owner->team()
              && close(enemy->state.track, test::sonarSourceFixture.reveal)
              && enemy->state.trackTeam == owner->team(),
              "Sonar did not reveal and track every opposing actor");
    }
    check(ally->state.reveal == 0.0 && ally->state.track == 0.0,
          "Sonar marked a teammate");

    for (int frame = 0; frame < test::sonarSourceFixture.castFrames; ++frame)
        check(specials.body(*owner, dt), "Sonar stopped owning the actor before its source cast ended");
    check(!specials.pose(*owner).active,
          "Sonar remained active after the source cast duration");
    const auto& sonar = test::sonarSourceFixture;
    check(close(owner->state.pos.x, sonar.x, 1e-6)
          && close(owner->state.pos.y, sonar.y, 1e-6)
          && close(owner->state.pos.z, sonar.z, 1e-6)
          && close(owner->state.vel.x, sonar.vx, 1e-6)
          && close(owner->state.vel.y, sonar.vy, 1e-6)
          && close(owner->state.vel.z, sonar.vz, 1e-6),
          "Sonar cast motion diverged from the live source snapshot");

    PlayerSimulation normal(stage, geometry, paint, 0);
    PlayerSimulation revealed(stage, geometry, paint, 0);
    normal.spawnAt({-12.0, 0.0, -10.0}, 0.0);
    revealed.spawnAt({12.0, 0.0, -10.0}, 0.0);
    normal.input.move = {1.0, 0.0, 0.0};
    revealed.input.move = {1.0, 0.0, 0.0};
    revealed.state.reveal = sonar.reveal;
    for (int frame = 0; frame < 120; ++frame) {
        normal.tick(dt);
        revealed.tick(dt);
    }
    check(close(horizontalSpeed(revealed) / horizontalSpeed(normal), sonar.observedSlow, 1e-6),
          "Sonar reveal did not apply the source movement slowdown");

    PlayerSimulation normalShooter(stage, geometry, paint, 0);
    PlayerSimulation revealedShooter(stage, geometry, paint, 0);
    normalShooter.state.ink = revealedShooter.state.ink = 100.0;
    normalShooter.input.fire = revealedShooter.input.fire = true;
    revealedShooter.state.reveal = sonar.reveal;
    normalShooter.tick(dt);
    revealedShooter.tick(dt);
    const double normalCost = 100.0 - normalShooter.state.ink;
    const double revealedCost = 100.0 - revealedShooter.state.ink;
    check(normalCost > 0.0 && close(revealedCost / normalCost, sonar.observedInkMul, 1e-8),
          "Sonar reveal did not apply the source ink multiplier");

    PlayerSimulation expiry(stage, geometry, paint, 1);
    expiry.state.reveal = expiry.state.track = sonar.reveal;
    expiry.state.revealTeam = expiry.state.trackTeam = 0;
    const int expiryFrames = static_cast<int>(std::round(sonar.expirySeconds / dt));
    for (int frame = 0; frame < expiryFrames; ++frame) expiry.tick(dt);
    check(close(expiry.state.reveal, sonar.revealAfterExpiry)
          && close(expiry.state.track, sonar.trackAfterExpiry),
          "Sonar reveal or tracking did not expire on the source timer");
}

int framesToWailPlacement(WorldSpecials& specials, PlayerSimulation& actor, bool fireAtThreshold = false) {
    bool sub = false, subReleased = false;
    int frames = 0;
    if (fireAtThreshold) {
        for (; frames < 12; ++frames) {
            specials.weapon(actor, dt, false, false, sub, subReleased);
            specials.tickActor(actor, dt);
        }
        const bool rejected = specials.weapon(actor, dt, true, true, sub, subReleased);
        check(rejected && specials.pose(actor).active
              && test::wailSourceFixture.thresholdRejected == 1,
              "Wail accepted fire at the source's strict 0.2 second boundary");
        specials.tickActor(actor, dt);
        check(specials.weapon(actor, dt, true, true, sub, subReleased)
              && !specials.pose(actor).active,
              "Wail did not place after the source fire threshold");
        return test::wailSourceFixture.pressFrames;
    }

    for (; frames < 400 && specials.pose(actor).active; ++frames) {
        specials.weapon(actor, dt, false, false, sub, subReleased);
        if (!specials.pose(actor).active) break;
        specials.tickActor(actor, dt);
    }
    if (specials.pose(actor).active)
        specials.weapon(actor, dt, false, false, sub, subReleased);
    return frames;
}

void testWail(const StageData& stage) {
    using namespace restoration;
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    CombatWorld world(stage, geometry, paint);
    WorldSpecials specials(world);
    auto owner = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    auto target = std::make_unique<PlayerSimulation>(stage, geometry, paint, 1, &world);
    auto offAxis = std::make_unique<PlayerSimulation>(stage, geometry, paint, 1, &world);
    auto ally = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    prepare(*owner, "wail", {0.0, 0.0, 0.0});
    prepare(*target, "wail", {0.0, 0.0, 18.0});
    prepare(*offAxis, "wail", {2.0, 0.0, 18.0});
    prepare(*ally, "wail", {0.0, 0.0, 18.0});
    owner->input.aimYaw = 0.2;
    owner->input.aimPitch = 0.8;
    target->state.hp = offAxis->state.hp = ally->state.hp = 5000.0;

    const auto& wail = test::wailSourceFixture;
    const glm::dvec3 mouth(wail.mouthX, wail.mouthY, wail.mouthZ);
    const glm::dvec3 direction(wail.dirX, wail.dirY, wail.dirZ);
    const auto wall = geometry.raycast(mouth, direction, wail.range);
    check(wall.hit && wall.dist < wail.range && wail.wallPiercing == 1,
          "Wail fixture line no longer crosses a solid wall");
    const glm::dvec3 targetCenter = mouth + direction * 18.0;
    target->state.pos = targetCenter - glm::dvec3(0.0, 0.8, 0.0);
    offAxis->state.pos = target->state.pos + glm::dvec3(2.0, 0.0, 0.0);
    ally->state.pos = target->state.pos;

    check(specials.activate(*owner, "wail"), "Charged Wail did not activate");
    check(specials.pose(*owner).active, "Wail did not enter its hold-and-aim state");
    check(specials.pose(*owner).noSquid && specials.pose(*owner).aimFace,
          "Wail source noSquid/aimFace state was absent from its public pose");
    check(framesToWailPlacement(specials, *owner, true) == wail.pressFrames,
          "Wail press timing differs from the source snapshot");
    for (int frame = 0; frame < wail.stepChargeFrames; ++frame)
        specials.tickWorld(dt);
    for (int frame = 0; frame < wail.stepBlastFrames; ++frame)
        specials.tickWorld(dt);
    const double damage = 5000.0 - target->state.hp;
    check(damage > 0.0 && close(damage, wail.steppedLineDamage, 1e-6),
          "Wail failed to damage a target behind the source-blocking wall");
    check(close(5000.0 - offAxis->state.hp, wail.offAxisDamage, 1e-6),
          "Wail damaged a target outside its source beam radius");
    check(close(5000.0 - ally->state.hp, 0.0),
          "Wail damaged a teammate");
    for (int frame = 0; frame < wail.stepFadeFrames; ++frame)
        specials.tickWorld(dt);
    check(close(5000.0 - target->state.hp, wail.steppedLineDamage, 1e-6),
          "Wail continued damaging after its source fade completed");

    auto timeout = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    prepare(*timeout, "wail", {0.0, 0.0, 0.0});
    check(specials.activate(*timeout, "wail"), "Timeout Wail did not activate");
    const int timeoutFrames = framesToWailPlacement(specials, *timeout);
    check(timeoutFrames == wail.timeoutFrames,
          "Wail did not auto-place on its source hold-time limit");

    auto bot = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    prepare(*bot, "wail", {0.0, 0.0, 0.0});
    bot->setBotControlled(true);
    // This is the captured gameplay uniform; source visual UUID draws are not native gameplay RNG.
    const double sourceUniform = wail.botUniform;
    bot->setRandomSource([sourceUniform] { return sourceUniform; });
    check(specials.activate(*bot, "wail"), "Bot Wail did not activate");
    const int botFrames = framesToWailPlacement(specials, *bot);
    check(botFrames == wail.botFrames,
          "Bot Wail did not auto-place using the captured source uniform");

    auto zooka = std::make_unique<PlayerSimulation>(stage, geometry, paint, 0, &world);
    prepare(*zooka, "zooka", {0.0, 0.0, 0.0});
    check(specials.activate(*zooka, "zooka") && specials.pose(*zooka).noSquid
          && specials.pose(*zooka).aimFace && close(specials.pose(*zooka).moveSpeed, 4.2),
          "Zooka source movement/noSquid/aimFace state was absent from its public pose");

    StageGeometry cleanupGeometry(stage);
    FacePaint cleanupPaint(stage, cleanupGeometry);
    CombatWorld cleanupWorld(stage, cleanupGeometry, cleanupPaint);
    WorldSpecials cleanupSpecials(cleanupWorld);
    auto cleanupOwner = std::make_unique<PlayerSimulation>(stage, cleanupGeometry, cleanupPaint, 0, &cleanupWorld);
    auto cleanupTarget = std::make_unique<PlayerSimulation>(stage, cleanupGeometry, cleanupPaint, 1, &cleanupWorld);
    prepare(*cleanupOwner, "wail", {0.0, 0.0, 0.0});
    cleanupOwner->input.aimYaw = 0.2;
    cleanupOwner->input.aimPitch = 0.8;
    cleanupTarget->state.hp = 5000.0;
    const glm::dvec3 cleanupMouth(wail.mouthX, wail.mouthY, wail.mouthZ);
    const glm::dvec3 cleanupDirection(wail.dirX, wail.dirY, wail.dirZ);
    cleanupTarget->state.pos = cleanupMouth + cleanupDirection * 18.0 - glm::dvec3(0.0, 0.8, 0.0);
    check(cleanupSpecials.activate(*cleanupOwner, "wail"), "Cleanup Wail did not activate");
    check(framesToWailPlacement(cleanupSpecials, *cleanupOwner, true) == wail.pressFrames,
          "Cleanup Wail did not place on the source press frame");
    cleanupSpecials.cleanup(*cleanupOwner);
    for (int frame = 0; frame < wail.stepChargeFrames + wail.stepBlastFrames; ++frame)
        cleanupSpecials.tickWorld(dt);
    check(close(cleanupTarget->state.hp, 5000.0),
          "Wail beam continued damaging after owner cleanup before charge completed");
    std::cout << "Sonar and Wail match actual source state, timing, movement, ink, and wall-piercing snapshots\n";
}

void testNoSquidActorState(const StageData& stage, std::string_view specialId) {
    using namespace restoration;
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    PlayerSimulation actor(stage, geometry, paint, 0);
    prepare(actor, specialId, {-12.0, 0.0, -10.0});
    actor.state.grounded = true;
    actor.input.move = {1.0, 0.0, 0.0};
    actor.input.squid = true;
    actor.input.special = true;
    actor.tick(dt);
    check(actor.combatPose().special && actor.combatPose().specialKind == std::string(specialId),
          "Held-squid special did not activate through PlayerSimulation");
    actor.input.special = false;
    for (int frame = 0; frame < 120; ++frame) actor.tick(dt);
    check(!actor.state.squid,
          "A source noSquid special allowed the actor to enter squid form");
    const double expectedSpeed = specialId == "wail" ? test::wailSourceFixture.holdSpeed : 4.2;
    check(close(horizontalSpeed(actor), expectedSpeed, 1e-6),
          "Special movement speed override differs from the live source contract");
}
} // namespace

int main() {
    try {
        const auto stage = makeStage();
        testSonar(stage);
        testWail(stage);
        testNoSquidActorState(stage, "zooka");
        testNoSquidActorState(stage, "wail");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
