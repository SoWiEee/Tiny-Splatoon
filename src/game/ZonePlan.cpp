#include "ZonePlan.hpp"

#include "ArsenalData.hpp"
#include "BotSimulation.hpp"
#include "FacePaint.hpp"
#include "NavGraph.hpp"
#include "PlayerSimulation.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"
#include "ZoneControl.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;

double horizontal(const glm::dvec3& v) { return std::hypot(v.x, v.z); }
double angleDiff(double from, double to) { return std::remainder(to - from, 2.0 * pi); }
bool chargeKind(std::string_view kind) {
    return kind == "charger" || kind == "spinner" || kind == "splatling";
}
bool longKind(std::string_view kind) {
    return kind == "charger" || kind == "spinner" || kind == "splatling" || kind == "bow";
}
bool painterKind(std::string_view kind) {
    return kind == "roller" || kind == "brush" || kind == "bucket" || kind == "slosh" || kind == "slosher";
}
bool thrownSub(std::string_view kind) {
    return kind == "bomb" || kind == "sticky" || kind == "burst" || kind == "mist" || kind == "seeker";
}
std::size_t randomIndex(double r, std::size_t size) {
    if (!size) return 0;
    return std::min(size - 1, static_cast<std::size_t>(std::max(0.0, r) * static_cast<double>(size)));
}
}

ZonePlan::ZonePlan(const ZoneControl& zones, const StageData& stage, const StageGeometry& geometry,
                   const FacePaint& paint, const NavGraph& nav, std::uint32_t seed, RandomSource randomSource)
    : zones_(zones), stage_(stage), geometry_(geometry), paint_(paint), nav_(nav),
      seed_(seed ? seed : 1), randomSource_(std::move(randomSource)) {
    const auto& states = zones_.zones();
    info_.resize(states.size());

    struct FaceRange { int face = -1, begin = -1, end = -1; };
    std::vector<FaceRange> ranges;
    ranges.reserve(stage_.faces.size());
    for (int faceId = 0; faceId < static_cast<int>(stage_.faces.size()); ++faceId) {
        const auto& fg = paint_.faceGrid(faceId);
        if (fg.offset < 0 || fg.columns <= 0 || fg.rows <= 0) continue;
        ranges.push_back({faceId, fg.offset, fg.offset + fg.columns * fg.rows});
    }
    std::sort(ranges.begin(), ranges.end(), [](const FaceRange& a, const FaceRange& b) { return a.begin < b.begin; });

    const auto faceForCell = [&ranges](int cell) -> const FaceRange* {
        const auto it = std::upper_bound(ranges.begin(), ranges.end(), cell,
            [](int value, const FaceRange& range) { return value < range.begin; });
        if (it == ranges.begin()) return nullptr;
        const auto& range = *std::prev(it);
        return cell >= range.begin && cell < range.end ? &range : nullptr;
    };

    for (std::size_t zi = 0; zi < states.size(); ++zi) {
        const auto& zone = states[zi];
        auto& info = info_[zi];
        info.id = zone.id;
        info.y0 = zone.region.y0;
        info.y1 = zone.region.y1;
        std::unordered_map<int, int> localByNode;
        for (const int cell : zone.region.cells) {
            const auto* range = faceForCell(cell);
            if (!range) continue;
            const auto& face = stage_.faces[range->face];
            const auto& fg = paint_.faceGrid(range->face);
            const auto flat = cell - fg.offset;
            if (flat < 0 || flat >= fg.columns * fg.rows || static_cast<std::size_t>(cell) >= paint_.dead().size()) continue;
            const int col = flat % fg.columns, row = flat / fg.columns;
            const auto point = face.origin + face.u * ((col + 0.5) * fg.cellWidth) + face.v * ((row + 0.5) * fg.cellHeight);
            info.radius = std::max(info.radius, std::hypot(point.x - zone.center.x, point.z - zone.center.z));
            if (paint_.dead()[cell]) continue;
            const int nodeId = nav_.nearest(point, 1.0);
            if (nodeId < 0) continue;
            const auto& node = nav_.nodes()[nodeId];
            if (std::hypot(node.x - point.x, node.z - point.z) > 2.2 || std::abs(node.y - point.y) > 1.8) continue;
            auto found = localByNode.find(nodeId);
            int local = -1;
            if (found == localByNode.end()) {
                local = static_cast<int>(info.nodes.size());
                localByNode.emplace(nodeId, local);
                info.nodes.push_back(nodeId);
            } else local = found->second;
            info.cellNode.emplace_back(cell, local);
        }
        info.reachable = info.cellNode.size();
        info.need[0].assign(info.nodes.size(), 0.0f);
        info.need[1].assign(info.nodes.size(), 0.0f);
        info.ink[0].assign(info.nodes.size(), 0);
        info.ink[1].assign(info.nodes.size(), 0);
        info.neighbors.resize(info.nodes.size());
        for (std::size_t i = 0; i < info.nodes.size(); ++i) {
            const auto& a = nav_.nodes()[info.nodes[i]];
            for (std::size_t j = 0; j < info.nodes.size(); ++j) {
                const auto& b = nav_.nodes()[info.nodes[j]];
                if (std::abs(a.x - b.x) < 2.4 && std::abs(a.z - b.z) < 2.4 &&
                    std::abs(a.y - b.y) < 1.0 && std::hypot(a.x - b.x, a.z - b.z) <= 2.3)
                    info.neighbors[i].push_back(static_cast<int>(j));
            }
        }
        for (const int nodeId : nav_.validIds()) {
            const auto& node = nav_.nodes()[nodeId];
            const double d = std::hypot(node.x - zone.center.x, node.z - zone.center.z);
            if (node.zone >= 0) continue;
            if (d <= info.radius + 16.0) info.ring.push_back(nodeId);
            if (d >= info.radius + 3.0 && d <= info.radius + 18.0) info.far.push_back(nodeId);
        }
        if (info.ring.empty()) info.ring = info.nodes;
        if (info.far.empty()) info.far = info.ring;

        for (int team = 0; team < 2; ++team) {
            const auto& spawn = stage_.spawnPads[team];
            const double ex = spawn.x - zone.center.x, ez = spawn.z - zone.center.z;
            const double el = std::hypot(ex, ez);
            double bestScore = -std::numeric_limits<double>::infinity();
            int bestNode = -1;
            if (el > 1e-6) for (const int nodeId : info.ring) {
                const auto& node = nav_.nodes()[nodeId];
                const double dx = node.x - zone.center.x, dz = node.z - zone.center.z;
                const double d = std::hypot(dx, dz);
                if (d < info.radius + 3.0 || d > info.radius + 9.0 || node.wet == 2) continue;
                const double score = ((dx * ex + dz * ez) / (std::max(d, 1.0) * el)) * 3.0 -
                    std::abs(d - (info.radius + 5.0)) * 0.3 - std::abs(node.y - zone.center.y) * 0.35 - (node.wet ? 1.0 : 0.0);
                if (score > bestScore) { bestScore = score; bestNode = nodeId; }
            }
            if (bestNode >= 0) {
                auto& spot = info.stage[team];
                spot.valid = true;
                const auto& best = nav_.nodes()[bestNode];
                spot.point = {best.x, best.y, best.z};
                for (const int nodeId : info.ring) {
                    const auto& node = nav_.nodes()[nodeId];
                    if (std::hypot(node.x - best.x, node.z - best.z) < 3.2 &&
                        std::abs(node.y - best.y) < 1.2 && node.wet != 2) spot.nodes.push_back(nodeId);
                }
                if (spot.nodes.empty()) spot.nodes.push_back(bestNode);
            }
        }
    }
}

