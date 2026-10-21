#include "MatchSimulation.hpp"
#include "NavGraph.hpp"
#include "ZoneControl.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"
#include "zone_control_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace restoration;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "Zone control: " << message << '\n'; }
}

std::string zoneDiagnostics(const ZoneControl& zones) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << " [time=" << zones.time() << " owner=" << zones.owner()
        << " objective=" << zones.objective().id
        << " count=" << zones.counts()[0] << ',' << zones.counts()[1]
        << " penalty=" << zones.penalty()[0] << ',' << zones.penalty()[1]
        << " overtime=" << zones.overtime() << " neutral=" << zones.neutralTime()
        << " final=" << zones.final() << " nextSwap=" << zones.nextSwap();
    if (!zones.penaltyLog().empty()) {
        const auto& last = zones.penaltyLog().back();
        out << " lastPenalty=" << last.penalty << " start=" << last.start << " end=" << last.end;
    }
    if (zones.ended()) out << " ended=" << zones.reason() << " winner=" << zones.winner().value_or(-1);
    out << ']';
    return out.str();
}

std::string vectorText(const glm::dvec3& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(9) << value.x << ',' << value.y << ',' << value.z;
    return out.str();
}

void checkZone(bool condition, const std::string& message, const ZoneControl& zones) {
    check(condition, condition ? message : message + zoneDiagnostics(zones));
}

const StageData* stage(std::string_view id, std::string_view mode) {
    const auto& stages = ReferenceStages();
    const auto found = std::find_if(stages.begin(), stages.end(), [&](const StageData& value) {
        return value.id == id && value.mode == mode;
    });
    return found == stages.end() ? nullptr : &*found;
}

void advance(ZoneControl& zones, const std::vector<std::unique_ptr<PlayerSimulation>>& actors, double seconds) {
    while (seconds > 1e-9) {
        const double dt = std::min(0.1, seconds);
        zones.update(dt, actors);
        seconds -= dt;
    }
}

void paintCells(FacePaint& paint, const ZoneState& zone, int team, std::size_t count) {
    count = std::min(count, zone.region.cells.size());
    paint.setCells(std::vector<int>(zone.region.cells.begin(), zone.region.cells.begin() + static_cast<std::ptrdiff_t>(count)), team);
}

std::size_t atLeast(double fraction, std::size_t size) {
    return static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(size)));
}

std::vector<std::unique_ptr<PlayerSimulation>> oneActor(const StageData& stage, const StageGeometry& geometry, FacePaint& paint) {
    std::vector<std::unique_ptr<PlayerSimulation>> actors;
    actors.push_back(std::make_unique<PlayerSimulation>(stage, geometry, paint, 0));
    return actors;
}

void compareSourceGeometry() {
    for (const auto& expected : zone_fixtures::source) {
        const auto* data = stage(expected.id, "zones");
        check(data != nullptr, std::string(expected.id) + " source Zones stage exists");
        if (!data) continue;
        StageGeometry geometry(*data);
        FacePaint paint(*data, geometry);
        ZoneControl zones(*data, geometry, paint, 300.0, [] { return 0.5; });
        check(zones.zones().size() == expected.zones.size(), std::string(expected.id) + " zone count matches source");
        for (std::size_t i = 0; i < std::min(zones.zones().size(), expected.zones.size()); ++i) {
            const auto& actual = zones.zones()[i];
            const auto& oracle = expected.zones[i];
            std::string cellDetail = " actualCells=" + std::to_string(actual.region.cells.size()) +
                                     " expectedCells=" + std::to_string(oracle.cells.size());
            const auto common = std::min(actual.region.cells.size(), oracle.cells.size());
            const auto mismatch = std::mismatch(actual.region.cells.begin(), actual.region.cells.begin() + static_cast<std::ptrdiff_t>(common),
                                                oracle.cells.begin());
            if (mismatch.first != actual.region.cells.begin() + static_cast<std::ptrdiff_t>(common)) {
                const auto index = static_cast<std::size_t>(mismatch.first - actual.region.cells.begin());
                cellDetail += " firstMismatchAt=" + std::to_string(index) +
                              " actual=" + std::to_string(*mismatch.first) +
                              " expected=" + std::to_string(oracle.cells[index]);
            }
            check(actual.region.cells == oracle.cells,
                  std::string(expected.id) + " zone " + std::to_string(i) + " source cell mask" + cellDetail);
            check(glm::length(actual.center - oracle.center) <= 1e-6,
                  std::string(expected.id) + " zone " + std::to_string(i) + " source center actual=" +
                      vectorText(actual.center) + " expected=" + vectorText(oracle.center));
            check(std::abs(actual.reach - oracle.reach) <= 1e-6,
                  std::string(expected.id) + " zone " + std::to_string(i) + " source reach actual=" +
                      std::to_string(actual.reach) + " expected=" + std::to_string(oracle.reach));
        }
    }
}

