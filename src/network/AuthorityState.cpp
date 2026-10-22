#include "AuthorityState.hpp"

#include "Wire.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace restoration::network {
namespace {

constexpr std::uint8_t kBossPacket = 1;
constexpr std::uint8_t kZonesPacket = 2;
constexpr double kMaxCoordinate = 100000.0;
constexpr double kMaxClock = 10000000.0;
constexpr std::size_t kMaxZones = 32;
constexpr std::size_t kMaxRings = 64;
constexpr std::size_t kMaxBarrels = 128;
constexpr std::size_t kMaxPenaltyRecords = 512;
constexpr std::size_t kMaxHitGuardBytes = 128;

void writeVec3(Writer& w, const glm::dvec3& v) { w.f64(v.x).f64(v.y).f64(v.z); }
glm::dvec3 readVec3(Reader& r) { return {r.f64(), r.f64(), r.f64()}; }

bool bounded(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}
bool boundedVec(const glm::dvec3& v) {
    return bounded(v.x, -kMaxCoordinate, kMaxCoordinate) &&
           bounded(v.y, -kMaxCoordinate, kMaxCoordinate) &&
           bounded(v.z, -kMaxCoordinate, kMaxCoordinate);
}
void require(bool condition, const char* message) {
    if (!condition) throw WireError(message);
}

void validateMove(const BossMove& m, double clock) {
    require(static_cast<unsigned>(m.id) <= static_cast<unsigned>(BossMoveId::Frenzy), "unknown boss move");
    require(bounded(m.t0, 0.0, kMaxClock) && bounded(m.tele, 0.0, 600.0) &&
            bounded(m.act, 0.0, 600.0) && bounded(m.rec, 0.0, 600.0), "invalid boss move timeline");
    require(m.t0 <= clock + 1.0, "boss move starts in the future");
    require(boundedVec(m.point) && boundedVec(m.origin), "invalid boss move position");
    for (double v : {m.yaw, m.length, m.speed, m.a0, m.a1, m.rot0})
        require(bounded(v, -kMaxCoordinate, kMaxCoordinate), "invalid boss move value");
    require(m.count >= 0 && m.count <= static_cast<int>(MaxAuthorityCrablets), "invalid boss move count");
    require(m.rings.size() <= kMaxRings, "too many boss move rings");
    for (const double ring : m.rings) require(bounded(ring, 0.0, 600.0), "invalid boss move ring");
    require(m.barrels.size() <= kMaxBarrels, "too many boss move barrels");
    for (const auto& b : m.barrels) {
        require(bounded(b.x, -kMaxCoordinate, kMaxCoordinate) && bounded(b.y, -kMaxCoordinate, kMaxCoordinate) &&
                bounded(b.z, -kMaxCoordinate, kMaxCoordinate) && bounded(b.throwAt, 0.0, 600.0), "invalid boss barrel");
    }
}

void writeMove(Writer& w, const BossMove& m) {
    w.u8(static_cast<std::uint8_t>(m.id)).f64(m.t0).f64(m.tele).f64(m.act).f64(m.rec).u32(m.seed);
    writeVec3(w, m.point); writeVec3(w, m.origin);
    w.f64(m.yaw).f64(m.length).f64(m.speed).f64(m.a0).f64(m.a1).f64(m.rot0)
     .boolean(m.wall).boolean(m.spin).boolean(m.stun).i32(m.count)
     .u16(static_cast<std::uint16_t>(m.rings.size()));
    for (const double ring : m.rings) w.f64(ring);
    w.u16(static_cast<std::uint16_t>(m.barrels.size()));
    for (const auto& barrel : m.barrels) w.f64(barrel.x).f64(barrel.y).f64(barrel.z).f64(barrel.throwAt);
}

BossMove readMove(Reader& r, double clock) {
    BossMove m;
    const auto id = r.u8();
    require(id <= static_cast<std::uint8_t>(BossMoveId::Frenzy), "unknown boss move");
    m.id = static_cast<BossMoveId>(id);
    m.t0 = r.f64(); m.tele = r.f64(); m.act = r.f64(); m.rec = r.f64(); m.seed = r.u32();
    m.point = readVec3(r); m.origin = readVec3(r);
    m.yaw = r.f64(); m.length = r.f64(); m.speed = r.f64(); m.a0 = r.f64(); m.a1 = r.f64(); m.rot0 = r.f64();
    m.wall = r.boolean(); m.spin = r.boolean(); m.stun = r.boolean(); m.count = r.i32();
    const auto rings = r.u16(); require(rings <= kMaxRings, "too many boss move rings");
    m.rings.reserve(rings);
    for (std::uint16_t i = 0; i < rings; ++i) m.rings.push_back(r.f64());
    const auto barrels = r.u16(); require(barrels <= kMaxBarrels, "too many boss move barrels");
    m.barrels.reserve(barrels);
    for (std::uint16_t i = 0; i < barrels; ++i) m.barrels.push_back({r.f64(), r.f64(), r.f64(), r.f64()});
    validateMove(m, clock);
    return m;
}

void validateBoss(const BossAuthorityState& s) {
    require(boundedVec(s.state.pos) && boundedVec(s.state.vel), "invalid boss state position");
    for (double v : {s.state.yaw, s.state.turn, s.state.clock, s.state.hp, s.state.maxHp, s.state.animT0})
        require(bounded(v, -kMaxClock, kMaxClock), "invalid boss state value");
    require(s.state.clock >= 0.0 && s.state.maxHp > 0.0 && s.state.hp >= 0.0 && s.state.hp <= s.state.maxHp,
            "invalid boss health or clock");
    require(s.state.phase >= 1 && s.state.phase <= 3 && s.state.target >= -1 && s.state.target < 8 &&
            s.state.killer >= -1 && s.state.killer < 8 && s.state.anim >= 0 && s.state.anim <= 2,
            "invalid boss state enum");
    require(s.state.animT0 >= 0.0 && s.state.animT0 <= s.state.clock + 1.0, "invalid boss animation clock");
    if (s.move) validateMove(*s.move, s.state.clock);
    require(s.moves.size() <= MaxAuthorityMoves, "too many boss timeline entries");
    for (const auto& m : s.moves) validateMove(m, s.state.clock);
    require(s.crablets.size() <= MaxAuthorityCrablets, "too many boss crablets");
    std::set<int> crabIds;
    for (const auto& c : s.crablets) {
        require(c.id >= 0 && c.id <= 1000000 && crabIds.insert(c.id).second && c.target >= -1 && c.target < 8,
                "invalid boss crablet identity");
        require(boundedVec(c.pos) && bounded(c.yaw, -kMaxClock, kMaxClock) && bounded(c.hp, 0.0, kMaxClock) &&
                bounded(c.age, 0.0, kMaxClock) && bounded(c.retarget, -kMaxClock, kMaxClock) &&
                bounded(c.speed, 0.0, kMaxClock) && (!c.burst || c.dead), "invalid boss crablet state");
    }
    for (const auto& h : s.hitShapes) {
        require(h.socket.size() <= 128 && boundedVec(h.pos) && bounded(h.radius, 0.0, kMaxCoordinate),
                "invalid boss hit shape");
    }
    require(s.poseSockets.size() <= MaxAuthoritySockets, "too many boss pose sockets");
    for (const auto& socket : s.poseSockets)
        require(!socket.name.empty() && socket.name.size() <= 128 && boundedVec(socket.pos), "invalid boss pose socket");
    for (const double damage : s.damage) require(bounded(damage, 0.0, kMaxClock), "invalid boss damage total");
    for (const int hits : s.weakHits) require(hits >= 0 && hits <= 100000000, "invalid boss weak-hit count");
    for (const int splats : s.splats) require(splats >= 0 && splats <= 100000000, "invalid boss splat count");
    require(s.hitGuards.size() <= MaxAuthorityHitGuards, "too many boss hit guards");
    std::set<std::string> guards;
    for (const auto& guard : s.hitGuards)
        require(!guard.empty() && guard.size() <= kMaxHitGuardBytes && guards.insert(guard).second,
                "invalid boss hit guard");
}

void writeBossState(Writer& w, const BossState& s) {
    writeVec3(w, s.pos); writeVec3(w, s.vel);
    w.f64(s.yaw).f64(s.turn).f64(s.clock).f64(s.hp).f64(s.maxHp)
     .i32(s.phase).i32(s.target).i32(s.anim).i32(s.killer).f64(s.animT0)
     .boolean(s.visible).boolean(s.dead).boolean(s.invulnerable).boolean(s.stunned);
}
BossState readBossState(Reader& r) {
    BossState s;
    s.pos = readVec3(r); s.vel = readVec3(r);
    s.yaw = r.f64(); s.turn = r.f64(); s.clock = r.f64(); s.hp = r.f64(); s.maxHp = r.f64();
    s.phase = r.i32(); s.target = r.i32(); s.anim = r.i32(); s.killer = r.i32(); s.animT0 = r.f64();
    s.visible = r.boolean(); s.dead = r.boolean(); s.invulnerable = r.boolean(); s.stunned = r.boolean();
    return s;
}

void writeCrablet(Writer& w, const BossCrablet& c) {
    w.i32(c.id).i32(c.target); writeVec3(w, c.pos);
    w.f64(c.yaw).f64(c.hp).f64(c.age).f64(c.retarget).f64(c.speed).boolean(c.dead).boolean(c.burst);
}
BossCrablet readCrablet(Reader& r) {
    BossCrablet c;
    c.id = r.i32(); c.target = r.i32(); c.pos = readVec3(r);
    c.yaw = r.f64(); c.hp = r.f64(); c.age = r.f64(); c.retarget = r.f64(); c.speed = r.f64(); c.dead = r.boolean(); c.burst = r.boolean();
    return c;
}

void writeFlood(Writer& w, const ZoneFloodState& f) {
    w.i32(f.team).f64(f.start).f64(f.duration).f64(f.reach).boolean(f.easeOut);
}
ZoneFloodState readFlood(Reader& r) {
    ZoneFloodState f;
    f.team = r.i32(); f.start = r.f64(); f.duration = r.f64(); f.reach = r.f64(); f.easeOut = r.boolean();
    return f;
}

void validateZones(const ZoneAuthorityState& s) {
    require(bounded(s.duration, 0.01, kMaxClock) && bounded(s.clock, 0.0, kMaxClock) &&
            bounded(s.sampleT, -10.0, 10.0) && bounded(s.neutralT, 0.0, kMaxClock) &&
            bounded(s.overtimeT, 0.0, kMaxClock) &&
            ((std::isfinite(s.nextSwap) && bounded(s.nextSwap, 0.0, kMaxClock)) ||
             s.nextSwap == std::numeric_limits<double>::infinity()),
            "invalid Zones clock");
    require(s.zones.size() <= kMaxZones && !s.zones.empty(), "invalid Zones count");
    require(s.activeObjective >= 0 && s.activeObjective < 3 && s.owner >= -1 && s.owner <= 1 &&
            s.lastOwner >= -1 && s.lastOwner <= 1 && s.overtimeLosing >= -1 && s.overtimeLosing <= 1,
            "invalid Zones owner/objective");
    for (const double v : s.count) require(bounded(v, 0.0, kMaxClock), "invalid Zones count value");
    for (const double v : s.penalty) require(bounded(v, 0.0, kMaxClock), "invalid Zones penalty");
    for (const double v : s.start) require(bounded(v, 0.0, kMaxClock), "invalid Zones start count");
    for (const double v : s.hold) require(bounded(v, 0.0, kMaxClock), "invalid Zones hold time");
    for (const auto& tie : s.tieEnd) if (tie) require(bounded(*tie, 0.0, kMaxClock), "invalid Zones tie clock");
    if (s.winner) require(*s.winner == 0 || *s.winner == 1, "invalid Zones winner");
    require(s.reason.size() <= 128, "Zones reason exceeds its limit");
    for (std::size_t i = 0; i < s.zones.size(); ++i) {
        const auto& z = s.zones[i];
        require(z.id == static_cast<int>(i) && z.owner >= -1 && z.owner <= 1 &&
                bounded(z.hold, -kMaxClock, kMaxClock) && bounded(z.warnAt, -kMaxClock, kMaxClock),
                "invalid zone entry");
        for (const double share : z.share) require(bounded(share, 0.0, 1.0), "invalid zone share");
        if (z.flood) {
            const auto& f = *z.flood;
            require((f.team == -1 || f.team == 0 || f.team == 1) && bounded(f.start, 0.0, s.clock + 1.0) &&
                    bounded(f.duration, 0.01, 600.0) && bounded(f.reach, 0.0, kMaxCoordinate), "invalid zone flood metadata");
        }
    }
    require(s.floods.size() <= MaxAuthorityFloods, "too many active zone floods");
    std::set<int> floodZones;
    for (const auto& f : s.floods) {
        require(f.zone >= 0 && f.zone < static_cast<int>(s.zones.size()) && floodZones.insert(f.zone).second &&
                (f.team == -1 || f.team == 0 || f.team == 1) && f.next <= std::numeric_limits<std::uint32_t>::max() &&
                bounded(f.elapsed, 0.0, 600.0) && bounded(f.duration, 0.01, 600.0) &&
                f.elapsed <= f.duration + 0.1 && bounded(f.reach, 0.0, kMaxCoordinate), "invalid zone flood run");
    }
    require(s.penaltyLog.size() <= kMaxPenaltyRecords, "too many Zones penalty records");
    for (const auto& p : s.penaltyLog) {
        require(p.team == 0 || p.team == 1, "invalid Zones penalty team");
        for (const double v : {p.time, p.start, p.end, p.penalty, p.total})
            require(bounded(v, 0.0, kMaxClock), "invalid Zones penalty record");
    }
}

bool readHeader(Reader& r, std::uint8_t expected) {
    if (r.u16() != WireVersion || r.u8() != expected) throw WireError("unsupported authority-state packet");
    return true;
}

} // namespace

