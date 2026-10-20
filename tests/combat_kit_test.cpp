#include "../src/game/PlayerSimulation.hpp"
#include "combat_kit_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using namespace restoration;

int main() {
    StageData stage{};
    stage.bounds = {-30, 30, -30, 30};
    stage.spawnPads = {glm::dvec3(0), glm::dvec3(20, 0, 20)};
    stage.blocks.push_back({glm::dvec3(0, -0.5, 0), glm::dvec3(100, 0.5, 100),
        {glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)},
        {-1, -1, -1, -1, -1, -1}, true, false, false, false, false, false, false});
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry, 2048);

    std::unique_ptr<PlayerSimulation> actor;
    std::unique_ptr<WeaponRunner> runner;
    int caseIndex = -1;
    int frame = 0;
    int failures = 0;
    bool previousFire = false;
    bool previousJump = false;
    double fireYaw = 0.0;
    auto same = [](double a, double b) { return std::abs(a - b) < 1e-8; };

    for (const auto& f : test::combatKitFixtures) {
        if (caseIndex != f.caseIndex) {
            caseIndex = f.caseIndex;
            frame = 0;
            previousFire = previousJump = false;
            fireYaw = 0.0;
            runner.reset();
            actor.reset();
            actor = std::make_unique<PlayerSimulation>(stage, geometry, paint);
            actor->setLoadout(f.id);
            actor->state.grounded = true;
            actor->input.aimYaw = 0;
            actor->input.aimPitch = 0;
            if (std::string(f.id) == "blade") {
                actor->input.hasAimPoint = true;
                actor->input.aimPoint = {4, 1, 20};
            }
            actor->setRandomSource([] { return 0.5; });
            runner = std::make_unique<WeaponRunner>(*actor);
        }

        const double dt = 1.0 / 60.0;
        const int holdFrames = static_cast<int>(std::lround(f.hold / dt));
        const int jumpFrames = static_cast<int>(std::lround(f.jump / dt));
        while (frame < f.frame) {
            const int sourceFrame = frame + 1;
            const bool fire = sourceFrame <= holdFrames;
            const bool jump = sourceFrame <= jumpFrames;
            actor->input.fire = fire;
            actor->input.jump = jump;
            if (std::string(f.id) == "mitts" && jump && !previousJump)
                runner->tryDodge();

            const int shotsBefore = actor->shotsFired();
            runner->tick(dt, fire, fire && !previousFire);
            previousFire = fire;
            previousJump = jump;
            ++frame;

            if (actor->shotsFired() != shotsBefore) {
                const auto& projectiles = actor->projectiles();
                if (std::string(f.id) == "bow") {
                    const auto it = std::find_if(projectiles.rbegin(), projectiles.rend(),
                        [](const auto& p) { return p.weaponId == "bow" && p.centerArrow; });
                    if (it != projectiles.rend())
                        fireYaw = std::atan2(it->flightDir.x, it->flightDir.z);
                } else if (std::string(f.id) == "blade") {
                    fireYaw = runner->pose().actionYaw;
                } else {
                    const auto it = std::find_if(projectiles.rbegin(), projectiles.rend(),
                        [&](const auto& p) {
                            return p.weaponId == f.id && (f.id == std::string("mitts") ||
                                p.kind == "bladewave" || p.centerArrow);
                        });
                    if (it != projectiles.rend())
                        fireYaw = std::atan2(it->vel.x, it->vel.z);
                }
            }
        }

        const auto pose = runner->pose();
        const auto& projectiles = actor->projectiles();
        const auto arrow = std::find_if(projectiles.rbegin(), projectiles.rend(),
            [](const auto& p) { return p.weaponId == "bow" && p.centerArrow; });
        const int arrowCount = static_cast<int>(std::count_if(projectiles.begin(), projectiles.end(),
            [](const auto& p) { return p.kind == "arrow"; }));
        const int tier = arrow == projectiles.rend() ? -1 : arrow->tier;
        const double range = arrow == projectiles.rend() ? 0 : arrow->range;
        const double damage = arrow == projectiles.rend() ? 0 : arrow->damage;
        const int lodge = arrow == projectiles.rend() ? 0 : static_cast<int>(arrow->lodge);
        const bool leap = f.id == std::string("mitts") && pose.leaping;
        const bool ok = same(actor->state.ink, f.ink) && same(pose.charge, f.charge) &&
            same(fireYaw, f.yaw) && same(actor->state.vel.x, f.vx) &&
            same(actor->state.vel.y, f.vy) && same(actor->state.vel.z, f.vz) &&
            actor->shotsFired() == f.shots && pose.bowRingEvents == f.ringEvents &&
            pose.bowRing == f.ring && pose.charging == bool(f.charging) &&
            pose.firing == bool(f.firing) && leap == bool(f.leaping) &&
            pose.mittsLeapEvents == f.leaps && arrowCount == f.arrows && tier == f.tier &&
            lodge == f.lodge && same(range, f.range) && same(damage, f.damage) &&
            runner->busy() == bool(f.busy);
        if (!ok && failures++ < 16) {
            std::cerr << f.id << " case=" << f.caseIndex << " frame=" << f.frame
                << " ink=" << actor->state.ink << "/" << f.ink
                << " charge=" << pose.charge << "/" << f.charge
                << " yaw=" << fireYaw << "/" << f.yaw
                << " shots=" << actor->shotsFired() << "/" << f.shots
                << " ring=" << pose.bowRing << "/" << f.ring
                << " ringEvents=" << pose.bowRingEvents << "/" << f.ringEvents
                << " charging=" << pose.charging << "/" << f.charging
                << " firing=" << pose.firing << "/" << f.firing
                << " busy=" << runner->busy() << "/" << f.busy
                << " leaping=" << leap << "/" << f.leaping
                << " leapEvents=" << pose.mittsLeapEvents << "/" << f.leaps
                << " actorVelocity=" << actor->state.vel.x << "," << actor->state.vel.y << ","
                << actor->state.vel.z << "/" << f.vx << "," << f.vy << "," << f.vz
                << " arrows=" << arrowCount << "/" << f.arrows
                << " tier=" << tier << "/" << f.tier
                << " lodge=" << lodge << "/" << f.lodge
                << " range=" << range << "/" << f.range
                << " damage=" << damage << "/" << f.damage
                << " velocity=" << (arrow == projectiles.rend() ? 0 : arrow->vel.x) << ","
                << (arrow == projectiles.rend() ? 0 : arrow->vel.y) << ","
                << (arrow == projectiles.rend() ? 0 : arrow->vel.z) << "\n";
        }
    }

    if (failures) return 1;
    std::cout << test::combatKitFixtures.size()
        << " source Bow/Blade/Mitts action, state, and event snapshots passed\n";
}