void ZonePlan::resetAfterAuthorityChange(){
    assignments_.clear();botState_.clear();
    focus_.fill(-1);nextSpecial_.fill(0.0);waves_={};
    time_=0.0;scanTimer_=0.0;roleTimer_=-1.0;activeSignature_=0;activeSetSignature_=0;initialized_=false;
    for(auto& info:info_){
        for(auto& values:info.need)std::fill(values.begin(),values.end(),0.0f);
        for(auto& values:info.ink)std::fill(values.begin(),values.end(),0);
        info.needSum={0.0,0.0};
    }
}

double ZonePlan::random() {
    if (randomSource_) return std::clamp(randomSource_(), 0.0, std::nextafter(1.0, 0.0));
    seed_ += 0x6d2b79f5u;
    std::uint32_t t = seed_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

ZoneBotAssignment ZonePlan::assignment(const BotSimulation& bot) const {
    const auto found = assignments_.find(&bot);
    if (found != assignments_.end()) return found->second;
    const auto& active = zones_.activeIndices();
    if (!active.empty() && active.front() >= 0 && active.front() < static_cast<int>(zones_.zones().size()))
        return {ZoneBotRole::Paint, zones_.zones()[active.front()].id};
    return {};
}

ZoneWaveState ZonePlan::waveState(int team) const {
    return team >= 0 && team < 2 ? waves_[team].state : ZoneWaveState::Free;
}

void ZonePlan::update(double dt, const std::vector<BotSimulation*>& bots) {
    if (!std::isfinite(dt) || dt < 0.0) return;
    time_ += dt;
    const auto& active = zones_.activeIndices();
    if (active.empty()) return;

    bool scanned = false;
    scanTimer_ -= dt;
    if (scanTimer_ <= 0.0) {
        scanTimer_ = 0.33;
        scanned = true;
        const auto& ink = paint_.grid();
        const auto& dead = paint_.dead();
        for (const int activeIndex : active) {
            if (activeIndex < 0 || activeIndex >= static_cast<int>(zones_.zones().size())) continue;
            const auto& zone = zones_.zones()[activeIndex];
            auto found = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& i) { return i.id == zone.id; });
            if (found == info_.end()) continue;
            auto& info = *found;
            auto& n0 = info.need[0]; auto& n1 = info.need[1];
            auto& k0 = info.ink[0]; auto& k1 = info.ink[1];
            std::fill(n0.begin(), n0.end(), 0.0f); std::fill(n1.begin(), n1.end(), 0.0f);
            std::fill(k0.begin(), k0.end(), 0); std::fill(k1.begin(), k1.end(), 0);
            double s0 = 0.0, s1 = 0.0;
            for (const auto& [cell, local] : info.cellNode) {
                if (cell < 0 || static_cast<std::size_t>(cell) >= ink.size() || static_cast<std::size_t>(cell) >= dead.size() || dead[cell]) continue;
                const auto color = ink[cell];
                if (color == 1 && k0[local] < std::numeric_limits<unsigned short>::max()) ++k0[local];
                else if (color == 2 && k1[local] < std::numeric_limits<unsigned short>::max()) ++k1[local];
                if (color != 1) { const float value = color == 2 ? 1.5f : 1.0f; n0[local] += value; s0 += value; }
                if (color != 2) { const float value = color == 1 ? 1.5f : 1.0f; n1[local] += value; s1 += value; }
            }
            info.needSum = {s0, s1};
        }
    }

    if (scanned) {
        for (int team = 0; team < 2; ++team) {
            auto& wave = waves_[team];
            if (zones_.owner() != 1 - team) { wave.state = ZoneWaveState::Free; wave.zone = -1; continue; }
            if (wave.state == ZoneWaveState::Free) {
                int weakest = -1;
                double weakestShare = std::numeric_limits<double>::infinity();
                for (const int activeIndex : active) {
                    if (activeIndex < 0 || activeIndex >= static_cast<int>(zones_.zones().size())) continue;
                    const auto& z = zones_.zones()[activeIndex];
                    if (z.share[1 - team] < weakestShare) { weakestShare = z.share[1 - team]; weakest = z.id; }
                }
                wave.zone = weakest;
                wave.state = ZoneWaveState::Stage;
                wave.started = time_;
            }
            const auto zIt = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& z) { return z.id == wave.zone; });
            const auto infoIt = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& i) { return i.id == wave.zone; });
            if (zIt == zones_.zones().end() || infoIt == info_.end()) continue;
            const auto& zone = *zIt; const auto& info = *infoIt; const auto& stageSpot = info.stage[team];
            int alive = 0, gathered = 0;
            bool teammateSpecial = false;
            for (const auto* bot : bots) {
                if (!bot || bot->actor().team() != team || !bot->actor().state.alive) continue;
                const auto& actor = bot->actor();
                ++alive;
                const double dZone = std::hypot(actor.state.pos.x - zone.center.x, actor.state.pos.z - zone.center.z);
                const bool nearStage = stageSpot.valid && std::hypot(actor.state.pos.x - stageSpot.point.x, actor.state.pos.z - stageSpot.point.z) < 8.0;
                if (nearStage || dZone < info.radius + 2.0) ++gathered;
                if (actor.state.specialCharge >= actor.weapon().number("specialCost", 190.0) && dZone < info.radius + 18.0)
                    teammateSpecial = true;
            }
            if (wave.state == ZoneWaveState::Stage) {
                const double waited = time_ - wave.started;
                if ((alive >= 3 && gathered >= 3) || (teammateSpecial && gathered >= 2) ||
                    (gathered >= alive && alive > 0 && waited > 7.0) || waited > 15.0) {
                    wave.state = ZoneWaveState::Push;
                    wave.started = time_;
                    ++wave.pushes;
                    nextSpecial_[team] = time_;
                }
            } else if (wave.state == ZoneWaveState::Push && alive <= 1 && time_ - wave.started > 4.0) {
                wave.state = ZoneWaveState::Stage;
                wave.started = time_;
            }
        }
    }

    // Match the source planner's stable role signature: re-deal on rotations, owner changes,
    // retake-wave transitions, then every two seconds for local adaptation.
    std::uint64_t activeSetSignature = 1469598103934665603ull;
    for (const int index : active) activeSetSignature = (activeSetSignature ^ static_cast<std::uint64_t>(index + 1)) * 1099511628211ull;
    for (const char c : zones_.objective().id) activeSetSignature = (activeSetSignature ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    const bool rotated = initialized_ && activeSetSignature != activeSetSignature_;
    std::uint64_t signature = 1469598103934665603ull;
    const auto mix = [&signature](std::uint64_t v) { signature = (signature ^ v) * 1099511628211ull; };
    mix(static_cast<std::uint64_t>(zones_.owner() + 2));
    for (const int activeIndex : active) {
        mix(static_cast<std::uint64_t>(activeIndex + 1));
        if (activeIndex >= 0 && activeIndex < static_cast<int>(zones_.zones().size()))
            mix(static_cast<std::uint64_t>(zones_.zones()[activeIndex].owner + 2));
    }
    mix(static_cast<std::uint64_t>(waves_[0].state)); mix(static_cast<std::uint64_t>(waves_[1].state));
    if (!initialized_ || rotated || signature != activeSignature_ || time_ - roleTimer_ > 2.0 || time_ < roleTimer_) {
        activeSignature_ = signature;
        activeSetSignature_ = activeSetSignature;
        roleTimer_ = time_;
        for (int team = 0; team < 2; ++team) assignTeam(team, bots);
        initialized_ = true;
    }
}

