#include "BotSimulation.hpp"
#include "BossSimulation.hpp"

#include "ArsenalData.hpp"
#include "CombatWorld.hpp"
#include "FacePaint.hpp"
#include "ZonePlan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;
double horizontal(glm::dvec3 v) { return std::hypot(v.x, v.z); }
double angleDiff(double from, double to) { return std::remainder(to - from, 2 * pi); }
double wander(double x) { return std::sin(x) * 0.6 + std::sin(x * 2.27 + 1.3) * 0.4; }
glm::dvec3 heading(double yaw) { return {std::sin(yaw), 0, std::cos(yaw)}; }
glm::dvec3 flatUnit(glm::dvec3 v) { const double d = horizontal(v); return d > 1e-6 ? glm::dvec3(v.x / d, 0, v.z / d) : glm::dvec3(0.0); }
bool melee(std::string_view kind) { return kind == "roller" || kind == "brush" || kind == "blade" || kind == "mitts"; }
bool charge(std::string_view kind) { return kind == "charger" || kind == "spinner" || kind == "splatling" || kind == "bow"; }
}

BotSimulation::BotSimulation(PlayerSimulation& actor, const StageData& stage, const StageGeometry& geometry,
                             FacePaint& paint, const NavGraph& nav, CombatWorld& world,
                             std::string_view difficulty, std::uint32_t seed, std::function<double()> randomSource)
    : actor_(actor), stage_(stage), geometry_(geometry), paint_(paint), nav_(nav), world_(world), seed_(seed ? seed : 1), randomSource_(std::move(randomSource)) {
    setDifficulty(difficulty);
    for (int id = 0; id < static_cast<int>(stage_.faces.size()); ++id) {
        const auto& f = stage_.faces[id];
        if (!f.turf || paint_.faceGrid(id).offset < 0) continue;
        const glm::dvec3 p0 = f.origin, p1 = p0 + f.u * f.width, p2 = p0 + f.v * f.height, p3 = p1 + f.v * f.height;
        turfFaces_.push_back({id, std::min({p0.x, p1.x, p2.x, p3.x}), std::max({p0.x, p1.x, p2.x, p3.x}),
                              std::min({p0.z, p1.z, p2.z, p3.z}), std::max({p0.z, p1.z, p2.z, p3.z})});
    }
    reset();
}

void BotSimulation::setDifficulty(std::string_view name) {
    if (name != "easy" && name != "normal" && name != "hard") name = "normal";
    const auto& d = DifficultyTuning(name);
    difficulty_ = {d.number("reaction"), d.number("aimError"), d.number("fireDiscipline"),
                   d.number("awareness"), d.number("aimOmega"), d.number("aimTurn")};
}

double BotSimulation::random() {
    if(randomSource_)return randomSource_();
    // ctx.js mulberry32, used to make native decisions reproducible against a source run with Math.random patched.
    seed_ += 0x6d2b79f5u;
    std::uint32_t t = seed_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

void BotSimulation::reset() {
    path_.clear(); pathIndex_ = 0; goal_ = -1; repath_ = goalTimer_ = goalCheck_ = 0;
    target_ = nullptr; seeTimer_ = react_ = lostTimer_ = 0;
    mode_ = BotMode::Paint;
    sweep_ = random() * 10;
    aimYaw_ = actor_.state.yaw; aimPitch_ = aimYawVelocity_ = aimPitchVelocity_ = 0;
    acquisitionTime_ = 9; acquisitionSignYaw_ = acquisitionSignPitch_ = 0;
    strafe_ = random() < 0.5 ? 1 : -1; strafeTimer_ = 0; strafeSmooth_ = 0; strafeAmplitude_ = 1;
    bombCooldown_ = 3 + random() * 4; fightHold_ = 0;
    think_ = random() * 0.2; chargeRelease_ = 0.95 + random() * 0.05;
    phase1_ = random() * 20; phase2_ = random() * 20; time_ = random() * 10;
    moveYaw_ = actor_.state.yaw; moveMagnitude_ = 0; dodgeCooldown_ = 1 + random() * 2;
    jumpCooldown_ = 0; paintPause_ = paintScan_ = paintYawOffset_ = 0;
    bestDistance_ = std::numeric_limits<double>::infinity(); noProgress_ = 0;
    displacementTimer_ = moveAccumulation_ = wiggleTimer_ = wiggleYaw_ = airStill_ = strikeTimer_ = 0;
    displacementPosition_ = lastPosition_ = actor_.state.pos;
    climbTimer_ = noClimbUntil_ = lookaheadTimer_ = navBackTimer_ = 0; ledgeReplanTime_ = -9; ledgeReplans_ = 0; climbAiming_ = false;
    threatScanTimer_ = random() * 0.25; threatEvadeTimer_ = 0; threatId_ = 0;
    threatAcquisition_ = 0; shieldBlockedTime_ = flankTimer_ = canopyTimer_ = 0; flankSide_ = canopySide_ = 0;
    threatCandidateId_ = 0; shieldId_ = canopyId_ = 0; navBackId_ = -1; threatActs_ = 0;
    threatYawSign_ = threatPitchSign_ = threatEvadeYaw_ = bombReleaseTimer_ = specialTime_ = barrageCooldown_ = 0;
    threatMemory_.clear(); shieldSnapshot_.clear();
    threatLos_ = threatPulse_ = wasPlanted_ = lastThreatGround_ = wasShieldBreaking_ = canopyBreaking_ = false;
    bombAim_ = releaseBomb_ = false; skipped_ = wasDead_ = firing_ = threatFiring_ = threatAiming_ = false;
    elapsed_ = 0;
    lookaheadIndex_ = lookaheadPathIndex_ = -1; strikes_ = 0;
    needJump_ = false;
}

BotSimulation::PaintStats BotSimulation::regionStats(double x, double y, double z, double radius) const {
    PaintStats result;
    const int own = actor_.team() + 1;
    for (const auto& index : turfFaces_) {
        if (x + radius < index.minX || x - radius > index.maxX || z + radius < index.minZ || z - radius > index.maxZ) continue;
        const auto& face = stage_.faces[index.id];
        if (std::abs(face.origin.y - y) > 2.5) continue;
        const auto& grid = paint_.faceGrid(index.id);
        const glm::dvec3 relative = glm::dvec3(x, y, z) - face.origin;
        const double u = glm::dot(relative, face.u), v = glm::dot(relative, face.v);
        const int i0 = std::max(0, static_cast<int>(std::floor((u - radius) / grid.cellWidth)));
        const int i1 = std::min(grid.columns - 1, static_cast<int>(std::floor((u + radius) / grid.cellWidth)));
        const int j0 = std::max(0, static_cast<int>(std::floor((v - radius) / grid.cellHeight)));
        const int j1 = std::min(grid.rows - 1, static_cast<int>(std::floor((v + radius) / grid.cellHeight)));
        for (int j = j0; j <= j1; j += 2) for (int i = i0; i <= i1; i += 2) {
            const double du = (i + 0.5) * grid.cellWidth - u, dv = (j + 0.5) * grid.cellHeight - v;
            if (du * du + dv * dv > radius * radius) continue;
            const int k = grid.offset + j * grid.columns + i;
            if (paint_.dead()[k]) continue;
            ++result.count;
            const int color = paint_.grid()[k];
            if (color == own) ++result.own; else if (color) ++result.enemy; else ++result.empty;
        }
    }
    if (result.count) {
        result.own /= result.count; result.enemy /= result.count; result.empty /= result.count;
    }
    return result;
}

void BotSimulation::perceive() {
    const auto& me = actor_.state;
    PlayerSimulation* best = nullptr;
    double bestScore = std::numeric_limits<double>::infinity();
    const glm::dvec3 eye = me.pos + glm::dvec3(0, 1.3, 0);
    for (auto* other : world_.actors()) {
        if (!other || other == &actor_ || other->team() == actor_.team() || !other->state.alive) continue;
        const auto& e = other->state;
        const double d = glm::length(e.pos - me.pos);
        if (d > difficulty_.awareness) continue;
        if (e.submerged && d > 3 && !(horizontal(e.vel) > 7 && d < 9)) continue;
        if (!geometry_.los(eye, e.pos + glm::dvec3(0, e.squid ? 0.3 : 1.0, 0))) continue;
        const double score = d - (other == target_ ? 4.0 : 0.0);
        if (score < bestScore) { bestScore = score; best = other; }
    }
    if (best) {
        if (best != target_) {
            target_ = best; react_ = difficulty_.reaction * (0.7 + random() * 0.6); repath_ = 0;
            acquisitionTime_ = 0;
            const double sign = random() < 0.5 ? -1 : 1;
            acquisitionSignYaw_ = sign * (0.5 + random() * 0.5);
            acquisitionSignPitch_ = (random() - 0.5) * 1.2;
        }
        seeTimer_ = 1.2; lostTimer_ = 0;
    } else {
        seeTimer_ -= 0.2;
        if (target_) {
            lostTimer_ += 0.2;
            if (lostTimer_ > 2.5 || glm::length(target_->state.pos - me.pos) > difficulty_.awareness + 6) target_ = nullptr;
        }
    }
    react_ -= 0.2;
}

bool BotSimulation::pathTo(glm::dvec3 pos, double maxUp) {
    int start = nav_.nearest(actor_.state.pos, 1.2, true);
    if (start >= 0 && actor_.state.grounded && nav_.nodes()[start].y - actor_.state.pos.y > 0.5) {
        const int lower = nav_.nearest(actor_.state.pos, 0.45, true);
        if (lower >= 0) start = lower;
    }
    const int goal = nav_.nearest(pos, maxUp);
    repath_ = 0.8 + random() * 0.4;
    if (start < 0 || goal < 0) { path_.clear(); return false; }
    auto next = nav_.path(start, goal, actor_.team(), 6000, time_ < noClimbUntil_);
    if (next.empty()) { path_.clear(); return false; }
    path_ = std::move(next); pathIndex_ = std::min(1, static_cast<int>(path_.size()) - 1);
    goal_ = goal; bestDistance_ = std::numeric_limits<double>::infinity(); noProgress_ = 0;
    lookaheadIndex_ = lookaheadPathIndex_ = -1;
    return true;
}

void BotSimulation::pickPaintGoal(const std::vector<BotSimulation*>& bots) {
    const auto& me = actor_.state;
    const auto& valid = nav_.validIds();
    if (valid.empty()) { path_.clear(); return; }
    const auto& enemyPad = stage_.spawnPads[1 - actor_.team()];
    const auto& ownPad = stage_.spawnPads[actor_.team()];
    const double total = glm::length(ownPad - enemyPad);
    int best = -1; double bestScore = -std::numeric_limits<double>::infinity();
    const auto kind = actor_.weaponProfile().kind;
    for (int i = 0; i < 28; ++i) {
        const int id = valid[std::min(valid.size() - 1, static_cast<std::size_t>(random() * valid.size()))];
        const auto& node = nav_.nodes()[id];
        if (node.zone >= 0 || node.wet == 2) continue;
        const double d = std::hypot(node.x - me.pos.x, node.z - me.pos.z);
        if (d > 38) continue;
        const auto near = regionStats(node.x, node.y, node.z, 3);
        if (!near.count) continue;
        const auto wide = regionStats(node.x, node.y, node.z, 6.5);
        const double value = (near.empty + near.enemy * 1.4) * 0.55 + (wide.empty + wide.enemy * 1.4) * 0.45;
        const double progress = 1 - std::hypot(node.x - enemyPad.x, node.z - enemyPad.z) / std::max(1.0, total);
        double score = value * 16 - d * 0.14 + std::clamp(progress, 0.0, 0.8) * 2.5 + random() * 1.5 - (node.wet ? 1.5 : 0.0);
        if (charge(kind)) score += std::clamp(node.y, 0.0, 5.0) * 1.6;
        if (value < 0.15) score -= 8;
        for (auto* mate : bots) {
            if (!mate || mate == this || mate->actor().team() != actor_.team() || mate->goal() < 0) continue;
            const auto& g = nav_.nodes()[mate->goal()];
            if (std::hypot(g.x - node.x, g.z - node.z) < 8) score -= 5;
        }
        if (score > bestScore) { bestScore = score; best = id; }
    }
    goalTimer_ = 4 + random() * 3;
    if (best >= 0) {
        const auto& n = nav_.nodes()[best];
        pathTo({n.x, n.y, n.z}, 0.3);
    }
}

void BotSimulation::pickRefill() {
    const auto& me = actor_.state;
    glm::dvec3 best(0); bool found = false; double bestRadius = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 14; ++i) {
        const double a = random() * 2 * pi, r = 1 + random() * 7;
        const glm::dvec3 p = me.pos + glm::dvec3(std::cos(a) * r, 0, std::sin(a) * r);
        const auto st = regionStats(p.x, p.y, p.z, 1.2);
        if (st.count && st.own > 0.6 && r < bestRadius) { bestRadius = r; best = p; found = true; }
    }
    for (int i = 0; i < 16 && !found; ++i) {
        const double a = random() * 2 * pi, r = 8 + random() * 14;
        const glm::dvec3 p = me.pos + glm::dvec3(std::cos(a) * r, 0, std::sin(a) * r);
        const auto st = regionStats(p.x, p.y, p.z, 1.5);
        if (st.count && st.own > 0.6) { best = p; found = true; }
    }
    if (!found || !pathTo(best, 0.4)) pathTo(stage_.spawnPads[actor_.team()], 1.2);
    repath_ = 1.2;
}

