#include "../src/game/BotSimulation.hpp"
#include "../src/game/ArsenalData.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/game/NavGraph.hpp"
#include "../src/world/StageGeometry.hpp"
#include "fixtures/ai_bot_oracle_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <glm/geometric.hpp>

namespace {
using namespace restoration;

std::string_view name(BotMode mode) {
    switch (mode) {
    case BotMode::Paint: return "paint";
    case BotMode::Fight: return "fight";
    case BotMode::Refill: return "refill";
    case BotMode::Retreat: return "retreat";
    }
    return "unknown";
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    using namespace restoration;
    const auto& stages = ReferenceStages();
    const auto found = std::find_if(stages.begin(), stages.end(), [](const StageData& stage) {
        return stage.id == "halyard" && stage.mode == "turf";
    });
    if (found == stages.end()) {
        std::cerr << "AI bot oracle: missing halyard Turf stage\n";
        return 1;
    }

    int failures = 0;
    int comparable = 0;
    for (const auto& expected : ai_bot_fixtures::oracles) {
        ++comparable;
        StageGeometry geometry(*found);
        FacePaint paint(*found, geometry);
        NavGraph nav(*found, geometry);
        CombatWorld world(*found, geometry, paint);
        PlayerSimulation actor(*found, geometry, paint, 0, &world);
        PlayerSimulation foe(*found, geometry, paint, 1, &world);
        actor.setLoadout("shooter", "bomb", "zooka");
        const bool shieldFight = expected.threat == "shield-fight";
        const bool canopyRoute = expected.threat == "canopy-route";
        foe.setLoadout((shieldFight || canopyRoute) ? "brolly" : "shooter", expected.threat == "waddle" ? "waddle" : "bomb", "zooka");
        actor.spawnAt(expected.bot, 0.0);
        foe.spawnAt(expected.foe, 0.0);
        actor.state.ink = expected.ink;
        actor.state.hp = expected.hp;
        if (shieldFight) {
            foe.input.aimYaw = std::atan2(actor.state.pos.x - foe.state.pos.x, actor.state.pos.z - foe.state.pos.z);
            foe.input.fire = true;
            for (int frame = 0; frame < 30; ++frame) foe.tick(1.0 / 60.0);
        } else if (canopyRoute) {
            foe.spawnAt(expected.bot + glm::dvec3(0, 0, -2.4), 0.0);
            world.launchCanopy(foe, foe.weapon().number("canopyHp"), {0, 0, 1}, 0.9);
            foe.state.alive = false;
        } else if (expected.threat == "waddle") {
            foe.input.aimYaw = 0.0;
            world.deploy(foe, "waddle");
            foe.state.alive = false;
        } else if (!expected.hasFoe) foe.state.alive = false;

        BotSimulation brain(actor, *found, geometry, paint, nav, world, expected.difficulty, expected.seed);
        const std::vector<BotSimulation*> bots{&brain};
        bool previousSub = false;
        int fireFrames = 0, subPresses = 0;
        for (int frame = 0; frame < expected.frames; ++frame) {
            brain.tick(1.0 / 60.0, true, bots);
            if (actor.input.fire) ++fireFrames;
            if (actor.input.sub && !previousSub) ++subPresses;
            previousSub = actor.input.sub;
        }

        const auto& move = actor.input.move;
        const double moveError = glm::length(move - expected.move);
        const double yawError = std::abs(std::remainder(actor.input.aimYaw - expected.aim.x, 6.2831853071795864769));
        const double pitchError = std::abs(actor.input.aimPitch - expected.aim.y);
        const bool ok = name(brain.mode()) == expected.mode && brain.hasTarget() == expected.targetSeen &&
            fireFrames == expected.fireFrames && subPresses == expected.subPresses &&
            moveError <= 0.03 && yawError <= 0.02 && pitchError <= 0.02 &&
            actor.input.fire == expected.fire && actor.input.sub == expected.sub &&
            actor.input.special == expected.special && actor.input.squid == expected.squid &&
            actor.input.jump == expected.jump && actor.input.cheer == expected.cheer;
        if (!ok) {
            ++failures;
            std::cerr << expected.name << ": source oracle mismatch mode=" << name(brain.mode())
                      << " target=" << brain.hasTarget() << " fireFrames=" << fireFrames
                      << " subPresses=" << subPresses << " move=" << move.x << ',' << move.y << ',' << move.z
                      << " aim=" << actor.input.aimYaw << ',' << actor.input.aimPitch
                      << " actions=" << actor.input.fire << ',' << actor.input.sub << ',' << actor.input.special
                      << ',' << actor.input.squid << ',' << actor.input.jump
                      << ',' << actor.input.cheer
                      << " expected mode=" << expected.mode << " target=" << expected.targetSeen
                      << " fireFrames=" << expected.fireFrames << " subPresses=" << expected.subPresses
                      << " move=" << expected.move.x << ',' << expected.move.y << ',' << expected.move.z
                      << " aim=" << expected.aim.x << ',' << expected.aim.y
                      << " actions=" << expected.fire << ',' << expected.sub << ',' << expected.special
                      << ',' << expected.squid << ',' << expected.jump << ',' << expected.cheer << '\n';
        }
    }
    if (failures) std::cerr << failures << " AI bot source-oracle failures\n";
    else std::cout << "AI bot: source intent matched for " << comparable << " source scenarios\n";
    return failures ? 1 : 0;
}
