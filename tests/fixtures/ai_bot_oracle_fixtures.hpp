#pragma once
// Generated from source BotBrain with patched mulberry32 RNG and fixed source-shaped device/shield query snapshots; do not hand edit.
#include <array>
#include <string_view>
#include <glm/vec3.hpp>
namespace restoration::ai_bot_fixtures {
struct Oracle { std::string_view name, difficulty; unsigned seed; int frames; glm::dvec3 bot, foe; double ink, hp; bool hasFoe; std::string_view threat, mode; bool targetSeen; int fireFrames, subPresses; glm::dvec3 move; glm::dvec2 aim; bool fire, sub, special, squid, jump, cheer; };
inline const std::array<Oracle, 10> oracles{{
  Oracle{"duel-easy", "easy", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, true, "", "fight", true, 27, 0, {0.362913188, 0, -0.931822953}, {-0.920938571, -0.040500584}, true, false, false, false, false, false},
  Oracle{"duel-normal", "normal", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, true, "", "fight", true, 69, 0, {0.362913188, 0, -0.931822953}, {-0.961121397, -0.039364326}, true, false, false, false, false, false},
  Oracle{"duel-hard", "hard", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, true, "", "fight", true, 76, 0, {0.362913188, 0, -0.931822953}, {-0.974234248, -0.03693087}, true, false, false, false, false, false},
  Oracle{"paint", "normal", 3u, 30, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, false, "", "paint", false, 30, 0, {0.999088118, 0, 0}, {1.730840795, -0.380710767}, true, false, false, false, false, false},
  Oracle{"blocked-los", "normal", 3u, 30, {-13.5, 0, -43.5}, {-21.5, 0, -40.5}, 100, 100, true, "", "paint", false, 30, 0, {0.999088118, 0, 0}, {1.730840795, -0.380710767}, true, false, false, false, false, false},
  Oracle{"low-ink-refill", "normal", 3u, 2, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 5, 100, false, "", "refill", false, 0, 0, {0.372910915, 0, 0}, {0.075832617, -0.004827654}, false, false, false, true, false, false},
  Oracle{"low-hp-retreat", "normal", 4u, 2, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 15, true, "", "retreat", true, 0, 0, {0.130457534, 0, -0.100697717}, {-0.08208345, -0.002362848}, false, false, false, true, false, false},
  Oracle{"waddle-device", "normal", 3u, 90, {-13.5, 0, -43.5}, {-13.5, 0, -51.5}, 100, 100, false, "waddle", "paint", false, 40, 0, {0.841470984, 0, 0.540302305}, {-3.123661985, 0.084424172}, false, false, false, false, false, false},
  Oracle{"shield-fight", "normal", 3u, 90, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, true, "shield-fight", "fight", true, 69, 0, {0.164398987, 0, 0.98639392}, {-0.961121397, -0.039364326}, true, false, false, false, false, false},
  Oracle{"canopy-route", "normal", 3u, 30, {-13.5, 0, -43.5}, {-19.5, 0, -39.5}, 100, 100, false, "canopy-route", "paint", false, 12, 0, {0, 0, 0.303215928}, {2.977517138, -0.199608456}, true, false, false, false, false, false},
}};
} // namespace restoration::ai_bot_fixtures