void captureAndContestThresholds() {
    const auto* data = stage("tidewater", "zones");
    check(data != nullptr, "tidewater Zones stage exists");
    if (!data) return;
    StageGeometry geometry(*data);
    FacePaint paint(*data, geometry);
    ZoneControl zones(*data, geometry, paint, 300.0, [] { return 0.5; });
    auto actors = oneActor(*data, geometry, paint);
    const auto& zone = zones.zones().front();
    const auto captureCells = atLeast(0.80, zone.region.cells.size());
    const auto contestCells = atLeast(0.40, zone.region.cells.size());

    paintCells(paint, zone, 0, captureCells - 1);
    advance(zones, actors, 0.1);
    check(zone.owner == -1, "79-point-below-80 coverage stays neutral");
    paintCells(paint, zone, 0, captureCells);
    advance(zones, actors, 0.2);
    check(zone.owner == 0 && zones.owner() == 0, "80 percent captures the zone and objective");
    advance(zones, actors, 0.8);

    paintCells(paint, zone, 1, contestCells - 1);
    advance(zones, actors, 0.2);
    check(zone.owner == 0, "coverage below 40 percent does not neutralize a held zone");
    paintCells(paint, zone, 1, contestCells);
    advance(zones, actors, 0.2);
    check(zone.owner == -1 && zones.owner() == -1, "40 percent neutralizes a held zone");

    paintCells(paint, zone, 1, captureCells);
    advance(zones, actors, 0.2);
    check(zone.owner == 1 && zones.owner() == 1, "neutral zone can be recaptured at 80 percent");
    check(actors.front()->zoneTurf() == 0.0, "capture fill does not credit a character with zone turf");
}

void dualZoneCenterAndPenalty() {
    const auto* twoZone = stage("halyard", "zones");
    check(twoZone != nullptr, "halyard Zones stage exists");
    if (twoZone) {
        StageGeometry geometry(*twoZone);
        FacePaint paint(*twoZone, geometry);
        ZoneControl zones(*twoZone, geometry, paint, 300.0, [] { return 0.5; });
        auto actors = oneActor(*twoZone, geometry, paint);
        const auto need = atLeast(0.80, zones.zones()[1].region.cells.size());
        paintCells(paint, zones.zones()[0], 0, zones.zones()[0].region.cells.size());
        advance(zones, actors, 0.1);
        check(zones.zones()[0].owner == 0 && zones.owner() == -1,
              "holding only one centre zone does not control the two-zone objective");
        paintCells(paint, zones.zones()[1], 0, need);
        advance(zones, actors, 0.2);
        check(zones.zones()[1].owner == 0 && zones.owner() == 0,
              "holding both centre zones controls the objective");
        check(actors.front()->zoneTurf() == 0.0, "centre capture flood does not award character turf credit");
    }

    const auto* singleZone = stage("tidewater", "zones");
    if (!singleZone) { check(false, "tidewater Zones stage exists for penalty"); return; }
    StageGeometry geometry(*singleZone);
    FacePaint paint(*singleZone, geometry);
    ZoneControl zones(*singleZone, geometry, paint, 300.0, [] { return 0.5; });
    auto actors = oneActor(*singleZone, geometry, paint);
    const auto& zone = zones.zones().front();
    const auto take = atLeast(0.80, zone.region.cells.size());
    paintCells(paint, zone, 0, zone.region.cells.size());
    advance(zones, actors, 0.1);
    // Stay clear of the 7.5 half-point in 0.75 * (start - end); double step accumulation
    // can otherwise make the nominal 10-second hold round down on one platform.
    advance(zones, actors, 10.0);
    paintCells(paint, zone, 1, atLeast(0.40, zone.region.cells.size()));
    advance(zones, actors, 0.2);
    paintCells(paint, zone, 1, take);
    advance(zones, actors, 0.2);
    checkZone(zones.owner() == 1, "opponent captures after neutralizing the held zone", zones);
    checkZone(zones.penaltyLog().size() == 1 && zones.penaltyLog().front().team == 0,
              "losing an objective after the opponent takes it records a penalty", zones);
    if (!zones.penaltyLog().empty()) {
        const auto& penalty = zones.penaltyLog().front();
        const double expected = std::max(0.0, std::round(0.75 * (penalty.start - penalty.end))) +
                                (penalty.start == 100.0 ? 1.0 : 0.0);
        checkZone(penalty.penalty == expected && penalty.penalty == 9.0 &&
                      std::abs(penalty.start - 100.0) < 1e-6 && std::abs(penalty.end - 89.9) < 0.2,
                  "penalty follows round(0.75 * count gap) plus the fresh-capture bonus", zones);
    }
}

