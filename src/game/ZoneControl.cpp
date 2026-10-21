#include "ZoneControl.hpp"

#include "ArsenalData.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace restoration {
namespace {
constexpr double kCount = 100.0;
constexpr double kFinal = 30.0;
constexpr double kWarn = 0.30;
constexpr double kWarnGap = 4.0;
constexpr double kMinStint = 10.0;
constexpr double kFillTime = 0.6;
constexpr double kWipeTime = 0.7;
constexpr double kControl = 0.80;
constexpr double kContest = 0.40;
constexpr double kCenterRate = 1.0;
constexpr double kHomeRate = 0.5;
constexpr double kAwayRate = 2.0;
constexpr double kPenaltyRate = 0.75;
constexpr double kGaugeHeld = 4.5;
constexpr double kGaugeNeutral = 1.5;
constexpr double kOvertimeGrace = 10.0;
constexpr double kOvertimeMax = 300.0;
constexpr double kSamplePeriod = 0.2;

bool insidePolygon(const ZonePolygon& polygon, double x, double z) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[j];
        if ((a.y > z) != (b.y > z) && x < ((b.x - a.x) * (z - a.y)) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

bool inAny(const std::vector<ZonePolygon>& polygons, double x, double z) {
    for (const auto& polygon : polygons) if (insidePolygon(polygon, x, z)) return true;
    return false;
}

ZoneDefinition placeholderCenter() {
    return {std::vector<ZonePolygon>{ZonePolygon{{-5.0, -5.0}, {5.0, -5.0}, {5.0, 5.0}, {-5.0, 5.0}}}, -2.0, 6.0};
}
ZoneDefinition placeholderSide(const StageData& stage) {
    const auto& pad = stage.spawnPads[0];
    const double sx = pad.x * 0.4, sz = pad.z * 0.45;
    return {std::vector<ZonePolygon>{ZonePolygon{{sx - 4, sz - 4}, {sx + 4, sz - 4}, {sx + 4, sz + 4}, {sx - 4, sz + 4}}}, -2.0, 6.0};
}

} // namespace

ZoneControl::ZoneControl(const StageData& stage, const StageGeometry& geometry, FacePaint& paint,
                         double duration, RandomSource random)
    : stage_(stage), geometry_(geometry), paint_(paint), duration_(duration), random_(std::move(random)) {
    if (!std::isfinite(duration_) || duration_ <= 0.0) throw std::invalid_argument("invalid Zones duration");
    if (!random_) random_ = [] { return 0.5; };

    const auto* layout = FindZoneStageDefinition(stage.id);
    std::vector<ZoneDefinition> center;
    ZoneDefinition side;
    if (layout) {
        center = layout->center;
        side = layout->side;
    } else {
        placeholder_ = true;
        center.push_back(placeholderCenter());
        side = placeholderSide(stage_);
    }
    if (center.empty() || side.polygons.empty()) throw std::invalid_argument("invalid Zones stage layout");

    const auto addZone = [&](const ZoneDefinition& def, std::string kind, int home) {
        ZoneState zone;
        zone.id = static_cast<int>(zones_.size());
        zone.kind = std::move(kind);
        zone.home = home;
        zone.region.polygons = def.polygons;
        zone.region.y0 = def.y0;
        zone.region.y1 = def.y1;

        double x0 = std::numeric_limits<double>::infinity(), x1 = -x0;
        double z0 = x0, z1 = -x0;
        for (const auto& poly : def.polygons) for (const auto& p : poly) {
            x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
            z0 = std::min(z0, p.y); z1 = std::max(z1, p.y);
        }
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (std::size_t faceId = 0; faceId < stage_.faces.size(); ++faceId) {
            const auto& face = stage_.faces[faceId];
            if (!face.turf) continue;
            const auto& grid = paint_.faceGrid(static_cast<int>(faceId));
            if (grid.offset < 0) continue;
            for (int j = 0; j < grid.rows; ++j) for (int i = 0; i < grid.columns; ++i) {
                const auto cell = grid.offset + j * grid.columns + i;
                if (paint_.dead()[static_cast<std::size_t>(cell)]) continue;
                const double s = (i + 0.5) * grid.cellWidth, t = (j + 0.5) * grid.cellHeight;
                const auto p = face.origin + face.u * s + face.v * t;
                if (p.x < x0 || p.x > x1 || p.z < z0 || p.z > z1 || p.y < def.y0 || p.y > def.y1 ||
                    !inAny(def.polygons, p.x, p.z)) continue;
                zone.region.cells.push_back(cell);
                zone.region.cellCenters.push_back(p);
                cx += p.x; cy += p.y; cz += p.z;
            }
        }
        const double n = static_cast<double>(std::max<std::size_t>(1, zone.region.cells.size()));
        zone.center = {cx / n, cy / n, cz / n};
        for (const auto& poly : def.polygons) for (const auto& p : poly)
            zone.reach = std::max(zone.reach, std::hypot(p.x - zone.center.x, p.y - zone.center.z));
        zones_.push_back(std::move(zone));
    };

    for (const auto& def : center) addZone(def, "center", -1);
    addZone(side, "side", 0);
    auto mirrored = side;
    for (auto& polygon : mirrored.polygons) for (auto& point : polygon) { point.x = -point.x; point.y = -point.y; }
    addZone(mirrored, "side", 1);

    ZoneObjective middle; middle.id = "center"; middle.home = -1;
    for (std::size_t i = 0; i < center.size(); ++i) middle.zones.push_back(static_cast<int>(i));
    objectives_.push_back(std::move(middle));
    const int sideA = static_cast<int>(center.size());
    const int sideB = sideA + 1;
    objectives_.push_back({"sideA", {sideA}, 0});
    objectives_.push_back({"sideB", {sideB}, 1});
    activeIndices_ = objectives_[0].zones;
    final_ = duration_ <= kFinal;
    schedule();
}

void ZoneControl::emit(ZoneEvent event) {
    event.time = clock_;
    event.counts = count_;
    event.penalties = penalty_;
    if (event.objective.empty() && activeObjective_ >= 0 && activeObjective_ < static_cast<int>(objectives_.size()))
        event.objective = objectives_[static_cast<std::size_t>(activeObjective_)].id;
    events_.push_back(std::move(event));
}

void ZoneControl::flood(ZoneState& zone, int team) {
    const bool fill = team == 0 || team == 1;
    const double duration = fill ? kFillTime : kWipeTime;
    const double reach = zone.reach + (fill ? 0.4 : 3.5);
    zone.flood = ZoneFloodState{fill ? team : -1, clock_, duration, reach, fill};
    if (!fill) zone.hold = clock_ + duration + 0.05;

    floods_.erase(std::remove_if(floods_.begin(), floods_.end(), [&](const auto& run) { return run.zone == zone.id; }), floods_.end());
    FloodRun run;
    run.zone = zone.id;
    run.team = fill ? team : -1;
    run.duration = duration;
    run.reach = reach;
    run.easeOut = fill;
    std::vector<std::pair<double, int>> ordered;
    ordered.reserve(zone.region.cells.size());
    for (std::size_t i = 0; i < zone.region.cells.size(); ++i) {
        const auto& p = zone.region.cellCenters[i];
        ordered.emplace_back(std::hypot(p.x - zone.center.x, p.z - zone.center.z), zone.region.cells[i]);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    run.distance.reserve(ordered.size()); run.cells.reserve(ordered.size());
    for (const auto& [distance, cell] : ordered) { run.distance.push_back(distance); run.cells.push_back(cell); }
    floods_.push_back(std::move(run));

    VisualFlood request;
    request.regionId = zone.id;
    request.team = fill ? team : -1;
    request.cells = zone.region.cells;
    request.polygons = zone.region.polygons;
    request.y0 = zone.region.y0; request.y1 = zone.region.y1;
    request.center = zone.center;
    request.reach = reach; request.start = clock_; request.duration = duration; request.easeOut = fill;
    paint_.requestVisualFlood(std::move(request));
}

void ZoneControl::advanceFloods(double dt) {
    for (std::size_t r = 0; r < floods_.size(); ++r) {
        auto& flood = floods_[r];
        flood.elapsed = std::min(flood.duration, flood.elapsed + dt);
        const bool done = flood.elapsed >= flood.duration;
        const double x = flood.duration > 0.0 ? flood.elapsed / flood.duration : 1.0;
        const double radius = done ? std::numeric_limits<double>::infinity()
            : flood.reach * (flood.easeOut ? 1.0 - (1.0 - x) * (1.0 - x) : x);
        std::vector<int> reached;
        const std::size_t begin = flood.next;
        while (flood.next < flood.distance.size() && flood.distance[flood.next] <= radius) ++flood.next;
        reached.insert(reached.end(), flood.cells.begin() + static_cast<std::ptrdiff_t>(begin),
                       flood.cells.begin() + static_cast<std::ptrdiff_t>(flood.next));
        if (!reached.empty()) paint_.setCells(reached, flood.team);
    }
    floods_.erase(std::remove_if(floods_.begin(), floods_.end(), [](const auto& flood) {
        return flood.elapsed >= flood.duration;
    }), floods_.end());
}

void ZoneControl::sample() {
    const auto& grid = paint_.grid();
    for (const int id : activeIndices_) {
        auto& zone = zones_[static_cast<std::size_t>(id)];
        int a = 0, b = 0;
        for (const int cell : zone.region.cells) {
            const auto value = grid[static_cast<std::size_t>(cell)];
            if (value == 1) ++a;
            else if (value == 2) ++b;
        }
        const double n = static_cast<double>(std::max<std::size_t>(1, zone.region.cells.size()));
        zone.share = {a / n, b / n};
        if (clock_ < zone.hold) continue;
        if (zone.owner == -1) {
            const int team = zone.share[0] >= kControl ? 0 : zone.share[1] >= kControl ? 1 : -1;
            if (team >= 0) setZoneOwner(zone, team);
        } else if (zone.share[1 - zone.owner] >= kContest) {
            setZoneOwner(zone, -1);
        } else {
            const double opponent = zone.share[1 - zone.owner];
            if (opponent < kWarn - 0.08) zone.warnArmed = true;
            else if (opponent >= kWarn && zone.warnArmed && clock_ - zone.warnAt >= kWarnGap) {
                zone.warnArmed = false;
                zone.warnAt = clock_;
                ZoneEvent event; event.type = ZoneEventType::Contested; event.zone = zone.id;
                event.owner = zone.owner; event.team = 1 - zone.owner; event.share = opponent;
                emit(std::move(event));
            }
        }
    }
    const bool allZero = std::all_of(activeIndices_.begin(), activeIndices_.end(), [&](int id) { return zones_[static_cast<std::size_t>(id)].owner == 0; });
    const bool allOne = std::all_of(activeIndices_.begin(), activeIndices_.end(), [&](int id) { return zones_[static_cast<std::size_t>(id)].owner == 1; });
    setOwner(allZero ? 0 : allOne ? 1 : -1);
}

void ZoneControl::setZoneOwner(ZoneState& zone, int owner) {
    zone.owner = owner;
    if (owner >= 0) {
        zone.warnArmed = true;
        zone.warnAt = -99.0;
        flood(zone, owner);
    }
    ZoneEvent event; event.type = ZoneEventType::ZoneOwnerChanged;
    event.zone = zone.id; event.owner = owner; event.team = owner;
    emit(std::move(event));
}

int losingCount(const std::array<double, 2>& count) {
    return count[0] == count[1] ? -1 : count[0] > count[1] ? 0 : 1;
}

void ZoneControl::setOwner(int owner) {
    if (owner == owner_) return;
    const int previous = owner_;
    owner_ = owner;
    if (owner == -1) neutralT_ = 0.0;
    else {
        const int other = 1 - owner;
        if (lastOwner_ == other) addPenalty(other);
        if (lastOwner_ != owner) {
            start_[static_cast<std::size_t>(owner)] = count_[static_cast<std::size_t>(owner)] + penalty_[static_cast<std::size_t>(owner)];
            tieEnd_[static_cast<std::size_t>(owner)].reset();
        }
        lastOwner_ = owner;
    }
    holdT_ = {0.0, 0.0};
    ZoneEvent event; event.type = ZoneEventType::ControlChanged; event.owner = owner; event.previousOwner = previous;
    emit(std::move(event));
}

void ZoneControl::addPenalty(int team) {
    const auto index = static_cast<std::size_t>(team);
    const double start = start_[index];
    const double total = count_[index] + penalty_[index];
    const double end = tieEnd_[index] ? std::max(*tieEnd_[index], total) : total;
    const double amount = std::max(0.0, std::round(kPenaltyRate * (start - end))) + (start == kCount ? 1.0 : 0.0);
    penalty_[index] += amount;
    penaltyLog_.push_back({clock_, start, end, amount, count_[index] + penalty_[index], team});
    ZoneEvent event; event.type = ZoneEventType::PenaltyAdded; event.team = team;
    event.penalty = amount; event.start = start; event.end = end;
    emit(std::move(event));
}

void ZoneControl::tickCount(int team, double dt) {
    const auto index = static_cast<std::size_t>(team);
    const double rate = objective().home < 0 ? kCenterRate : objective().home == team ? kHomeRate : kAwayRate;
    double decrement = rate * dt;
    const double before = count_[index], opponent = count_[1 - index];
    const double fromPenalty = std::min(penalty_[index], decrement);
    penalty_[index] -= fromPenalty;
    decrement -= fromPenalty;
    count_[index] = std::max(0.0, count_[index] - decrement);
    if (!tieEnd_[index] && before > opponent && count_[index] <= opponent) tieEnd_[index] = opponent;
}

void ZoneControl::fillSpecials(double dt, const std::vector<std::unique_ptr<PlayerSimulation>>& actors) {
    const int team = owner_ >= 0 ? 1 - owner_ : losingCount(count_);
    if (team < 0) return;
    const double rate = owner_ >= 0 ? kGaugeHeld : kGaugeNeutral;
    for (const auto& actor : actors) {
        if (actor->team() != team || !actor->state.alive || actor->combatPose().special) continue;
        const double limit = actor->weapon().number("specialCost", 190.0);
        actor->state.specialCharge = std::min(limit, actor->state.specialCharge + rate * dt);
    }
}

void ZoneControl::schedule() {
    const double random = std::clamp(random_(), 0.0, std::nextafter(1.0, 0.0));
    const double delay = 30.0 + random * 30.0;
    const double left = duration_ - clock_ - kFinal;
    nextSwap_ = left - delay >= kMinStint ? delay : activeObjective_ == 0 ? std::numeric_limits<double>::infinity() : std::max(0.5, left + 0.5);
}

void ZoneControl::swapObjective(int next, bool finalLock) {
    if (next < 0 || next >= static_cast<int>(objectives_.size())) return;
    if (next == activeObjective_ && !finalLock) return;
    for (const int id : activeIndices_) zones_[static_cast<std::size_t>(id)].owner = -1;
    activeObjective_ = next;
    activeIndices_ = objectives_[static_cast<std::size_t>(next)].zones;
    for (const int id : activeIndices_) {
        auto& zone = zones_[static_cast<std::size_t>(id)];
        zone.owner = -1;
        zone.warnArmed = true;
        flood(zone, -1);
    }
    if (finalLock) { final_ = true; nextSwap_ = 0.0; }
    else schedule();
    if (owner_ != -1) {
        const int previous = owner_;
        owner_ = -1;
        neutralT_ = 0.0;
        holdT_ = {0.0, 0.0};
        ZoneEvent control; control.type = ZoneEventType::ControlChanged; control.owner = -1; control.previousOwner = previous;
        emit(std::move(control));
    }
    sampleT_ = 0.0;
    ZoneEvent event; event.type = ZoneEventType::ObjectiveChanged; event.objective = objectives_[static_cast<std::size_t>(next)].id;
    event.activeZones = activeIndices_; event.final = finalLock; event.moved = true;
    emit(std::move(event));
    if (finalLock) {
        ZoneEvent finalEvent; finalEvent.type = ZoneEventType::FinalStarted; finalEvent.final = true;
        finalEvent.activeZones = activeIndices_; finalEvent.moved = true;
        emit(std::move(finalEvent));
    }
}

void ZoneControl::startFinal() {
    if (final_) return;
    final_ = true;
    nextSwap_ = 0.0;
    if (activeObjective_ != 0) swapObjective(0, true);
    else {
        ZoneEvent active; active.type = ZoneEventType::ObjectiveChanged;
        active.objective = objectives_[0].id; active.activeZones = activeIndices_;
        active.final = true; active.moved = false; emit(std::move(active));
        ZoneEvent event; event.type = ZoneEventType::FinalStarted; event.final = true;
        event.activeZones = activeIndices_; event.moved = false; emit(std::move(event));
    }
}

void ZoneControl::end(int winner, std::string reason) {
    if (winner_ || winner < 0 || winner > 1) return;
    winner_ = winner;
    reason_ = std::move(reason);
    ZoneEvent event; event.type = ZoneEventType::MatchEnded; event.winner = winner; event.reason = reason_;
    emit(std::move(event));
}

void ZoneControl::update(double dt, const std::vector<std::unique_ptr<PlayerSimulation>>& actors) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.1) throw std::invalid_argument("invalid Zones step");
    if (winner_) return;
    clock_ += dt;
    advanceFloods(dt);
    sampleT_ -= dt;
    if (sampleT_ <= 0.0) { sampleT_ = kSamplePeriod; sample(); }
    if (owner_ >= 0) {
        tickCount(owner_, dt);
        holdT_[static_cast<std::size_t>(owner_)] += dt;
        if (count_[static_cast<std::size_t>(owner_)] <= 0.0 && penalty_[static_cast<std::size_t>(owner_)] <= 0.0) {
            end(owner_, "knockout");
            return;
        }
    } else neutralT_ += dt;
    fillSpecials(dt, actors);
    if (overtime_) { updateOvertime(dt); return; }
    if (!final_) {
        if (duration_ - clock_ <= kFinal) startFinal();
        else {
            nextSwap_ -= dt;
            if (nextSwap_ <= 0.0) {
                const int next = activeObjective_ == 0 ? (random_() < 0.5 ? 1 : 2) : 0;
                swapObjective(next, false);
            }
        }
    }
}

