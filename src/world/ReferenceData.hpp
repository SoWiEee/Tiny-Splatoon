#pragma once

#include <array>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace restoration {

struct BlockData {
    glm::dvec3 center, half;
    std::array<glm::dvec3, 3> axes;
    std::array<int, 6> faces;
    bool solid, grate, roof, perch, noNav, rail, hidden;
};

struct FaceData {
    glm::dvec3 origin, u, v, normal, color;
    double width, height;
    int block, pattern, mural;
    bool paintable, turf, wall, ceiling, groundedBottom;
};

struct StageData {
    std::string_view id, name, mode, theme, water;
    glm::dvec4 bounds; // minX, maxX, minZ, maxZ
    std::array<glm::dvec3, 2> spawnPads;
    double spawnBarrier;
    bool onlineOnly, noBots, noBoss;
    int layoutBlockCount, propColliderCount;
    std::vector<BlockData> blocks;
    std::vector<FaceData> faces;
};

const std::vector<StageData>& ReferenceStages();

} // namespace restoration