// Role assignment mirrors bots.js: specialist perches, objective holders, a small forward
// pressure group while we hold, then the two-zone centre's 1:3 / 3:1 split.
void ZonePlan::assignTeam(int team, const std::vector<BotSimulation*>& allBots) {
    const auto& active = zones_.activeIndices();
    if (active.empty()) return;
    std::vector<BotSimulation*> bots;
    for (auto* bot : allBots) if (bot && bot->actor().team() == team) bots.push_back(bot);
    std::unordered_map<const BotSimulation*, ZoneBotAssignment> next;
    if (bots.empty()) { for (auto it = assignments_.begin(); it != assignments_.end();) it = it->first && it->first->actor().team() == team ? assignments_.erase(it) : std::next(it); return; }
    const bool holdAll = zones_.owner() == team;
    const auto kind = [holdAll](const BotSimulation* bot) {
        const auto w = bot->actor().weaponProfile().kind;
        if (w == "charger" || ((w == "spinner" || w == "splatling") && holdAll)) return 2;
        return painterKind(w) ? 1 : 0;
    };
    int pushCount = 0;
    if (holdAll && bots.size() >= 3) {
        bool secure = bots.size() >= 4;
        for (const int activeIndex : active) {
            if (activeIndex < 0 || activeIndex >= static_cast<int>(zones_.zones().size()) || zones_.zones()[activeIndex].share[team] < 0.9)
                secure = false;
        }
        pushCount = secure ? 2 : 1;
    }
    std::vector<BotSimulation*> pusherCandidates;
    for (auto* bot : bots) if (kind(bot) != 2) pusherCandidates.push_back(bot);
    std::stable_sort(pusherCandidates.begin(), pusherCandidates.end(), [&](const auto* a, const auto* b) {
        const auto pa = assignment(*a).role == ZoneBotRole::Push;
        const auto pb = assignment(*b).role == ZoneBotRole::Push;
        if (pa != pb) return pa;
        return kind(a) < kind(b);
    });
    if (static_cast<int>(pusherCandidates.size()) > pushCount) pusherCandidates.resize(pushCount);

    std::unordered_map<const BotSimulation*, int> zoneOf;
    if (active.size() == 1) {
        const int id = zones_.zones()[active.front()].id;
        for (auto* bot : bots) zoneOf[bot] = id;
    } else if (active.size() == 2) {
        const auto& z0 = zones_.zones()[active[0]]; const auto& z1 = zones_.zones()[active[1]];
        int w0 = z0.owner == team ? 1 : 3, w1 = z1.owner == team ? 1 : 3;
        if (w0 == 3 && w1 == 3) {
            const double f0 = z0.share[team] + (focus_[team] == z0.id ? 0.1 : 0.0);
            const double f1 = z1.share[team] + (focus_[team] == z1.id ? 0.1 : 0.0);
            if (f0 >= f1) w1 = 1; else w0 = 1;
            focus_[team] = f0 >= f1 ? z0.id : z1.id;
        }
        const int cap0 = static_cast<int>(std::lround(static_cast<double>(bots.size()) * w0 / (w0 + w1)));
        const auto distanceTo = [&](const BotSimulation* bot, const ZoneState& zone) {
            const glm::dvec3 p = bot->actor().state.alive ? bot->actor().state.pos : stage_.spawnPads[team];
            return std::hypot(p.x - zone.center.x, p.z - zone.center.z);
        };
        std::stable_sort(bots.begin(), bots.end(), [&](const auto* a, const auto* b) {
            const auto pa = assignment(*a).zone, pb = assignment(*b).zone;
            const double ka = distanceTo(a, z0) - distanceTo(a, z1) + (pa == z0.id ? -5.0 : pa == z1.id ? 5.0 : 0.0);
            const double kb = distanceTo(b, z0) - distanceTo(b, z1) + (pb == z0.id ? -5.0 : pb == z1.id ? 5.0 : 0.0);
            return ka < kb;
        });
        for (std::size_t i = 0; i < bots.size(); ++i) zoneOf[bots[i]] = i < static_cast<std::size_t>(cap0) ? z0.id : z1.id;
    } else {
        // Current reference stages use at most two simultaneously active zones.
        for (auto* bot : bots) zoneOf[bot] = zones_.zones()[active.front()].id;
    }

    const auto wave = waves_[team];
    for (auto* bot : bots) {
        ZoneBotRole role = std::find(pusherCandidates.begin(), pusherCandidates.end(), bot) != pusherCandidates.end() ? ZoneBotRole::Push :
            kind(bot) == 2 ? ZoneBotRole::Watch : holdAll && kind(bot) != 1 ? ZoneBotRole::Guard : ZoneBotRole::Paint;
        int zone = zoneOf[bot];
        if (wave.state != ZoneWaveState::Free) {
            zone = wave.zone;
            if (role != ZoneBotRole::Watch && wave.state == ZoneWaveState::Stage && !onZone(zone, bot->actor().state.pos))
                role = ZoneBotRole::Stage;
        }
        next[bot] = {role, zone};
    }
    for (auto it = assignments_.begin(); it != assignments_.end();) {
        if (it->first && it->first->actor().team() == team) it = assignments_.erase(it); else ++it;
    }
    assignments_.insert(next.begin(), next.end());
}

