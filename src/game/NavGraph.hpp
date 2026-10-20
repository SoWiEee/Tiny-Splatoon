#pragma once

#include "StageGeometry.hpp"

#include <cstdint>
#include <vector>
#include <glm/vec2.hpp>

namespace restoration {

enum class NavEdgeType { Walk, Jump, Drop, Climb };

struct NavEdge {
    int to = -1;
    double cost = 0.0;
    NavEdgeType type = NavEdgeType::Walk;
    glm::dvec3 wallPoint{0.0};
    glm::dvec2 wallNormal{0.0};
    double topY = 0.0;
};

struct NavNode {
    int id = -1, ix = 0, iz = 0, zone = -1, wet = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    bool overWater = false;
    std::vector<NavEdge> edges;
};

// Port of the reference 1 m, multi-level navigation graph in src/game/nav.js.
class NavGraph {
public:
    NavGraph(const StageData& stage, const StageGeometry& geometry);

    int nearest(glm::dvec3 pos, double maxUp = 0.8, bool start = false) const;
    std::vector<int> path(int start, int goal, int team, int maxIter = 6000, bool noClimb = false) const;
    const NavEdge* edge(int from, int to) const;
    NavEdgeType edgeType(int from, int to) const;

    const std::vector<NavNode>& nodes() const { return nodes_; }
    const std::vector<int>& validIds() const { return validIds_; }
    bool valid(int id) const { return id >= 0 && id < static_cast<int>(valid_.size()) && valid_[id]; }
    double step() const { return step_; }

private:
    void build();
    bool clear(double x, double y, double z) const;
    bool has(int ix, int iz, double y) const;
    bool blocked(const NavNode& a, const NavNode& b, double y) const;
    bool openAbove(const NavNode& below, double fromY) const;
    void climbEdges();
    void prune();
    int hashX(double x) const;
    int hashZ(double z) const;
    std::vector<int> queryBlocks(double minX, double minZ, double maxX, double maxZ) const;

    const StageData& stage_;
    const StageGeometry& geometry_;
    const double step_ = 1.0;
    double x0_ = 0.0, z0_ = 0.0, hashMinX_ = 0.0, hashMinZ_ = 0.0;
    int nx_ = 0, nz_ = 0, hashWidth_ = 0, hashDepth_ = 0;
    std::vector<std::vector<int>> cells_, hash_;
    mutable std::vector<std::uint32_t> blockStamps_;
    mutable std::uint32_t blockStamp_ = 0;
    std::vector<NavNode> nodes_;
    std::vector<std::uint8_t> valid_, exitable_;
    std::vector<int> validIds_;
};

} // namespace restoration
