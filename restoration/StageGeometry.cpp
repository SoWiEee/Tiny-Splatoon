#include "StageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

namespace restoration {

StageGeometry::StageGeometry(const StageData& stage) : stage_(stage),
    hashMinX_(stage.bounds.x - 8.0), hashMinZ_(stage.bounds.z - 8.0),
    hashWidth_(std::max(1,static_cast<int>(std::ceil((stage.bounds.y-stage.bounds.x+16.0)/4.0)))),
    hashDepth_(std::max(1,static_cast<int>(std::ceil((stage.bounds.w-stage.bounds.z+16.0)/4.0)))),
    hash_(static_cast<std::size_t>(hashWidth_)*hashDepth_) {
    for(int id=0;id<static_cast<int>(stage.blocks.size());++id) {
        const auto& b=stage.blocks[id];
        const glm::dvec3 extent=glm::abs(b.axes[0])*b.half.x+glm::abs(b.axes[1])*b.half.y+glm::abs(b.axes[2])*b.half.z;
        const auto lo=b.center-extent, hi=b.center+extent;
        for(int z=hashZ(lo.z);z<=hashZ(hi.z);++z)
            for(int x=hashX(lo.x);x<=hashX(hi.x);++x) hash_[z*hashWidth_+x].push_back(id);
    }
}

int StageGeometry::hashX(double x) const {
    return std::clamp(static_cast<int>(std::floor((x-hashMinX_)/4.0)),0,hashWidth_-1);
}
int StageGeometry::hashZ(double z) const {
    return std::clamp(static_cast<int>(std::floor((z-hashMinZ_)/4.0)),0,hashDepth_-1);
}
std::vector<int> StageGeometry::queryBlocks(double minX,double minZ,double maxX,double maxZ) const {
    std::vector<int> result;
    std::vector<bool> visited(stage_.blocks.size(),false);
    for(int z=hashZ(minZ);z<=hashZ(maxZ);++z) for(int x=hashX(minX);x<=hashX(maxX);++x)
        for(int id:hash_[z*hashWidth_+x]) if(!visited[id]) {visited[id]=true;result.push_back(id);}
    return result;
}

bool StageGeometry::pointInBlock(const BlockData& block, const glm::dvec3& point, double pad) const {
    const glm::dvec3 local = point - block.center;
    return std::abs(glm::dot(local, block.axes[0])) < block.half.x + pad &&
           std::abs(glm::dot(local, block.axes[1])) < block.half.y + pad &&
           std::abs(glm::dot(local, block.axes[2])) < block.half.z + pad;
}

bool StageGeometry::pointInside(const glm::dvec3& point, double pad, int exclude) const {
    for(int z=hashZ(point.z-0.01);z<=hashZ(point.z+0.01);++z)
        for(int x=hashX(point.x-0.01);x<=hashX(point.x+0.01);++x)
            for(int id:hash_[z*hashWidth_+x]) {
                const auto& block=stage_.blocks[id];
                if(id!=exclude && block.solid && pointInBlock(block,point,pad)) return true;
            }
    return false;
}

double StageGeometry::groundHeight(double x, double z, double yMax, bool skipGrates) const {
    double best = -std::numeric_limits<double>::infinity();
    for (int id : queryBlocks(x-0.01,z-0.01,x+0.01,z+0.01)) {
        const auto& block=stage_.blocks[id];
        if (!block.solid || (skipGrates && block.grate)) continue;
        const auto& n = block.axes[1];
        if (n.y < 0.5) continue;
        const glm::dvec3 top = block.center + n * block.half.y;
        const double y = top.y - (n.x * (x - top.x) + n.z * (z - top.z)) / n.y;
        if (y <= yMax && y > best && pointInBlock(block, {x, y - 0.01, z}, 0.001)) best = y;
    }
    return best;
}

RayHit StageGeometry::raycast(const glm::dvec3& origin, const glm::dvec3& dir, double maxDist, bool skipGrates) const {
    RayHit hit;
    hit.dist = maxDist;
    const glm::dvec3 end=origin+dir*maxDist;
    // Preserve the reference hash traversal: equal-distance hits use its last candidate.
    for (int i : queryBlocks(std::min(origin.x,end.x),std::min(origin.z,end.z),
                            std::max(origin.x,end.x),std::max(origin.z,end.z))) {
        const auto& block = stage_.blocks[i];
        if (!block.solid || (skipGrates && block.grate)) continue;
        const glm::dvec3 local = origin - block.center;
        double tmin = -std::numeric_limits<double>::infinity();
        double tmax = std::numeric_limits<double>::infinity();
        int entryAxis = -1;
        double entrySign = 0.0;
        bool miss = false;
        for (int axis = 0; axis < 3; ++axis) {
            const double o = glm::dot(local, block.axes[axis]);
            const double d = glm::dot(dir, block.axes[axis]);
            const double half = block.half[axis];
            if (std::abs(d) < 1e-9) {
                if (o < -half || o > half) { miss = true; break; }
                continue;
            }
            double t1 = (-half - o) / d;
            double t2 = (half - o) / d;
            double sign = -1.0;
            if (t1 > t2) { std::swap(t1, t2); sign = 1.0; }
            if (t1 > tmin) { tmin = t1; entryAxis = axis; entrySign = sign; }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) { miss = true; break; }
        }
        if (miss || tmax < 0.0 || tmin < 0.0 || tmin > hit.dist) continue;
        hit.hit = true;
        hit.dist = tmin;
        hit.block = i;
        hit.point = origin + dir * tmin;
        hit.normal = block.axes[entryAxis] * entrySign;
        hit.face = block.faces[entryAxis * 2 + (entrySign > 0.0 ? 0 : 1)];
        if (hit.face >= 0) {
            const auto& face = stage_.faces[hit.face];
            hit.u = glm::dot(hit.point - face.origin, face.u);
            hit.v = glm::dot(hit.point - face.origin, face.v);
        } else {
            hit.u = hit.v = 0.0;
        }
    }
    return hit;
}

} // namespace restoration