void BotSimulation::pickRetreat() {
    const auto& me = actor_.state;
    glm::dvec3 best(0); bool found = false; double bestScore = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < 16; ++i) {
        const double a = random() * 2 * pi, r = 3 + random() * 8;
        const glm::dvec3 p = me.pos + glm::dvec3(std::cos(a) * r, 0, std::sin(a) * r);
        const auto st = regionStats(p.x, p.y, p.z, 1.4);
        if (!st.count) continue;
        const double away = target_ ? horizontal(p - target_->state.pos) - horizontal(me.pos - target_->state.pos) : 0;
        const double score = st.own * 6 + away * 0.8 - r * 0.15 +
            (target_ && !geometry_.los(p + glm::dvec3(0, 1, 0), target_->state.pos + glm::dvec3(0, 1, 0)) ? 4.0 : 0.0);
        if (score > bestScore) { bestScore = score; best = p; found = true; }
    }
    if (found) pathTo(best, 0.5); else path_.clear();
    repath_ = 1.0;
}

bool BotSimulation::wet(double x, double z, double y) const {
    return !std::isfinite(geometry_.groundHeight(x, z, y + 0.6));
}

bool BotSimulation::nearWater(double radius) const {
    const auto& p = actor_.state.pos;
    for (int k = 0; k < 8; ++k) {
        const double a = k * pi / 4.0;
        if (wet(p.x + std::cos(a) * radius, p.z + std::sin(a) * radius, p.y)) return true;
    }
    return false;
}

bool BotSimulation::dryLine(glm::dvec3 a, glm::dvec3 b) const {
    const double distance = horizontal(b - a);
    const int samples = std::max(1, static_cast<int>(std::ceil(distance / 0.45)));
    for (int i = 1; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        if (wet(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.y)) return false;
    }
    return true;
}

bool BotSimulation::fatLos(glm::dvec3 a, glm::dvec3 b) const {
    const glm::dvec3 d = b - a;
    const double length = horizontal(d);
    if (length < 1e-6) return true;
    const glm::dvec3 side{-d.z / length * 0.34, 0, d.x / length * 0.34};
    for (int s : {0, 1, -1}) {
        const glm::dvec3 from = a + side * static_cast<double>(s) + glm::dvec3(0, 0.45, 0);
        const glm::dvec3 to = b + side * static_cast<double>(s) + glm::dvec3(0, 0.45, 0);
        const glm::dvec3 delta = to - from;
        const double rayLength = glm::length(delta);
        if (rayLength >= 1e-4 && geometry_.raycast(from, delta / rayLength, rayLength - 0.05, false).hit) return false;
    }
    return true;
}

double BotSimulation::pathRemaining() const {
    if (path_.empty()) return 0;
    const auto& n = nav_.nodes()[path_.back()];
    return std::hypot(n.x - actor_.state.pos.x, n.z - actor_.state.pos.z);
}

glm::dvec3 BotSimulation::steer(double dt) {
    glm::dvec3 out(0);
    if (path_.empty() || pathIndex_ >= static_cast<int>(path_.size())) return out;
    const auto& me = actor_.state;
    while (pathIndex_ < static_cast<int>(path_.size())) {
        const auto& n = nav_.nodes()[path_[pathIndex_]];
        const double dx = n.x - me.pos.x, dz = n.z - me.pos.z, dy = n.y - me.pos.y;
        if (dx * dx + dz * dz < 0.36 && dy < 0.9 && dy > -1.8) {
            ++pathIndex_; bestDistance_ = std::numeric_limits<double>::infinity(); noProgress_ = 0;
        } else break;
    }
    if (pathIndex_ >= static_cast<int>(path_.size())) return out;
    const auto& cur = nav_.nodes()[path_[pathIndex_]];
    const double distance = std::hypot(cur.x - me.pos.x, cur.z - me.pos.z);
    const auto edgeType = pathIndex_ > 0 ? nav_.edgeType(path_[pathIndex_ - 1], path_[pathIndex_]) : NavEdgeType::Walk;
    if (me.grounded && cur.y - me.pos.y > 0.9 && distance < 1.2 && edgeType != NavEdgeType::Jump && edgeType != NavEdgeType::Climb) {
        path_.clear(); repath_ = goalTimer_ = 0;
        if (time_ - ledgeReplanTime_ > 2.0) ledgeReplans_ = 0;
        ledgeReplanTime_ = time_;
        if (++ledgeReplans_ >= 4) { ledgeReplans_ = 0; wiggle(0.8); }
        return out;
    }
    if (cur.y - me.pos.y < -0.9 && distance < 0.8 && pathIndex_ > 0) {
        const auto& prev = nav_.nodes()[path_[pathIndex_ - 1]];
        return flatUnit({cur.x - prev.x, 0, cur.z - prev.z});
    }
    int lookahead = pathIndex_;
    lookaheadTimer_ -= dt;
    if (lookaheadTimer_ > 0 && lookaheadPathIndex_ == pathIndex_ && lookaheadIndex_ < static_cast<int>(path_.size()))
        lookahead = lookaheadIndex_;
    else {
        for (int k = pathIndex_ + 1; k < std::min(static_cast<int>(path_.size()), pathIndex_ + 7); ++k) {
            const auto& n = nav_.nodes()[path_[k]];
            if (std::abs(n.y - me.pos.y) > 0.4 || nav_.edgeType(path_[k - 1], path_[k]) != NavEdgeType::Walk) break;
            const glm::dvec3 p{n.x, n.y, n.z};
            if (!fatLos(me.pos, p) || !dryLine(me.pos, p)) break;
            lookahead = k;
        }
        lookaheadTimer_ = 0.1; lookaheadPathIndex_ = pathIndex_; lookaheadIndex_ = lookahead;
    }
    const auto& n = nav_.nodes()[path_[lookahead]];
    out = flatUnit({n.x - me.pos.x, 0, n.z - me.pos.z});
    if (edgeType == NavEdgeType::Jump && cur.y - me.pos.y > 0.4 && distance < 1.6) needJump_ = true;
    for (auto* other : world_.actors()) {
        if (!other || other == &actor_ || !other->state.alive) continue;
        const double dx = me.pos.x - other->state.pos.x, dz = me.pos.z - other->state.pos.z, d2 = dx * dx + dz * dz;
        if (d2 < 1.96 && d2 > 1e-4) {
            const double k = (1.4 - std::sqrt(d2)) * 0.7;
            const double side = dx * -out.z + dz * out.x >= 0 ? 1 : -1;
            const double ox = out.x, oz = out.z;
            out.x = ox - oz * side * k; out.z = oz + ox * side * k;
        }
    }
    const double length = horizontal(out);
    if (length > 1) out /= length;
    if (distance < bestDistance_ - 0.2) { bestDistance_ = distance; noProgress_ = 0; }
    else noProgress_ += dt;
    return out;
}

void BotSimulation::wiggle(double duration) {
    wiggleTimer_ = duration; wiggleYaw_ = random() * 2 * pi;
    path_.clear(); goalTimer_ = repath_ = 0;
    if (actor_.state.grounded) needJump_ = true;
}

void BotSimulation::unstick(double dt, glm::dvec3& move) {
    const auto& a = actor_.state;
    strikeTimer_ -= dt;
    if (strikeTimer_ <= 0) strikes_ = 0;
    const bool still = !a.grounded && std::abs(a.vel.y) < 0.6 && horizontal(a.vel) < 0.4;
    airStill_ = still ? airStill_ + dt : 0.0;
    if (airStill_ > 0.4 && wiggleTimer_ <= 0.0) {
        wiggle(0.6);
    }
    if (wiggleTimer_ > 0.0) {
        wiggleTimer_ -= dt;
        move = heading(wiggleYaw_);
        noProgress_ = 0.0;
        bestDistance_ = std::numeric_limits<double>::infinity();
    }
}

void BotSimulation::backOnNav(double dt, glm::dvec3& move) {
    const auto& a = actor_.state;
    if (!a.grounded || a.climbing) { navBackId_ = -1; return; }
    navBackTimer_ -= dt;
    if (navBackTimer_ > 0.0 && navBackId_ >= 0) {
        const auto& n = nav_.nodes()[navBackId_];
        move = flatUnit(glm::dvec3(n.x - a.pos.x, 0, n.z - a.pos.z));
        return;
    }
    navBackTimer_ = 0.6;
    if (nav_.nearest(a.pos, 1.2, true) >= 0) { navBackId_ = -1; return; }
    std::vector<std::pair<double, int>> candidates;
    candidates.reserve(12);
    for (int id : nav_.validIds()) {
        const auto& n = nav_.nodes()[id];
        const double d2 = (n.x - a.pos.x) * (n.x - a.pos.x) + (n.z - a.pos.z) * (n.z - a.pos.z);
        if (d2 < 100.0 && std::abs(n.y - a.pos.y) < 1.2 && !n.wet) candidates.emplace_back(d2, id);
    }
    std::stable_sort(candidates.begin(), candidates.end());
    navBackId_ = -1;
    for (std::size_t k = 0; k < std::min<std::size_t>(12, candidates.size()); ++k) {
        const auto& n = nav_.nodes()[candidates[k].second];
        const glm::dvec3 p{n.x, n.y, n.z};
        if (dryLine(a.pos, p) && fatLos(a.pos, p)) { navBackId_ = n.id; break; }
    }
    if (navBackId_ < 0 && !candidates.empty()) navBackId_ = candidates.front().second;
    if (navBackId_ >= 0) {
        const auto& n = nav_.nodes()[navBackId_];
        move = flatUnit(glm::dvec3(n.x - a.pos.x, 0, n.z - a.pos.z));
    }
}

