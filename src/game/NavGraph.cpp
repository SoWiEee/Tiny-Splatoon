#include "NavGraph.hpp"
#include "NavDirections.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
class Heap {
public:
    bool empty() const { return ids_.empty(); }
    void push(int id, double priority) {
        auto i = ids_.size();
        ids_.push_back(id); priorities_.push_back(priority);
        while (i > 0) {
            const auto parent = (i - 1) / 2;
            if (priorities_[parent] <= priority) break;
            ids_[i] = ids_[parent]; priorities_[i] = priorities_[parent]; i = parent;
        }
        ids_[i] = id; priorities_[i] = priority;
    }
    int pop() {
        const int top = ids_.front(), lastId = ids_.back();
        const double lastPriority = priorities_.back();
        ids_.pop_back(); priorities_.pop_back();
        if (!ids_.empty()) {
            std::size_t i = 0;
            while (true) {
                const std::size_t left = i * 2 + 1, right = left + 1;
                std::size_t smallest = i;
                double p = lastPriority;
                if (left < ids_.size() && priorities_[left] < p) { smallest = left; p = priorities_[left]; }
                if (right < ids_.size() && priorities_[right] < p) { smallest = right; p = priorities_[right]; }
                if (smallest == i) break;
                ids_[i] = ids_[smallest]; priorities_[i] = priorities_[smallest]; i = smallest;
            }
            ids_[i] = lastId; priorities_[i] = lastPriority;
        }
        return top;
    }
private:
    std::vector<int> ids_;
    std::vector<double> priorities_;
};
}

NavGraph::NavGraph(const StageData& stage, const StageGeometry& geometry)
    : stage_(stage), geometry_(geometry) {
    hashMinX_ = stage.bounds.x - 8.0; hashMinZ_ = stage.bounds.z - 8.0;
    hashWidth_ = std::max(1, static_cast<int>(std::ceil((stage.bounds.y - stage.bounds.x + 16.0) / 4.0)));
    hashDepth_ = std::max(1, static_cast<int>(std::ceil((stage.bounds.w - stage.bounds.z + 16.0) / 4.0)));
    hash_.resize(static_cast<std::size_t>(hashWidth_) * hashDepth_);
    blockStamps_.resize(stage.blocks.size());
    for (int id = 0; id < static_cast<int>(stage.blocks.size()); ++id) {
        const auto& b = stage.blocks[id];
        const glm::dvec3 extent = glm::abs(b.axes[0]) * b.half.x + glm::abs(b.axes[1]) * b.half.y + glm::abs(b.axes[2]) * b.half.z;
        for (int z = hashZ(b.center.z - extent.z); z <= hashZ(b.center.z + extent.z); ++z)
            for (int x = hashX(b.center.x - extent.x); x <= hashX(b.center.x + extent.x); ++x)
                hash_[z * hashWidth_ + x].push_back(id);
    }
    build();
}

int NavGraph::hashX(double x) const { return std::clamp(static_cast<int>(std::floor((x - hashMinX_) / 4.0)), 0, hashWidth_ - 1); }
int NavGraph::hashZ(double z) const { return std::clamp(static_cast<int>(std::floor((z - hashMinZ_) / 4.0)), 0, hashDepth_ - 1); }

std::vector<int> NavGraph::queryBlocks(double minX, double minZ, double maxX, double maxZ) const {
    std::vector<int> ids;
    if (++blockStamp_ == 0) { std::fill(blockStamps_.begin(), blockStamps_.end(), 0); blockStamp_ = 1; }
    for (int z = hashZ(minZ); z <= hashZ(maxZ); ++z)
        for (int x = hashX(minX); x <= hashX(maxX); ++x)
            for (int id : hash_[z * hashWidth_ + x])
                if (blockStamps_[id] != blockStamp_) { blockStamps_[id] = blockStamp_; ids.push_back(id); }
    return ids;
}

bool NavGraph::clear(double x, double y, double z) const {
    constexpr double radius = 0.38 + 0.08;
    for (double h : {0.15, 0.4, 0.85, 1.4}) {
        if (geometry_.pointInside({x, y + h, z})) return false;
        if (h < 0.35) continue;
        for (int k = 0; k < 8; ++k) {
            const auto& direction = kSourceRadialDirections[k];
            if (geometry_.pointInside({x + direction.x * radius, y + h, z + direction.y * radius})) return false;
        }
    }
    return true;
}