bool ZonePlan::onZone(int zoneId, const glm::dvec3& point) const {
    const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& v) { return v.id == zoneId; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& v) { return v.id == zoneId; });
    if (z == zones_.zones().end() || info == info_.end()) return false;
    const double d = std::hypot(point.x - z->center.x, point.z - z->center.z);
    return d < info->radius + 1.5 && point.y > info->y0 - 1.5 && point.y < info->y1 + 2.5;
}

void ZonePlan::syncBot(BotSimulation& bot) {
    auto& state = botState_[&bot];
    const auto now = assignment(bot);
    const auto& active = zones_.activeIndices();
    std::uint64_t objective = 1469598103934665603ull;
    const auto mix = [&objective](std::uint64_t v) { objective = (objective ^ v) * 1099511628211ull; };
    for (const int index : active) mix(static_cast<std::uint64_t>(index + 1));
    for (const char c : zones_.objective().id) mix(static_cast<unsigned char>(c));
    const bool rotated = state.hasAssignment && state.objectiveSignature != objective;
    if (!state.hasAssignment || state.assignment.role != now.role || state.assignment.zone != now.zone || rotated) {
        state.assignment = now;
        state.hasAssignment = true;
        state.objectiveSignature = objective;
        state.holdUntil = state.holdDuration = state.aimTimer = 0.0;
        state.aimNode = -1;
        state.bomb = {};
        bot.goalTimer_ = 0.0;
        if (rotated && bot.mode_ == BotMode::Paint) bot.path_.clear();
        if (rotated) state.jumpAt = time_ + 0.4 + random() * 2.0;
    }
}