bool BotSimulation::squidWouldDrop(glm::dvec3 move) const {
    const auto& p = actor_.state.pos;
    if (!std::isfinite(geometry_.groundHeight(p.x, p.z, 50.0, true))) return true;
    const double l = horizontal(move);
    return l > 0.05 && !std::isfinite(geometry_.groundHeight(p.x + move.x / l * 1.2,
                                                               p.z + move.z / l * 1.2, 50.0, true));
}

void BotSimulation::avoidWater(glm::dvec3& move) {
    const double length = horizontal(move);
    if (length < 0.05) return;
    const auto& p = actor_.state.pos;
    const double yaw = std::atan2(move.x, move.z);
    const auto safe = [&](double angle) {
        for (double d : {0.7, 1.3}) if (wet(p.x + std::sin(angle) * d, p.z + std::cos(angle) * d, p.y)) return false;
        return true;
    };
    if (safe(yaw)) return;
    for (double off : {0.5, -0.5, 1.0, -1.0, 1.6, -1.6, 2.2, -2.2}) {
        const double next = yaw + off;
        if (safe(next)) { move = heading(next) * length; return; }
    }
    move = glm::dvec3(0.0);
}

void BotSimulation::edgeGuard(glm::dvec3& move) {
    const auto& a = actor_.state;
    const double m = horizontal(move);
    if (m < 1e-4) return;
    const double dx = move.x / m, dz = move.z / m;
    const double look = 0.6 + horizontal(a.vel) * 0.17;
    const auto bad = [&](double x, double z) {
        return wet(a.pos.x + x * 0.45, a.pos.z + z * 0.45, a.pos.y) ||
               wet(a.pos.x + x * look, a.pos.z + z * look, a.pos.y);
    };
    if (!bad(dx, dz)) return;
    for (double ang : {0.8, -0.8, 1.45, -1.45}) {
        const double c = std::cos(ang), s = std::sin(ang);
        const double nx = dx * c + dz * s, nz = -dx * s + dz * c;
        if (!bad(nx, nz)) { move = {nx * m, 0, nz * m}; return; }
    }
    move = glm::dvec3(0.0);
}

void BotSimulation::climb(double dt, glm::dvec3& move, PlayerInput& input,
                          double& yaw, double& pitch, double& aimDistance) {
    climbAiming_ = false;
    if (pathIndex_ <= 0 || pathIndex_ >= static_cast<int>(path_.size())) { climbTimer_ = 0; return; }
    const auto* edge = nav_.edge(path_[pathIndex_ - 1], path_[pathIndex_]);
    if (!edge || edge->type != NavEdgeType::Climb) { climbTimer_ = 0; return; }
    const auto& a = actor_.state;
    const auto& top = nav_.nodes()[path_[pathIndex_]];
    if (a.grounded && a.pos.y > top.y - 0.4) { climbTimer_ = 0; return; }
    const double nx = edge->wallNormal.x, nz = edge->wallNormal.y;
    const double wx = edge->wallPoint.x, wz = edge->wallPoint.z;
    const double distance = (a.pos.x - wx) * nx + (a.pos.z - wz) * nz;
    if (!a.climbing && (distance > 2.4 || std::hypot(a.pos.x - wx, a.pos.z - wz) > 2.8)) return;
    climbTimer_ += dt;
    if (climbTimer_ > 7.0) {
        climbTimer_ = 0; noClimbUntil_ = time_ + 12; path_.clear(); repath_ = goalTimer_ = 0; return;
    }
    yaw = std::atan2(-nx, -nz);
    auto swim = [&]() { input.squid = true; input.fire = false; move = {-nx, 0, -nz}; pitch = 0.5; aimDistance = 2; climbAiming_ = true; };
    if (a.climbing) { swim(); return; }
    double gap = std::numeric_limits<double>::quiet_NaN();
    const glm::dvec3 direction{-nx, 0, -nz};
    for (double y = a.pos.y + 0.3; y < edge->topY - 0.1; y += 0.45) {
        const glm::dvec3 origin{wx + nx * 0.6, y, wz + nz * 0.6};
        const auto hit = geometry_.raycast(origin, direction, 1.2, true);
        if (!hit.hit) { gap = std::numeric_limits<double>::quiet_NaN(); break; }
        if (hit.face < 0 || paint_.sample(hit.face, hit.u, hit.v) != actor_.team() + 1) { gap = y; break; }
    }
    if (!std::isfinite(gap)) { swim(); return; }
    if (a.ink < 5.0) return;
    if (distance < 0.9) move = {nx * 0.6, 0, nz * 0.6};
    else if (distance > 1.6) move = {-nx * 0.6, 0, -nz * 0.6};
    else move = glm::dvec3(0.0);
    input.squid = false;
    const auto kind = actor_.weaponProfile().kind;
    const auto pose = actor_.combatPose();
    if (charge(kind)) input.fire = !pose.charging || pose.charge < 0.45;
    else input.fire = true;
    const double hd = std::max(0.5, distance), dy = gap + 0.3 - (a.pos.y + 1.1);
    pitch = std::atan2(dy, hd); aimDistance = std::hypot(hd, dy); climbAiming_ = true;
}

bool BotSimulation::fightSub(double distance) {
    const auto& sub = actor_.subWeapon();
    const auto kind = sub.kind;
    if (actor_.state.ink < sub.number("inkCost") + 8 || kind == "beacon") return false;
    const double roll = random() < 0.02 * (1 + difficulty_.fireDiscipline);
    bool go = false;
    if (kind == "burst") go = distance > 3 && distance < 12 && random() < 0.04;
    else if (kind == "scan") go = distance > 8 && distance < 18 && roll;
    else if (kind == "curtain") go = distance > 4 && distance < 14 && actor_.lastDamage() < 0.6 && random() < 0.05;
    else if (kind == "mine") go = distance < 10 && actor_.lastDamage() < 1.5 && random() < 0.05;
    else if (kind == "seeker") go = distance > 5 && distance < 15 && roll;
    else go = distance > 4 && distance < 14 && roll;
    if (go) bombCooldown_ = kind == "burst" ? 1.5 + random() * 2 : 5 + random() * 6;
    return go;
}

bool BotSimulation::paintSub() {
    const auto& a = actor_.state;
    const auto& sub = actor_.subWeapon();
    const bool placed = sub.flag("placed", false);
    if (a.ink < sub.number("inkCost") + (placed ? 4 : 20) || random() > (placed ? 0.04 : 0.01)) return false;
    const auto& pads = stage_.spawnPads;
    const double total = glm::length(pads[0] - pads[1]);
    const double progress = 1 - horizontal(a.pos - pads[1 - actor_.team()]) / std::max(total, 1.0);
    bool go = false;
    if (sub.kind == "sprinkler") {
        const glm::dvec3 p = a.pos + heading(aimYaw_) * 5.0;
        const auto st = regionStats(p.x, p.y, p.z, 3);
        go = st.count && st.own < 0.4;
    } else if (sub.kind == "mine") go = progress > 0.3 && progress < 0.7;
    else if (sub.kind == "beacon") go = progress > 0.4;
    if (go) bombCooldown_ = 8 + random() * 8;
    return go;
}

bool BotSimulation::wantSpecial(bool fighting, double distance, bool visible) {
    const auto configuredKind = actor_.specialWeapon().kind;
    const std::string_view id = configuredKind.empty() ? actor_.specialId() : configuredKind;
    const double r = random();
    if (fighting) {
        if (id == "slam") return distance < 4.5;
        if (id == "storm") return distance < 16;
        if (id == "barrage") return distance < 14;
        if (id == "bubbler") return distance < 12 && (actor_.lastDamage() < 0.6 || r < 0.02);
        if (id == "sonar") return r < 0.02;
        if (id == "strike") return distance > 14 && r < 0.01;
        if (id == "zooka") return visible && distance > 7 && distance < 36;
        if (id == "wail") return visible && distance > 5 && distance < 40;
        if (id == "kraken") return distance < 10;
        if (id == "blower") return distance < 14;
        if (id == "jetpack") return distance > 5 && distance < 22;
        if (id == "stamp") return distance < 9;
        if (id == "booyah") return distance > 9 && distance < 26 && r < 0.03;
        if (id == "zipcaster") return distance < 16;
        if (id == "crab") return distance < 20;
        return false;
    }
    if (id != "storm" && id != "strike" && id != "booyah" && id != "barrage" && id != "sonar" &&
        id != "kraken" && id != "crab" && id != "zooka" && id != "blower" && id != "stamp") return false;
    if (r > 0.012) return false;
    const auto stats = regionStats(actor_.state.pos.x, actor_.state.pos.y, actor_.state.pos.z, 6);
    return stats.own < 0.55;
}

void BotSimulation::specialControl(double dt, PlayerInput& input, glm::dvec3& move, double distance, bool visible) {
    const auto special = actor_.specialPose();
    if (!(special.active && special.kind == "booyah") && random() < dt * 1.3) {
        for (auto* other : world_.actors()) {
            if (!other || other == &actor_ || other->team() != actor_.team()) continue;
            const auto peerSpecial = other->specialPose();
            if (peerSpecial.active && peerSpecial.kind == "booyah" && !peerSpecial.thrown) {
                input.cheer = true;
                break;
            }
        }
    }
    if (target_) {
        const auto targetSpecial = target_->specialPose();
        if (targetSpecial.active && targetSpecial.kind == "stamp" && targetSpecial.guard > 0.0) {
            const auto& targetPos = target_->state.pos;
            const double dx = actor_.state.pos.x - targetPos.x;
            const double dz = actor_.state.pos.z - targetPos.z;
            const double distance = std::hypot(dx, dz);
            const double facing = targetSpecial.faceYaw.value_or(target_->state.yaw);
            const double front = (dx * std::sin(facing) + dz * std::cos(facing)) / (distance > 0.0 ? distance : 1.0);
            const double arc = target_->specialWeapon().number("deflectArc", 70.0) * pi / 180.0;
            if (front > std::cos(arc)) input.fire = input.sub = false;
        }
    }
    if (!special.active) {
        specialTime_ = barrageCooldown_ = 0.0;
        return;
    }

    const auto& a = actor_.state;
    const bool fighting = mode_ == BotMode::Fight && target_;
    const auto toward = [&]() {
        if (!target_ || distance > 5.0 || std::abs(target_->state.pos.y - a.pos.y) > 1.2) return;
        const double dx = target_->state.pos.x - a.pos.x, dz = target_->state.pos.z - a.pos.z;
        const double length = std::hypot(dx, dz);
        const double inverse = length > 1e-9 ? 1.0 / length : 1.0;
        if (geometry_.groundHeight(a.pos.x + dx * inverse * 1.5, a.pos.z + dz * inverse * 1.5, a.pos.y + 1.0) < a.pos.y - 1.5) return;
        move = {dx * inverse, 0.0, dz * inverse};
    };
    const auto configuredKind = actor_.specialWeapon().kind;
    const std::string_view id = configuredKind.empty() ? special.kind : configuredKind;
    if (id == "barrage") {
        barrageCooldown_ -= dt;
        if (barrageCooldown_ <= 0.0 && (!fighting || distance < 16.0)) {
            input.sub = true;
            bombAim_ = true;
            barrageCooldown_ = 0.35 + random() * 0.3;
        }
    } else if (id == "zooka") {
        input.fire = fighting ? visible && distance < 42.0 : random() < 0.06;
        input.squid = false;
    } else if (id == "kraken") {
        input.squid = false;
        if (fighting) {
            toward();
            input.fire = distance < 3.6 && visible;
            if (a.grounded && distance > 6.0 && random() < dt * 0.8) input.jump = true;
        }
    } else if (id == "stamp") {
        input.squid = false;
        specialTime_ -= dt;
        if (specialTime_ > 0.0) {
            if (!a.grounded) input.fire = true;
        } else if (fighting && target_) {
            const double bodyYaw = special.faceYaw.value_or(a.yaw);
            const double targetYaw = std::atan2(target_->state.pos.x - a.pos.x, target_->state.pos.z - a.pos.z);
            const double off = std::abs(angleDiff(bodyYaw, targetYaw));
            const bool near = std::abs(target_->state.pos.y - a.pos.y) < 1.8;
            if (a.grounded && near && distance < 2.6 && off > 2.3) {
                input.jump = true;
                specialTime_ = 0.4;
            } else if (a.grounded && near && off < 0.3 && distance > 3.3 && distance < 4.3) {
                input.jump = true;
                specialTime_ = 0.4;
            } else if (off > 0.9 && distance < 9.0) move = glm::dvec3(0.0);
            else {
                if (near && distance < 12.0) toward();
                input.fire = off < 0.7 && distance < actor_.specialWeapon().number("reach") +
                    actor_.specialWeapon().number("radius") + 0.5;
            }
            if (visible && distance > 9.0 && distance < 20.0 && random() < dt * 0.8) input.sub = true;
        } else input.fire = random() < 0.5;
    } else if (id == "crab") {
        input.fire = fighting ? visible && distance < 24.0 : random() < 0.3;
        input.sub = fighting && visible && distance > 8.0 && distance < 22.0 && random() < dt * 1.5;
        input.squid = (fighting && special.hitPoints < actor_.specialWeapon().number("hp") * 0.3 && distance > 8.0) ||
            (!fighting && pathRemaining() > 12.0);
    } else if (id == "blower") {
        specialTime_ += dt;
        input.fire = specialTime_ < 0.9;
        if (specialTime_ > 1.4) specialTime_ = 0.0;
        input.squid = false;
    } else if (id == "booyah") {
        input.squid = false;
        input.fire = special.charge >= 1.0 && random() < dt * 3.0;
    } else if (id == "jetpack") {
        input.fire = fighting && visible && distance < 30.0;
    } else if (id == "zipcaster") {
        if (fighting && visible && distance > 6.0 && distance < 18.0 && random() < dt * 0.6) input.sub = true;
    }
}