bool NavGraph::has(int ix, int iz, double y) const {
    if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) return false;
    for (int id : cells_[iz * nx_ + ix]) if (std::abs(nodes_[id].y - y) <= 0.5) return true;
    return false;
}

bool NavGraph::blocked(const NavNode& a, const NavNode& b, double y) const {
    glm::dvec3 dir{b.x - a.x, 0, b.z - a.z};
    const double length = glm::length(dir);
    if (length < 1e-4) return false;
    return geometry_.raycast({a.x, y + 0.6, a.z}, dir / length, length, false).hit;
}

bool NavGraph::openAbove(const NavNode& below, double fromY) const {
    for (double y = below.y + 1.0; y < fromY - 0.05; y += 0.3)
        if (geometry_.pointInside({below.x, y, below.z})) return false;
    return true;
}

void NavGraph::build() {
    x0_ = stage_.bounds.x + step_ / 2.0; z0_ = stage_.bounds.z + step_ / 2.0;
    nx_ = static_cast<int>(std::floor((stage_.bounds.y - stage_.bounds.x) / step_));
    nz_ = static_cast<int>(std::floor((stage_.bounds.w - stage_.bounds.z) / step_));
    cells_.resize(static_cast<std::size_t>(nx_) * nz_);
    for (int iz = 0; iz < nz_; ++iz) for (int ix = 0; ix < nx_; ++ix) {
        const double x = x0_ + ix * step_, z = z0_ + iz * step_;
        std::vector<double> heights;
        for (int id : queryBlocks(x - 0.01, z - 0.01, x + 0.01, z + 0.01)) {
            const auto& b = stage_.blocks[id];
            if (!b.solid || b.axes[1].y < 0.6 || b.roof || b.rail || b.noNav) continue;
            const auto& n = b.axes[1];
            const glm::dvec3 top = b.center + n * b.half.y;
            const double y = top.y - (n.x * (x - top.x) + n.z * (z - top.z)) / n.y;
            if (!geometry_.pointInBlock(b, {x, y - 0.02, z}, 0.001)) continue;
            bool duplicate = false;
            for (double h : heights) if (std::abs(h - y) < 0.15) { duplicate = true; break; }
            if (!duplicate) heights.push_back(y);
        }
        auto& cell = cells_[iz * nx_ + ix];
        for (double y : heights) {
            if (!clear(x, y, z)) continue;
            NavNode node;
            node.id = static_cast<int>(nodes_.size()); node.x = x; node.y = y; node.z = z; node.ix = ix; node.iz = iz;
            for (int t = 0; t < 2; ++t) {
                const auto& pad = stage_.spawnPads[t];
                if (std::hypot(x - pad.x, z - pad.z) < stage_.spawnBarrier + 0.6 && y > pad.y - 1.0) node.zone = t;
            }
            nodes_.push_back(node); cell.push_back(node.id);
        }
    }
    for (auto& node : nodes_) {
        for (auto [radius, level] : {std::pair{1.2, 2}, std::pair{2.2, 1}}) {
            if (node.wet) break;
            for (int k = 0; k < 8; ++k) {
                const auto& direction = kSourceRadialDirections[k];
                if (!std::isfinite(geometry_.groundHeight(node.x + direction.x * radius,
                                                           node.z + direction.y * radius, node.y + 0.6))) {
                    node.wet = level; break;
                }
            }
        }
        node.overWater = !std::isfinite(geometry_.groundHeight(node.x, node.z, node.y + 0.3, true));
    }
    for (auto& node : nodes_) for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dz) continue;
        const int jx = node.ix + dx, jz = node.iz + dz;
        if (jx < 0 || jz < 0 || jx >= nx_ || jz >= nz_) continue;
        for (int mid : cells_[jz * nx_ + jx]) {
            const auto& m = nodes_[mid];
            const double dy = m.y - node.y, flat = std::hypot(dx, dz) * step_;
            if (std::abs(dy) <= 0.5) {
                if (dx && dz && (!has(node.ix + dx, node.iz, node.y) || !has(node.ix, node.iz + dz, node.y))) continue;
                if (blocked(node, m, std::max(node.y, m.y))) continue;
                node.edges.push_back({mid, flat, NavEdgeType::Walk});
            } else if (dy > 0.5 && dy <= 1.25 && !(dx && dz)) {
                if (!blocked(node, m, m.y)) node.edges.push_back({mid, flat + 2.5, NavEdgeType::Jump});
            } else if (dy < -0.5 && dy >= -3.4 && !(dx && dz) && openAbove(m, node.y)) {
                if (!blocked(node, m, node.y)) node.edges.push_back({mid, flat + 0.8, NavEdgeType::Drop});
            }
        }
    }
    climbEdges();
    prune();
}

