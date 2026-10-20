#include "CharacterLod.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
struct Thresholds { double hero, heroLocal, far; };
constexpr std::array<Thresholds, 4> quality{{
    {INFINITY, INFINITY, 150.0}, {760.0, 260.0, 110.0},
    {430.0, 150.0, 78.0}, {300.0, 110.0, 54.0}
}};
int pick(double px, int current, const Thresholds& q, bool local) {
    const double hero = local ? q.heroLocal : q.hero;
    int tier = current < 0 ? (px >= hero ? 0 : px < q.far ? 2 : 1) : current;
    if (tier == 0 && px < hero * 0.88) tier = px < q.far ? 2 : 1;
    else if (tier == 1 && px >= hero * 1.12) tier = 0;
    else if (tier == 1 && px < q.far * 0.88) tier = 2;
    else if (tier == 2 && px >= q.far * 1.12) tier = px >= hero * 1.12 ? 0 : 1;
    return tier;
}
}

double CharacterLod::screenPixels(const glm::dmat4& root, glm::dvec3 camera,
                                  double projectionY, int viewportHeight) {
    const glm::dvec3 center = glm::dvec3(root * glm::dvec4(0.0, 0.66, 0.0, 1.0));
    return 1.32 * std::abs(root[1][1]) * projectionY /
        std::max(0.2, glm::distance(center, camera)) * viewportHeight * 0.5;
}

CharacterLodFrame CharacterLod::update(double screenPixels, bool local, double dt,
                                       CharacterQuality setting) {
    if (to_ >= 0) {
        fade_ = std::min(1.0, fade_ + dt / 0.3);
        if (fade_ >= 1.0) { tier_ = to_; to_ = -1; fade_ = 0.0; }
    } else if (screenPixels > 0.0) {
        const int wanted = pick(screenPixels, tier_, quality[static_cast<int>(setting)], local);
        if (tier_ < 0) tier_ = wanted;
        else if (wanted != tier_ && dt > 0.0) { to_ = wanted; fade_ = 0.0; }
    }
    CharacterLodFrame result;
    result.tier = static_cast<CharacterTier>(tier_ < 0 ? 1 : tier_);
    result.transitioning = to_ >= 0;
    result.to = static_cast<CharacterTier>(to_ >= 0 ? to_ : tier_ < 0 ? 1 : tier_);
    result.fade = fade_;
    return result;
}

const char* CharacterTierName(CharacterTier tier) {
    constexpr const char* names[] = {"hero", "game", "far"};
    return names[static_cast<int>(tier)];
}
} // namespace restoration