bool BotSimulation::deviceThreat(const CombatThreat& threat) const {
    return threat.kind == "waddle" || threat.kind == "torpedo" || threat.kind == "canopy";
}

double BotSimulation::threatScore(const CombatThreat& d) const {
    const auto& a = actor_.state;
    const double dx = d.pos.x - a.pos.x, dz = d.pos.z - a.pos.z, dh = std::hypot(dx, dz);
    const double dy = d.pos.y + d.aimY - (a.pos.y + 0.8);
    if (dh > 18 || std::abs(dy) > 6) return 0;
    const bool mine = d.target == &actor_;
    if (d.state == "fly") {
        if (d.ground) {
            const double disc = std::max(0.0, d.vel.y * d.vel.y + 48 * (d.pos.y - a.pos.y));
            const double t = (d.vel.y + std::sqrt(disc)) / 24;
            return std::hypot(d.pos.x + d.vel.x * t - a.pos.x, d.pos.z + d.vel.z * t - a.pos.z) < (d.senseRadius ? d.senseRadius : 7) + 1 ? 0.6 : 0;
        }
        return dh < (d.lockRange ? d.lockRange : 6.5) + 3 && dx * d.vel.x + dz * d.vel.z < 0 ? 0.5 : 0;
    }
    if (!d.ground) {
        if (d.state == "unfold") return mine ? 3 : dh < d.radius ? 0.8 : 0;
        if (d.state != "launch") return 0;
        const double speed = std::max(2.0, glm::length(d.vel)), d3 = std::hypot(dh, dy);
        if (mine) return 4 / (0.25 + d3 / std::max(6.0, speed));
        const double tca = -(dx * d.vel.x + dy * d.vel.y + dz * d.vel.z) / (speed * speed);
        if (tca > 0 && tca < 1.2) {
            const double mx = -dx - d.vel.x * tca, my = -dy - d.vel.y * tca, mz = -dz - d.vel.z * tca;
            if (mx * mx + my * my + mz * mz < 1.6) return 2;
        }
        return d3 < d.radius ? 0.8 : 0;
    }
    if (d.state == "sense") return dh < (d.senseRadius ? d.senseRadius : 7) + 0.6 ? 0.7 : 0;
    if (!d.locked) return 0;
    const double eta = std::max(0.0, dh - (d.trigger ? d.trigger : 1.2)) / (d.speed ? d.speed : 4);
    if (mine) return (4 / (0.5 + eta)) * (d.left > 0 && d.left < eta * 0.8 ? 0.3 : 1);
    if (dh < d.radius + 0.8) return 1.7;
    const double vl = horizontal(d.vel);
    if (dh < 9 && vl > 0.5 && -(dx * d.vel.x + dz * d.vel.z) / (dh * vl) > 0.8) return 1.2 / (0.5 + eta);
    return 0;
}

void BotSimulation::deviceAim(double dt, const CombatThreat& d, glm::dvec3& move,
                              PlayerInput& input, double& yaw, double& pitch, double& aimDistance) {
    const auto& a = actor_.state;
    const auto weapon = actor_.weaponProfile();
    const double ex = a.pos.x, ey = a.pos.y + 1.1, ez = a.pos.z;
    double dx = d.pos.x - ex, dy = d.pos.y + d.aimY - ey, dz = d.pos.z - ez;
    const double d3 = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double lead = weapon.kind == "charger" ? 0 : std::min(0.5, d3 / std::max(2.0, weapon.projectileSpeed));
    dx += d.vel.x * lead; dy += d.vel.y * lead; dz += d.vel.z * lead;
    const double idealYaw = std::atan2(dx, dz);
    const double idealPitch = weapon.kind == "roller" && d.ground ? -0.3 : std::atan2(dy, std::hypot(dx, dz));
    const double acq = std::exp(-threatAcquisition_ / std::max(0.12, difficulty_.reaction * 0.9));
    const double error = difficulty_.aimError * 0.7;
    yaw = idealYaw + error * (0.7 * wander(time_ * 1.7 + phase1_) + 2.2 * acq * threatYawSign_);
    pitch = idealPitch + error * 0.6 * (0.7 * wander(time_ * 2.1 + phase2_) + 1.5 * acq * threatPitchSign_);
    aimDistance = std::max(1.0, d3);
    const auto origin = glm::dvec3(ex, ey + 0.1, ez), target = d.pos + glm::dvec3(0, d.aimY + 0.05, 0);
    const auto delta = target - origin;
    const double distance = glm::length(delta);
    const bool los = distance < 1e-6 || !geometry_.raycast(origin, delta / distance, distance, false).hit;
    const auto& data = actor_.weapon();
    double lo = 0.0, hi = weapon.range * (charge(weapon.kind) ? 0.9 : 1.0);
    if (weapon.kind == "roller") { lo = d.ground && d.state == "walk" ? 7.3 : d.ground ? 5.0 : 1.5; hi = d.ground && d.state == "walk" ? 9.0 : d.ground ? 6.6 : 6.0; }
    else if (weapon.kind == "brush") { lo = d.ground ? 0.8 : 0.8; hi = d.ground && d.state == "walk" ? 99.0 : d.ground ? 2.8 : 4.5; }
    else if (weapon.kind == "blade") hi = d.ground ? 4.0 : 5.5;
    const double inkNeed = std::max({2.5, data.number("inkPerShot", 0.0), data.number("inkFull", 0.0) * 0.25});
    const bool canShoot = d.shootable && los && d3 >= lo && d3 <= hi && a.ink > inkNeed;
    const bool mine = d.target == &actor_;
    const double dh = horizontal(d.pos - a.pos);
    const double eta = d.ground ? std::max(0.0, dh - (d.trigger ? d.trigger : 1.2)) / (d.speed ? d.speed : 4) :
        (!d.ground && d.state == "launch" ? d3 / std::max(2.0, glm::length(d.vel)) : 9.0);
    const bool waitIn = !canShoot && !d.ground ? false :
        !canShoot && d.ground && d.shootable && los && a.ink > inkNeed && d3 > hi && d3 < hi + 4 && hi - (d.trigger ? d.trigger : 1.2) > 2.5;
    const bool urgent = d.ground ? eta < 2.2 || dh < d.radius + 0.5 : d.state == "launch" || d.state == "unfold";
    if (!canShoot && !waitIn && !urgent) {
        const auto away = flatUnit(a.pos - d.pos);
        const auto drift = move + away * 0.5;
        move = horizontal(drift) > 1.0 ? flatUnit(drift) : drift;
        threatAiming_ = false;
        threatFiring_ = false;
        return;
    }
    const bool shooting = canShoot || waitIn;
    if (shooting) {
        input.squid = false; input.fire = true;
        if (waitIn || (d.ground && weapon.kind == "roller")) move = glm::dvec3(0.0);
        else if (d.ground && melee(weapon.kind)) move = flatUnit(a.pos - d.pos) * 0.35;
        else if (d.ground && dh < 4.5) move = flatUnit(a.pos - d.pos);
        else if (d.ground) move *= 0.25;
        else if (urgent) {
            const double side = (d.vel.x * (a.pos.z - d.pos.z) - d.vel.z * (a.pos.x - d.pos.x)) >= 0 ? 1 : -1;
            move = flatUnit({-d.vel.z * side, 0, d.vel.x * side});
            if (eta < (charge(weapon.kind) ? 0.8 : 0.45)) {
                if (charge(weapon.kind) && actor_.combatPose().charging) input.fire = false;
                if (eta < 0.3 && a.grounded) input.jump = true;
            }
        } else move *= 0.25;
    } else {
        input.fire = false;
        const glm::dvec3 away = flatUnit(a.pos - d.pos);
        if (!d.ground && d.state == "launch") {
            const double side = (a.pos.x - d.pos.x) * d.vel.z - (a.pos.z - d.pos.z) * d.vel.x >= 0 ? 1 : -1;
            move = flatUnit({-d.vel.z * side, 0, d.vel.x * side});
            if (d.timeToImpact < 0.3 && a.grounded) input.jump = true;
        } else {
            if ((threatEvadeTimer_ -= dt) <= 0) {
                threatEvadeTimer_ = 0.45 + random() * 0.3;
                threatEvadeYaw_ = pickEvade(d);
            }
            move = heading(threatEvadeYaw_);
            input.squid = a.groundTeam == 1 && !squidWouldDrop(move);
            (void)mine;
        }
    }
    const double off = std::hypot(angleDiff(aimYaw_, idealYaw), aimPitch_ - idealPitch);
    const double tol = std::max(0.035, std::atan2(d.ground ? 0.24 : 0.28, d3)) * (threatFiring_ ? 2.2 : 1.3);
    input.fire = shooting ? (d3 >= lo && d3 <= hi && off < tol) :
        (charge(weapon.kind) && actor_.combatPose().charging && !input.squid);
    threatFiring_ = input.fire && off < tol;
    if (input.fire) input.squid = false;
    input.sub = false;
    threatAiming_ = true;
}