bool ZoneControl::timeUp() {
    const int losing = losingCount(count_);
    const bool grace = owner_ == -1 && lastOwner_ == losing && neutralT_ < kOvertimeGrace;
    if (losing >= 0 && (owner_ == losing || grace)) {
        overtime_ = true; overtimeT_ = 0.0; overtimeLosing_ = losing;
        ZoneEvent event; event.type = ZoneEventType::OvertimeStarted; event.losing = losing;
        emit(std::move(event));
        return false;
    }
    if (losing < 0) {
        overtime_ = true; overtimeT_ = 0.0; overtimeLosing_ = -1;
        ZoneEvent event; event.type = ZoneEventType::OvertimeStarted; event.losing = -1;
        emit(std::move(event));
        return false;
    }
    end(1 - losing, "time");
    return true;
}

void ZoneControl::updateOvertime(double dt) {
    overtimeT_ += dt;
    int losing = overtimeLosing_;
    if (losing < 0) {
        losing = losingCount(count_);
        if (losing < 0) {
            if (overtimeT_ >= kOvertimeMax) {
                double scores[2]{};
                for (const int id : activeIndices_) {
                    scores[0] += zones_[static_cast<std::size_t>(id)].share[0];
                    scores[1] += zones_[static_cast<std::size_t>(id)].share[1];
                }
                end(scores[0] == scores[1] ? (random_() < 0.5 ? 0 : 1) : scores[0] > scores[1] ? 0 : 1, "overtime-cap");
            }
            return;
        }
    }
    const int winning = 1 - losing;
    if (count_[static_cast<std::size_t>(losing)] < count_[static_cast<std::size_t>(winning)]) { end(losing, "comeback"); return; }
    if (owner_ == winning) { end(winning, "retake"); return; }
    if (owner_ != losing && neutralT_ >= kOvertimeGrace) { end(winning, "neutralised"); return; }
    if (overtimeT_ >= kOvertimeMax) end(winning, "overtime-cap");
}

bool ZoneControl::contains(const glm::dvec3& point) const {
    for (const int id : activeIndices_) {
        const auto& zone = zones_[static_cast<std::size_t>(id)];
        if (point.y < zone.region.y0 - 1.0 || point.y > zone.region.y1 + 2.5) continue;
        if (inAny(zone.region.polygons, point.x, point.z)) return true;
    }
    return false;
}

} // namespace restoration
