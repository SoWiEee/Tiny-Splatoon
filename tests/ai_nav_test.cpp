#include "../src/game/NavGraph.hpp"
#include "../src/game/NavDirections.hpp"
#include "ai_nav_fixtures.hpp"

#include <iostream>
#include <string>
#include <algorithm>
#include <glm/geometric.hpp>

using namespace restoration;

int main() {
    int failures = 0;
    const auto& stages = ReferenceStages();
    for (const auto& expected : ai_fixtures::stages) {
        const StageData* stage = nullptr;
        for (const auto& candidate : stages) if (candidate.id == expected.id && candidate.mode == "turf") { stage = &candidate; break; }
        if (!stage) { std::cerr << expected.id << ": missing Turf stage\n"; ++failures; continue; }
        StageGeometry geometry(*stage);
        NavGraph nav(*stage, geometry);
        for (const auto& source : ai_fixtures::sourceRays) {
            if (expected.id != std::string(source.stage)) continue;
            const auto& node = nav.nodes()[source.node];
            const auto& radial = kSourceRadialDirections[source.radial];
            const auto got = geometry.raycast({node.x, node.y + 0.35, node.z},
                {radial.x, 0, radial.y}, source.maxDist, source.skipGrates);
            if (got.hit != source.hit || got.block != source.block || got.face != source.face ||
                std::abs(got.dist - source.distance) > 1e-6 ||
                glm::length(got.point - source.point) > 1e-6 ||
                glm::length(got.normal - source.normal) > 1e-6) {
                std::cerr << expected.id << ": source radial ray node=" << source.node << " direction=" << source.radial
                          << " got hit=" << got.hit << " block=" << got.block << " face=" << got.face
                          << " dist=" << got.dist << " point=" << got.point.x << ',' << got.point.y << ',' << got.point.z
                          << " normal=" << got.normal.x << ',' << got.normal.y << ',' << got.normal.z
                          << " expected hit=" << source.hit << " block=" << source.block << " face=" << source.face
                          << " dist=" << source.distance << " point=" << source.point.x << ',' << source.point.y << ',' << source.point.z
                          << " normal=" << source.normal.x << ',' << source.normal.y << ',' << source.normal.z << '\n';
                ++failures;
            }
        }
        if (static_cast<int>(nav.nodes().size()) != expected.nodeCount ||
            static_cast<int>(nav.validIds().size()) != expected.validCount) {
            std::cerr << expected.id << ": node count " << nav.nodes().size() << "/" << nav.validIds().size()
                      << " expected " << expected.nodeCount << "/" << expected.validCount << '\n';
            ++failures;
        }
        std::array<int, 4> edges{};
        for (const auto& node : nav.nodes()) for (const auto& edge : node.edges) ++edges[static_cast<int>(edge.type)];
        if (edges != expected.edgeCounts) {
            std::cerr << expected.id << ": edge count " << edges[0] << '/' << edges[1] << '/' << edges[2] << '/' << edges[3]
                      << " expected " << expected.edgeCounts[0] << '/' << expected.edgeCounts[1] << '/'
                      << expected.edgeCounts[2] << '/' << expected.edgeCounts[3] << '\n';
            ++failures;
        }
        std::vector<ai_fixtures::Climb> climbs;
        for (const auto& node : nav.nodes()) for (const auto& edge : node.edges)
            if (edge.type == NavEdgeType::Climb) climbs.push_back({node.id, edge.to, edge.wallPoint, edge.wallNormal, edge.topY});
        const auto sameClimb = [](const auto& a, const auto& b) { return a.from == b.from && a.to == b.to; };
        if (climbs.size() != expected.climbs.size() || !std::equal(climbs.begin(), climbs.end(), expected.climbs.begin(), expected.climbs.end(), sameClimb)) {
            for (const auto& edge : climbs)
                if (std::find_if(expected.climbs.begin(), expected.climbs.end(), [&](const auto& e) { return sameClimb(e, edge); }) == expected.climbs.end())
                    std::cerr << expected.id << ": extra climb " << edge.from << " -> " << edge.to << " wall=" << edge.wallPoint.x << ',' << edge.wallPoint.y << ',' << edge.wallPoint.z << " normal=" << edge.wallNormal.x << ',' << edge.wallNormal.y << " top=" << edge.topY << '\n';
            for (const auto& edge : expected.climbs)
                if (std::find_if(climbs.begin(), climbs.end(), [&](const auto& e) { return sameClimb(e, edge); }) == climbs.end())
                    std::cerr << expected.id << ": missing climb " << edge.from << " -> " << edge.to << " wall=" << edge.wallPoint.x << ',' << edge.wallPoint.y << ',' << edge.wallPoint.z << " normal=" << edge.wallNormal.x << ',' << edge.wallNormal.y << " top=" << edge.topY << '\n';
            ++failures;
        }
        for (const auto& source : expected.climbs) {
            const auto it = std::find_if(climbs.begin(), climbs.end(), [&](const auto& edge) { return sameClimb(edge, source); });
            if (it == climbs.end()) continue;
            if (glm::length(it->wallPoint - source.wallPoint) > 1e-6 || glm::length(it->wallNormal - source.wallNormal) > 1e-6 || std::abs(it->topY - source.topY) > 1e-6) {
                std::cerr << expected.id << ": climb hit differs " << source.from << " -> " << source.to
                          << " got=" << it->wallPoint.x << ',' << it->wallPoint.y << ',' << it->wallPoint.z
                          << " normal=" << it->wallNormal.x << ',' << it->wallNormal.y << " top=" << it->topY
                          << " expected=" << source.wallPoint.x << ',' << source.wallPoint.y << ',' << source.wallPoint.z
                          << " normal=" << source.wallNormal.x << ',' << source.wallNormal.y << " top=" << source.topY << '\n';
                ++failures;
            }
        }
        for (std::size_t i = 0; i < expected.probes.size(); ++i) {
            const auto& p = expected.probes[i];
            const int got = nav.nearest(p.pos, p.maxUp, p.start);
            if (got != p.nearest) { std::cerr << expected.id << ": nearest " << i << " = " << got << " expected " << p.nearest << '\n'; ++failures; }
        }
        for (std::size_t i = 0; i < expected.routes.size(); ++i) {
            const auto& p = expected.routes[i];
            const auto got = nav.path(p.from, p.to, p.team, 6000, p.noClimb);
            if (got != p.nodes) {
                std::cerr << expected.id << ": route " << i << " differs, length " << got.size() << " expected " << p.nodes.size() << '\n';
                for (std::size_t j = 0; j < std::min(got.size(), p.nodes.size()); ++j) if (got[j] != p.nodes[j]) {
                    std::cerr << "  first route difference " << j << ": " << got[j] << " vs " << p.nodes[j] << '\n'; break;
                }
                ++failures;
            }
        }
    }
    if (failures) std::cerr << failures << " AI nav fixture failures\n";
    else std::cout << "AI nav: seven source Turf graphs and 21 routes matched\n";
    return failures ? 1 : 0;
}