void BotSimulation::threatControl(double dt, glm::dvec3& move, double& yaw, double& pitch, double& aimDistance) {
    threatAiming_ = false;
    threatFiring_ = false;
    const auto threats = world_.threats();
    const CombatThreat* candidate = nullptr;
    if ((threatScanTimer_ -= dt) <= 0) {
        threatScanTimer_ = 0.2 + random() * 0.1;
        shieldSnapshot_ = world_.shields();
        shieldSnapshot_.erase(std::remove_if(shieldSnapshot_.begin(), shieldSnapshot_.end(),
            [&](const CombatShield& shield) { return shield.team == actor_.team(); }), shieldSnapshot_.end());
        double bestScore = 0.2;
        for (const auto& d : threats) {
            if (!deviceThreat(d) || d.team == actor_.team() || !d.live) continue;
            const double score = threatScore(d);
            if (score > bestScore) { bestScore = score; candidate = &d; }
        }
        for (auto it = threatMemory_.begin(); it != threatMemory_.end();) {
            const bool live = std::any_of(threats.begin(), threats.end(), [&](const auto& d) { return d.id == it->first && d.live; });
            if (!live) it = threatMemory_.erase(it); else ++it;
        }
        if (candidate && threatMemory_.find(candidate->id) == threatMemory_.end())
            threatMemory_[candidate->id] = elapsed_ + difficulty_.reaction * (0.8 + random() * 0.7) + 0.05;
        threatCandidateId_ = candidate ? candidate->id : 0;
    }

    const CombatThreat* active = nullptr;
    for (const auto& d : threats) if (d.id == threatId_ && d.live) { active = &d; break; }
    candidate = nullptr;
    for (const auto& d : threats) if (d.id == threatCandidateId_ && d.live) { candidate = &d; break; }
    const auto notice = candidate ? threatMemory_.find(candidate->id) : threatMemory_.end();
    if (candidate && notice != threatMemory_.end() && elapsed_ >= notice->second &&
        (!active || threatScore(*candidate) > threatScore(*active) * 1.3)) {
        active = candidate;
        threatId_ = candidate->id;
        threatActs_ = 0;
        threatEvadeTimer_ = 0;
        threatAcquisition_ = 0;
        const double sign = random() < 0.5 ? -1 : 1;
        threatYawSign_ = sign * (0.5 + random() * 0.5);
        threatPitchSign_ = (random() - 0.5) * 1.2;
    }
    if (!active) { threatId_ = 0; return; }
    const double score = threatScore(*active);
    if (score < 0.15) { threatId_ = 0; threatFiring_ = false; return; }
    if (target_ && mode_ == BotMode::Fight && seeTimer_ > 0) {
        const double targetDistance = horizontal(target_->state.pos - actor_.state.pos);
        const double priority = targetDistance < 4 ? 2.4 :
            targetDistance < actor_.weaponProfile().range * 0.9 || targetDistance < target_->weaponProfile().range * 0.9 ? 1.5 : 0.5;
        const double dh = horizontal(active->pos - actor_.state.pos);
        if (score < priority) {
            if (active->target == &actor_ || dh < 4.0) {
                const auto away = flatUnit(actor_.state.pos - active->pos);
                const auto drift = move + away * 0.6;
                move = horizontal(drift) > 1.0 ? flatUnit(drift) : drift;
            }
            return;
        }
    }
    threatAcquisition_ += dt;
    const double eta = active->ground ? std::max(0.0, horizontal(active->pos - actor_.state.pos) - (active->trigger ? active->trigger : 1.2)) /
        (active->speed ? active->speed : 4.0) : active->timeToImpact;
    const auto pose = actor_.combatPose();
    const bool bodySpecial = pose.special && (pose.specialKind == "kraken" || pose.specialKind == "stamp" ||
        pose.specialKind == "crab" || pose.specialKind == "jetpack" || pose.specialKind == "zipcaster");
    if (bodySpecial || actor_.state.climbing || climbAiming_) return;
    const bool urgent = active->ground ? eta < 2.2 || horizontal(active->pos - actor_.state.pos) < active->radius + 0.5 :
        active->state == "launch" || active->state == "unfold";
    if (active->state != "fly" || !active->ground) ++threatActs_;
    if (!urgent && !active->shootable) {
        const auto away = flatUnit(actor_.state.pos - active->pos);
        const auto drift = move + away * 0.5;
        move = horizontal(drift) > 1.0 ? flatUnit(drift) : drift;
        return;
    }
    deviceAim(dt, *active, move, actor_.input, yaw, pitch, aimDistance);
}

double BotSimulation::pickEvade(const CombatThreat& d) {
    const auto& a = actor_.state;
    const double base = std::atan2(a.pos.x - d.pos.x, a.pos.z - d.pos.z);
    const NavNode* waypoint = !path_.empty() && pathIndex_ < static_cast<int>(path_.size()) ? &nav_.nodes()[path_[pathIndex_]] : nullptr;
    double best = base, bestScore = -std::numeric_limits<double>::infinity();
    for (double offset : {0.0, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.1, -2.1}) {
        const double yaw = base + offset;
        const double sx = std::sin(yaw), sz = std::cos(yaw);
        const glm::dvec3 end(a.pos.x + sx * 3.2, a.pos.y, a.pos.z + sz * 3.2);
        if (nav_.nearest(end, 1.0) < 0 || !dryLine(a.pos, end) || !fatLos(a.pos, end)) continue;
        double score = std::cos(offset) * (d.ground ? 2.0 : 1.0) + (d.ground ? 0.0 : std::abs(std::sin(offset)) * 1.2);
        const auto stats = regionStats(end.x, end.y, end.z, 1.3);
        if (stats.count) score += stats.own * 1.5;
        if (waypoint) {
            const double wx = waypoint->x - a.pos.x, wz = waypoint->z - a.pos.z, wl = std::hypot(wx, wz);
            score += 0.4 * (wx * sx + wz * sz) / std::max(wl, 1e-6);
        }
        if (!d.ground && !geometry_.los(d.pos, end + glm::dvec3(0, 1.0, 0))) score += 2.0;
        score += random() * 0.3;
        if (score > bestScore) { bestScore = score; best = yaw; }
    }
    return best;
}

void BotSimulation::fight(double dt, glm::dvec3& move, double& wantYaw, double& wantPitch, double& aimDistance) {
    if (!target_) return;
    auto& a = actor_.state;
    const auto& t = target_->state;
    const auto weapon = actor_.weaponProfile();
    const double dx = t.pos.x - a.pos.x, dz = t.pos.z - a.pos.z, dist = std::hypot(dx, dz);
    const double range = weapon.range;
    const double lead = weapon.kind == "charger" ? 0 : dist / std::max(2.0, weapon.projectileSpeed);
    const glm::dvec3 aim{t.pos.x + t.vel.x * lead, t.pos.y + (t.squid ? 0.3 : 0.85), t.pos.z + t.vel.z * lead};
    const glm::dvec3 rel = aim - (a.pos + glm::dvec3(0, 1.1, 0));
    const double idealYaw = std::atan2(rel.x, rel.z), idealPitch = std::atan2(rel.y, horizontal(rel));
    aimDistance = glm::length(rel);
    const double acq = std::exp(-acquisitionTime_ / std::max(0.12, difficulty_.reaction * 0.9));
    wantYaw = idealYaw + difficulty_.aimError * (0.75 * wander(time_ * 1.7 + phase1_) + 2.4 * acq * acquisitionSignYaw_);
    wantPitch = idealPitch + difficulty_.aimError * 0.6 * (0.75 * wander(time_ * 2.1 + phase2_) + 1.6 * acq * acquisitionSignPitch_);

    const double pref = weapon.kind == "charger" ? range * 0.8 : melee(weapon.kind) ? 0.5 : range * 0.7;
    if (strafeTimer_ <= 0) {
        strafeTimer_ = 0.6 + random() * 1.2;
        strafe_ = random() < 0.5 ? -1 : 1;
        strafeAmplitude_ = 0.5 + random() * 0.5;
    }
    strafeSmooth_ += (strafe_ * strafeAmplitude_ - strafeSmooth_) * (1 - std::exp(-5 * dt));
    const double nx = dx / std::max(dist, 0.01), nz = dz / std::max(dist, 0.01);
    double mx = 0, mz = 0;
    if (dist > pref + 1.2 && horizontal(move) > 0.1) { mx = move.x; mz = move.z; }
    else if (dist < pref - 1.5 && !melee(weapon.kind)) { mx = -nx; mz = -nz; }
    const auto pose = actor_.combatPose();
    if (!(charge(weapon.kind) && pose.charging)) { mx += -nz * strafeSmooth_ * 0.9; mz += nx * strafeSmooth_ * 0.9; }
    if (melee(weapon.kind) && dist < 7) { mx = nx; mz = nz; }
    const double ml = std::hypot(mx, mz);
    move = ml > 0.01 ? glm::dvec3(mx / ml, 0, mz / ml) : glm::dvec3(0.0);

    const double off = std::hypot(angleDiff(aimYaw_, idealYaw), aimPitch_ - idealPitch);
    const double tol = std::max(0.05, std::atan2(0.55, dist)) * (firing_ ? 2.4 : 1.5);
    const bool aimed = off < tol;
    firing_ = false;
    if (seeTimer_ > 0 && react_ <= 0 && aimed && a.ink / 100.0 > 0.02) {
        const double d = actor_.lastDamage();
        if (weapon.kind == "spinner") actor_.input.fire = pose.burstTime <= 0 && !(pose.charging && pose.charge >= chargeRelease_);
        else if (weapon.kind == "splatling") actor_.input.fire = !pose.streaming && dist < range * 1.1 && !(pose.charging && pose.charge >= (dist > range * 0.55 ? chargeRelease_ : 0.55 + 0.25 * chargeRelease_));
        else if (weapon.kind == "bucket") actor_.input.fire = dist < range * 1.05;
        else if (weapon.kind == "brush") actor_.input.fire = dist < 4.5;
        else if (weapon.kind == "charger") actor_.input.fire = !(pose.charging && pose.charge >= chargeRelease_);
        else if (weapon.kind == "roller") actor_.input.fire = dist < 5.5 || (pose.rolling && dist < 8);
        else actor_.input.fire = dist < range * 1.08;
        firing_ = actor_.input.fire;
        if (bombCooldown_ <= 0 && fightSub(dist)) { actor_.input.sub = true; bombAim_ = true; }
        if (weapon.kind == "splatling" && pose.charging) move *= 0.45;
        if (weapon.kind == "charger" && pose.charging) move *= 0.3;
    } else if (charge(weapon.kind) && pose.charging && seeTimer_ <= 0) actor_.input.fire = true;
    if (!actor_.input.fire && !pose.charging && dist > range * 1.15 && a.groundTeam == 1) actor_.input.squid = true;
    if (actor_.lastDamage() < 0.25 && dodgeCooldown_ <= 0 && a.grounded && !charge(weapon.kind) && random() < (weapon.kind == "twins" ? 0.6 : 0.3)) {
        actor_.input.jump = true;
        dodgeCooldown_ = weapon.kind == "twins" ? 0.9 + random() : 2 + random() * 2.5;
    }
    if (seeTimer_ > 0) shieldFight(dt, move, dist);
}

