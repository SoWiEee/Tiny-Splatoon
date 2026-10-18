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

RayHit StageGeometry::segment(const glm::dvec3& a, const glm::dvec3& b, bool skipGrates) const {
    const glm::dvec3 delta = b - a;
    const double length = glm::length(delta);
    if (length < 1e-6) return {};
    return raycast(a, delta / length, length, skipGrates);
}

bool StageGeometry::los(const glm::dvec3& a, const glm::dvec3& b) const {
    const glm::dvec3 delta = b - a;
    const double length = glm::length(delta);
    return length < 1e-4 || !raycast(a, delta / length, length - 0.05, true).hit;
}

glm::dvec3 StageGeometry::closestOnBlock(const BlockData& block, const glm::dvec3& point) const {
    const glm::dvec3 local = point - block.center;
    glm::dvec3 out = block.center;
    for (int k = 0; k < 3; ++k) out += block.axes[k] * std::clamp(glm::dot(local, block.axes[k]), -block.half[k], block.half[k]);
    return out;
}

namespace {
double blockExtentY(const BlockData& b) {
    return std::abs(b.axes[0].y) * b.half.x + std::abs(b.axes[1].y) * b.half.y + std::abs(b.axes[2].y) * b.half.z;
}

struct CapsuleContact { glm::dvec3 normal; double penetration; bool hit; };

CapsuleContact capsuleBlock(const StageGeometry& geometry, const BlockData& block,
                            const glm::dvec3& pos, double bottom, double top, double radius) {
    const glm::dvec3 a{pos.x, pos.y + bottom, pos.z};
    const glm::dvec3 b{pos.x, pos.y + top, pos.z};
    const glm::dvec3 ab = b - a;
    const double abLen2 = std::max(1e-6, glm::dot(ab, ab));
    double t = 0.5;
    for (int k = 0; k < 3; ++k) {
        const glm::dvec3 s = a + ab * t;
        const glm::dvec3 q = geometry.closestOnBlock(block, s);
        t = std::clamp(glm::dot(q - a, ab) / abLen2, 0.0, 1.0);
    }
    const glm::dvec3 s = a + ab * t;
    const glm::dvec3 q = geometry.closestOnBlock(block, s);
    glm::dvec3 n = s - q;
    const double dist = glm::length(n);
    if (dist > 1e-5) {
        if (dist >= radius) return {glm::dvec3(0.0), 0.0, false};
        return {n / dist, radius - dist, true};
    }
    const glm::dvec3 local = s - block.center;
    double bestPen = std::numeric_limits<double>::infinity();
    for (int k = 0; k < 3; ++k) {
        const double d = glm::dot(local, block.axes[k]);
        const double pen = block.half[k] - std::abs(d);
        if (pen < bestPen) { bestPen = pen; n = block.axes[k] * (d >= 0.0 ? 1.0 : -1.0); }
    }
    return {n, bestPen + radius, true};
}
} // namespace

Contacts StageGeometry::collideCapsule(glm::dvec3& pos, double radius, double height, int iterations, bool squid) const {
    Contacts c;
    const double top = std::max(radius, height - radius);
    for (int it = 0; it < iterations; ++it) {
        bool moved = false;
        for (int id : queryBlocks(pos.x-radius-0.2, pos.z-radius-0.2, pos.x+radius+0.2, pos.z+radius+0.2)) {
            const auto& b = stage_.blocks[id];
            if (!b.solid || (squid && b.grate)) continue;
            const double ey = blockExtentY(b);
            if (pos.y + height < b.center.y - ey - 0.05 || pos.y > b.center.y + ey + 0.05) continue;
            const auto hit = capsuleBlock(*this, b, pos, radius, top, radius);
            if (!hit.hit) continue;
            pos += hit.normal * (hit.penetration + 1e-4);
            moved = true;
            if (hit.normal.y > 0.6) { c.ground = true; c.groundNormal = hit.normal; c.groundBlock = id; }
            else if (hit.normal.y < -0.6) c.ceiling = true;
            else if (std::abs(hit.normal.y) < 0.55) { c.wall = true; c.wallNormal = hit.normal; c.wallBlock = id; }
        }
        if (!moved) break;
    }
    return c;
}

GroundHit StageGeometry::groundProbe(double x, double y, double z, double up, double down, double foot,
                                     bool skipGrates, double stepMin) const {
    GroundHit out;
    auto fill = [&](const RayHit& h, bool center) {
        out.y = h.point.y; out.normal = h.normal; out.block = h.block; out.face = h.face;
        out.u = h.u; out.v = h.v; out.center = center; out.grate = h.block >= 0 && stage_.blocks[h.block].grate;
    };
    const glm::dvec3 downDir{0.0, -1.0, 0.0};
    const RayHit center = raycast({x, y+up, z}, downDir, up+down, skipGrates);
    const bool have = center.hit && center.normal.y >= 0.68;
    if (have) fill(center, true);
    double best = have ? center.point.y + stepMin : -std::numeric_limits<double>::infinity();
    bool ring = false;
    for (int i = 0; i < 8; ++i) {
        const double angle = (static_cast<double>(i) / 8.0) * std::acos(-1.0) * 2.0;
        const RayHit h = raycast({x + std::cos(angle)*foot, y+up, z + std::sin(angle)*foot}, downDir, up+down, skipGrates);
        if (!h.hit || h.normal.y < 0.68) continue;
        if (h.point.y > best) { best = h.point.y; ring = true; fill(h, false); }
    }
    out.hit = have || ring;
    return out;
}

