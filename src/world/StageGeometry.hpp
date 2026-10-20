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

struct Contacts {
    bool ground = false, wall = false, ceiling = false;
    glm::dvec3 groundNormal{0.0, 1.0, 0.0}, wallNormal{0.0};
    int groundBlock = -1, wallBlock = -1;
};

struct GroundHit {
    bool hit = false, center = false, grate = false;
    double y = 0.0, u = 0.0, v = 0.0;
    glm::dvec3 normal{0.0, 1.0, 0.0};
    int block = -1, face = -1;
};

struct CameraProbe { double hard = 0.0, soft = 0.0; bool floor = false; };
struct SegmentCapsuleResult { double dist = 0.0, t = 0.0; };

// Collision queries use the oriented boxes exported from the browser Level.
class StageGeometry {
public:
    explicit StageGeometry(const StageData& stage);

    bool pointInBlock(const BlockData& block, const glm::dvec3& point, double pad = 0.0) const;
    bool pointInside(const glm::dvec3& point, double pad = 0.0, int exclude = -1) const;
    double groundHeight(double x, double z, double yMax = 50.0, bool skipGrates = false) const;
    // As in Physics.raycast, dir must be normalized; blocks containing origin are ignored.
    RayHit raycast(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist, bool skipGrates = false) const;
    RayHit segment(const glm::dvec3& a, const glm::dvec3& b, bool skipGrates = false) const;
    bool los(const glm::dvec3& a, const glm::dvec3& b) const;
    glm::dvec3 closestOnBlock(const BlockData& block, const glm::dvec3& point) const;
    Contacts collideCapsule(glm::dvec3& pos, double radius, double height, int iterations = 3, bool squid = false) const;
    GroundHit groundProbe(double x, double y, double z, double up, double down, double foot,
                          bool skipGrates = false, double stepMin = 0.12) const;
    Contacts collideBody(glm::dvec3& pos, double radius, double lift, double height,
                         bool horizontal = false, bool skipGrates = false, int iterations = 3) const;
    bool bodyFits(const glm::dvec3& pos, double radius, double lift, double height,
                  bool skipGrates = false, double margin = 0.01) const;
    CameraProbe cameraProbe(const glm::dvec3& pivot, const glm::dvec3& back, double want,
                            double radius, double pad = 0.3) const;
    static double pointCapsuleDist(const glm::dvec3& point, const glm::dvec3& base, double radius, double height);
    static SegmentCapsuleResult segmentCapsuleDist(const glm::dvec3& a, const glm::dvec3& b,
                                                    const glm::dvec3& base, double radius, double height);

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
