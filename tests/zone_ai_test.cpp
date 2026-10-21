#include "ArsenalData.hpp"
#include "BotSimulation.hpp"
#include "CombatWorld.hpp"
#include "FacePaint.hpp"
#include "NavGraph.hpp"
#include "PlayerSimulation.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"
#include "ZoneControl.hpp"
#include "ZonePlan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace restoration;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "Zone AI: " << message << '\n'; }
}

const StageData* findStage(std::string_view id) {
    const auto& stages = ReferenceStages();
    const auto found = std::find_if(stages.begin(), stages.end(), [&](const StageData& value) {
        return value.id == id && value.mode == "zones";
    });
    return found == stages.end() ? nullptr : &*found;
}

void sourceRolesAndRetakeWave() {
    const auto* stage = findStage("halyard");
    check(stage != nullptr, "halyard Zones stage exists");
    if (!stage) return;

    StageGeometry geometry(*stage);
    FacePaint paint(*stage, geometry);
    NavGraph nav(*stage, geometry);
    CombatWorld world(*stage, geometry, paint);
    world.setNavigation(nav);
    ZoneControl zones(*stage, geometry, paint, 300.0, [] { return 0.5; });

    std::vector<std::unique_ptr<PlayerSimulation>> actors;
    std::vector<std::unique_ptr<BotSimulation>> brains;
    std::vector<BotSimulation*> bots;
    const auto addBot = [&](int team, std::string_view main) {
        auto actor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, team, &world);
        actor->setLoadout(std::string(main), "bomb", "zooka");
        actor->setRandomSource([] { return 0.5; });
        actor->spawnAt(stage->spawnPads[team], team == 0 ? 0.0 : 3.14159265358979323846);
        auto brain = std::make_unique<BotSimulation>(*actor, *stage, geometry, paint, nav, world,
                                                     "normal", 41u + static_cast<unsigned>(actors.size()));
        actors.push_back(std::move(actor));
        bots.push_back(brain.get());
        brains.push_back(std::move(brain));
    };

    addBot(0, "shooter");
    addBot(1, "shooter");
    addBot(1, "shooter");
    addBot(1, "charger");
    for (const int zoneId : zones.activeIndices()) paint.setCells(zones.zones()[zoneId].region.cells, 1);
    zones.update(0.1, actors);
    check(zones.owner() == 1, "source role fixture gives team one the two-zone centre");

    ZonePlan plan(zones, *stage, geometry, paint, nav, 23u, [] { return 0.5; });
    for (auto& brain : brains) brain->setZonePlan(&plan);
    plan.update(0.0, bots);
    const auto target = zones.activeIndices().front();
    check(plan.waveState(0) == ZoneWaveState::Stage && plan.assignment(*brains[0]).zone == target,
          "retake wave stages on the first least-held centre zone, matching bots.js");

    std::array<int, 5> roles{};
    for (std::size_t i = 1; i < brains.size(); ++i)
        ++roles[static_cast<std::size_t>(plan.assignment(*brains[i]).role)];
    check(roles[static_cast<std::size_t>(ZoneBotRole::Push)] == 1 &&
              roles[static_cast<std::size_t>(ZoneBotRole::Guard)] == 1 &&
              roles[static_cast<std::size_t>(ZoneBotRole::Watch)] == 1,
          "source hold roles assign one push, one guard, and one charger watch");

    plan.update(15.1, bots);
    check(plan.waveState(0) == ZoneWaveState::Push && plan.assignment(*brains[0]).zone == target,
          "retake wave pushes after its source fifteen-second staging limit");
    actors[0]->state.alive = false;
    plan.update(4.1, bots);
    check(plan.waveState(0) == ZoneWaveState::Stage && plan.assignment(*brains[0]).zone == target,
          "retake wave regroups after the push is wiped out");
}

