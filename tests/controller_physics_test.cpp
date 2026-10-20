#include "../src/world/StageGeometry.hpp"
#include "controller_fixtures.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace restoration;

namespace {
bool near(double a, double b) { return std::abs(a-b) <= 1e-6; }
bool near(const glm::dvec3& a, const glm::dvec3& b) {
    return near(a.x,b.x) && near(a.y,b.y) && near(a.z,b.z);
}
bool matches(const Contacts& actual, const controller_fixtures::Collision& expected) {
    return actual.ground == expected.ground && actual.wall == expected.wall && actual.ceiling == expected.ceiling &&
           near(actual.groundNormal,expected.groundNormal) && near(actual.wallNormal,expected.wallNormal) &&
           actual.groundBlock == expected.groundBlock && actual.wallBlock == expected.wallBlock;
}
}

int main() {
    const auto& stages = ReferenceStages();
    StageData synthetic{};
    synthetic.bounds = {-12, 12, -12, 12};
    synthetic.blocks.assign(controller_fixtures::syntheticBlocks.begin(), controller_fixtures::syntheticBlocks.end());
    std::vector<std::unique_ptr<StageGeometry>> geometries;
    for (const auto& stage : stages) geometries.push_back(std::make_unique<StageGeometry>(stage));
    geometries.push_back(std::make_unique<StageGeometry>(synthetic));
    auto geometry = [&](int stage) -> const StageGeometry& { return *geometries.at(stage); };
    int failures = 0;
    auto fail = [&](const char* query, std::size_t index, int stage) {
        std::cerr << query << " fixture " << index << " stage " << stage << '\n';
        ++failures;
    };
    for (std::size_t i=0; i<controller_fixtures::probes.size(); ++i) {
        const auto& f = controller_fixtures::probes[i];
        const auto actual = geometry(f.stage).groundProbe(f.pos.x,f.pos.y,f.pos.z,0.6,1.2,0.42,f.skip);
        if (actual.hit != f.hit || actual.center != f.center ||
            (f.hit && (!near(actual.y,f.y) || !near(actual.normal,f.normal) ||
                       actual.block != f.block || actual.face != f.face || actual.grate != f.grate ||
                       (f.face >= 0 && (!near(actual.u,f.u) || !near(actual.v,f.v)))))) fail("groundProbe",i,f.stage);
    }
    for (std::size_t i=0; i<controller_fixtures::bodies.size(); ++i) {
        const auto& f = controller_fixtures::bodies[i];
        auto pos = f.pos;
        const auto actual = geometry(f.stage).collideBody(pos,0.4,0.28,1.65,f.horizontal,f.skip);
        if (!near(pos,f.result) || !matches(actual,f)) fail("collideBody",i,f.stage);
    }
    for (std::size_t i=0; i<controller_fixtures::capsules.size(); ++i) {
        const auto& f = controller_fixtures::capsules[i];
        auto pos = f.pos;
        const auto actual = geometry(f.stage).collideCapsule(pos,0.4,1.65,3,f.skip);
        if (!near(pos,f.result) || !matches(actual,f)) fail("collideCapsule",i,f.stage);
    }
    for (std::size_t i=0; i<controller_fixtures::fits.size(); ++i) {
        const auto& f = controller_fixtures::fits[i];
        if (geometry(f.stage).bodyFits(f.pos,0.4,0.28,1.65,f.skip) != f.fits) fail("bodyFits",i,f.stage);
    }
    for (std::size_t i=0; i<controller_fixtures::cameras.size(); ++i) {
        const auto& f = controller_fixtures::cameras[i];
        const auto actual = geometry(f.stage).cameraProbe(f.pivot,f.back,5,0.62);
        if (!near(actual.hard,f.hard) || !near(actual.soft,f.soft) || actual.floor != f.floor) fail("cameraProbe",i,f.stage);
    }
    const auto& syntheticGeometry = geometry(static_cast<int>(stages.size()));
    for (std::size_t i=0; i<controller_fixtures::segments.size(); ++i) {
        const auto& f = controller_fixtures::segments[i];
        const auto actual = syntheticGeometry.segment(f.a,f.b,f.skip);
        if (actual.hit != f.hit || (f.hit && (!near(actual.dist,f.dist) || actual.block != f.block)))
            fail("segment",i,static_cast<int>(stages.size()));
    }
    for (std::size_t i=0; i<controller_fixtures::closest.size(); ++i) {
        const auto& f = controller_fixtures::closest[i];
        if (!near(syntheticGeometry.closestOnBlock(synthetic.blocks[f.block],f.point),f.result))
            fail("closestOnBlock",i,static_cast<int>(stages.size()));
    }
    for (std::size_t i=0; i<controller_fixtures::capsuleDistances.size(); ++i) {
        const auto& f = controller_fixtures::capsuleDistances[i];
        const auto actual = StageGeometry::segmentCapsuleDist(f.a,f.b,f.base,0.4,1.65);
        if (!near(StageGeometry::pointCapsuleDist(f.a,f.base,0.4,1.65),f.point) ||
            !near(actual.dist,f.segment) || !near(actual.t,f.t)) fail("capsule distance",i,static_cast<int>(stages.size()));
    }
    if (failures) std::cerr << failures << " controller physics failures\n";
    else std::cout << "controller physics: " << controller_fixtures::probes.size() << " ground, "
                   << controller_fixtures::bodies.size() << " body, " << controller_fixtures::capsules.size()
                   << " capsule, " << controller_fixtures::fits.size() << " fit, "
                   << controller_fixtures::cameras.size() << " camera reference cases passed\n";
    return failures ? 1 : 0;
}