void rotationFinalAndWipe() {
    const auto* data = stage("tidewater", "zones");
    check(data != nullptr, "tidewater Zones stage exists for rotation");
    if (!data) return;
    StageGeometry geometry(*data);
    FacePaint paint(*data, geometry);
    ZoneControl zones(*data, geometry, paint, 120.0, [] { return 0.999999; });
    auto actors = oneActor(*data, geometry, paint);

    advance(zones, actors, 60.2);
    checkZone(zones.objective().id == "sideB", "scheduled rotation leaves centre for seeded side objective", zones);
    checkZone(zones.objective().zones.size() == 1 && zones.zones()[zones.activeIndices().front()].flood.has_value() &&
              zones.zones()[zones.activeIndices().front()].flood->team == -1,
          "new objective is wiped without character credit", zones);
    const double untilFinal = 120.0 - zones.time() - 30.0;
    checkZone(zones.nextSwap() > 0.0 && zones.nextSwap() <= untilFinal + 0.6,
              "near-final schedule targets the final lock instead of another short rotation", zones);

    advance(zones, actors, 29.7);
    checkZone(!zones.final() && zones.objective().id == "sideB", "side objective remains live up to the final lock", zones);
    advance(zones, actors, 0.2);
    checkZone(zones.final() && zones.objective().id == "center" && zones.nextSwap() == 0.0,
              "final lock returns a side objective to the centre and stops rotations", zones);
    const auto finalEvent = std::find_if(zones.events().begin(), zones.events().end(), [](const ZoneEvent& event) {
        return event.type == ZoneEventType::FinalStarted;
    });
    checkZone(finalEvent != zones.events().end() && finalEvent->moved,
              "moving into the final centre emits a moved final event", zones);
    check(actors.front()->zoneTurf() == 0.0, "objective wipe does not award character turf credit");
    advance(zones, actors, 1.0);
    checkZone(zones.objective().id == "center", "no objective rotation occurs after the final lock", zones);
}