void BotSimulation::shieldFight(double dt, glm::dvec3& move, double distance) {
    if (shieldSnapshot_.empty()) { shieldBlockedTime_ = 0; return; }
    const auto& a = actor_.state;
    const auto& t = target_->state;
    const glm::dvec3 eye = a.pos + glm::dvec3(0, 1.1, 0);
    const glm::dvec3 aim = t.pos + glm::dvec3(0, t.smoothY + (t.squid ? 0.3 : 0.85), 0);
    const CombatShield* blocked = nullptr;
    for (const auto& shield : shieldSnapshot_) {
        if (!shield.live) continue;
        const auto& c = shield.C;
        const auto& n = shield.N;
        const double p0 = glm::dot(eye - c, n), p1 = glm::dot(aim - c, n);
        if ((p0 > 0) == (p1 > 0) || p0 == p1) continue;
        const double k = p0 / (p0 - p1);
        const auto q = eye + (aim - eye) * k - c;
        if (glm::dot(q, q) > (shield.R + 0.12) * (shield.R + 0.12)) continue;
        blocked = &shield;
        break;
    }
    if (!blocked) {
        if (shieldBlockedTime_ > 0) {
            shieldBlockedTime_ = std::max(0.0, shieldBlockedTime_ - dt * 3);
            if (shieldBlockedTime_ == 0) flankSide_ = 0;
        }
        return;
    }
    if (shieldId_ != blocked->id) { shieldId_ = blocked->id; flankSide_ = 0; flankTimer_ = 0; }
    shieldBlockedTime_ += dt;

    const auto& n = blocked->N;
    const double sx = -n.z, sz = n.x;
    const double hl = std::max(0.3, horizontal(aim - eye));
    const double nx = (aim.x - eye.x) / hl, nz = (aim.z - eye.z) / hl;
    if (!flankSide_) {
        flankSide_ = ((eye.x - blocked->C.x) * sx + (eye.z - blocked->C.z) * sz >= 0) ? 1 : -1;
        flankTimer_ = 0;
    }
    double lx = -nz, lz = nx;
    if ((lx * sx + lz * sz) * flankSide_ < 0) { lx = -lx; lz = -lz; }
    if ((flankTimer_ -= dt) <= 0) {
        flankTimer_ = 0.5;
        const glm::dvec3 sideEnd(a.pos.x + lx * 1.8, a.pos.y, a.pos.z + lz * 1.8);
        if (!dryLine(a.pos, sideEnd) || !fatLos(a.pos, sideEnd)) {
            flankSide_ = -flankSide_;
            lx = -lx; lz = -lz;
        }
    }
    const double inward = distance > 6 ? 0.45 : distance < 2.5 ? -0.35 : 0.1;
    const double mx = lx + nx * inward, mz = lz + nz * inward, ml = std::hypot(mx, mz);
    move = ml > 1e-9 ? glm::dvec3(mx / ml, 0, mz / ml) : glm::dvec3(0.0);

    const auto& weapon = actor_.weapon();
    const auto kind = actor_.weaponProfile().kind;
    const double dps = weapon.number("damage", 0) && weapon.number("fireInterval", 0) && !charge(kind)
        ? weapon.number("damage") / weapon.number("fireInterval") : 120.0;
    const bool through = blocked->hp <= dps * 1.3 || shieldBlockedTime_ > 2.5 ||
        (!blocked->held && blocked->left < 0.5) || (canopyId_ == blocked->id && canopySide_ == 2);
    const auto pose = actor_.combatPose();
    if (!through && shieldBlockedTime_ > 0.15) {
        if (charge(kind)) actor_.input.fire = actor_.input.fire || pose.charging;
        else if (actor_.input.fire) actor_.input.fire = false;
    }

    const auto& sub = actor_.subWeapon();
    const auto subId = actor_.subId();
    const bool thrown = subId == "bomb" || subId == "sticky" || subId == "burst" || subId == "shaker" ||
        subId == "seeker" || subId == "waddle" || subId == "torpedo" || subId == "mist" || subId == "boomerang";
    if (shieldBlockedTime_ > 0.5 && bombCooldown_ <= 0 && thrown && world_.canDeploy(actor_, actor_.subId()) &&
        a.ink >= sub.number("inkCost", 0) + 8 && distance > 3 && distance < 13 && random() < dt * 2.5) {
        actor_.input.sub = true;
        bombAim_ = true;
        bombCooldown_ = 5 + random() * 4;
    }
}

bool BotSimulation::canopyControl(double dt, glm::dvec3& move, bool free,
                                  double& yaw, double& pitch, double& aimDistance) {
    const auto& a = actor_.state;
    const auto pose = actor_.combatPose();
    if (shieldSnapshot_.empty() || a.climbing || climbAiming_ || pose.special) {
        canopyId_ = 0;
        canopyBreaking_ = false;
        return false;
    }
    double moveLength = horizontal(move);
    const CombatShield* hit = nullptr;
    double along = 0, side = 1, width = 1;
    for (const auto& shield : shieldSnapshot_) {
        if (!shield.blocksActors || !shield.live) continue;
        const auto& n = shield.N;
        const double tx = -n.z, tz = n.x;
        const double px = a.pos.x - shield.pos.x, pz = a.pos.z - shield.pos.z, dy = a.pos.y - shield.pos.y;
        if (dy < -1.2 || dy > 1.6) continue;
        const double thisAlong = px * tx + pz * tz;
        const double across = px * n.x + pz * n.z - shield.planeOff;
        const double thisWidth = shield.halfW + 0.6 * 0.38 + 0.3;
        if (std::abs(across) > 3 || std::abs(thisAlong) > thisWidth + 2.5) continue;
        const double thisSide = across >= 0 ? 1 : -1;
        const bool coming = thisSide > 0 && across < 2.2 && std::abs(thisAlong) < thisWidth && shield.speed > 0.5;
        if (!coming) {
            if (moveLength <= 0.05) continue;
            const double mx = move.x / moveLength, mz = move.z / moveLength;
            const double mN = mx * n.x + mz * n.z, mT = mx * tx + mz * tz;
            if (mN * thisSide > -0.2) continue;
            const double tCross = -across / mN;
            if (tCross > 2.5 || std::abs(thisAlong + mT * tCross) > thisWidth) continue;
        }
        hit = &shield; along = thisAlong; side = thisSide; width = thisWidth; break;
    }
    if (hit && moveLength <= 0.05) moveLength = 1;
    if (!hit) { canopyId_ = 0; canopyBreaking_ = false; return false; }
    if (canopyId_ != hit->id) { canopyId_ = hit->id; canopySide_ = 0; canopyTimer_ = 0; canopyBreaking_ = false; }
    canopyTimer_ += dt;
    const auto& n = hit->N;
    const double tx = -n.z, tz = n.x;
    const auto edge = [&](double sg, double off) {
        return glm::dvec3(hit->pos.x + n.x * (hit->planeOff + side * off) + tx * sg * (width + 0.55),
                          a.pos.y,
                          hit->pos.z + n.z * (hit->planeOff + side * off) + tz * sg * (width + 0.55));
    };
    if (!canopySide_) {
        const auto canReach = [&](double sg) {
            const auto p1 = edge(sg, 0.9), p2 = edge(sg, -1.4);
            return dryLine(a.pos, p1) && fatLos(a.pos, p1) && dryLine(p1, p2) && fatLos(p1, p2);
        };
        const int preferred = along >= 0 ? 1 : -1;
        canopySide_ = canReach(preferred) ? preferred : canReach(-preferred) ? -preferred : 2;
    }
    if (canopySide_ != 2 && canopyTimer_ > 4) canopySide_ = 2;
    if (canopySide_ == 2) {
        if (!free) return false;
        canopyBreaking_ = true;
        const auto& c = hit->C;
        const double dx = c.x - a.pos.x, dy = c.y - (a.pos.y + 1.1), dz = c.z - a.pos.z;
        const double dh = std::hypot(dx, dz);
        yaw = std::atan2(dx, dz); pitch = std::atan2(dy, dh); aimDistance = std::max(1.0, std::hypot(dh, dy));
        const double aimOff = std::hypot(angleDiff(aimYaw_, yaw), aimPitch_ - pitch);
        const auto kind = actor_.weaponProfile().kind;
        const bool isMelee = melee(kind);
        const double hi = isMelee ? 3.5 : actor_.weaponProfile().range;
        actor_.input.fire = a.ink > 3 && aimDistance <= hi && aimOff < 0.3;
        if (actor_.input.fire) actor_.input.squid = false;
        move = glm::dvec3(n.x * side * 0.3, 0, n.z * side * 0.3);
        noProgress_ = 0; bestDistance_ = std::numeric_limits<double>::infinity();
        threatAiming_ = true;
        return true;
    }
    canopyBreaking_ = false;
    const double offset = std::abs(along) < width + 0.1 ? 0.9 : -1.4;
    const auto goal = edge(canopySide_, offset);
    const double gx = goal.x - a.pos.x, gz = goal.z - a.pos.z, gl = std::hypot(gx, gz);
    if (gl > 0.05) move = glm::dvec3(gx / gl * moveLength, 0, gz / gl * moveLength);
    noProgress_ = 0; bestDistance_ = std::numeric_limits<double>::infinity();
    return false;
}

void BotSimulation::paint(double dt, glm::dvec3& move, double& wantYaw, double& wantPitch) {
    const auto weapon = actor_.weaponProfile();
    sweep_ += dt * (weapon.kind == "charger" ? 0.8 : 2.1);
    paintScan_ -= dt;
    const bool wantMove = horizontal(move) > 0.1;
    if (paintScan_ <= 0 && !melee(weapon.kind)) {
        paintScan_ = 0.35 + random() * 0.15;
        const double reach = weapon.kind == "charger" || weapon.kind == "spinner" || weapon.kind == "splatling" ? 8.5 :
            weapon.kind == "bucket" ? 7.0 : weapon.kind == "blaster" ? 5.0 : 4.5;
        double bestOffset = 0, best = -1;
        for (double off : {0.0, -0.6, 0.6, -1.2, 1.2}) {
            const double yw = (wantMove ? std::atan2(move.x, move.z) : aimYaw_) + off;
            const auto p = actor_.state.pos + heading(yw) * reach;
            const auto stats = regionStats(p.x, p.y, p.z, 2.2);
            const double value = stats.count ? stats.empty + stats.enemy * 1.4 - std::abs(off) * 0.12 : -1;
            if (value > best) { best = value; bestOffset = off; }
        }
        paintYawOffset_ = bestOffset;
    }
    wantYaw += (melee(weapon.kind) ? 0 : paintYawOffset_) + std::sin(sweep_) * (melee(weapon.kind) ? 0 : 0.35);
    if (weapon.kind == "charger") wantPitch = -0.12;
    else if (weapon.kind == "spinner" || weapon.kind == "bucket") wantPitch = -0.16;
    else if (weapon.kind == "blaster") wantPitch = -0.28;
    else if (weapon.kind == "splatling") wantPitch = -0.3;
    else wantPitch = -0.42;
    const bool zoneAim = zonePlan_ && zonePlan_->paintAim(*this, dt, move, wantMove, wantYaw, wantPitch);
    const glm::dvec3 ahead = actor_.state.pos + heading(wantYaw) * 4.0;
    const auto stats = regionStats(ahead.x, ahead.y, ahead.z, 3.0);
    const bool needPaint = zoneAim || !stats.count || stats.own < 0.75;
    const auto pose = actor_.combatPose();
    if (melee(weapon.kind)) actor_.input.fire = actor_.state.ink > 8 && (needPaint || random() < 0.02) && wantMove;
    else if (weapon.kind == "spinner" || weapon.kind == "splatling") {
        if ((weapon.kind == "spinner" && pose.burstTime > 0) || (weapon.kind == "splatling" && pose.streaming)) actor_.input.fire = false;
        else if (pose.charging) {
            actor_.input.fire = pose.charge < 0.6;
            if (!actor_.input.fire) paintPause_ = 0.25 + random() * 0.3;
        } else actor_.input.fire = needPaint && actor_.state.ink > 25 && paintPause_ <= 0;
    } else if (weapon.kind == "charger") {
        if (pose.charging) { actor_.input.fire = pose.charge < 0.7; if (!actor_.input.fire) paintPause_ = 0.3 + random() * 0.35; }
        else actor_.input.fire = needPaint && actor_.state.ink > 30 && paintPause_ <= 0;
    } else actor_.input.fire = needPaint && actor_.state.ink > 18;
    if (!actor_.input.fire && pathRemaining() > 5 && actor_.state.groundTeam == 1) actor_.input.squid = true;
    if (bombCooldown_ <= 0 && paintSub()) { actor_.input.sub = true; bombAim_ = true; }
    if (zonePlan_) zonePlan_->bombAim(*this, dt, wantYaw, wantPitch);
}