void seededBotMovesAndPaintsItsActiveZone() {
    const auto* stage = findStage("halyard");
    check(stage != nullptr, "halyard Zones stage exists for seeded bot");
    if (!stage) return;

    StageGeometry geometry(*stage);
    FacePaint paint(*stage, geometry);
    NavGraph nav(*stage, geometry);
    CombatWorld world(*stage, geometry, paint);
    world.setNavigation(nav);
    ZoneControl zones(*stage, geometry, paint, 300.0, [] { return 0.5; });
    std::vector<std::unique_ptr<PlayerSimulation>> actors;
    auto actor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
    actor->setLoadout("shooter", "bomb", "zooka");
    actor->setRandomSource([] { return 0.5; });
    actor->spawnAt(stage->spawnPads[0], 0.0);
    actors.push_back(std::move(actor));
    auto brain = std::make_unique<BotSimulation>(*actors.front(), *stage, geometry, paint, nav, world, "normal", 0x5A17u);
    std::vector<BotSimulation*> bots{brain.get()};
    ZonePlan plan(zones, *stage, geometry, paint, nav, 0x5A17u, [] { return 0.5; });
    brain->setZonePlan(&plan);
    const auto start = actors.front()->state.pos;
    const int activeZone = zones.activeIndices().front();
    bool paintedBeforeCapture = false;
    int captureFrame = -1, firstInkFrame = -1, maxNeutralInk = 0, maxNeutralInkFrame = -1, fireFrames = 0;
    struct TracePoint {
        int frame = 0, goal = -1, navStart = -1, navSteps = 0, nextEdge = -1;
        int zoneInk = 0, owner = -1, fireFrames = 0;
        bool fire = false, hasGoal = false, grounded = false, climbing = false, squid = false, hasAimPoint = false;
        glm::dvec3 pos{0.0}, goalPos{0.0};
        double turf = 0.0, stuck = 0.0, yaw = 0.0, aimYaw = 0.0, aimPitch = 0.0;
    };
    constexpr std::array<int, 4> traceFrames{300, 900, 1200, 1800};
    std::array<TracePoint, traceFrames.size()> trace{};
    constexpr double dt = 1.0 / 60.0;
    // Keep the 5/15/20/30-second trace to distinguish a long but successful spawn
    // route from a bot that stalls at the boat edge. The paint assertion remains
    // tied to pre-capture zone ink.
    for (int frame = 0; frame < traceFrames.back(); ++frame) {
        zones.update(dt, actors);
        if (zones.zones()[activeZone].owner >= 0 && captureFrame < 0) captureFrame = frame;
        plan.update(dt, bots);
        brain->tick(dt, true, bots);
        actors.front()->tick(dt);
        world.tick(dt);
        const auto& cells = zones.zones()[activeZone].region.cells;
        const auto teamInk = std::count_if(cells.begin(), cells.end(), [&](int cell) {
            return paint.grid()[static_cast<std::size_t>(cell)] == 1;
        });
        if (actors.front()->input.fire) ++fireFrames;
        if (zones.zones()[activeZone].owner == -1) {
            if (teamInk > 0 && firstInkFrame < 0) firstInkFrame = frame;
            if (teamInk > maxNeutralInk) { maxNeutralInk = static_cast<int>(teamInk); maxNeutralInkFrame = frame; }
            if (teamInk > 0) paintedBeforeCapture = true;
        }
        const int elapsedFrames = frame + 1;
        const auto checkpoint = std::find(traceFrames.begin(), traceFrames.end(), elapsedFrames);
        if (checkpoint != traceFrames.end()) {
            auto& point = trace[static_cast<std::size_t>(checkpoint - traceFrames.begin())];
            point.frame = elapsedFrames;
            point.pos = actors.front()->state.pos;
            point.goal = brain->goal();
            point.hasGoal = point.goal >= 0 && point.goal < static_cast<int>(nav.nodes().size());
            if (point.hasGoal) {
                const auto& node = nav.nodes()[static_cast<std::size_t>(point.goal)];
                point.goalPos = {node.x, node.y, node.z};
            }
            point.navStart = nav.nearest(point.pos, 1.2, true);
            if (point.navStart >= 0 && point.goal >= 0) {
                const auto path = nav.path(point.navStart, point.goal, 0);
                point.navSteps = static_cast<int>(path.size());
                if (path.size() > 1) point.nextEdge = static_cast<int>(nav.edgeType(path[0], path[1]));
            }
            point.zoneInk = static_cast<int>(teamInk);
            point.owner = zones.zones()[activeZone].owner;
            point.fire = actors.front()->input.fire;
            point.grounded = actors.front()->state.grounded;
            point.climbing = actors.front()->state.climbing;
            point.squid = actors.front()->state.squid;
            point.yaw = actors.front()->state.yaw;
            point.aimYaw = actors.front()->input.aimYaw;
            point.aimPitch = actors.front()->input.aimPitch;
            point.hasAimPoint = actors.front()->input.hasAimPoint;
            point.fireFrames = fireFrames;
            point.turf = actors.front()->turfPainted();
            point.stuck = brain->stuckTime();
        }
    }

    const auto& moved = actors.front()->state.pos;
    const int goal = brain->goal();
    const auto& nodes = nav.nodes();
    const bool hasGoalNode = goal >= 0 && goal < static_cast<int>(nodes.size());
    const auto goalPos = hasGoalNode ? glm::dvec3(nodes[static_cast<std::size_t>(goal)].x,
                                                  nodes[static_cast<std::size_t>(goal)].y,
                                                  nodes[static_cast<std::size_t>(goal)].z)
                                     : glm::dvec3(0.0);
    std::ostringstream diagnostics;
    diagnostics << std::fixed << std::setprecision(3)
                << " [start=" << start.x << ',' << start.y << ',' << start.z
                << " pos=" << moved.x << ',' << moved.y << ',' << moved.z
                << " goal=" << goal;
    if (hasGoalNode) diagnostics << " goalPos=" << goalPos.x << ',' << goalPos.y << ',' << goalPos.z;
    diagnostics << " yaw=" << actors.front()->state.yaw
                << " inputMove=" << actors.front()->input.move.x << ',' << actors.front()->input.move.z
                << " aim=" << actors.front()->input.aimYaw << ',' << actors.front()->input.aimPitch
                << " hasAimPoint=" << actors.front()->input.hasAimPoint
                << " grounded=" << actors.front()->state.grounded << " climbing=" << actors.front()->state.climbing
                << " squid=" << actors.front()->state.squid
                << " fire=" << actors.front()->input.fire << " fireFrames=" << fireFrames
                << " mode=" << static_cast<int>(brain->mode())
                << " stuck=" << brain->stuckTime() << " turf=" << actors.front()->turfPainted()
                << " zoneCells=" << zones.zones()[activeZone].region.cells.size()
                << " maxNeutralInk=" << maxNeutralInk << " maxNeutralInkFrame=" << maxNeutralInkFrame
                << " firstInkFrame=" << firstInkFrame << " captureFrame=" << captureFrame
                << " owner=" << zones.zones()[activeZone].owner;
    const auto detail = diagnostics.str();
    for (const auto& point : trace) {
        std::cout << std::fixed << std::setprecision(3) << "Zone AI trace t=" << point.frame / 60.0
                  << " pos=" << point.pos.x << ',' << point.pos.y << ',' << point.pos.z
                  << " goal=" << point.goal << " navStart=" << point.navStart
                  << " navSteps=" << point.navSteps << " nextEdge=" << point.nextEdge;
        if (point.hasGoal) std::cout << " goalPos=" << point.goalPos.x << ',' << point.goalPos.y << ',' << point.goalPos.z;
        std::cout << " yaw=" << point.yaw << " aim=" << point.aimYaw << ',' << point.aimPitch
                  << " hasAimPoint=" << point.hasAimPoint
                  << " grounded=" << point.grounded << " climbing=" << point.climbing << " squid=" << point.squid
                  << " fire=" << point.fire << " fireFrames=" << point.fireFrames
                  << " turf=" << point.turf << " zoneInk=" << point.zoneInk
                  << " owner=" << point.owner << " stuck=" << point.stuck << '\n';
    }
    check(std::hypot(moved.x - start.x, moved.z - start.z) > 1.0 && brain->goal() >= 0,
          "fixed-seed normal bot navigates toward its active zone" + detail);
    check(paintedBeforeCapture && actors.front()->turfPainted() > 0.0,
          "fixed-seed normal bot inks active-zone cells before capture flood" + detail);
}

} // namespace

int main() {
    sourceRolesAndRetakeWave();
    seededBotMovesAndPaintsItsActiveZone();
    if (failures) return 1;
    std::cout << "Zone AI source roles, retake waves, movement, and active-zone painting passed\n";
    return 0;
}