bool ZonePlan::planGoal(BotSimulation& bot, const std::vector<BotSimulation*>& bots) {
    auto assigned = assignment(bot);
    auto infoIt = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& info) { return info.id == assigned.zone; });
    auto zoneIt = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& zone) { return zone.id == assigned.zone; });
    if (infoIt == info_.end() || zoneIt == zones_.zones().end() ||
        std::find_if(zones_.activeIndices().begin(), zones_.activeIndices().end(), [&](int index) {
            return index >= 0 && index < static_cast<int>(zones_.zones().size()) && zones_.zones()[index].id == assigned.zone;
        }) == zones_.activeIndices().end()) return false;
    auto& state = botState_[&bot];
    auto& info = *infoIt;
    const auto& zone = *zoneIt;
    const int team = bot.actor().team();
    const bool arrived = bot.path_.empty() || bot.pathIndex_ >= static_cast<int>(bot.path_.size());
    if (arrived && state.holdDuration > 0.0) {
        state.holdUntil = time_ + state.holdDuration;
        state.holdDuration = 0.0;
    }
    if (bot.wiggleTimer_ > 0.0 || (bot.goalTimer_ > 0.0 && !(arrived && time_ >= state.holdUntil))) return true;

    ZoneBotRole role = assigned.role;
    const double needFraction = info.needSum[team] / std::max(1.0, static_cast<double>(info.reachable));
    if (role == ZoneBotRole::Guard && needFraction > 0.1) role = ZoneBotRole::Paint;
    int nodeId = -1;
    double holdDuration = 0.0;
    if (role == ZoneBotRole::Stage) {
        const auto& spot = info.stage[team];
        if (spot.valid && !spot.nodes.empty()) {
            nodeId = spot.nodes[randomIndex(random(), spot.nodes.size())];
            holdDuration = 2.0 + random() * 2.0;
            bot.goalTimer_ = 12.0;
        } else role = ZoneBotRole::Guard;
    }

    if (role == ZoneBotRole::Push) {
        double bestScore = -std::numeric_limits<double>::infinity();
        const auto& enemyPad = stage_.spawnPads[1 - team];
        const double ex = enemyPad.x - zone.center.x, ez = enemyPad.z - zone.center.z;
        const double el = std::max(1.0, std::hypot(ex, ez));
        for (int k = 0; k < 28 && !info.far.empty(); ++k) {
            const int candidate = info.far[randomIndex(random(), info.far.size())];
            const auto& n = nav_.nodes()[candidate];
            if (n.zone >= 0 || n.wet == 2 || std::hypot(n.x - enemyPad.x, n.z - enemyPad.z) < 22.0) continue;
            const double dx = n.x - zone.center.x, dz = n.z - zone.center.z;
            const double d = std::hypot(dx, dz), side = (dx * ex + dz * ez) / (std::max(d, 1.0) * el);
            if (side < -0.1) continue;
            const auto stats = bot.regionStats(n.x, n.y, n.z, 3.0);
            if (!stats.count) continue;
            const double value = stats.empty + stats.enemy * 1.4;
            const double score = value * 10.0 + side * 2.0 - std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) * 0.1 +
                random() * 1.5 - (n.wet ? 1.5 : 0.0);
            if (score > bestScore) { bestScore = score; nodeId = candidate; }
        }
        bot.goalTimer_ = 4.0 + random() * 3.0;
    } else if (role == ZoneBotRole::Watch) {
        const double range = bot.actor().weaponProfile().range;
        const double dMax = std::min(range * 0.85, info.radius + 12.0);
        const double dMin = std::min(info.radius * 0.55, dMax - 2.0);
        const auto& enemyPad = stage_.spawnPads[1 - team];
        const double ex = enemyPad.x - zone.center.x, ez = enemyPad.z - zone.center.z, el = std::max(1.0, std::hypot(ex, ez));
        std::vector<std::pair<double, int>> candidates;
        for (int k = 0; k < 70 && !info.ring.empty(); ++k) {
            const int candidate = info.ring[randomIndex(random(), info.ring.size())];
            const auto& n = nav_.nodes()[candidate];
            const double dx = n.x - zone.center.x, dz = n.z - zone.center.z, d = std::hypot(dx, dz);
            if (d < dMin || d > dMax || n.wet == 2) continue;
            const double side = (dx * ex + dz * ez) / (std::max(d, 1.0) * el);
            double score = std::clamp(n.y - zone.center.y, -2.0, 4.0) * 1.1 - side * 1.6 -
                std::abs(d - range * 0.55) * 0.1 - std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) * 0.04 +
                random() * 1.2 - (n.wet ? 1.5 : 0.0);
            for (auto* mate : bots) if (mate && mate != &bot && mate->actor().team() == team && mate->actor().state.alive && mate->goal() >= 0) {
                const auto& g = nav_.nodes()[mate->goal()];
                if (std::hypot(g.x - n.x, g.z - n.z) < 5.0) score -= 4.0;
            }
            candidates.emplace_back(score, candidate);
        }
        std::sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.first > b.first; });
        const glm::dvec3 center = zone.center + glm::dvec3(0, 0.6, 0);
        for (std::size_t i = 0; i < std::min<std::size_t>(6, candidates.size()); ++i) {
            const auto& n = nav_.nodes()[candidates[i].second];
            if (geometry_.los({n.x, n.y + 1.4, n.z}, center)) { nodeId = candidates[i].second; break; }
        }
        if (nodeId >= 0) { holdDuration = 3.0 + random() * 3.0; bot.goalTimer_ = 12.0; }
    } else if (role == ZoneBotRole::Guard && nodeId < 0) {
        const auto& enemyPad = stage_.spawnPads[1 - team];
        const double ex = enemyPad.x - zone.center.x, ez = enemyPad.z - zone.center.z, el = std::max(1.0, std::hypot(ex, ez));
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int k = 0; k < 40 && !info.ring.empty(); ++k) {
            const int candidate = info.ring[randomIndex(random(), info.ring.size())];
            const auto& n = nav_.nodes()[candidate];
            const double dx = n.x - zone.center.x, dz = n.z - zone.center.z, d = std::hypot(dx, dz);
            if (d > info.radius + 3.0 || n.wet == 2) continue;
            const double side = (dx * ex + dz * ez) / (std::max(d, 1.0) * el);
            double score = side * 2.0 + (d > info.radius * 0.4 ? 1.0 : 0.0) -
                std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) * 0.05 + random() * 1.5 -
                std::abs(n.y - zone.center.y) * 0.5;
            for (auto* mate : bots) if (mate && mate != &bot && mate->actor().team() == team && mate->actor().state.alive && mate->goal() >= 0) {
                const auto& g = nav_.nodes()[mate->goal()];
                if (std::hypot(g.x - n.x, g.z - n.z) < 4.0) score -= 3.0;
            }
            if (score > bestScore) { bestScore = score; nodeId = candidate; }
        }
        holdDuration = 1.0 + random() * 1.6;
        bot.goalTimer_ = 7.0;
    }

    if (role == ZoneBotRole::Paint || nodeId < 0) {
        double bestScore = -std::numeric_limits<double>::infinity();
        const auto& need = info.need[team];
        const std::size_t step = info.nodes.size() > 110 ? 2 : 1;
        const std::size_t start = step == 2 ? randomIndex(random(), 2) : 0;
        for (std::size_t i = start; i < info.nodes.size(); i += step) {
            double v = 0.0;
            for (const int neighbor : info.neighbors[i]) v += need[neighbor];
            if (v < 4.0) continue;
            const auto& n = nav_.nodes()[info.nodes[i]];
            const double d = std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) + std::abs(n.y - bot.actor().state.pos.y) * 1.5;
            double score = std::min(v, 60.0) * 0.25 - d * 0.16 + random() * 2.0;
            for (auto* mate : bots) if (mate && mate != &bot && mate->actor().team() == team && mate->actor().state.alive && mate->goal() >= 0) {
                const auto& g = nav_.nodes()[mate->goal()];
                if (std::hypot(g.x - n.x, g.z - n.z) < 3.5) score -= 4.0;
            }
            if (score > bestScore) { bestScore = score; nodeId = info.nodes[i]; }
        }
        holdDuration = 0.0;
        bot.goalTimer_ = 3.5 + random() * 2.0;
    }

    if (nodeId < 0) {
        // The reference's no-paint fallback is a short guard attempt, then a forward push.
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int k = 0; k < 40 && !info.ring.empty(); ++k) {
            const int candidate = info.ring[randomIndex(random(), info.ring.size())];
            const auto& n = nav_.nodes()[candidate];
            if (std::hypot(n.x - zone.center.x, n.z - zone.center.z) > info.radius + 3.0 || n.wet == 2) continue;
            const double score = -std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) * 0.05 + random();
            if (score > bestScore) { bestScore = score; nodeId = candidate; }
        }
        if (nodeId >= 0) { holdDuration = 1.0 + random() * 1.5; bot.goalTimer_ = 6.0; }
    }
    if (nodeId < 0) {
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int k = 0; k < 28 && !info.far.empty(); ++k) {
            const int candidate = info.far[randomIndex(random(), info.far.size())];
            const auto& n = nav_.nodes()[candidate];
            if (n.zone >= 0 || n.wet == 2) continue;
            const double score = -std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z) * 0.1 + random();
            if (score > bestScore) { bestScore = score; nodeId = candidate; }
        }
        if (nodeId >= 0) bot.goalTimer_ = 4.0 + random() * 3.0;
    }
    if (nodeId < 0) { bot.pickPaintGoal(bots); return true; }

    const auto& node = nav_.nodes()[nodeId];
    if (bot.pathTo({node.x, node.y, node.z}, 0.3)) {
        state.holdDuration = holdDuration;
        state.holdUntil = 0.0;
        state.failures = 0;
    } else if (++state.failures >= 3) {
        state.failures = 0;
        bot.wiggle(0.8);
    }
    return true;
}