void NavGraph::climbEdges() {
    for (auto& node : nodes_) {
        std::unordered_set<int> seenTop;
        for (int k = 0; k < 8; ++k) {
            const auto& direction = kSourceRadialDirections[k];
            const glm::dvec3 dir{direction.x, 0, direction.y};
            const auto hit = geometry_.raycast({node.x, node.y + 0.35, node.z}, dir, 1.3, true);
            if (!hit.hit || std::abs(hit.normal.y) > 0.3 || dir.x * hit.normal.x + dir.z * hit.normal.z > -0.7) continue;
            if (hit.face < 0 || hit.face >= static_cast<int>(stage_.faces.size())) continue;
            const auto& face = stage_.faces[hit.face];
            if (!face.paintable) continue;
            const auto& block = stage_.blocks[face.block];
            if (block.grate || block.hidden) continue;
            const double topY = block.center.y + block.axes[1].y * block.half.y;
            if (topY - node.y < 1.3 || topY - node.y > 5.5) continue;
            const double qx = hit.point.x - hit.normal.x * 0.9, qz = hit.point.z - hit.normal.z * 0.9;
            const int ix = static_cast<int>(std::floor((qx - x0_) / step_ + 0.5)), iz = static_cast<int>(std::floor((qz - z0_) / step_ + 0.5));
            if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) continue;
            int top = -1;
            for (int id : cells_[iz * nx_ + ix]) if (std::abs(nodes_[id].y - topY) < 0.25) top = id;
            if (top < 0 || !seenTop.insert(top).second) continue;
            const auto& m = nodes_[top];
            NavEdge edge;
            edge.to = top; edge.type = NavEdgeType::Climb;
            edge.cost = std::hypot(m.x - node.x, m.z - node.z) + 3.5 + (m.y - node.y) * 1.5;
            edge.wallPoint = hit.point; edge.wallNormal = {hit.normal.x, hit.normal.z}; edge.topY = topY;
            node.edges.push_back(edge);
        }
    }
}

void NavGraph::prune() {
    const int count = static_cast<int>(nodes_.size());
    std::vector<std::vector<int>> reverse(count);
    for (const auto& node : nodes_) for (const auto& edge : node.edges) reverse[edge.to].push_back(node.id);
    std::vector<int> component(count, -1);
    int bestComponent = -1, bestSize = 0, componentIndex = 0;
    for (int i = 0; i < count; ++i) {
        if (component[i] >= 0) continue;
        std::vector<int> stack{i}; component[i] = componentIndex;
        int size = 0;
        while (!stack.empty()) {
            const int k = stack.back(); stack.pop_back(); ++size;
            for (const auto& e : nodes_[k].edges) if (component[e.to] < 0) { component[e.to] = componentIndex; stack.push_back(e.to); }
            for (int j : reverse[k]) if (component[j] < 0) { component[j] = componentIndex; stack.push_back(j); }
        }
        if (size > bestSize) { bestSize = size; bestComponent = componentIndex; }
        ++componentIndex;
    }
    int seed = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    const auto& pad = stage_.spawnPads[0];
    for (const auto& node : nodes_) {
        if (component[node.id] != bestComponent) continue;
        const double d = std::pow(node.x - pad.x, 2) + std::pow(node.z - pad.z, 2) + std::pow((node.y - pad.y) * 3, 2);
        if (d < bestDistance) { bestDistance = d; seed = node.id; }
    }
    valid_.assign(count, 0); exitable_.assign(count, 0);
    if (seed < 0) return;
    auto walk = [&](bool reverseEdges) {
        std::vector<std::uint8_t> seen(count, 0);
        std::vector<int> stack{seed}; seen[seed] = 1;
        while (!stack.empty()) {
            const int k = stack.back(); stack.pop_back();
            if (reverseEdges) {
                for (int j : reverse[k]) if (!seen[j]) { seen[j] = 1; stack.push_back(j); }
            } else {
                for (const auto& e : nodes_[k].edges) if (!seen[e.to]) { seen[e.to] = 1; stack.push_back(e.to); }
            }
        }
        return seen;
    };
    const auto forward = walk(false);
    exitable_ = walk(true);
    int validCount = 0;
    for (int i = 0; i < count; ++i) { valid_[i] = forward[i] && exitable_[i]; validCount += valid_[i]; }
    if (validCount < bestSize * 0.5) {
        for (int i = 0; i < count; ++i) valid_[i] = component[i] == bestComponent;
        exitable_ = valid_;
    }
    for (int i = 0; i < count; ++i) if (valid_[i]) validIds_.push_back(i);
}

