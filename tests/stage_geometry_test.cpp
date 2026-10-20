#include "../src/world/StageGeometry.hpp"
#include "reference_fixtures.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

using namespace restoration;

namespace {
bool near(double a, double b, double tolerance = 0.002) { return std::abs(a - b) <= tolerance; }
bool near(glm::dvec3 a, glm::dvec3 b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
}

int main() {
    const auto& stages = ReferenceStages();
    int failures = 0;
    auto check = [&](bool okay, const std::string& context) {
        if (!okay) { std::cerr << context << '\n'; ++failures; }
    };

    std::set<std::string> maps, modes;
    for (const auto& stage : stages) {
        maps.emplace(stage.id);
        modes.emplace(stage.mode);
        check(stage.bounds.x < stage.bounds.y && stage.bounds.z < stage.bounds.w, "invalid bounds: " + std::string(stage.id));
        check(!stage.blocks.empty() && !stage.faces.empty(), "empty geometry: " + std::string(stage.id));
        for (const auto& pad : stage.spawnPads) {
            check(pad.x >= stage.bounds.x && pad.x <= stage.bounds.y && pad.z >= stage.bounds.z && pad.z <= stage.bounds.w,
                  "spawn outside bounds: " + std::string(stage.id));
        }
        for (const auto& block : stage.blocks) for (int face : block.faces) {
            check(face >= -1 && face < static_cast<int>(stage.faces.size()), "invalid block face: " + std::string(stage.id));
        }
    }
    check(stages.size() == 23 && maps.size() == 8 && modes == std::set<std::string>{"boss", "turf", "zones"},
          "expected eight maps with 23 legal mode variants");

    for (size_t i = 0; i < fixtures::ground.size(); ++i) {
        const auto& expected = fixtures::ground[i];
        StageGeometry geometry(stages.at(expected.stage));
        const double actual = geometry.groundHeight(expected.x, expected.z, expected.yMax, expected.skipGrates);
        if ((std::isfinite(actual) == expected.hit) && (!expected.hit || near(actual, expected.y))) continue;
        check(false, "ground fixture " + std::to_string(i) + " (" + std::string(stages[expected.stage].id) + "): " + std::to_string(actual) + " vs " + std::to_string(expected.y));
    }
    for (size_t i = 0; i < fixtures::rays.size(); ++i) {
        const auto& expected = fixtures::rays[i];
        StageGeometry geometry(stages.at(expected.stage));
        const RayHit actual = geometry.raycast(expected.origin, expected.dir, expected.maxDist, expected.skipGrates);
        const bool okay = actual.hit == expected.hit && near(actual.dist, expected.dist) &&
            (!expected.hit || (actual.block == expected.block && actual.face == expected.face &&
                               near(actual.point, expected.point) && near(actual.normal, expected.normal) &&
                               (actual.face < 0 || (near(actual.u, expected.u) && near(actual.v, expected.v)))));
        if (!okay) check(false, "ray fixture " + std::to_string(i) + " (" + std::string(stages[expected.stage].id) +
                               "): block " + std::to_string(actual.block) + " vs " + std::to_string(expected.block));
    }

    StageData synthetic{};
    const std::array<glm::dvec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<int, 6> noFaces{{-1, -1, -1, -1, -1, -1}};
    synthetic.blocks.push_back({{0, 0, 0}, {1, 1, 1}, axes, noFaces, true, false, false, false, false, false, false});
    synthetic.blocks.push_back({{4, 0, 0}, {1, 1, 1}, axes, noFaces, true, true, false, false, false, false, false});
    synthetic.blocks.push_back({{8, 0, 0}, {1, 1, 1}, axes, noFaces, true, true, false, false, false, true, true});
    constexpr double c = 0.70710678;
    synthetic.blocks.push_back({{12, 0, 0}, {2, 1, 0.3}, {{{c, 0, -c}, {0, 1, 0}, {c, 0, c}}}, noFaces,
                                true, false, false, false, false, false, false});
    synthetic.blocks.push_back({{16, 0, 0}, {0.5, 0.2, 2}, {{{0, 0, 1}, {-c, c, 0}, {c, c, 0}}}, noFaces,
                                true, false, false, false, false, false, false});
    StageGeometry geometry(synthetic);
    check(geometry.pointInBlock(synthetic.blocks[0], {0, 0, 0}) &&
          !geometry.pointInBlock(synthetic.blocks[0], {1, 0, 0}), "pointInBlock strict boundary");
    check(geometry.pointInside({0, 0, 0}) && !geometry.pointInside({0, 0, 0}, 0, 0), "pointInside exclusion");
    check(near(geometry.groundHeight(0, 0), 1) && !std::isfinite(geometry.groundHeight(0, 0, 0.9)),
          "ground height and yMax");
    check(near(geometry.groundHeight(4, 0), 1) && !std::isfinite(geometry.groundHeight(4, 0, 50, true)),
          "grate support vs squid fall-through");
    check(near(geometry.groundHeight(8, 0), 1) && !std::isfinite(geometry.groundHeight(8, 0, 50, true)),
          "rail support vs projectile pass-through");
    check(near(geometry.groundHeight(12, 0), 1) && !std::isfinite(geometry.groundHeight(13.8, 0)),
          "rotated narrow box footprint");
    check(geometry.groundHeight(17, 0) > geometry.groundHeight(15, 0) + 1.5, "sloped ramp support");
    check(geometry.raycast({4, 4, 0}, {0, -1, 0}, 10).block == 1 &&
          !geometry.raycast({4, 4, 0}, {0, -1, 0}, 10, true).hit, "grate ray traversal");
    check(geometry.raycast({12, 4, 0}, {0, -1, 0}, 10).block == 3 &&
          !geometry.raycast({12, 0, 0}, {0, 1, 0}, 10).hit, "oriented ray and inside-origin behavior");

    if (failures) std::cerr << failures << " stage geometry failures\n";
    else std::cout << "stage geometry: " << fixtures::ground.size() << " ground and " << fixtures::rays.size()
                   << " ray reference cases passed across " << stages.size() << " variants\n";
    return failures ? 1 : 0;
}