bool ZonePlan::shouldEngage(const BotSimulation& bot, const PlayerSimulation* target) const {
    if (!target) return false;
    const auto role = assignment(bot).role;
    if (role == ZoneBotRole::Push) return true;
    const double distance = std::hypot(target->state.pos.x - bot.actor().state.pos.x, target->state.pos.z - bot.actor().state.pos.z);
    const double range = bot.actor().weaponProfile().range;
    if (distance < range * (role == ZoneBotRole::Watch ? 1.05 : 1.25)) return true;
    if (role == ZoneBotRole::Watch || role == ZoneBotRole::Stage) return false;
    const int zoneId = assignment(bot).zone;
    const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == zoneId; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == zoneId; });
    if (z == zones_.zones().end() || info == info_.end()) return false;
    return std::hypot(target->state.pos.x - z->center.x, target->state.pos.z - z->center.z) < info->radius + 5.0;
}

bool ZonePlan::holdGround(const BotSimulation& bot, const PlayerSimulation* target) const {
    if (!target) return false;
    const auto assigned = assignment(bot);
    const auto role = assigned.role;
    const double distance = std::hypot(target->state.pos.x - bot.actor().state.pos.x, target->state.pos.z - bot.actor().state.pos.z);
    const double range = bot.actor().weaponProfile().range;
    if (role == ZoneBotRole::Watch) return distance < range * 1.05 && distance > 3.0;
    if (role == ZoneBotRole::Stage) return distance > 4.0;
    if (role == ZoneBotRole::Push || distance < 4.0) return false;
    const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
    if (z == zones_.zones().end() || info == info_.end()) return false;
    const double targetDistance = std::hypot(target->state.pos.x - z->center.x, target->state.pos.z - z->center.z);
    const double selfDistance = std::hypot(bot.actor().state.pos.x - z->center.x, bot.actor().state.pos.z - z->center.z);
    return targetDistance > info->radius + (role == ZoneBotRole::Guard ? 3.0 : 6.0) && targetDistance > selfDistance + 2.0;
}

bool ZonePlan::paintAim(BotSimulation& bot, double dt, glm::dvec3 move, bool moving, double& wantYaw, double& wantPitch) {
    auto& state = botState_[&bot];
    const auto assigned = assignment(bot);
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
    const auto zone = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
    if (info == info_.end() || zone == zones_.zones().end()) return false;
    const auto kind = bot.actor().weaponProfile().kind;
    if (kind == "roller" || kind == "brush") return false;

    const glm::dvec3 center = zone->center;
    if (state.faceUntil > time_) {
        wantYaw = std::atan2(center.x - bot.actor().state.pos.x, center.z - bot.actor().state.pos.z);
        return true;
    }
    state.aimTimer -= dt;
    if (state.aimTimer <= 0.0) {
        state.aimTimer = 0.7 + random() * 0.4;
        state.aimNode = -1;
        double best = -std::numeric_limits<double>::infinity();
        const auto& need = info->need[bot.actor().team()];
        double far = chargeKind(kind) ? std::min(bot.actor().weaponProfile().range * 0.9, 16.0) :
            kind == "bucket" || kind == "slosh" || kind == "slosher" ? 7.0 : kind == "blaster" ? 6.0 : 5.5;
        if (std::hypot(bot.actor().state.pos.x - center.x, bot.actor().state.pos.z - center.z) <= info->radius + far) {
            for (std::size_t li = 0; li < info->nodes.size(); ++li) {
                if (need[li] < 3.0f) continue;
                const auto& n = nav_.nodes()[info->nodes[li]];
                const double d = std::hypot(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z);
                if (d < 1.4 || d > far || std::abs(n.y - bot.actor().state.pos.y) > 3.5) continue;
                const double score = need[li] - d * 0.35;
                if (score > best) { best = score; state.aimNode = info->nodes[li]; }
            }
        }
        if (state.aimNode >= 0 && moving && !chargeKind(kind)) {
            const auto& n = nav_.nodes()[state.aimNode];
            const double moveYaw = std::atan2(move.x, move.z);
            const double targetYaw = std::atan2(n.x - bot.actor().state.pos.x, n.z - bot.actor().state.pos.z);
            if (std::abs(angleDiff(moveYaw, targetYaw)) >= 1.1) state.aimNode = -1;
        }
    }
    if (state.aimNode < 0 || state.aimNode >= static_cast<int>(nav_.nodes().size())) return false;
    const auto& node = nav_.nodes()[state.aimNode];
    const double distance = std::hypot(node.x - bot.actor().state.pos.x, node.z - bot.actor().state.pos.z);
    wantYaw = std::atan2(node.x - bot.actor().state.pos.x, node.z - bot.actor().state.pos.z) + std::sin(bot.sweep_) * 0.22;
    if (chargeKind(kind) || distance < 3.2)
        wantPitch = std::clamp(std::atan2(node.y + 0.05 - (bot.actor().state.pos.y + 1.1), std::max(0.5, distance)), -0.95, 0.15);
    return true;
}

