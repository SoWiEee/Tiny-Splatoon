#pragma once
// Generated from source BotBrain running-special control scenarios; do not hand edit.
#include <array>
#include <string_view>
#include <glm/vec3.hpp>
namespace restoration::bot_special_fixtures {
struct Oracle { std::string_view name, special; unsigned seed; int frames; glm::dvec3 bot, foe; bool grounded, active, ready; double charge; int specialFrames, fireFrames, subPresses, jumpFrames, squidFrames; glm::dvec3 move; bool fire, sub, specialInput, jump, squid; };
inline const std::array<Oracle, 11> oracles{{
  Oracle{"activation-ready-kraken", "kraken", 3u, 1, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, false, true, 0, 1, 0, 0, 0, 0, {0.167634293, 0, -0.123323544}, false, false, true, false, false},
  Oracle{"grounded-kraken-attack", "kraken", 3u, 90, {-13.5, 0, -43.5}, {-13.5, 0, -40.3}, true, true, false, 0, 0, 90, 0, 1, 0, {0, 0, 0.999999999}, true, false, false, false, false},
  Oracle{"zipcaster-grapple", "zipcaster", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 0, 0, 69, 2, 1, 0, {0.981640076, 0, -0.190742652}, true, false, false, false, false},
  Oracle{"booyah-full-charge", "booyah", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 1, 0, 6, 0, 1, 0, {0.362913188, 0, -0.931822953}, false, false, false, false, false},
  Oracle{"crab-tank", "crab", 3u, 90, {-13.5, 0, -43.5}, {-21.5, 0, -38.5}, true, true, false, 0, 0, 90, 6, 1, 0, {0.527146126, 0, 0.843433802}, true, false, false, false, false},
  Oracle{"jetpack-fire", "jetpack", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, false, true, false, 0, 0, 90, 0, 0, 0, {0.305347922, 0, -0.952240855}, true, false, false, false, false},
  Oracle{"blower-cycle", "blower", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 0, 0, 58, 0, 1, 0, {0.362913188, 0, -0.931822953}, true, false, false, false, false},
  Oracle{"barrage-bombs", "barrage", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 0, 0, 69, 3, 1, 0, {0.273149946, 0, -0.961971468}, true, false, false, false, false},
  Oracle{"barrage-sticky-bombs", "barrage_sticky", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 0, 0, 69, 3, 1, 0, {0.273149946, 0, -0.961971468}, true, false, false, false, false},
  Oracle{"zooka-fire", "zooka", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, true, true, false, 0, 0, 90, 0, 1, 0, {0.362913188, 0, -0.931822953}, true, false, false, false, false},
  Oracle{"stamp-turn", "stamp", 3u, 90, {-13.5, 0, -43.5}, {-13.5, 0, -45.7}, true, true, false, 0, 0, 50, 0, 1, 0, {0.999999999, 0, 0}, false, false, false, false, false},
}};
} // namespace restoration::bot_special_fixtures