std::vector<std::uint8_t> EncodeBossAuthorityState(const BossAuthorityState& state) {
    validateBoss(state);
    Writer w;
    w.u16(WireVersion).u8(kBossPacket);
    writeBossState(w, state.state);
    w.boolean(state.move.has_value()); if (state.move) writeMove(w, *state.move);
    w.u16(static_cast<std::uint16_t>(state.moves.size())); for (const auto& move : state.moves) writeMove(w, move);
    w.u16(static_cast<std::uint16_t>(state.crablets.size())); for (const auto& crab : state.crablets) writeCrablet(w, crab);
    for (const auto& shape : state.hitShapes) {
        w.string(shape.socket, 128); writeVec3(w, shape.pos); w.f64(shape.radius).boolean(shape.weak).boolean(shape.active);
    }
    w.boolean(state.externalHitShapes);
    w.u16(static_cast<std::uint16_t>(state.poseSockets.size()));
    for (const auto& socket : state.poseSockets) { w.string(socket.name, 128); writeVec3(w, socket.pos); }
    for (const double damage : state.damage) w.f64(damage);
    for (const int hits : state.weakHits) w.u32(static_cast<std::uint32_t>(hits));
    for (const int splats : state.splats) w.u32(static_cast<std::uint32_t>(splats));
    w.u16(static_cast<std::uint16_t>(state.hitGuards.size()));
    for (const auto& guard : state.hitGuards) w.string(guard, kMaxHitGuardBytes);
    w.boolean(state.finished);
    return w.take();
}

