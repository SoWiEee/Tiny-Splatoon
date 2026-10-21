#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace restoration {

struct SwimWakeActor {
    const void* id = nullptr;
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
    bool alive = false, local = false, swimming = false, climbing = false;
};

struct SwimWakeUniforms {
    std::array<glm::dvec4, 48> points{};
    std::array<glm::dvec4, 4> bounds{};
    std::array<glm::dvec4, 4> heads{};
    std::array<glm::dvec4, 4> forwards{};
};

struct SwimWakeUniformValues {
    std::vector<double> points, bounds, heads, forwards;
};

// CPU mirror of splat-ref/src/fx/swimWake.js. Its packed values feed the copied
// level.frag uniforms so the ink surface carries the submerged wake and mound.
class SwimWake {
public:
    static constexpr std::size_t SlotCount = 4;
    static constexpr std::size_t PointCount = 12;
    static constexpr double PointLife = 1.15;
    static constexpr double PointSpacing = 0.75;
    static constexpr double Break = -9.0;

    SwimWake() { reset(); }

    void reset() {
        for (auto& slot : slots_) {
            slot.actor = nullptr;
            slot.points.clear();
            slot.k = 0.0;
            slot.speedK = 0.0;
            slot.forward = {0.0, 0.0, 1.0};
            slot.head = glm::dvec3(0.0);
            slot.hasLast = false;
            slot.last = glm::dvec3(0.0);
            slot.swimming = false;
            slot.distanceKey = 0.0;
        }
        uniforms_ = {};
        for (auto& point : uniforms_.points) point = {0.0, 0.0, 0.0, Break};
        for (auto& bounds : uniforms_.bounds) bounds = {0.0, -999.0, 0.0, 0.0};
        for (auto& head : uniforms_.heads) head = {0.0, -999.0, 0.0, 0.0};
        for (auto& forward : uniforms_.forwards) forward = {0.0, 0.0, 1.0, 0.0};
        flatten();
    }

    // A paused frame is a strict no-op: no samples, expiry, or damping occurs
    // until both the shader clock and gameplay simulation resume.
    void update(double dt, double now, const std::vector<SwimWakeActor>& actors, glm::dvec3 camera) {
        if (!(dt > 0.0) || !std::isfinite(dt) || !std::isfinite(now)) return;

        struct Candidate { const SwimWakeActor* actor; double d2; };
        std::vector<Candidate> candidates;
        candidates.reserve(actors.size());
        for (const auto& actor : actors) {
            if (!actor.id || !actor.alive || (!actor.swimming && !actor.climbing)) continue;
            const glm::dvec3 offset = actor.position - camera;
            const double d2 = glm::dot(offset, offset);
            if (!actor.local && d2 > 38.0 * 38.0) continue;
            candidates.push_back({&actor, actor.local ? -1.0 : d2});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.d2 < b.d2;
        });

        std::array<const SwimWakeActor*, SlotCount> active{};
        for (auto& slot : slots_) slot.swimming = false;
        for (const auto& candidate : candidates) {
            const auto& actor = *candidate.actor;
            auto found = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& slot) {
                return slot.actor == actor.id;
            });
            if (found == slots_.end()) found = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
                return slot.actor == nullptr;
            });
            if (found == slots_.end() && actor.local) {
                found = std::max_element(slots_.begin(), slots_.end(), [](const Slot& a, const Slot& b) {
                    return a.distanceKey < b.distanceKey;
                });
                if (found != slots_.end()) {
                    found->points.clear();
                    found->hasLast = false;
                    found->k = 0.0;
                }
            }
            if (found == slots_.end()) continue;
            const auto index = static_cast<std::size_t>(found - slots_.begin());
            if (found->actor != actor.id) {
                found->actor = actor.id;
                found->points.clear();
                found->hasLast = false;
                found->k = 0.0;
            }
            found->distanceKey = candidate.d2;
            found->swimming = true;
            active[index] = &actor;
        }

        for (std::size_t i = 0; i < SlotCount; ++i) {
            auto& slot = slots_[i];
            if (!slot.actor) continue;
            while (!slot.points.empty() && (slot.points.front().isBreak || now - slot.points.front().born > PointLife))
                slot.points.erase(slot.points.begin());
            if (slot.swimming && active[i]) {
                const auto& actor = *active[i];
                const glm::dvec3 p = actor.position;
                const double speed = glm::length(actor.velocity);
                const bool climbing = actor.climbing;
                if (speed > 0.6) {
                    const glm::dvec3 heading(actor.velocity.x, climbing ? actor.velocity.y : 0.0, actor.velocity.z);
                    if (glm::dot(heading, heading) > 1e-12) slot.forward = glm::normalize(heading);
                }
                slot.speedK = damp(slot.speedK, std::clamp(speed / 11.8, 0.0, 1.0), 10.0, dt);
                const glm::dvec3 lastOffset = slot.last - p;
                if (slot.hasLast && glm::dot(lastOffset, lastOffset) > 2.5 * 2.5) {
                    if (!slot.points.empty() && !slot.points.back().isBreak) slot.points.push_back({{}, Break, true});
                    slot.hasLast = false;
                }
                if (!slot.hasLast || glm::dot(lastOffset, lastOffset) > PointSpacing * PointSpacing) {
                    if (speed > 0.8 || !slot.hasLast) {
                        slot.points.push_back({p, now, false});
                        slot.last = p;
                        slot.hasLast = true;
                    }
                }
                slot.head = p;
                slot.k = damp(slot.k, 1.0, 14.0, dt);
            } else {
                if (!slot.points.empty() && !slot.points.back().isBreak) slot.points.push_back({{}, Break, true});
                slot.hasLast = false;
                slot.k = damp(slot.k, 0.0, 16.0, dt);
                if (slot.k < 0.004 && std::none_of(slot.points.begin(), slot.points.end(), [](const Point& p) { return !p.isBreak; })) {
                    slot.actor = nullptr;
                    slot.points.clear();
                    slot.k = 0.0;
                }
            }
            while (slot.points.size() > PointCount - 1) slot.points.erase(slot.points.begin());
        }
        pack(now);
        flatten();
    }

    const SwimWakeUniforms& uniforms() const { return uniforms_; }
    const SwimWakeUniformValues& uniformValues() const { return values_; }