void BotSimulation::finish(double dt, glm::dvec3 move, double wantYaw, double wantPitch, double aimDistance, bool fighting) {
    auto& input = actor_.input;
    const auto& a = actor_.state;
    if (bombAim_) { input.sub = true; input.squid = false; bombAim_ = false; releaseBomb_ = true; }
    else if (releaseBomb_) { input.sub = false; input.squid = false; releaseBomb_ = false; }
    const bool snappy = fighting || threatAiming_;
    const double omega = snappy ? difficulty_.aimOmega : 8.0;
    const double maxRate = snappy ? difficulty_.aimTurn : 6.0;
    wantPitch = std::clamp(wantPitch, -1.1, 1.0);
    aimYawVelocity_ += (omega * omega * angleDiff(aimYaw_, wantYaw) - 2 * omega * aimYawVelocity_) * dt;
    aimYawVelocity_ = std::clamp(aimYawVelocity_, -maxRate, maxRate);
    aimYaw_ += aimYawVelocity_ * dt;
    if (aimYaw_ > pi) aimYaw_ -= 2 * pi; else if (aimYaw_ < -pi) aimYaw_ += 2 * pi;
    aimPitchVelocity_ += (omega * omega * (wantPitch - aimPitch_) - 2 * omega * aimPitchVelocity_) * dt;
    aimPitchVelocity_ = std::clamp(aimPitchVelocity_, -maxRate * 0.7, maxRate * 0.7);
    aimPitch_ = std::clamp(aimPitch_ + aimPitchVelocity_ * dt, -1.1, 1.0);
    input.aimYaw = aimYaw_; input.aimPitch = aimPitch_;
    const double cp = std::cos(aimPitch_);
    const double distance = (climbAiming_ || threatAiming_ || (fighting && target_)) ? aimDistance : mode_ == BotMode::Refill ? 1.6 : 6.0;
    input.aimPoint = {a.pos.x + std::sin(aimYaw_) * cp * distance,
                      a.pos.y + 1.1 + std::sin(aimPitch_) * distance,
                      a.pos.z + std::cos(aimYaw_) * cp * distance};
    input.hasAimPoint = true;
    if (!fighting && !threatAiming_ && input.aimPoint.y < a.pos.y) input.aimPoint.y = a.pos.y;

    avoidWater(move);
    if (input.squid && squidWouldDrop(move)) input.squid = false;
    const double ml = std::min(1.0, horizontal(move));
    if (ml > 0.01) {
        const double desired = std::atan2(move.x, move.z), d = angleDiff(moveYaw_, desired);
        if (moveMagnitude_ < 0.05) moveYaw_ = desired;
        else if (std::abs(d) > 2.1) { moveYaw_ = desired; moveMagnitude_ *= 0.35; }
        else moveYaw_ += std::clamp(d, -11 * dt, 11 * dt);
    }
    moveMagnitude_ += (ml - moveMagnitude_) * (1 - std::exp(-14 * dt));
    input.move = heading(moveYaw_) * moveMagnitude_;
    if (moveMagnitude_ > 0.05 && a.grounded && !a.climbing) edgeGuard(input.move);
    const auto profile = actor_.weaponProfile();
    const auto pose = actor_.combatPose();
    const bool trying = !path_.empty() && ml > 0.1 && !(charge(profile.kind) && (pose.charging || pose.burstTime > 0));
    if (!trying) noProgress_ = 0;
    if (noProgress_ > 0.7 && jumpCooldown_ <= 0 && a.grounded && !nearWater(1.2)) { input.jump = true; jumpCooldown_ = 1; }
    if (noProgress_ > 1.5 && !path_.empty() && pathIndex_ < static_cast<int>(path_.size()) - 1 && !skipped_) {
        ++pathIndex_; skipped_ = true; bestDistance_ = std::numeric_limits<double>::infinity();
    }
    if (noProgress_ > 2.4) {
        noProgress_ = 0; skipped_ = false; path_.clear(); goalTimer_ = repath_ = 0;
        if (++strikes_ >= 2) { strikes_ = 0; wiggle(0.9); }
        strikeTimer_ = 8;
    }
    if (noProgress_ == 0) skipped_ = false;
    if (needJump_ && jumpCooldown_ <= 0 && a.grounded) { input.jump = true; jumpCooldown_ = 0.6; needJump_ = false; }
    displacementTimer_ += dt; moveAccumulation_ += moveMagnitude_ * dt;
    if (displacementTimer_ >= 1.5) {
        const double moved = horizontal(a.pos - displacementPosition_);
        if (moveAccumulation_ / displacementTimer_ > 0.45 && moved < 0.4 && !(fighting && target_) && a.grounded && !climbAiming_ && !a.climbing) {
            if (++strikes_ >= 2) strikes_ = 0;
            strikeTimer_ = 8; wiggle(0.7);
        }
        displacementPosition_ = a.pos; displacementTimer_ = moveAccumulation_ = 0;
    }
}

void BotSimulation::tick(double dt, bool playing, const std::vector<BotSimulation*>& bots) {
    if(boss_){tickBoss(dt,playing);return;}
    auto& a = actor_.state;
    auto& input = actor_.input;
    if (!a.alive || !playing) {
        input.move = glm::dvec3(0.0); input.fire = input.squid = input.sub = input.jump = input.special = false;
        path_.clear(); target_ = nullptr; moveMagnitude_ = 0; wasDead_ = !a.alive; return;
    }
    time_ += dt; elapsed_ += dt; think_ -= dt; jumpCooldown_ -= dt; bombCooldown_ -= dt; strafeTimer_ -= dt;
    paintPause_ -= dt; dodgeCooldown_ -= dt; acquisitionTime_ += dt;
    const bool respawned = wasDead_;
    wasDead_ = false;
    if (zonePlan_) {
        if (respawned) {
            aimYaw_ = a.yaw; aimPitch_ = aimYawVelocity_ = aimPitchVelocity_ = 0.0;
            zonePlan_->onRespawn(*this, bots);
        }
        zonePlan_->syncBot(*this);
    }
    if (think_ <= 0) { think_ = 0.15 + random() * 0.1; perceive(); }
    if (target_ && !target_->state.alive) target_ = nullptr;
    const double inkFrac = a.ink / 100.0, hpFrac = a.hp / 100.0;
    const auto weapon = actor_.weaponProfile();
    const bool isMelee = melee(weapon.kind);
    if (mode_ == BotMode::Retreat) {
        retreatTime_ -= dt;
        if (hpFrac > 0.85 || retreatTime_ <= 0 || (!target_ && hpFrac > 0.6)) { mode_ = BotMode::Paint; path_.clear(); goalTimer_ = 0; }
    } else if (target_ && seeTimer_ > 0 && ((hpFrac < 0.34 && !isMelee && actor_.lastDamage() < 0.8) || hpFrac < 0.2) &&
               random() < 0.6 * dt * 60 * difficulty_.fireDiscipline) {
        mode_ = BotMode::Retreat; retreatTime_ = 2.2 + random() * 1.4; repath_ = 0; pickRetreat();
    }
    if (mode_ != BotMode::Refill && mode_ != BotMode::Retreat && inkFrac < 0.12 &&
        !(target_ && seeTimer_ > 0 && !isMelee && inkFrac > 0.05)) {
        mode_ = BotMode::Refill; refillThreshold_ = 0.85 + random() * 0.1;
    }
    if (mode_ == BotMode::Refill && inkFrac >= refillThreshold_) mode_ = BotMode::Paint;
    if (mode_ != BotMode::Refill && mode_ != BotMode::Retreat) {
        const bool engage = target_ && (!zonePlan_ || zonePlan_->shouldEngage(*this, target_));
        if (zonePlan_ && mode_ == BotMode::Fight && !engage) goalTimer_ = 0.0;
        mode_ = engage ? BotMode::Fight : BotMode::Paint;
    }
    if (zonePlan_) zonePlan_->maybeRotationJump(*this, bots);

    goalTimer_ -= dt; repath_ -= dt;
    if (mode_ == BotMode::Fight && target_) {
        if (zonePlan_ && zonePlan_->holdGround(*this, target_)) { path_.clear(); repath_ = 0.3; }
        else if (repath_ <= 0) pathTo(target_->state.pos, 0.6);
    } else if (mode_ == BotMode::Refill) {
        if (repath_ <= 0 || path_.empty()) pickRefill();
    } else if (mode_ == BotMode::Retreat) {
        if (repath_ <= 0 || path_.empty()) pickRetreat();
    } else {
        goalCheck_ -= dt;
        if (goalCheck_ <= 0 && goal_ >= 0 && !path_.empty()) {
            goalCheck_ = 1;
            const auto& g = nav_.nodes()[goal_];
            const auto stats = regionStats(g.x, g.y, g.z, 3);
            if (stats.count && stats.own > 0.85) goalTimer_ = 0;
        }
        if (zonePlan_) {
            if (goalTimer_ <= 0 || path_.empty() || pathIndex_ >= static_cast<int>(path_.size())) {
                if (!zonePlan_->planGoal(*this, bots)) pickPaintGoal(bots);
            }
        } else if (goalTimer_ <= 0 || path_.empty() || pathIndex_ >= static_cast<int>(path_.size())) pickPaintGoal(bots);
    }
    glm::dvec3 move = steer(dt);
    unstick(dt, move);
    if (path_.empty() && wiggleTimer_ <= 0) backOnNav(dt, move);
    const bool wantMove = horizontal(move) > 0.1;
    input.fire = input.sub = input.special = input.squid = input.jump = input.cheer = false;
    double wantYaw = wantMove ? std::atan2(move.x, move.z) : a.yaw;
    double wantPitch = -0.1, fightDistance = 0, aimDistance = 6;
    const bool visible = target_ && seeTimer_ > 0;
    if ((mode_ == BotMode::Fight || mode_ == BotMode::Retreat) && target_) {
        const auto delta = target_->state.pos - a.pos;
        fightDistance = horizontal(delta);
        if (mode_ == BotMode::Fight) fight(dt, move, wantYaw, wantPitch, aimDistance);
        else input.squid = true;
    } else if (mode_ == BotMode::Paint) paint(dt, move, wantYaw, wantPitch);
    else if (mode_ == BotMode::Refill) {
        input.squid = a.groundTeam == 1 || pathRemaining() > 2;
        if (a.groundTeam != 1 && pathRemaining() < 1.5 && inkFrac > 0.03) { input.squid = false; input.fire = true; wantPitch = -1.0; }
    }
    if ((mode_ == BotMode::Fight || mode_ == BotMode::Paint) && !actor_.specialPose().active &&
        a.specialCharge >= actor_.weapon().number("specialCost", 190.0)) {
        const bool fighting = mode_ == BotMode::Fight && target_;
        if (zonePlan_) {
            const bool genericFightSpecial = fighting && wantSpecial(true, fightDistance, visible);
            if (genericFightSpecial || zonePlan_->shouldUseSpecial(*this, fighting)) input.special = true;
        } else if (wantSpecial(fighting, fightDistance, visible)) input.special = true;
    }
    specialControl(dt, input, move, fightDistance, visible);
    threatControl(dt, move, wantYaw, wantPitch, aimDistance);
    canopyControl(dt, move, !threatAiming_, wantYaw, wantPitch, aimDistance);
    climb(dt, move, input, wantYaw, wantPitch, aimDistance);
    finish(dt, move, wantYaw, wantPitch, aimDistance, mode_ == BotMode::Fight);
}