void ZonePlan::bombAim(BotSimulation& bot, double dt, double& wantYaw, double& wantPitch) {
    auto& state = botState_[&bot];
    auto& bomb = state.bomb;
    if (bot.actor_.input.sub || bot.bombAim_) return;
    const auto assigned = assignment(bot);
    if (assigned.role == ZoneBotRole::Watch) return;
    const auto zone = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
    if (zone == zones_.zones().end() || info == info_.end()) return;
    const auto& actor = bot.actor_;
    const auto subKind = actor.subWeapon().kind;
    if (!thrownSub(subKind) || actor.state.ink < actor.subWeapon().number("inkCost", 70.0) + 10.0) return;

    if (bomb.active) {
        if (bomb.released) { bomb = {}; return; }
        bomb.remaining -= dt;
        if (bomb.remaining <= 0.0) { bomb = {}; return; }
        wantYaw = bomb.yaw;
        wantPitch = bomb.pitch;
        if (std::abs(angleDiff(bot.aimYaw_, bomb.yaw)) < 0.09 && std::abs(bot.aimPitch_ - bomb.pitch) < 0.09) {
            bot.bombAim_ = true;
            bomb.released = true;
            bot.bombCooldown_ = 4.0 + random() * 3.0;
        }
        return;
    }
    bomb.scan -= dt;
    if (bomb.scan > 0.0 || bot.bombCooldown_ > 0.0) return;
    bomb.scan = 0.5;
    const int team = actor.team();
    if (zone->owner != 1 - team && zone->share[1 - team] < 0.25) return;
    int bestInk = 24, bestNode = -1;
    double distance = 0.0;
    const auto& enemyInk = info->ink[1 - team];
    for (std::size_t i = 0; i < info->nodes.size(); ++i) {
        if (enemyInk[i] < 6) continue;
        const auto& node = nav_.nodes()[info->nodes[i]];
        const double d = std::hypot(node.x - actor.state.pos.x, node.z - actor.state.pos.z);
        if (d < 4.5 || d > 12.0 || std::abs(node.y - actor.state.pos.y) > 3.0) continue;
        int total = 0;
        for (const int neighbor : info->neighbors[i]) total += enemyInk[neighbor];
        if (total > bestInk) { bestInk = total; bestNode = info->nodes[i]; distance = d; }
    }
    if (bestNode < 0) return;
    const auto& target = nav_.nodes()[bestNode];
    const double dy = target.y - actor.state.pos.y;
    const double speed = actor.subWeapon().number("throwSpeed", 13.5);
    double pitch = 0.0, bestError = std::numeric_limits<double>::infinity();
    bool validPitch = false;
    for (int i = 0; i <= 50; ++i) {
        const double p = -0.5 + 0.025 * i;
        const double tp = std::clamp(p + 0.28, -0.3, 1.1);
        const double vx = std::cos(tp) * speed, vy = std::sin(tp) * speed + 1.5;
        const double disc = vy * vy + 48.0 * (1.35 - dy);
        if (disc < 0.0) continue;
        const double error = std::abs(vx * (vy + std::sqrt(disc)) / 24.0 - distance);
        if (error < bestError) { bestError = error; pitch = p; }
    }
    if (bestError >= 1.2) return;
    bomb.active = true;
    bomb.released = false;
    bomb.remaining = 1.1;
    bomb.yaw = std::atan2(target.x - actor.state.pos.x, target.z - actor.state.pos.z);
    bomb.pitch = pitch;
    wantYaw = bomb.yaw;
    wantPitch = bomb.pitch;
}

bool ZonePlan::shouldUseSpecial(BotSimulation& bot, bool fighting) {
    const int team = bot.actor().team();
    const auto assigned = assignment(bot);
    const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
    if (z == zones_.zones().end() || info == info_.end()) return false;
    const auto wave = waves_[team];
    if (wave.state == ZoneWaveState::Stage && !fighting) return false;
    if (wave.state == ZoneWaveState::Push && time_ < nextSpecial_[team]) return false;
    const double distance = std::hypot(bot.actor().state.pos.x - z->center.x, bot.actor().state.pos.z - z->center.z);
    if (distance > info->radius + 12.0) return false;
    int foes = 0;
    for (const auto* actor : bot.actor().combatWorld().actors()) {
        if (!actor || actor->team() == team || !actor->state.alive) continue;
        if (std::hypot(actor->state.pos.x - z->center.x, actor->state.pos.z - z->center.z) < info->radius + 8.0) ++foes;
    }
    if (zones_.owner() == team && foes == 0) return false;
    const auto id = bot.actor().specialWeapon().kind.empty() ? std::string_view(bot.actor().specialId()) : bot.actor().specialWeapon().kind;
    if ((id == "sonar" || id == "bubbler" || id == "wail" || id == "zooka" || id == "stamp" || id == "crab") && foes == 0) return false;
    const bool thrown = id == "storm" || id == "barrage" || id == "booyah" || id == "zooka" || id == "wail" || id == "blower";
    if (thrown && !fighting && distance > info->radius * 0.7) {
        const double toZone = std::atan2(z->center.x - bot.actor().state.pos.x, z->center.z - bot.actor().state.pos.z);
        if (std::abs(angleDiff(bot.aimYaw_, toZone)) > 0.6) { botState_[&bot].faceUntil = time_ + 0.8; return false; }
    }
    const double chance = fighting ? 0.03 : wave.state == ZoneWaveState::Push ? 0.2 : 0.08;
    if (random() >= chance) return false;
    if (wave.state == ZoneWaveState::Push) nextSpecial_[team] = time_ + 2.2 + random() * 0.8;
    return true;
}