Contacts StageGeometry::collideBody(glm::dvec3& pos, double radius, double lift, double height,
                                    bool horizontal, bool skipGrates, int iterations) const {
    Contacts c;
    const double bottom = lift + radius, top = std::max(bottom, height - radius);
    for (int it = 0; it < iterations; ++it) {
        bool moved = false;
        for (int id : queryBlocks(pos.x-radius-0.2, pos.z-radius-0.2, pos.x+radius+0.2, pos.z+radius+0.2)) {
            const auto& b = stage_.blocks[id];
            if (!b.solid || (skipGrates && b.grate)) continue;
            const double ey = blockExtentY(b);
            if (pos.y + height < b.center.y - ey - 0.05 || pos.y + lift > b.center.y + ey + 0.05) continue;
            const auto hit = capsuleBlock(*this, b, pos, bottom, top, radius);
            if (!hit.hit) continue;
            const double hl = std::hypot(hit.normal.x, hit.normal.z);
            if (horizontal && hl > 0.3 && hit.normal.y > -0.6) {
                const double push = std::min(0.45, hit.penetration / hl) + 1e-4;
                pos.x += hit.normal.x / hl * push; pos.z += hit.normal.z / hl * push;
                c.wall = true; c.wallNormal = {hit.normal.x / hl, 0.0, hit.normal.z / hl}; c.wallBlock = id;
            } else {
                pos += hit.normal * (hit.penetration + 1e-4);
                if (hit.normal.y > 0.6) { c.ground = true; c.groundNormal = hit.normal; c.groundBlock = id; }
                else if (hit.normal.y < -0.6) c.ceiling = true;
                else if (std::abs(hit.normal.y) < 0.6) { c.wall = true; c.wallNormal = hit.normal; c.wallBlock = id; }
            }
            moved = true;
        }
        if (!moved) break;
    }
    return c;
}

bool StageGeometry::bodyFits(const glm::dvec3& pos, double radius, double lift, double height,
                             bool skipGrates, double margin) const {
    if (radius == margin) return true;
    const double bottom = lift + radius, top = std::max(bottom, height-radius);
    for (int id : queryBlocks(pos.x-radius-0.1, pos.z-radius-0.1, pos.x+radius+0.1, pos.z+radius+0.1)) {
        const auto& b = stage_.blocks[id];
        if (!b.solid || (skipGrates && b.grate)) continue;
        const double ey = blockExtentY(b);
        if (pos.y + height < b.center.y - ey || pos.y + lift > b.center.y + ey) continue;
        const auto hit = capsuleBlock(*this, b, pos, bottom, top, std::abs(radius-margin));
        if (hit.hit && hit.penetration > 0.0) return false;
    }
    return true;
}

CameraProbe StageGeometry::cameraProbe(const glm::dvec3& pivot, const glm::dvec3& back,
                                       double want, double radius, double pad) const {
    glm::dvec3 reference{0.0, 1.0, 0.0};
    if (std::abs(back.y) > 0.95) reference = {1.0, 0.0, 0.0};
    const glm::dvec3 side = glm::normalize(glm::cross(back, reference));
    const glm::dvec3 up = glm::normalize(glm::cross(side, back));
    CameraProbe out{want, want, false};
    auto ray = [&](double ox, double oy, double weight) {
        const RayHit hit = raycast(pivot + side*(ox*radius) + up*(oy*radius), back, want+pad, true);
        if (!hit.hit) return;
        const double limit = std::max(0.0, hit.dist-pad);
        if (weight >= 1.0 && limit < out.hard) { out.hard = limit; out.floor = hit.normal.y > 0.6; }
        out.soft = std::min(out.soft, limit+(want-limit)*(1.0-weight));
    };
    ray(0.0, 0.0, 1.0);
    for (int i = 0; i < 6; ++i) { const double a = (i/6.0)*std::acos(-1.0)*2.0; ray(std::cos(a)*0.5, std::sin(a)*0.5, 0.75); }
    for (int i = 0; i < 8; ++i) { const double a = (i/8.0)*std::acos(-1.0)*2.0+0.39; ray(std::cos(a), std::sin(a), 0.4); }
    out.soft = std::min(out.soft, out.hard);
    return out;
}

double StageGeometry::pointCapsuleDist(const glm::dvec3& point, const glm::dvec3& base, double radius, double height) {
    const double y = std::clamp(point.y, base.y+radius, base.y+std::max(radius, height-radius));
    const double dx = point.x-base.x, dy = point.y-y, dz = point.z-base.z;
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}

SegmentCapsuleResult StageGeometry::segmentCapsuleDist(const glm::dvec3& a, const glm::dvec3& b,
                                                       const glm::dvec3& base, double radius, double height) {
    double best = std::numeric_limits<double>::infinity(), bt = 0.0;
    for (int i = 0; i <= 6; ++i) {
        const double t = i/6.0;
        const double d = pointCapsuleDist(a+(b-a)*t, base, radius, height);
        if (d < best) { best = d; bt = t; }
    }
    double lo = std::max(0.0, bt-1.0/6.0), hi = std::min(1.0, bt+1.0/6.0);
    for (int k = 0; k < 8; ++k) {
        const double m1 = lo+(hi-lo)/3.0, m2 = hi-(hi-lo)/3.0;
        const double d1 = pointCapsuleDist(a+(b-a)*m1, base, radius, height);
        const double d2 = pointCapsuleDist(a+(b-a)*m2, base, radius, height);
        if (d1 < d2) hi = m2; else lo = m1;
    }
    const double t = (lo+hi)/2.0;
    return {pointCapsuleDist(a+(b-a)*t, base, radius, height), t};
}

} // namespace restoration
