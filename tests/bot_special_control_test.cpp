#include "../src/game/ArsenalData.hpp"
#include "../src/game/BotSimulation.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/game/NavGraph.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "../src/world/StageGeometry.hpp"
#include "bot_special_control_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <glm/geometric.hpp>

int main() {
    using namespace restoration;
    const auto& stages = ReferenceStages();
    const auto found = std::find_if(stages.begin(), stages.end(), [](const StageData& stage) {
        return stage.id == "halyard" && stage.mode == "turf";
    });
    if (found == stages.end()) {
        std::cerr << "Bot special control: missing halyard Turf stage\n";
        return 1;
    }

    int failures = 0;
    for (const auto& expected : bot_special_fixtures::oracles) {
        StageGeometry geometry(*found);
        FacePaint paint(*found, geometry);
        NavGraph nav(*found, geometry);
        CombatWorld world(*found, geometry, paint);
        PlayerSimulation actor(*found, geometry, paint, 0, &world);
        PlayerSimulation foe(*found, geometry, paint, 1, &world);
        actor.setLoadout("shooter", "bomb", std::string(expected.special));
        foe.setLoadout("shooter", "bomb", "zooka");
        actor.spawnAt(expected.bot, 0.0);
        foe.spawnAt(expected.foe, 0.0);

        constexpr double dt = 1.0 / 60.0;
        if (expected.active) {
            actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
            actor.input.special = true;
            actor.tick(dt);
            actor.input = {};
            for (int frame = 0; actor.specialPose().charge < expected.charge && frame < 600; ++frame) {
                actor.tick(dt);
            }
        } else if (expected.ready) {
            actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
        }
        if (!expected.active && actor.state.grounded != expected.grounded) {
            std::cerr << expected.name << ": source/native starting grounded state differs\n";
            ++failures;
            continue;
        }
        if (expected.active) {
            const auto pose = actor.specialPose();
            if (!pose.active || pose.kind != expected.special || actor.state.grounded != expected.grounded ||
                (expected.special == "booyah" && pose.charge < 1.0)) {
                std::cerr << expected.name << ": native special setup did not reach its source fixture state\n";
                ++failures;
                continue;
            }
            // Match the source's synthetic, frozen actor pose after native activation initializes its special state.
            actor.state.pos = expected.bot;
            actor.state.vel = glm::dvec3(0.0);
            actor.state.yaw = 0.0;
            actor.state.hp = actor.state.ink = 100.0;
            actor.state.grounded = expected.grounded;
            actor.state.climbing = actor.state.submerged = actor.state.squid = false;
            actor.state.groundTeam = 0;
            actor.state.smoothY = 0.0;
        }

        BotSimulation brain(actor, *found, geometry, paint, nav, world, "normal", expected.seed);
        // The source oracle advances BotBrain against a fixed actor pose; keep this actor frozen during controller updates too.
        const std::vector<BotSimulation*> bots{&brain};
        bool previousSub = false;
        int specialFrames = 0, fireFrames = 0, subPresses = 0, jumpFrames = 0, squidFrames = 0;
        for (int frame = 0; frame < expected.frames; ++frame) {
            brain.tick(dt, true, bots);
            if (actor.input.special) ++specialFrames;
            if (actor.input.fire) ++fireFrames;
            if (actor.input.sub && !previousSub) ++subPresses;
            if (actor.input.jump) ++jumpFrames;
            if (actor.input.squid) ++squidFrames;
            previousSub = actor.input.sub;
            if (expected.ready && actor.input.special) break;
        }

        const bool ok = specialFrames == expected.specialFrames && fireFrames == expected.fireFrames &&
            subPresses == expected.subPresses && jumpFrames == expected.jumpFrames && squidFrames == expected.squidFrames &&
            glm::length(actor.input.move - expected.move) <= 1e-6 &&
            actor.input.fire == expected.fire && actor.input.sub == expected.sub &&
            actor.input.special == expected.specialInput && actor.input.jump == expected.jump &&
            actor.input.squid == expected.squid;
        if (!ok) {
            ++failures;
            std::cerr << expected.name << ": source intent mismatch special/fire/sub/jump/squid="
                      << specialFrames << '/' << fireFrames << '/' << subPresses << '/' << jumpFrames << '/' << squidFrames
            << " move=" << std::setprecision(9) << actor.input.move.x << ',' << actor.input.move.y << ',' << actor.input.move.z
                      << " final=" << actor.input.special << '/' << actor.input.fire << '/' << actor.input.sub << '/'
                      << actor.input.jump << '/' << actor.input.squid << " expected="
                      << expected.specialFrames << '/' << expected.fireFrames << '/' << expected.subPresses << '/'
                      << expected.jumpFrames << '/' << expected.squidFrames << " final=" << expected.specialInput << '/'
                      << expected.fire << '/' << expected.sub << '/' << expected.jump << '/' << expected.squid << '\n';
        }
    }
    if (failures) std::cerr << failures << " Bot special source-oracle failures\n";
    else std::cout << "Bot special control: intent matched source BotBrain for "
                   << bot_special_fixtures::oracles.size() << " source scenarios\n";
    return failures ? 1 : 0;
}
