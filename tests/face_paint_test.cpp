#include "../src/world/FacePaint.hpp"
#include "../src/world/StageGeometry.hpp"
#include "reference_paint_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace restoration;

namespace {
bool near(double a, double b, double tolerance = 0.002) { return std::abs(a - b) <= tolerance; }
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& context) {
        if (!ok) { std::cerr << context << '\n'; ++failures; }
    };
    for (const auto& expected : test::paintFixtures()) {
        const auto& stages = ReferenceStages();
        const auto found = std::find_if(stages.begin(), stages.end(), [&](const StageData& stage) {
            return stage.id == expected.stageId && stage.mode == "turf";
        });
        check(found != stages.end(), std::string(expected.stageId) + " stage present");
        if (found == stages.end()) continue;
        StageGeometry geometry(*found);
        FacePaint paint(*found, geometry, expected.atlasSize);
        const std::string prefix = std::string(expected.stageId) + ": ";
        check(near(paint.pixelsPerMetre(), expected.ppm, 0.0001), prefix + "atlas density");
        check(paint.usedHeight() == expected.usedHeight, prefix + "atlas used height");
        check(paint.turfTotal() == expected.turfTotal, prefix + "live turf cells");
        check(near(paint.turfArea(), expected.turfArea, 0.01), prefix + "turf area " + std::to_string(paint.turfArea()) + " vs " + std::to_string(expected.turfArea));
        check(paint.grid().size() == static_cast<std::size_t>(expected.gridSize), prefix + "grid size");
        check(found->faces.size() == expected.faces.size(), prefix + "face count");
        int faceMismatch = 0;
        for (int id = 0; id < static_cast<int>(expected.faces.size()) && id < static_cast<int>(found->faces.size()); ++id) {
            const auto& e = expected.faces[id];
            const auto& a = paint.atlas(id);
            const auto& g = paint.faceGrid(id);
            if (a.x != e.x || a.y != e.y || a.width != e.w || a.height != e.h ||
                g.offset != e.grid || g.columns != e.nu || g.rows != e.nv ||
                !near(g.cellWidth, e.cu, 0.0001) || !near(g.cellHeight, e.cv, 0.0001)) {
                if (faceMismatch < 3) std::cerr << prefix << "face " << id << ": atlas " << a.x << "," << a.y << "," << a.width << "," << a.height << " vs " << e.x << "," << e.y << "," << e.w << "," << e.h << "; grid " << g.offset << "," << g.columns << "," << g.rows << " vs " << e.grid << "," << e.nu << "," << e.nv << '\n';
                ++faceMismatch;
            }
        }
        check(faceMismatch == 0, prefix + "atlas/grid faces mismatched: " + std::to_string(faceMismatch));
        std::vector<int> actualDead;
        for (int i = 0; i < static_cast<int>(paint.dead().size()); ++i) if (paint.dead()[i]) actualDead.push_back(i);
        check(actualDead == expected.dead, prefix + "buried cell mask");
        for (int i = 0; i < static_cast<int>(expected.steps.size()); ++i) {
            const auto& e = expected.steps[i];
            const auto beforeGrid = paint.grid();
            SplatOptions options;
            options.stretch = e.stretch;
            options.hasStretch = e.hasStretch;
            options.stretchAmt = e.stretchAmt;
            options.kind = static_cast<SplatKind>(e.kind);
            options.cosmetic = e.cosmetic;
            const double claimed = paint.splat(e.center, e.radius, e.team, e.seed, options);
            check(near(claimed, e.claimed, 0.002) && paint.counts()[0] == e.counts[0] &&
                      paint.counts()[1] == e.counts[1] && paint.version() == e.version,
                  prefix + "reference splat step " + std::to_string(i) + ": area " +
                      std::to_string(claimed) + " vs " + std::to_string(e.claimed) +
                      ", counts " + std::to_string(paint.counts()[0]) + "," +
                      std::to_string(paint.counts()[1]) + ", version " + std::to_string(paint.version()));
            std::vector<std::pair<int, int>> changed;
            for (int k = 0; k < static_cast<int>(paint.grid().size()); ++k)
                if (paint.grid()[k] != beforeGrid[k]) changed.emplace_back(k, paint.grid()[k]);
            check(changed == e.changed, prefix + "reference grid cells step " + std::to_string(i));
            paint.takeVisualSplats();
        }
        const auto before = paint.version();
        const auto unchanged = paint.splat(expected.steps.front().center, 0.2, 0, 0.3,
                                           SplatOptions{{0, 0, 0}, 1, false, true});
        check(unchanged == 0 && paint.version() == before, prefix + "cosmetic splat ignores gameplay grid");
        paint.clear();
        check(paint.counts() == std::array<int, 2>{0, 0} && paint.coverage() == std::array<double, 2>{0, 0}
                  && std::all_of(paint.grid().begin(), paint.grid().end(), [](std::uint8_t x) { return x == 0; }),
              prefix + "clear");
        check(paint.sample(-1, 0, 0) == 0, prefix + "invalid face sample");
        try { paint.splat({0, 0, 0}, -1, 0, 0.5); check(false, prefix + "negative radius accepted"); }
        catch (const std::invalid_argument&) {}
    }
    {
        const auto& stage = ReferenceStages().front();
        StageGeometry geometry(stage);
        FacePaint paint(stage, geometry, 2048);
        const auto grid = paint.grid();
        const auto counts = paint.counts();
        const int version = paint.version();
        SplatOptions cosmetic; cosmetic.cosmetic = true;
        check(paint.splat(stage.spawnPads[0], 0.12, 0, 0.25, cosmetic) == 0,
              "cosmetic splat does not claim gameplay ink");
        const auto visual = paint.takeVisualSplats();
        check(visual.size() == 1 && visual[0].cosmetic && visual[0].kind == SplatKind::Speck,
              "cosmetic splat is queued for visual ink");
        check(paint.grid() == grid && paint.counts() == counts && paint.version() == version,
              "visual splat leaves gameplay grid unchanged");
        paint.clear();
        check(paint.takeVisualClear(), "clear queues a visual atlas reset");
        check(!paint.takeVisualClear(), "visual atlas reset is consumed once");
    }
    if (failures) return 1;
    std::cout << "Reference face paint layout and splats passed\n";
}
