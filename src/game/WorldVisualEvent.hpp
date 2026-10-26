#pragma once
#include <glm/vec3.hpp>
#include <cstdint>
#include <limits>
#include <string>

namespace restoration {
struct WorldVisualEvent {
    std::string kind;
    glm::dvec3 pos{0.0}, normal{0.0,1.0,0.0};
    double radius = 0.0;
    int team = 0;
    double charge = 0, width = 0.08, seed = 0;
    std::string source;
    std::uint64_t id = 0;
    bool sourceLocal = false;
    bool sourceVictimLocal = false;
    std::uint32_t victimNid = std::numeric_limits<std::uint32_t>::max();
};
}
