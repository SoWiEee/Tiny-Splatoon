#include "SwimWake.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}
bool same(const restoration::SwimWakeUniforms& a, const restoration::SwimWakeUniforms& b) {
    const auto sameVec = [](const auto& left, const auto& right) {
        for (int c = 0; c < 4; ++c) if (left[c] != right[c]) return false;
        return true;
    };
    for (std::size_t i = 0; i < a.points.size(); ++i) if (!sameVec(a.points[i], b.points[i])) return false;
    for (std::size_t i = 0; i < a.bounds.size(); ++i) if (!sameVec(a.bounds[i], b.bounds[i])) return false;
    for (std::size_t i = 0; i < a.heads.size(); ++i) if (!sameVec(a.heads[i], b.heads[i])) return false;
    for (std::size_t i = 0; i < a.forwards.size(); ++i) if (!sameVec(a.forwards[i], b.forwards[i])) return false;
    return true;
}
}

int main() {
    try {
        using namespace restoration;
        int localId = 0, botAId = 0, botBId = 0, botCId = 0, botDId = 0;
        std::vector<SwimWakeActor> actors{
            {&botAId, {8.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, true, false, true, false},
            {&botBId, {12.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, true, false, true, false},
            {&botCId, {16.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, true, false, true, false},
            {&botDId, {20.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, true, false, true, false},
            {&localId, {0.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, true, true, true, false},
        };
        SwimWake wake;
        wake.update(0.05, 0.05, actors, {0.0, 0.0, 0.0});
        const auto& first = wake.uniforms();
        check(close(first.heads[0].x, 0.0) && first.heads[0].y > -999.0,
              "Local swimmer did not receive the first wake slot");
        check(close(first.points[0].w, 0.05) && close(first.points[1].w, 0.05),
              "First swim step did not pack its trail sample and live head");
        check(first.bounds[0].w >= 1.6 && first.heads[0].w > 0.0,
              "Swim wake did not produce the surface bounds and mound weight");

        actors.back().position.x = 0.6;
        actors.back().velocity = {0.0, 0.0, 0.0};
        wake.update(0.05, 0.10, actors, {0.0, 0.0, 0.0});
        check(close(wake.uniforms().points[1].x, 0.6) && close(wake.uniforms().points[1].w, 0.10),
              "Live head stopped closing the trail while below the point spacing threshold");

        const auto beforePause = wake.uniforms();
        actors.back().position.x = 6.0;
        wake.update(0.0, 0.10, actors, {0.0, 0.0, 0.0});
        check(same(beforePause, wake.uniforms()), "Paused wake update changed trail or transition state");

        actors.back().position = {3.6, 0.0, 0.0};
        actors.back().velocity = {0.0, 0.0, 4.0};
        wake.update(0.05, 0.15, actors, {0.0, 0.0, 0.0});
        bool hasBreak = false;
        for (std::size_t i = 0; i < SwimWake::PointCount; ++i)
            hasBreak = hasBreak || wake.uniforms().points[i].w == SwimWake::Break;
        check(hasBreak, "Teleport did not break the wake trail");

        // Keep the lifecycle assertion isolated from slot reuse: bot D was
        // previously outside the four selected wake slots and can claim the
        // local slot as soon as it is released.
        for (std::size_t i = 0; i + 1 < actors.size(); ++i) actors[i].swimming = false;
        actors.back().swimming = false;
        for (int frame = 0; frame < 26; ++frame)
            wake.update(0.05, 0.20 + 0.05 * frame, actors, {0.0, 0.0, 0.0});
        check(close(wake.uniforms().heads[0].y, -999.0), "Expired swim slot was not released after the mound and points faded");
        actors[3].swimming = true;
        wake.update(0.05, 1.50, actors, {0.0, 0.0, 0.0});
        check(close(wake.uniforms().heads[0].x, actors[3].position.x) && wake.uniforms().heads[0].y > -999.0,
              "Released wake slot was not available for another swimmer");
        wake.reset();
        const auto& reset = wake.uniforms();
        check(close(reset.points[0].w, SwimWake::Break) && close(reset.bounds[0].y, -999.0) &&
              close(reset.heads[0].y, -999.0) && close(reset.forwards[0].z, 1.0),
              "Wake reset did not restore the source shader defaults");
        std::cout << "SWIM_WAKE slots=4 points=12 localPriority=pass pause=pass teleportBreak=pass reset=pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
