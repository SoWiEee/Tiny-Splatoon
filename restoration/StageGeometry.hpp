#pragma once

#include "ReferenceData.hpp"

namespace restoration {

struct RayHit {
    bool hit = false;
    double dist = 0.0;
    glm::dvec3 point{0.0};
    glm::dvec3 normal{0.0};
    int block = -1;
    int face = -1;
    double u = 0.0;
    double v = 0.0;
};

// Collision queries use the oriented boxes exported from the browser Level.
class StageGeometry {
public:
    explicit StageGeometry(const StageData& stage);

    bool pointInBlock(const BlockData& block, const glm::dvec3& point, double pad = 0.0) const;
    bool pointInside(const glm::dvec3& point, double pad = 0.0, int exclude = -1) const;
    double groundHeight(double x, double z, double yMax = 50.0, bool skipGrates = false) const;
    // As in Physics.raycast, dir must be normalized; blocks containing origin are ignored.
    RayHit raycast(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist, bool skipGrates = false) const;

private:
    int hashX(double x) const;
    int hashZ(double z) const;
    std::vector<int> queryBlocks(double minX, double minZ, double maxX, double maxZ) const;
    const StageData& stage_;
    double hashMinX_, hashMinZ_;
    int hashWidth_, hashDepth_;
    std::vector<std::vector<int>> hash_;
};

} // namespace restoration
