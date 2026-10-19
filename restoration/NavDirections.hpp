#pragma once
// Generated from the source Math.cos/sin radial samples in src/game/nav.js.
#include <array>
#include <glm/vec2.hpp>
namespace restoration {
inline constexpr std::array<glm::dvec2, 8> kSourceRadialDirections{{
  glm::dvec2{1.0, 0.0},
  glm::dvec2{0.7071067811865476, 0.7071067811865475},
  glm::dvec2{6.123233995736766e-17, 1.0},
  glm::dvec2{-0.7071067811865475, 0.7071067811865476},
  glm::dvec2{-1.0, 1.2246467991473532e-16},
  glm::dvec2{-0.7071067811865477, -0.7071067811865475},
  glm::dvec2{-1.8369701987210297e-16, -1.0},
  glm::dvec2{0.7071067811865474, -0.7071067811865477},
}};
} // namespace restoration
