#include "../src/game/MatchSimulation.hpp"
#include "../src/world/ReferenceData.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const restoration::StageData& bossStage() {
    for (const auto& stage : restoration::ReferenceStages())
        if (stage.id == "tidewater" && stage.mode == "turf" && !stage.onlineOnly)
            return stage;
    throw std::runtime_error("Missing legal offline Boss stage");
}

restoration::MatchSimulation makeMatch(const restoration::StageData& stage,
    restoration::StageGeometry& geometry, restoration::FacePaint& paint,
    restoration::NavGraph& nav, int duration, unsigned seed) {
    restoration::MatchOptions options;
    options.mode = "boss";
    options.duration = duration;
    options.seed = seed;
    return restoration::MatchSimulation(stage, geometry, paint, nav, options);
}
}

int main() {
    using namespace restoration;
    try {
        const auto& stage = bossStage();
        StageGeometry geometry(stage);
        FacePaint paint(stage, geometry, 2048);
        NavGraph nav(stage, geometry);

        auto loss = makeMatch(stage, geometry, paint, nav, 180, 2026);
        require(loss.phase() == MatchPhase::Intro, "Boss match must start with its intro");
        require(loss.actors().size() == 8 && loss.bots().size() == 7,
            "Boss match must start with one local actor and seven squad bots");
        require(loss.boss() && std::abs(loss.boss()->state().maxHp - 93600.0) < 1e-6,
            "Boss normal HP must count local actor at full weight and seven bots at 0.6");
        require(std::abs(loss.remaining() - 180.0) < 1e-9, "Boss duration option must initialize the clock");

        int poseUpdates = 0;
        double lastBossClock = -1;
        loss.setBossPoseSink([&](BossSimulation& boss) {
            ++poseUpdates;
            lastBossClock = boss.state().clock;
        });
        require(poseUpdates == 1 && lastBossClock == 0,
            "Installing the CPU pose sink must publish the reset pose");
        loss.startPlaying();
        loss.paused = true;
        loss.tick(1.0 / 60.0);
        require(poseUpdates == 1, "Paused simulation must not advance the Boss pose");
        loss.paused = false;
        loss.tick(1.0 / 60.0);
        require(poseUpdates == 2 && std::abs(lastBossClock - 1.0 / 60.0) < 1e-9,
            "Boss CPU pose must publish once after each fixed simulation update");

        // The maximum accepted simulation step keeps this lifecycle check quick
        // while still traversing the actual match timeout and finish branches.
        loss.startPlaying();
        for (int i = 0; i < 1800 && loss.phase() == MatchPhase::Playing; ++i)
            loss.tick(0.1);
        require(loss.phase() == MatchPhase::Finish, "Boss timeout must enter the finish phase");
        const auto lossHp = loss.boss()->state().hp;
        require(!loss.boss()->state().dead, "Timeout must be a Boss defeat, not a Boss kill");
        BossSegmentHit arbitraryHit;
        arbitraryHit.weak = true;
        require(loss.boss()->hit(loss.local(), 1000000.0, arbitraryHit, "shooter") == 0,
            "A finished Boss must reject damage from already-traveling attacks");
        for (int i = 0; i < 40 && loss.phase() == MatchPhase::Finish; ++i) loss.tick(0.1);
        require(loss.phase() == MatchPhase::Judge && loss.result() && loss.result()->boss,
            "Boss timeout must produce a Boss result");
        require(!loss.result()->boss->win && std::abs(loss.result()->boss->hp - lossHp) < 1e-9,
            "Timeout result must be a loss with frozen Boss HP");

        auto win = makeMatch(stage, geometry, paint, nav, 300, 7);
        win.startPlaying();
        for (int i = 0; i < 20; ++i) win.tick(0.1);
        require(win.boss()->state().visible, "Boss must become targetable after its reveal interval");
        BossSegmentHit lethalWeakHit;
        lethalWeakHit.weak = true;
        lethalWeakHit.point = {4.5, 2.25, 1.75};
        win.boss()->hit(win.local(), 1000000.0, lethalWeakHit, "shooter");
        const auto& bossHits = win.boss()->hudHitEvents();
        require(bossHits.size() == 1 && bossHits[0].serial == 1 && bossHits[0].attackerSlot == 0 &&
                bossHits[0].weak && bossHits[0].damage == win.boss()->state().maxHp &&
                bossHits[0].point.x == lethalWeakHit.point.x && bossHits[0].point.y == lethalWeakHit.point.y &&
                bossHits[0].point.z == lethalWeakHit.point.z,
            "Boss HUD events must report the landed weakpoint hit and its world impact point");
        win.tick(1.0 / 60.0);
        require(win.phase() == MatchPhase::Finish && win.boss()->state().dead,
            "A lethal weakpoint hit must enter the victory finish phase");
        for (int i = 0; i < 60 && win.phase() == MatchPhase::Finish; ++i) win.tick(0.1);
        require(win.phase() == MatchPhase::Judge && win.result() && win.result()->boss &&
                win.result()->boss->win && win.result()->boss->hp == 0,
            "Boss defeat must produce a win result with zero Boss HP");

        std::cout << "Boss lifecycle: 8-member squad, CPU pose tick, timeout loss and lethal weakpoint win\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