void timeoutOvertimeAndKnockout() {
    const auto* data = stage("tidewater", "zones");
    check(data != nullptr, "tidewater Zones stage exists for match endings");
    if (!data) return;

    {
        StageGeometry geometry(*data);
        FacePaint paint(*data, geometry);
        ZoneControl zones(*data, geometry, paint, 300.0, [] { return 0.5; });
        auto actors = oneActor(*data, geometry, paint);
        paintCells(paint, zones.zones().front(), 0, zones.zones().front().region.cells.size());
        advance(zones, actors, 0.1);
        advance(zones, actors, 1.0);
        check(zones.timeUp() && zones.ended() && zones.winner() == 0 && zones.reason() == "time",
              "time expires immediately when the trailing team does not hold the objective");
    }

    {
        StageGeometry geometry(*data);
        FacePaint paint(*data, geometry);
        ZoneControl zones(*data, geometry, paint, 300.0, [] { return 0.5; });
        auto actors = oneActor(*data, geometry, paint);
        const auto& zone = zones.zones().front();
        paintCells(paint, zone, 0, zone.region.cells.size());
        advance(zones, actors, 0.1);
        advance(zones, actors, 20.0);
        paintCells(paint, zone, 1, atLeast(0.40, zone.region.cells.size()));
        advance(zones, actors, 0.2);
        paintCells(paint, zone, 1, zone.region.cells.size());
        advance(zones, actors, 0.2);
        checkZone(zones.owner() == 1 && zones.counts()[0] < zones.counts()[1],
                  "team holding the zone can still trail on count at the horn", zones);
        checkZone(!zones.timeUp() && zones.overtime(), "trailing holder starts overtime", zones);
        // Let the source-matched 0.6 s capture flood finish before placing the
        // neutralising ink. The flood advances before coverage sampling and would
        // otherwise overwrite these cells.
        advance(zones, actors, 3.0);
        paintCells(paint, zone, 0, atLeast(0.40, zone.region.cells.size()));
        advance(zones, actors, 0.2);
        advance(zones, actors, 10.1);
        checkZone(zones.ended() && zones.winner() == 0 && zones.reason() == "neutralised",
                  "overtime ends for the leader after the losing team is off objective for ten seconds", zones);
    }

    {
        StageGeometry geometry(*data);
        FacePaint paint(*data, geometry);
        ZoneControl zones(*data, geometry, paint, 20.0, [] { return 0.5; });
        auto actors = oneActor(*data, geometry, paint);
        paintCells(paint, zones.zones().front(), 0, zones.zones().front().region.cells.size());
        advance(zones, actors, 0.1);
        while (!zones.ended() && zones.time() < 120.0) advance(zones, actors, 0.1);
        check(zones.ended() && zones.winner() == 0 && zones.reason() == "knockout",
              "count reaching zero ends the match by knockout");
    }
}

void pauseAndRestart() {
    const auto* data = stage("halyard", "zones");
    check(data != nullptr, "halyard Zones stage exists for match lifecycle");
    if (!data) return;
    StageGeometry geometry(*data);
    FacePaint paint(*data, geometry);
    NavGraph nav(*data, geometry);
    MatchOptions options;
    options.mode = "zones";
    options.duration = 120;
    options.seed = 29;
    MatchSimulation match(*data, geometry, paint, nav, options);
    match.startPlaying();
    const auto& active = match.zones()->zones();
    paint.setCells(active[0].region.cells, 0);
    paint.setCells(active[1].region.cells, 0);
    match.paused = true;
    match.tick(0.1);
    check(match.zones()->time() == 0.0 && match.remaining() == 120.0 && match.zones()->owner() == -1,
          "pause freezes the Zones clock, score, and capture processing");

    match.paused = false;
    match.tick(0.1);
    check(match.zones()->owner() == 0 && match.zones()->time() > 0.0 && match.remaining() < 120.0,
          "unpausing resumes capture and match time");
    match.restart();
    check(match.zones() && match.zones()->time() == 0.0 && match.zones()->owner() == -1 &&
              match.zones()->counts() == std::array<double, 2>{100.0, 100.0} &&
              match.zones()->events().empty() && !match.paused,
          "restart creates a clean Zones match");
    check(paint.counts() == std::array<int, 2>{0, 0} &&
              std::all_of(paint.grid().begin(), paint.grid().end(), [](std::uint8_t value) { return value == 0; }),
          "restart clears gameplay ink");
}

} // namespace

int main() {
    compareSourceGeometry();
    captureAndContestThresholds();
    dualZoneCenterAndPenalty();
    rotationFinalAndWipe();
    timeoutOvertimeAndKnockout();
    pauseAndRestart();
    if (failures) return 1;
    std::cout << "Zone control matched source geometry and rule thresholds\n";
    return 0;
}