void ZonePlan::onRespawn(BotSimulation& bot, const std::vector<BotSimulation*>& bots) {
    auto tryJump = [&](double minDistance, double chance) {
        const int team = bot.actor().team();
        const auto assigned = assignment(bot);
        const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
        const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
        if (z == zones_.zones().end() || info == info_.end() || assigned.role == ZoneBotRole::Push) return false;
        const double selfDistance = std::hypot(bot.actor().state.pos.x - z->center.x, bot.actor().state.pos.z - z->center.z);
        if (selfDistance < minDistance || random() > chance) return false;
        const auto wave = waves_[team];
        const bool staging = wave.state != ZoneWaveState::Free && wave.zone == assigned.zone && info->stage[team].valid;
        glm::dvec3 destination{0.0};
        PlayerSimulation* target = nullptr;
        double best = staging ? 9.0 : std::min(selfDistance - 12.0, info->radius + 14.0);
        for (auto* candidate : bot.actor().combatWorld().actors()) {
            if (!candidate || candidate == &bot.actor() || candidate->team() != team || !candidate->state.alive ||
                candidate->superJumpState() || candidate->state.hp < 45.0 ||
                glm::length(candidate->state.pos - bot.actor().state.pos) <= 18.0) continue;
            const double d = staging ? std::hypot(candidate->state.pos.x - info->stage[team].point.x,
                                                  candidate->state.pos.z - info->stage[team].point.z) :
                std::hypot(candidate->state.pos.x - z->center.x, candidate->state.pos.z - z->center.z);
            if (staging && onZone(assigned.zone, candidate->state.pos)) continue;
            if (d < best) { best = d; target = candidate; destination = candidate->state.pos; }
        }
        std::uint64_t beaconId = 0;
        if (!staging) for (const auto& device : bot.actor().combatWorld().devices()) {
            if (!device.live || device.kind != "beacon" || device.team != team || device.uses <= 0) continue;
            const double d = std::hypot(device.pos.x - z->center.x, device.pos.z - z->center.z);
            if (d >= best - 2.0 || glm::length(device.pos - bot.actor().state.pos) <= 18.0) continue;
            glm::dvec3 anchor;
            if (bot.actor().combatWorld().beaconAnchor(team, device.id, anchor)) {
                best = d; target = nullptr; destination = anchor; beaconId = device.id;
            }
        }
        if (!target && beaconId == 0) return false;
        if (!bot.actor().superJump(destination)) return false;
        if (beaconId) bot.actor().combatWorld().consumeBeacon(bot.actor(), beaconId);
        bot.path_.clear(); bot.goalTimer_ = 0.0;
        return true;
    };
    tryJump(24.0, 0.8);
}

void ZonePlan::maybeRotationJump(BotSimulation& bot, const std::vector<BotSimulation*>& bots) {
    auto& state = botState_[&bot];
    if (state.jumpAt <= 0.0 || time_ < state.jumpAt) return;
    state.jumpAt = 0.0;
    if (bot.mode_ != BotMode::Paint || bot.target_ || !bot.actor().state.alive || bot.actor().specialPose().active) return;
    const int team = bot.actor().team();
    const auto assigned = assignment(bot);
    const auto z = std::find_if(zones_.zones().begin(), zones_.zones().end(), [&](const ZoneState& item) { return item.id == assigned.zone; });
    const auto info = std::find_if(info_.begin(), info_.end(), [&](const ZoneInfo& item) { return item.id == assigned.zone; });
    if (z == zones_.zones().end() || info == info_.end() || assigned.role == ZoneBotRole::Push) return;
    const double distance = std::hypot(bot.actor().state.pos.x - z->center.x, bot.actor().state.pos.z - z->center.z);
    if (distance < 34.0 || random() > 0.5) return;
    const auto wave = waves_[team];
    const bool staging = wave.state != ZoneWaveState::Free && wave.zone == assigned.zone && info->stage[team].valid;
    PlayerSimulation* target = nullptr;
    glm::dvec3 destination{0.0};
    double best = staging ? 9.0 : std::min(distance - 12.0, info->radius + 14.0);
    for (auto* candidate : bot.actor().combatWorld().actors()) {
        if (!candidate || candidate == &bot.actor() || candidate->team() != team || !candidate->state.alive ||
            candidate->superJumpState() || candidate->state.hp < 45.0 || glm::length(candidate->state.pos - bot.actor().state.pos) <= 18.0) continue;
        const double d = staging ? std::hypot(candidate->state.pos.x - info->stage[team].point.x,
                                              candidate->state.pos.z - info->stage[team].point.z) :
            std::hypot(candidate->state.pos.x - z->center.x, candidate->state.pos.z - z->center.z);
        if (staging && onZone(assigned.zone, candidate->state.pos)) continue;
        if (d < best) { best = d; target = candidate; destination = candidate->state.pos; }
    }
    if (target && bot.actor().superJump(destination)) { bot.path_.clear(); bot.goalTimer_ = 0.0; }
}

} // namespace restoration