int NavGraph::nearest(glm::dvec3 pos, double maxUp, bool start) const {
    const int ix = static_cast<int>(std::floor((pos.x - x0_) / step_ + 0.5));
    const int iz = static_cast<int>(std::floor((pos.z - z0_) / step_ + 0.5));
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int radius = 0; radius <= 3; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
            if (std::max(std::abs(dx), std::abs(dz)) != radius) continue;
            const int jx = ix + dx, jz = iz + dz;
            if (jx < 0 || jz < 0 || jx >= nx_ || jz >= nz_) continue;
            for (int id : cells_[jz * nx_ + jx]) {
                if (!valid_[id] && !(start && exitable_[id])) continue;
                const auto& node = nodes_[id];
                if (node.y > pos.y + maxUp) continue;
                const double d = std::pow(node.x - pos.x, 2) + std::pow(node.z - pos.z, 2) + std::pow((node.y - pos.y) * 2.5, 2);
                if (d < bestDistance) { bestDistance = d; best = id; }
            }
        }
        if (best >= 0) return best;
    }
    return best;
}

std::vector<int> NavGraph::path(int start, int goal, int team, int maxIter, bool noClimb) const {
    if (start < 0 || goal < 0 || start >= static_cast<int>(nodes_.size()) || goal >= static_cast<int>(nodes_.size())) return {};
    const int count = static_cast<int>(nodes_.size());
    std::vector<float> g(count);
    std::vector<int> from(count, -1);
    std::vector<std::uint8_t> seen(count, 0), closed(count, 0);
    const auto& destination = nodes_[goal];
    const auto heuristic = [&](const NavNode& n) { return std::hypot(n.x - destination.x, n.z - destination.z) + std::abs(n.y - destination.y) * 0.5; };
    Heap heap;
    seen[start] = 1; g[start] = 0; heap.push(start, heuristic(nodes_[start]));
    int iterations = 0;
    while (!heap.empty() && iterations++ < maxIter) {
        const int cur = heap.pop();
        if (cur == goal) break;
        if (closed[cur]) continue;
        closed[cur] = 1;
        for (const auto& edge : nodes_[cur].edges) {
            if (noClimb && edge.type == NavEdgeType::Climb) continue;
            const auto& node = nodes_[edge.to];
            if (node.zone >= 0 && node.zone != team) continue;
            const double next = g[cur] + edge.cost + (node.wet == 2 ? 2.0 : node.wet == 1 ? 0.5 : 0.0);
            if (!seen[edge.to] || next < g[edge.to]) {
                seen[edge.to] = 1; g[edge.to] = static_cast<float>(next); from[edge.to] = cur;
                heap.push(edge.to, next + heuristic(node));
            }
        }
    }
    if (!seen[goal]) return {};
    std::vector<int> result;
    for (int k = goal; k != -1; k = from[k]) { result.push_back(k); if (result.size() > 4000) break; }
    std::reverse(result.begin(), result.end());
    return result;
}

const NavEdge* NavGraph::edge(int from, int to) const {
    if (from < 0 || from >= static_cast<int>(nodes_.size())) return nullptr;
    for (const auto& e : nodes_[from].edges) if (e.to == to) return &e;
    return nullptr;
}

NavEdgeType NavGraph::edgeType(int from, int to) const {
    const auto* e = edge(from, to);
    return e ? e->type : NavEdgeType::Walk;
}

} // namespace restoration