void BotSimulation::tickBoss(double dt,bool playing){
    auto& state=actor_.state;auto& input=actor_.input;
    if(!state.alive||!playing){
        input.move={0,0,0};input.fire=input.squid=input.sub=input.jump=input.special=false;
        path_.clear();moveMagnitude_=0;return;
    }
    time_+=dt;elapsed_+=dt;think_-=dt;jumpCooldown_-=dt;bombCooldown_-=dt;
    strafeTimer_-=dt;acquisitionTime_+=dt;react_-=dt;bossEvadeTime_-=dt;
    input.fire=input.sub=input.special=input.squid=input.jump=input.cheer=false;
    const auto& boss=boss_->state();const auto& profile=actor_.weaponProfile();
    if(think_<=0){
        think_=.12+random()*.1;
        bossCrab_=bossShape_=-1;double crabDistance=9;
        for(int i=0;i<static_cast<int>(boss_->crablets().size());++i){
            const auto& crab=boss_->crablets()[i];if(crab.dead)continue;
            const double d=horizontal(crab.pos-state.pos);
            if(d<crabDistance&&geometry_.los(state.pos+glm::dvec3(0,1.2,0),crab.pos+glm::dvec3(0,.35,0))){
                crabDistance=d;bossCrab_=i;
            }
        }
        if(bossCrab_<0&&boss.visible&&!boss.dead){
            const auto& shapes=boss_->hitShapes();
            int belly=-1;for(int i=0;i<10;++i)if(shapes[i].active&&shapes[i].socket=="belly")belly=i;
            if(belly>=0)bossShape_=belly;
            else if(bossShapeTime_>0&&bossShape_>=0&&shapes[bossShape_].active){}
            else{
                bossShapeTime_=1.2+random()*1.5;
                const bool eyes=profile.kind=="charger"||random()<.25+difficulty_.fireDiscipline*.3;
                double best=std::numeric_limits<double>::infinity();
                for(int i=0;i<10;++i){const auto& shape=shapes[i];if(!shape.active)continue;
                    if(!geometry_.los(state.pos+glm::dvec3(0,1.2,0),shape.pos))continue;
                    const double d=glm::length(shape.pos-state.pos)+(shape.weak?(eyes?-50:10):0);
                    if(d<best){best=d;bossShape_=i;}
                }
            }
        }
        bossShapeTime_-=.2;
    }
    BossThreat danger=boss_->threat(state.pos.x,state.pos.y,state.pos.z,1.4);
    if(danger.level>0||danger.ringIn>=0){
        if(bossThreatSeen_<0){bossThreatSeen_=time_+difficulty_.reaction*(.5+random()*.9);
            bossHopMiss_=random()<(.45-difficulty_.fireDiscipline*.4);}
        if(time_<bossThreatSeen_){danger.level=0;danger.ringIn=-1;danger.beam=danger.cover=false;}
    }else bossThreatSeen_=-1;
    const double inkFraction=state.ink/100;
    if(mode_==BotMode::Refill&&inkFraction>=refillThreshold_){mode_=BotMode::Fight;path_.clear();goalTimer_=0;}
    if(mode_!=BotMode::Refill&&inkFraction<.1&&!(bossCrab_>=0&&inkFraction>.03)){
        mode_=BotMode::Refill;refillThreshold_=.8+random()*.15;path_.clear();
    }
    if(mode_!=BotMode::Refill)mode_=BotMode::Fight;
    goalTimer_-=dt;repath_-=dt;
    const bool evading=danger.level>.2&&!(danger.beam&&state.groundTeam==1);
    if(evading&&(bossEvadeTime_<=0||path_.empty())){
        double best=-std::numeric_limits<double>::infinity();int node=-1;
        for(int i=0;i<12;++i){
            const double angle=std::atan2(danger.ax,danger.az)+(random()-.5)*2.4,r=3+random()*5;
            const glm::dvec3 point=state.pos+glm::dvec3(std::sin(angle)*r,0,std::cos(angle)*r);
            const int id=nav_.nearest(point,1.2);if(id<0)continue;
            const auto& n=nav_.nodes()[id];const auto threat=boss_->threat(n.x,n.y,n.z,1.6);
            const double score=-threat.level*8-horizontal(glm::dvec3(n.x,n.y,n.z)-state.pos)*.25+
                (std::sin(angle)*danger.ax+std::cos(angle)*danger.az)*1.5+random()*.5;
            if(score>best){best=score;node=id;}
        }
        if(node>=0){const auto& n=nav_.nodes()[node];pathTo({n.x,n.y,n.z},1.0);goalTimer_=.8;}
        bossEvadeTime_=.45;
    }else if(!evading&&bossWasEvading_){path_.clear();goalTimer_=0;}
    bossWasEvading_=evading;
    if(!evading){
        if(mode_==BotMode::Refill){if(repath_<=0||path_.empty())pickRefill();}
        else if(goalTimer_<=0||path_.empty()||pathIndex_>=static_cast<int>(path_.size())||(boss.stunned&&!bossRushing_)){
            goalTimer_=2.4+random()*2;bossRushing_=boss.stunned;
            const bool rush=boss.stunned||(boss.phase>=3&&random()<.3);
            const double reach=profile.kind=="charger"?15:profile.kind=="roller"?3.2:std::clamp(profile.range*.7,4.5,11.0);
            const double radius=3.4+(boss.stunned?std::min(reach,6.0):reach);
            double best=-std::numeric_limits<double>::infinity();int goal=-1;
            for(int i=0;i<12;++i){
                const double offset=rush?(random()-.5)*1.3:(random()<.5?1:-1)*(.95+random()*1.9);
                const double angle=boss.yaw+offset,r=radius*(.85+random()*.3);
                const glm::dvec3 point{boss.pos.x+std::sin(angle)*r,boss.pos.y+.5,boss.pos.z+std::cos(angle)*r};
                const int id=nav_.nearest(point,2.5);if(id<0)continue;
                const auto& n=nav_.nodes()[id];
                if(std::hypot(n.x-point.x,n.z-point.z)>2.5)continue;
                double score=-std::hypot(n.x-state.pos.x,n.z-state.pos.z)*.08+random();
                if(boss_->threat(n.x,n.y,n.z,1.5).level>0)score-=6;
                if(geometry_.los({n.x,n.y+1.3,n.z},boss.pos+glm::dvec3(0,2.5,0)))score+=3;
                if(score>best){best=score;goal=id;}
            }
            if(goal>=0){const auto& n=nav_.nodes()[goal];pathTo({n.x,n.y,n.z},1.0);}else path_.clear();
        }
    }
    glm::dvec3 move=steer(dt);unstick(dt,move);
    if(path_.empty()&&wiggleTimer_<=0)backOnNav(dt,move);
    bool wantMove=horizontal(move)>.1;
    double wantYaw=wantMove?std::atan2(move.x,move.z):state.yaw,wantPitch=-.1,aimDistance=6;
    const bool dive=danger.beam&&state.groundTeam==1;
    glm::dvec3 aimPoint(0);double hitRadius=.5;
    if(bossCrab_>=0){const auto& c=boss_->crablets()[bossCrab_];aimPoint=c.pos+glm::dvec3(0,.35,0);hitRadius=.5;}
    else if(bossShape_>=0){const auto& shape=boss_->hitShapes()[bossShape_];aimPoint=shape.pos;hitRadius=shape.radius*.85;}
    if((bossCrab_>=0||bossShape_>=0)&&mode_!=BotMode::Refill&&!dive){
        const glm::dvec3 rel=aimPoint-(state.pos+glm::dvec3(0,1.1,0));
        const double dist=horizontal(rel),idealYaw=std::atan2(rel.x,rel.z),idealPitch=std::atan2(rel.y,dist);
        aimDistance=glm::length(rel);
        const double acquisition=std::exp(-acquisitionTime_/std::max(.12,difficulty_.reaction*.9));
        wantYaw=idealYaw+difficulty_.aimError*.8*(.75*wander(time_*1.7+phase1_)+2.4*acquisition*acquisitionSignYaw_);
        wantPitch=idealPitch+difficulty_.aimError*.48*(.75*wander(time_*2.1+phase2_)+1.6*acquisition*acquisitionSignPitch_);
        const bool inRange=dist<profile.range+(bossCrab_>=0?0:hitRadius*.6);
        if(!wantMove&&danger.level<=0&&profile.kind!="charger"){
            if(strafeTimer_<=0){strafeTimer_=.8+random()*1.4;strafe_=random()<.5?-1:1;strafeAmplitude_=.35+random()*.4;}
            strafeSmooth_+=(strafe_*strafeAmplitude_-strafeSmooth_)*(1-std::exp(-4*dt));
            const double nx=rel.x/std::max(.01,dist),nz=rel.z/std::max(.01,dist);
            move={-nz*strafeSmooth_,0,nx*strafeSmooth_};if(dist<4)move-=glm::dvec3(nx*.6,0,nz*.6);
            wantMove=horizontal(move)>.1;
        }
        const double off=std::hypot(angleDiff(aimYaw_,idealYaw),aimPitch_-idealPitch);
        const double tolerance=std::max(.05,std::atan2(hitRadius,std::max(dist,.5)))*(firing_?2.4:1.5);
        const bool los=geometry_.los(state.pos+glm::dvec3(0,1.2,0),aimPoint);
        firing_=false;
        if(inRange&&los&&off<tolerance&&react_<=0&&inkFraction>.02){
            const auto pose=actor_.combatPose();
            if(profile.kind=="charger")input.fire=!(pose.charging&&pose.charge>=chargeRelease_);
            else if(profile.kind=="splatling")input.fire=!pose.streaming&&!(pose.charging&&pose.charge>=chargeRelease_*.9);
            else if(profile.kind=="spinner")input.fire=pose.burstTime<=0&&!(pose.charging&&pose.charge>=chargeRelease_);
            else if(profile.kind=="bow")input.fire=!(pose.charging&&pose.charge>=std::max(.5,chargeRelease_));
            else if(profile.kind=="blade")input.fire=!(pose.charge>=.98);
            else if(profile.kind=="brolly")input.fire=!firing_;
            else if(profile.kind=="roller")input.fire=dist<5.5||(pose.rolling&&dist<8);
            else input.fire=true;
            firing_=input.fire;
            if(bombCooldown_<=0&&bossCrab_<0&&state.ink>40&&dist>5&&dist<13&&random()<.025){
                input.sub=true;bombCooldown_=6+random()*6;bombAim_=true;
            }
        }else if(charge(profile.kind)&&actor_.combatPose().charging&&los)input.fire=true;
        if(state.specialCharge>=actor_.weapon().number("specialCost",190)&&danger.level<=0&&bossCrab_<0){
            if(actor_.specialId()=="slam"&&dist<5.5)input.special=true;
            if(actor_.specialId()=="storm"&&dist<13&&los)input.special=true;
        }
    }
    if(mode_==BotMode::Refill){input.squid=state.groundTeam==1||pathRemaining()>2;
        if(state.groundTeam!=1&&pathRemaining()<1.5&&inkFraction>.03){input.squid=false;input.fire=true;wantPitch=-1;}}
    if(danger.ringIn>=0&&danger.ringIn<.2&&state.grounded&&jumpCooldown_<=0&&!bossHopMiss_){input.jump=true;jumpCooldown_=.5;}
    if(dive){input.squid=true;input.fire=false;}
    else if(!input.fire&&!actor_.combatPose().charging&&state.groundTeam==1&&(pathRemaining()>4||evading))input.squid=true;
    if(!wantMove&&!input.fire&&state.groundTeam!=1)input.squid=false;
    finish(dt,move,wantYaw,wantPitch,aimDistance,true);
    input.aimPoint=aimPoint;input.hasAimPoint=bossCrab_>=0||bossShape_>=0;
}

} // namespace restoration