private:
    struct Point { glm::dvec3 position{0.0}; double born = 0.0; bool isBreak = false; };
    struct Slot {
        const void* actor = nullptr;
        std::vector<Point> points;
        double k = 0.0, speedK = 0.0, distanceKey = 0.0;
        glm::dvec3 forward{0.0, 0.0, 1.0}, head{0.0}, last{0.0};
        bool hasLast = false, swimming = false;
    };

    static double damp(double value, double target, double rate, double dt) {
        return value + (target - value) * (1.0 - std::exp(-rate * dt));
    }

    void pack(double now) {
        for (auto& point : uniforms_.points) point = {0.0, 0.0, 0.0, Break};
        for (auto& bounds : uniforms_.bounds) bounds = {0.0, -999.0, 0.0, 0.0};
        for (auto& head : uniforms_.heads) head = {0.0, -999.0, 0.0, 0.0};
        for (auto& forward : uniforms_.forwards) forward = {0.0, 0.0, 1.0, 0.0};
        for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
            const auto& slot = slots_[slotIndex];
            const std::size_t base = slotIndex * PointCount;
            if (!slot.actor) continue;
            std::size_t n = 0;
            glm::dvec3 center(0.0);
            std::size_t count = 0;
            for (const auto& point : slot.points) {
                uniforms_.points[base + n++] = point.isBreak
                    ? glm::dvec4(0.0, 0.0, 0.0, Break)
                    : glm::dvec4(point.position, point.born);
                if (!point.isBreak) { center += point.position; ++count; }
            }
            if (slot.swimming) {
                uniforms_.points[base + n++] = glm::dvec4(slot.head, now);
                center += slot.head;
                ++count;
            }
            if (count) {
                center /= static_cast<double>(count);
                double radius = 0.0;
                for (std::size_t j = 0; j < n; ++j) {
                    const auto& point = uniforms_.points[base + j];
                    if (point.w != Break) radius = std::max(radius, glm::length(glm::dvec3(point) - center));
                }
                radius = std::max(radius, glm::length(slot.head - center));
                uniforms_.bounds[slotIndex] = glm::dvec4(center, radius + 1.6);
            } else uniforms_.bounds[slotIndex] = {0.0, -999.0, 0.0, 0.0};
            uniforms_.heads[slotIndex] = glm::dvec4(slot.head, slot.k);
            uniforms_.forwards[slotIndex] = glm::dvec4(slot.forward, slot.speedK);
            for (std::size_t j = n; j < PointCount; ++j)
                uniforms_.points[base + j] = {0.0, 0.0, 0.0, Break};
        }
    }

    void flatten() {
        values_.points.resize(uniforms_.points.size() * 4);
        values_.bounds.resize(uniforms_.bounds.size() * 4);
        values_.heads.resize(uniforms_.heads.size() * 4);
        values_.forwards.resize(uniforms_.forwards.size() * 4);
        const auto copy = [](const auto& source, auto& destination) {
            std::size_t at = 0;
            for (const auto& value : source) for (int component = 0; component < 4; ++component)
                destination[at++] = value[component];
        };
        copy(uniforms_.points, values_.points);
        copy(uniforms_.bounds, values_.bounds);
        copy(uniforms_.heads, values_.heads);
        copy(uniforms_.forwards, values_.forwards);
    }

    std::array<Slot, SlotCount> slots_{};
    SwimWakeUniforms uniforms_{};
    SwimWakeUniformValues values_{};
};

} // namespace restoration