bool DecodeBossAuthorityState(const std::vector<std::uint8_t>& bytes, BossAuthorityState& state, std::string* error) {
    try {
        Reader r(bytes); readHeader(r, kBossPacket);
        BossAuthorityState next;
        next.state = readBossState(r);
        if (r.boolean()) next.move = readMove(r, next.state.clock);
        const auto moves = r.u16(); require(moves <= MaxAuthorityMoves, "too many boss timeline entries");
        next.moves.reserve(moves); for (std::uint16_t i = 0; i < moves; ++i) next.moves.push_back(readMove(r, next.state.clock));
        const auto crabs = r.u16(); require(crabs <= MaxAuthorityCrablets, "too many boss crablets");
        next.crablets.reserve(crabs); for (std::uint16_t i = 0; i < crabs; ++i) next.crablets.push_back(readCrablet(r));
        for (auto& shape : next.hitShapes) {
            shape.socket = r.string(128); shape.pos = readVec3(r); shape.radius = r.f64();
            shape.weak = r.boolean(); shape.active = r.boolean();
        }
        next.externalHitShapes = r.boolean();
        const auto sockets = r.u16(); require(sockets <= MaxAuthoritySockets, "too many boss pose sockets");
        next.poseSockets.reserve(sockets);
        for (std::uint16_t i = 0; i < sockets; ++i) next.poseSockets.push_back({r.string(128), readVec3(r)});
        for (auto& damage : next.damage) damage = r.f64();
        for (auto& hits : next.weakHits) { const auto v = r.u32(); require(v <= 100000000, "invalid boss weak-hit count"); hits = static_cast<int>(v); }
        for (auto& splats : next.splats) { const auto v = r.u32(); require(v <= 100000000, "invalid boss splat count"); splats = static_cast<int>(v); }
        const auto guards = r.u16(); require(guards <= MaxAuthorityHitGuards, "too many boss hit guards");
        next.hitGuards.reserve(guards);
        for (std::uint16_t i = 0; i < guards; ++i) next.hitGuards.push_back(r.string(kMaxHitGuardBytes));
        next.finished = r.boolean(); r.finish(); validateBoss(next);
        state = std::move(next);
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<std::uint8_t> EncodeZonesAuthorityState(const ZoneAuthorityState& state) {
    validateZones(state);
    Writer w;
    w.u16(WireVersion).u8(kZonesPacket)
     .f64(state.duration).f64(state.clock).f64(state.sampleT).f64(state.neutralT).f64(state.overtimeT)
     .u8(static_cast<std::uint8_t>(state.zones.size()));
    for (const auto& zone : state.zones) {
        w.i32(zone.id).i32(zone.owner).f64(zone.share[0]).f64(zone.share[1])
         .f64(zone.hold).f64(zone.warnAt).boolean(zone.warnArmed).boolean(zone.flood.has_value());
        if (zone.flood) writeFlood(w, *zone.flood);
    }
    w.u8(static_cast<std::uint8_t>(state.floods.size()));
    for (const auto& flood : state.floods) {
        w.i32(flood.zone).i32(flood.team).u32(static_cast<std::uint32_t>(flood.next))
         .f64(flood.elapsed).f64(flood.duration).f64(flood.reach).boolean(flood.easeOut);
    }
    w.i32(state.activeObjective).i32(state.owner).i32(state.lastOwner).i32(state.overtimeLosing);
    for (const double v : state.count) w.f64(v);
    for (const double v : state.penalty) w.f64(v);
    for (const double v : state.start) w.f64(v);
    for (const auto& tie : state.tieEnd) { w.boolean(tie.has_value()); if (tie) w.f64(*tie); }
    for (const double v : state.hold) w.f64(v);
    // The wire reader/writer intentionally reject non-finite doubles. Reserve -1
    // as an explicit representation for the valid "no further swap" infinity.
    w.f64(std::isinf(state.nextSwap) ? -1.0 : state.nextSwap)
     .boolean(state.final).boolean(state.overtime).boolean(state.winner.has_value());
    if (state.winner) w.i32(*state.winner);
    w.string(state.reason, 128).u16(static_cast<std::uint16_t>(state.penaltyLog.size()));
    for (const auto& p : state.penaltyLog)
        w.f64(p.time).f64(p.start).f64(p.end).f64(p.penalty).f64(p.total).i32(p.team);
    return w.take();
}

bool DecodeZonesAuthorityState(const std::vector<std::uint8_t>& bytes, ZoneAuthorityState& state, std::string* error) {
    try {
        Reader r(bytes); readHeader(r, kZonesPacket);
        ZoneAuthorityState next;
        next.duration = r.f64(); next.clock = r.f64(); next.sampleT = r.f64(); next.neutralT = r.f64(); next.overtimeT = r.f64();
        const auto zones = r.u8(); require(zones > 0 && zones <= kMaxZones, "invalid Zones count");
        next.zones.reserve(zones);
        for (std::uint8_t i = 0; i < zones; ++i) {
            ZoneAuthorityEntry zone;
            zone.id = r.i32(); zone.owner = r.i32(); zone.share[0] = r.f64(); zone.share[1] = r.f64();
            zone.hold = r.f64(); zone.warnAt = r.f64(); zone.warnArmed = r.boolean();
            if (r.boolean()) zone.flood = readFlood(r);
            next.zones.push_back(std::move(zone));
        }
        const auto floods = r.u8(); require(floods <= MaxAuthorityFloods, "too many active zone floods");
        next.floods.reserve(floods);
        for (std::uint8_t i = 0; i < floods; ++i) {
            ZoneAuthorityFloodRun flood;
            flood.zone = r.i32(); flood.team = r.i32(); flood.next = r.u32();
            flood.elapsed = r.f64(); flood.duration = r.f64(); flood.reach = r.f64(); flood.easeOut = r.boolean();
            next.floods.push_back(flood);
        }
        next.activeObjective = r.i32(); next.owner = r.i32(); next.lastOwner = r.i32(); next.overtimeLosing = r.i32();
        for (auto& v : next.count) v = r.f64();
        for (auto& v : next.penalty) v = r.f64();
        for (auto& v : next.start) v = r.f64();
        for (auto& tie : next.tieEnd) if (r.boolean()) tie = r.f64();
        for (auto& v : next.hold) v = r.f64();
        next.nextSwap = r.f64();
        if (next.nextSwap == -1.0) next.nextSwap = std::numeric_limits<double>::infinity();
        next.final = r.boolean(); next.overtime = r.boolean();
        if (r.boolean()) next.winner = r.i32();
        next.reason = r.string(128);
        const auto records = r.u16(); require(records <= kMaxPenaltyRecords, "too many Zones penalty records");
        next.penaltyLog.reserve(records);
        for (std::uint16_t i = 0; i < records; ++i) {
            ZonePenaltyRecord p;
            p.time = r.f64(); p.start = r.f64(); p.end = r.f64(); p.penalty = r.f64(); p.total = r.f64(); p.team = r.i32();
            next.penaltyLog.push_back(p);
        }
        r.finish(); validateZones(next);
        state = std::move(next);
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

} // namespace restoration::network
