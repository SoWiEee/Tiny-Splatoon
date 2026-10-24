#include "LobbyPresentation.hpp"
#include "ArsenalData.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace restoration {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSwimSpeed = 10.0;
constexpr double kSwimAcceleration = 26.0;
constexpr double kSwimBrake = 13.0;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
double smoothstep(double a, double b, double x) {
    const double t = clamp01((x - a) / std::max(1e-9, b - a));
    return t * t * (3.0 - 2.0 * t);
}
double wrapAngle(double x) {
    while (x > kPi) x -= 2.0 * kPi;
    while (x < -kPi) x += 2.0 * kPi;
    return x;
}
double dampAngle(double from, double to, double rate, double dt) {
    return from + wrapAngle(to - from) * (1.0 - std::exp(-rate * dt));
}
double horizontalDistance(const glm::dvec3& a, const glm::dvec3& b) {
    return glm::length(glm::dvec2(a.x - b.x, a.z - b.z));
}

struct Climb {
    double begin = 0.0, end = 0.0, y0 = 0.0, y1 = 0.0, height = 0.0;
};
struct SampledPath {
    std::vector<glm::dvec3> points;
    std::vector<double> cumulative;
    std::vector<Climb> climbs;
    double length = 0.0;
};

double distance(const glm::dvec3& a, const glm::dvec3& b) { return glm::length(a - b); }

// Centripetal Catmull-Rom in the horizontal plane, matching THREE.CatmullRomCurve3.
glm::dvec2 catmull(const std::vector<glm::dvec3>& p, std::size_t segment, double u) {
    const glm::dvec3& p1 = p[segment];
    const glm::dvec3& p2 = p[segment + 1];
    const glm::dvec3& p0 = segment ? p[segment - 1] : p1 * 2.0 - p2;
    const glm::dvec3& p3 = segment + 2 < p.size() ? p[segment + 2] : p2 * 2.0 - p1;
    auto flat = [](const glm::dvec3& v) { return glm::dvec2(v.x, v.z); };
    const glm::dvec2 a = flat(p0), b = flat(p1), c = flat(p2), d = flat(p3);
    double dt0 = std::pow(glm::length(b - a), 0.5);
    double dt1 = std::pow(glm::length(c - b), 0.5);
    double dt2 = std::pow(glm::length(d - c), 0.5);
    if (dt1 < 1e-4) dt1 = 1.0;
    if (dt0 < 1e-4) dt0 = dt1;
    if (dt2 < 1e-4) dt2 = dt1;
    const glm::dvec2 v0 = (b - a) / dt0 - (c - a) / (dt0 + dt1) + (c - b) / dt1;
    const glm::dvec2 v1 = (c - b) / dt1 - (d - b) / (dt1 + dt2) + (d - c) / dt2;
    const glm::dvec2 A = 2.0 * b - 2.0 * c + v0 + v1;
    const glm::dvec2 B = -3.0 * b + 3.0 * c - 2.0 * v0 - v1;
    const glm::dvec2 C = v0;
    return ((A * u + B) * u + C) * u + b;
}

double polylineY(const std::vector<glm::dvec3>& points, const glm::dvec2& q) {
    double best = std::numeric_limits<double>::max();
    double y = points.front().y;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const glm::dvec2 a(points[i].x, points[i].z), b(points[i + 1].x, points[i + 1].z);
        const glm::dvec2 delta = b - a;
        const double l2 = glm::dot(delta, delta);
        const double u = l2 > 1e-8 ? clamp01(glm::dot(q - a, delta) / l2) : 0.0;
        const glm::dvec2 error = a + delta * u - q;
        const double d = glm::dot(error, error);
        if (d < best - 1e-9) {
            best = d;
            y = glm::mix(points[i].y, points[i + 1].y, u);
        }
    }
    return y;
}

SampledPath makePath(const std::vector<glm::dvec3>& raw) {
    SampledPath path;
    if (raw.empty()) return path;
    std::vector<glm::dvec3> points{raw.front()};
    for (const auto& p : raw)
        if (distance(p, points.back()) > 0.03) points.push_back(p);
    if (points.size() < 2) points.push_back(points.front() + glm::dvec3(0.0, 0.0, 0.05));

    // Estimate curve length, then invert its sampled arc length to get ~8 cm spacing.
    const std::size_t perSegment = 24;
    std::vector<glm::dvec2> dense;
    dense.reserve((points.size() - 1) * perSegment + 1);
    for (std::size_t s = 0; s + 1 < points.size(); ++s)
        for (std::size_t j = 0; j < perSegment; ++j)
            dense.push_back(catmull(points, s, static_cast<double>(j) / perSegment));
    dense.push_back(glm::dvec2(points.back().x, points.back().z));
    std::vector<double> arc(dense.size(), 0.0);
    for (std::size_t i = 1; i < dense.size(); ++i) arc[i] = arc[i - 1] + glm::length(dense[i] - dense[i - 1]);
    const double curveLength = arc.back();
    const std::size_t count = std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(curveLength / 0.08)));
    path.points.reserve(count + 1);
    for (std::size_t i = 0; i <= count; ++i) {
        const double wanted = curveLength * static_cast<double>(i) / count;
        auto upper = std::lower_bound(arc.begin(), arc.end(), wanted);
        std::size_t hi = static_cast<std::size_t>(upper - arc.begin());
        hi = std::clamp<std::size_t>(hi, 1, arc.size() - 1);
        const std::size_t lo = hi - 1;
        const double f = (wanted - arc[lo]) / std::max(1e-9, arc[hi] - arc[lo]);
        glm::dvec2 q = glm::mix(dense[lo], dense[hi], f);
        path.points.emplace_back(q.x, polylineY(points, q), q.y);
    }
    path.cumulative.assign(path.points.size(), 0.0);
    for (std::size_t i = 1; i < path.points.size(); ++i) {
        const double step = horizontalDistance(path.points[i - 1], path.points[i]);
        path.cumulative[i] = path.cumulative[i - 1] + step;
        const double dy = path.points[i].y - path.points[i - 1].y;
        if (std::abs(dy) > 0.004 && std::abs(dy) > 0.3 * step) {
            if (!path.climbs.empty() && path.cumulative[i - 1] - path.climbs.back().end < 0.35)
                path.climbs.back().end = path.cumulative[i];
            else
                path.climbs.push_back({path.cumulative[i - 1], path.cumulative[i]});
        }
    }
    path.length = path.cumulative.back();
    for (auto& climb : path.climbs) {
        const double s0 = std::max(0.0, climb.begin - 0.3);
        const double s1 = std::min(path.length, climb.end + 0.25);
        auto at = [&](double s) {
            auto hi = std::lower_bound(path.cumulative.begin(), path.cumulative.end(), s);
            const std::size_t index = std::clamp<std::size_t>(static_cast<std::size_t>(hi - path.cumulative.begin()), 1, path.points.size() - 1);
            const std::size_t lo = index - 1;
            const double u = (s - path.cumulative[lo]) / std::max(1e-6, path.cumulative[index] - path.cumulative[lo]);
            return glm::mix(path.points[lo], path.points[index], u).y;
        };
        climb.begin = s0;
        climb.end = s1;
        climb.y0 = at(s0);
        climb.y1 = at(s1);
        climb.height = 0.34 + 0.3 * std::abs(climb.y1 - climb.y0);
    }
    return path;
}

struct PathPoint { glm::dvec3 position{0.0}, direction{0.0, 0.0, 1.0}; double lift = 0.0; bool climbing = false; };
PathPoint pathAt(const SampledPath& path, double s) {
    PathPoint out;
    if (path.points.empty()) return out;
    s = std::clamp(s, 0.0, path.length);
    auto hiIt = std::upper_bound(path.cumulative.begin(), path.cumulative.end(), s);
    std::size_t hi = static_cast<std::size_t>(hiIt - path.cumulative.begin());
    hi = std::clamp<std::size_t>(hi, 1, path.points.size() - 1);
    const std::size_t lo = hi - 1;
    const double u = (s - path.cumulative[lo]) / std::max(1e-6, path.cumulative[hi] - path.cumulative[lo]);
    out.position = glm::mix(path.points[lo], path.points[hi], u);
    const glm::dvec3& a = path.points[lo ? lo - 1 : 0];
    const glm::dvec3& b = path.points[std::min(path.points.size() - 1, hi + 1)];
    out.direction = glm::dvec3(b.x - a.x, 0.0, b.z - a.z);
    const double directionLength2 = glm::dot(out.direction, out.direction);
    if (directionLength2 < 1e-10) out.direction = {0.0, 0.0, 1.0};
    else out.direction /= std::sqrt(directionLength2);
    for (const auto& climb : path.climbs) {
        if (s > climb.begin && s < climb.end) {
            const double x = (s - climb.begin) / std::max(1e-9, climb.end - climb.begin);
            out.position.y = glm::mix(climb.y0, climb.y1, x);
            out.lift = 4.0 * climb.height * x * (1.0 - x);
            out.climbing = true;
            break;
        }
    }
    return out;
}

enum class Motion { Idle, Swim, Leap, Pop, Dive, Land, Jump, HubReturn, Launched };
struct Actor {
    LobbyActorState state;
    Motion motion = Motion::Idle;
    SampledPath path;
    double time = 0.0, delay = 0.0, distance = 0.0, speed = 0.0;
    double vertical = 0.0, previousY = 0.0, formBlend = 0.0;
    int targetMark = -1;
    std::string swimEnd;
    bool dead = false, reducedAtStart = false;
    double leapDuration = 0.5, leapHeight = 0.62;
    glm::dvec3 leapFrom{0.0}, leapTo{0.0};
    double jumpDelay = 0.0;
    bool takeoffSent = false, eventSent = false;
    std::uint64_t emoteSerial = 0;
    bool hasEmoteSerial = false, emoteActive = false;
    double emoteUntil = 0.0, actionAt=-99;
    std::string idleAction;
    bool actionLanded=false;
};

enum class Mode { Hub, Room };
constexpr double kLobbyFade = 0.45;
bool isRoomPhase(network::RoomPhase phase) {
    return phase == network::RoomPhase::Lobby || phase == network::RoomPhase::Starting || phase == network::RoomPhase::Match;
}
} // namespace

struct LobbyPresentation::Impl {
    explicit Impl(const LobbyLayout& value) : layout(value) {
        for (std::size_t i = 0; i < 8; ++i) {
            arrivals[i] = makePath(layout.arrivalPaths[i].points);
            exits[i] = makePath(layout.exitPaths[i].points);
        }
        local = makeActor(0, "Player", "shooter", {}, CharacterStyle{}, true);
        local->state.position = layout.hubSpot.position;
        local->state.yaw = layout.hubSpot.yaw;
        activeCamera = &layout.hubCamera;
    }

    LobbyLayout layout;
    std::array<SampledPath, 8> arrivals, exits;
    std::unordered_map<network::MemberId, std::shared_ptr<Actor>> actors;
    std::shared_ptr<Actor> local;
    std::vector<LobbyActorState> snapshot;
    std::vector<LobbyPresentationEvent> events;
    const LobbyCamera* activeCamera = nullptr;
    bool hub = true, hasPhase = false, countdownActive = false, fired = false, handed = false;
    bool roomRosterActive = false;
    network::RoomPhase previousPhase = network::RoomPhase::Offline;
    std::string roomCode;
    std::uint64_t launchEpoch = 0, cancelledEpoch = std::numeric_limits<std::uint64_t>::max();
    double launchTime = 0.0, handoffAt = 0.0, roomTime = 0.0, nextArrivalAt = 0.0, setProgress = 0.0;
    int shownCountdown = -2;
    bool reducedMotion = false;
    int localTeam = 0;
    bool bossMode = false;

    std::shared_ptr<Actor> makeActor(network::MemberId id, const std::string& name, const std::string& main,
                                    const std::string& sub, const CharacterStyle& style, bool isLocal) {
        auto actor = std::make_shared<Actor>();
        actor->state.id = id;
        actor->state.name = name;
        actor->state.main = main.empty() ? "shooter" : main;
        actor->state.sub = sub.empty()?std::string(GetEquipment(EquipmentCategory::Main,actor->state.main).sub):sub;
        actor->state.style = style;
        actor->state.local = isLocal;
        actor->state.visible = isLocal;
        actor->state.form = "kid";
        actor->state.dance = "menu_idle";
        actors[id] = actor;
        return actor;
    }

    void emit(const char* kind, Actor& actor, double strength = 1.0) {
        events.push_back({kind, actor.state.id, actor.state.position, strength});
    }
    void emitAt(const char* kind, network::MemberId id, const glm::dvec3& p, double strength = 1.0) {
        events.push_back({kind, id, p, strength});
    }
    glm::dvec3 localPosition() const { return local ? local->state.position : glm::dvec3(0.0); }

    void setAttributes(Actor& actor, const std::string& name, const std::string& main, const std::string& sub,
        const CharacterStyle& style, int team, int /*mark*/, bool ready, bool host, bool isLocal,
                       const std::string& /*emote*/) {
        auto& s = actor.state;
        const std::string mainId=main.empty()?"shooter":main;
        const std::string subId=sub.empty()?std::string(GetEquipment(EquipmentCategory::Main,mainId).sub):sub;
        if (s.main != mainId || s.sub != subId) {
            s.main = mainId;
            s.sub = subId;
            emit("weapon", actor, 0.5);
        }
        if (s.style.hair != style.hair || s.style.skin != style.skin || s.style.outfit != style.outfit ||
            s.style.eyes != style.eyes || s.style.hat != style.hat || s.style.brows != style.brows) {
            s.style = style;
            emit("style_pop", actor, 0.5);
        }
        if (s.ready != ready){
            emit(ready ? "ready" : "unready", actor, 1.0);
            actor.idleAction=ready?"ready":"unready";actor.actionAt=roomTime;actor.actionLanded=false;
        }
        s.name = name;
        s.ready = ready;
        s.host = host;
        s.local = isLocal;
        if (ready && actor.motion == Motion::Idle && !actor.emoteActive) {
            s.dance = "lobby_pose";
        } else if (!ready && actor.motion == Motion::Idle && s.dance == "lobby_pose" && !actor.emoteActive) {
            s.dance = "menu_idle";
        }
        if (actor.motion == Motion::Idle && s.mark < 0) s.team = team;
    }

    void updateEmote(Actor& actor, const network::Member& member) {
        if (!actor.hasEmoteSerial) {
            actor.emoteSerial = member.emoteSerial;
            actor.hasEmoteSerial = true;
            return;
        }
        if (actor.emoteSerial == member.emoteSerial) return;
        actor.emoteSerial = member.emoteSerial;
        if (!member.emoteSerial || member.emote.empty() || (actor.motion!=Motion::Idle&&actor.motion!=Motion::Land)) return;
        actor.state.dance = member.emote == "booyah" || member.emote == "wave" ||
                            member.emote == "dance" || member.emote == "flex" ? "victory" : member.emote;
        const double duration = member.emote == "booyah" ? 2.9 : member.emote == "wave" ? 2.35 :
                                member.emote == "dance" ? 3.4 : member.emote == "flex" ? 1.55 : 2.0;
        actor.idleAction=member.emote;actor.actionAt=roomTime;
        actor.emoteUntil = roomTime + duration;
        actor.emoteActive = true;
        events.push_back({"emote_" + member.emote, actor.state.id, actor.state.position, 0.7});
    }

    void beginSwim(Actor& actor, SampledPath path, const char* end, double delay, double initialSpeed) {
        actor.path = std::move(path);
        if (actor.path.points.empty()) {
            actor.path.points = {actor.state.position, actor.state.position + glm::dvec3(0.0, 0.0, 0.05)};
            actor.path.cumulative = {0.0, 0.05};
            actor.path.length = 0.05;
        }
        actor.motion = Motion::Swim;
        actor.time = 0.0;
        actor.delay = delay;
        actor.distance = 0.0;
        actor.speed = initialSpeed;
        actor.vertical = 0.0;
        actor.swimEnd = end;
        actor.state.position = actor.path.points.front();
        actor.state.velocityWorld = glm::dvec3(0.0);
        actor.state.speed = initialSpeed;
        actor.state.visible = true;
        actor.state.form = "swim";
        actor.state.dance.clear();
        actor.eventSent = false;
    }

    void beginArrival(Actor& actor, int mark, double delay) {
        actor.targetMark = mark;
        actor.state.mark = mark;
        actor.state.team = bossMode ? 0 : (mark >= 4 ? 1 : 0);
        actor.state.position = arrivals[static_cast<std::size_t>(mark)].points.empty()
            ? layout.lineupSpots[static_cast<std::size_t>(mark)].position
            : arrivals[static_cast<std::size_t>(mark)].points.front();
        beginSwim(actor, arrivals[static_cast<std::size_t>(mark)], "leap", delay, 8.0);
        emit("arrival", actor, 0.7);
    }

    void beginStand(Actor& actor, int mark) {
        const auto& spot = layout.lineupSpots[static_cast<std::size_t>(mark)];
        actor.targetMark = actor.state.mark = mark;
        actor.state.team = bossMode ? 0 : (mark >= 4 ? 1 : 0);
        actor.state.position = spot.position;
        actor.state.yaw = spot.yaw;
        actor.state.velocityWorld = glm::dvec3(0.0);
        actor.state.velocityY = actor.state.speed = 0.0;
        actor.state.visible = true;
        actor.state.form = "kid";
        actor.state.dance = actor.state.ready ? "lobby_pose" : "menu_idle";
        actor.motion = Motion::Idle;
        actor.time = actor.delay = 0.0;
    }

    SampledPath switchPath(int from, int to, const glm::dvec3& current) const {
        if (from < 0 || from >= 8 || to < 0 || to >= 8) return makePath({current, layout.lineupSpots[std::clamp(to, 0, 7)].position});
        const auto& e = exits[static_cast<std::size_t>(from)].points;
        const auto& n = arrivals[static_cast<std::size_t>(to)].points;
        if (e.empty() || n.empty()) return makePath({current, layout.lineupSpots[static_cast<std::size_t>(to)].position});
        std::size_t bi = 0, bj = n.size() - 1;
        double best = std::numeric_limits<double>::max();
        std::vector<double> ec(e.size(), 0.0), nc(n.size(), 0.0);
        for (std::size_t i = 1; i < e.size(); ++i) ec[i] = ec[i - 1] + distance(e[i - 1], e[i]);
        for (std::size_t j = 1; j < n.size(); ++j) nc[j] = nc[j - 1] + distance(n[j - 1], n[j]);
        for (std::size_t i = 0; i < e.size(); ++i) for (std::size_t j = 0; j < n.size(); ++j) {
            if (std::abs(e[i].y - n[j].y) > 0.12) continue;
            const double cost = ec[i] + 1.7 * distance(e[i], n[j]) + nc.back() - nc[j];
            if (cost < best) { best = cost; bi = i; bj = j; }
        }
        std::vector<glm::dvec3> raw{current};
        for (std::size_t i = 1; i <= bi; ++i) raw.push_back(e[i]);
        if (distance(raw.back(), e[bi]) > 0.03) raw.push_back(e[bi]);
        for (std::size_t j = bj; j < n.size(); ++j) raw.push_back(n[j]);
        return makePath(raw);
    }

    void beginDepart(Actor& actor, int newMark, bool toHub = false) {
        actor.targetMark = newMark;
        actor.motion = Motion::Dive;
        actor.time = 0.0;
        actor.vertical = 0.0;
        actor.state.form = "kid";
        actor.state.dance.clear();
        actor.swimEnd = toHub ? "hub" : (newMark < 0 ? "gone" : "switch");
        actor.path = {};
        const int oldMark = actor.state.mark;
        if (oldMark >= 0 && oldMark < 8) {
            actor.path = (newMark >= 0 && newMark < 8) ? switchPath(oldMark, newMark, actor.state.position)
                                                        : exits[static_cast<std::size_t>(oldMark)];
            if (!actor.path.points.empty()) actor.path.points.front() = actor.state.position;
        }
        emit(toHub ? "leave" : (newMark >= 0 ? "switch" : "dive"), actor, 0.7);
    }

    void beginPop(Actor& actor, const glm::dvec3& position, double yaw, double delay = 0.30) {
        actor.motion = Motion::Pop;
        actor.time = 0.0;
        actor.delay = delay;
        actor.state.position = position;
        actor.state.yaw = yaw;
        actor.state.visible = false;
        actor.state.form = "swim";
        actor.eventSent = false;
    }

    void beginInitialPop(Actor& actor, int mark, double delay) {
        const auto& spot = layout.lineupSpots[static_cast<std::size_t>(mark)];
        actor.targetMark = actor.state.mark = mark;
        actor.state.team = bossMode ? 0 : (mark >= 4 ? 1 : 0);
        beginPop(actor, spot.position, spot.yaw, delay);
        emit("arrival", actor, 0.7);
    }

    void cancelCountdown() {
        if (!countdownActive || fired) return;
        cancelledEpoch = launchEpoch;
        countdownActive = false;
        handed = false;
        shownCountdown = -2;
        emitAt("countdown_cancel", 0, local ? local->state.position : glm::dvec3(0.0));
    }

    void startCountdown(const network::RoomView& view) {
        if (countdownActive && launchEpoch == view.matchEpoch) return;
        if (cancelledEpoch == view.matchEpoch) return;
        launchEpoch = view.matchEpoch;
        countdownActive = true;
        fired = false;
        handed = false;
        launchTime = 0.0;
        handoffAt = 0.0;
        shownCountdown = -2;
        emitAt("countdown_start", 0, localPosition());
        for (auto& [id, actor] : actors) {
            (void)id;
            if (actor->dead || actor->motion != Motion::Idle && actor->motion != Motion::Land) continue;
            actor->state.dance = "lobby_pose";
            actor->state.velocityWorld = glm::dvec3(0.0);
            actor->state.speed = 0.0;
        }
    }

    void fireLaunch() {
        fired = true;
        emitAt("go", 0, localPosition());
        std::vector<std::shared_ptr<Actor>> ordered;
        for (const auto& [id, actor] : actors) {
            (void)id;
            if (!actor->dead && actor->state.visible && actor->state.mark >= 0 && actor->state.mark < 8 &&
                (actor->motion==Motion::Idle||actor->motion==Motion::Land))ordered.push_back(actor);
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a->state.mark < b->state.mark; });
        const double duration = 0.75 + 0.07 * static_cast<double>(ordered.empty()?0:ordered.size()-1);
        handoffAt = launchTime + std::clamp(duration, 0.5, 1.3) + 0.1;
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            auto& actor = *ordered[i];
            actor.motion = reducedMotion ? Motion::Launched : Motion::Jump;
            actor.jumpDelay = 0.07 * static_cast<double>(i);
            actor.time = 0.0;actor.actionAt=roomTime;
            actor.state.position.y=layout.lineupSpots[std::size_t(actor.state.mark)].position.y;
            actor.state.pitch=actor.state.twirl=0;actor.state.squash=1;
            actor.takeoffSent = false;
            actor.state.velocityWorld = glm::dvec3(0.0);
            if (reducedMotion) {
                actor.state.visible = false;
                actor.state.form = "squid";
                emit("superjump", actor, 0.4);
            }
        }
    }

    void updateCountdown(const network::RoomView& view, double dt) {
        if (view.phase == network::RoomPhase::Starting || view.phase == network::RoomPhase::Match) startCountdown(view);
        else if (countdownActive && !fired && (view.phase == network::RoomPhase::Lobby || !isRoomPhase(view.phase))) cancelCountdown();
        if (!countdownActive) return;
        const double old = launchTime;
        launchTime += dt;
        auto crossed = [&](double t) { return old < t && launchTime >= t; };
        if (crossed(0.35)) { shownCountdown = 3; emitAt("countdown_3", 0, localPosition()); }
        if (crossed(1.20)) { shownCountdown = 2; emitAt("countdown_2", 0, localPosition()); }
        if (crossed(2.05)) { shownCountdown = 1; emitAt("countdown_1", 0, localPosition()); }
        if (!fired && crossed(2.90)) { shownCountdown = 0; fireLaunch(); }
        if (fired && !handed && launchTime >= handoffAt) {
            handed = true;
            emitAt("handoff", 0, localPosition());
        }
    }

    void updateAttributesFromProfile(Actor& actor, const PlayerProfile& profile) {
        setAttributes(actor, profile.name, profile.weapon, profile.sub, profile.style, localTeam, actor.state.mark,
                      actor.state.ready, actor.state.host, true, {});
    }

    void updateMotion(Actor& actor, double dt) {
        if (actor.dead) return;
        const glm::dvec3 before = actor.state.position;
        const double baseY = actor.state.position.y;
        actor.state.velocityY = 0.0;
        if (reducedMotion) {
            actor.state.velocityWorld = glm::dvec3(0.0);
            actor.state.speed = 0.0;
            return;
        }
        const double step = std::clamp(dt, 0.0, 0.10);
        if (actor.motion == Motion::Idle || actor.motion == Motion::Launched) {
            actor.state.velocityWorld = glm::dvec3(0.0);
            actor.state.speed = 0.0;
            if(actor.motion==Motion::Idle&&!actor.idleAction.empty()){
                const double at=roomTime-actor.actionAt;
                const double floor=actor.state.mark>=0?layout.lineupSpots[std::size_t(actor.state.mark)].position.y:layout.hubSpot.position.y;
                actor.state.position.y=floor;
                const bool ready=actor.idleAction=="ready",unready=actor.idleAction=="unready";
                const double height=ready?.2:actor.idleAction=="booyah"?.32:actor.idleAction=="flex"?.12:0;
                const double duration=ready?.36:.42;
                if(height>0&&at<duration){
                    const double x=at/duration;actor.state.position.y+=4*height*x*(1-x);
                    actor.state.velocityY=4*height*(1-2*x)/duration;
                    actor.state.squash=1+.08*clamp01(actor.state.velocityY/2);
                }else if(ready){
                    if(!actor.actionLanded){actor.actionLanded=true;emit("ready_land",actor,.6);}
                    if(at<1.55)actor.state.dance="victory";
                    else {actor.idleAction.clear();actor.state.dance="lobby_pose";}
                    actor.state.squash=1-.12*std::sin(14*std::max(0.,at-duration))*std::exp(-6*std::max(0.,at-duration));
                }else if(unready){
                    actor.state.squash=1-.1*std::sin(kPi*clamp01(at/.3));
                    if(at>.4)actor.idleAction.clear();
                }else if(roomTime>=actor.emoteUntil)actor.idleAction.clear();
            }
            if (actor.motion == Motion::Idle && actor.emoteActive && roomTime >= actor.emoteUntil) {
                actor.emoteActive = false;
                actor.state.dance = actor.state.ready ? "lobby_pose" : "menu_idle";
            }
            if (actor.motion == Motion::Idle && !actor.state.ready && actor.state.dance == "") actor.state.dance = "menu_idle";
            return;
        }
        actor.time += step;
        if (actor.motion == Motion::Swim) {
            if (actor.delay > 0.0) {
                actor.delay = std::max(0.0, actor.delay - step);
                actor.state.velocityWorld = glm::dvec3(0.0);
                actor.state.speed = 0.0;
                return;
            }
            const double end = actor.swimEnd == "leap" ? std::max(0.0, actor.path.length - 2.3)
                              : actor.swimEnd == "pop" ? actor.path.length : actor.path.length;
            const double endSpeed = actor.swimEnd == "leap" ? 4.6 : actor.swimEnd == "pop" ? 1.2 : kSwimSpeed;
            const double cap = std::sqrt(endSpeed * endSpeed + 2.0 * kSwimBrake * std::max(0.0, end - actor.distance));
            const PathPoint current = pathAt(actor.path, actor.distance);
            const double targetSpeed = std::min(kSwimSpeed * (current.climbing ? 0.62 : 1.0), cap);
            if (actor.speed < targetSpeed) actor.speed = std::min(targetSpeed, actor.speed + kSwimAcceleration * step);
            else actor.speed = std::max(targetSpeed, actor.speed - kSwimBrake * 1.6 * step);
            const double oldVertical = current.position.y + (current.climbing ? current.lift : 0.1);
            actor.distance = std::min(end, actor.distance + actor.speed * step);
            const PathPoint p = pathAt(actor.path, actor.distance);
            actor.state.position = p.position + glm::dvec3(0.0, p.climbing ? p.lift : 0.1, 0.0);
            actor.state.yaw = dampAngle(actor.state.yaw, std::atan2(p.direction.x, p.direction.z), 14.0, step);
            actor.state.form = "swim";
            actor.vertical = (actor.state.position.y - oldVertical) / std::max(step, 1e-3);
            actor.state.velocityY = actor.vertical;
            if (actor.distance >= end - 1e-4) {
                if (actor.swimEnd == "leap") {
                    actor.motion = Motion::Leap;
                    actor.time = 0.0;
                    actor.leapFrom = p.position;
                    actor.leapTo = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].position;
                    const double span = horizontalDistance(actor.leapFrom, actor.leapTo);
                    actor.leapDuration = 0.5 * (0.8 + 0.2 * clamp01(span / 2.3));
                    actor.leapHeight = 0.62 + std::max(0.0, actor.leapTo.y - actor.leapFrom.y) * 0.4;
                    actor.state.form = "squid";
                    emit("leap", actor, 0.7);
                } else if (actor.swimEnd == "pop") {
                    beginPop(actor, layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].position,
                             layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].yaw);
                } else if (actor.swimEnd == "switch") {
                    actor.motion = Motion::Land;
                    actor.time = 0.0;
                    actor.state.position = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].position;
                    actor.state.mark = actor.targetMark;
                    actor.state.form = "kid";
                    emit("switch_land", actor, 0.6);
                } else if (actor.swimEnd == "hub") {
                    actor.motion = Motion::HubReturn;
                    actor.time = 0.0;
                } else {
                    actor.state.visible = false;
                    actor.motion = Motion::Launched;
                    if (!actor.state.local) actor.dead = true;
                    else actor.motion = Motion::HubReturn;
                    emit(actor.state.local ? "hub_arrival" : "leave_complete", actor, 0.5);
                }
            }
        } else if (actor.motion == Motion::Leap) {
            const double x = clamp01(actor.time / actor.leapDuration);
            actor.state.position = glm::mix(actor.leapFrom, actor.leapTo, x);
            actor.state.position.y += 4.0 * actor.leapHeight * x * (1.0 - x);
            actor.state.velocityY = (4.0 * actor.leapHeight * (1.0 - 2.0 * x)) / actor.leapDuration +
                                    (actor.leapTo.y - actor.leapFrom.y) / actor.leapDuration;
            actor.state.pitch = 0.0;
            actor.state.squash = actor.state.form == "kid" ? 1.0 + 0.1 * clamp01(actor.state.velocityY / 4.0) - 0.05 * clamp01(-actor.state.velocityY / 5.0) : 1.0;
            if (x > 0.42) actor.state.form = "kid";
            else actor.state.form = "squid";
            const auto& spot = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))];
            actor.state.yaw = actor.state.yaw + wrapAngle(spot.yaw - actor.state.yaw) * smoothstep(0.2, 1.0, x);
            if (x >= 1.0) {
                actor.state.position = spot.position;
                actor.state.yaw = spot.yaw;
                actor.motion = Motion::Land;
                actor.time = 0.0;
                actor.state.form = "kid";
                actor.state.squash = 1.05;
                actor.state.dance = actor.state.ready ? "lobby_pose" : "menu_idle";
                emit("land", actor, 1.0);
            }
        } else if (actor.motion == Motion::Pop) {
            if (actor.time < actor.delay) {
                actor.state.position.y -= 0.0;
                actor.state.visible = false;
            } else {
                actor.state.visible = true;
                const double t = actor.time - actor.delay;
                constexpr double y0 = -1.1, tp = 0.36, peak = 0.34;
                const double g = 2.0 * (peak - y0) / (tp * tp), v0 = g * tp;
                const double y = y0 + v0 * t - 0.5 * g * t * t;
                actor.state.position.y = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].position.y + std::max(0.0, y);
                actor.state.velocityY = v0 - g * t;
                actor.state.form = y > -0.42 ? "kid" : "swim";
                if (!actor.eventSent && y > 0.0) { actor.eventSent = true; emit("pop", actor, 0.8); }
                if (t >= tp && y <= 0.0) {
                    actor.state.position = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.targetMark, 0, 7))].position;
                    actor.motion = Motion::Land;
                    actor.time = 0.0;
                    actor.state.form = "kid";
                    actor.state.dance = actor.state.ready ? "lobby_pose" : "menu_idle";
                    emit("land", actor, 0.8);
                }
            }
        } else if (actor.motion == Motion::Dive) {
            actor.state.squash = actor.time < 0.24 ? 1.0 - 0.14 * std::sin(kPi * 0.5 * clamp01(actor.time / 0.24)) : 1.0;
            if (actor.time >= 0.16 + 0.08 + 0.36) {
                const int target = actor.targetMark;
                const std::string end = actor.swimEnd;
                if (end == "hub") {
                    actor.state.mark = -1;
                    if (!actor.path.points.empty()) beginSwim(actor, actor.path, "hub", 0.0, 1.6);
                    else actor.motion = Motion::HubReturn;
                } else if (target >= 0 && target < 8 && !actor.path.points.empty()) {
                    actor.state.team = bossMode ? 0 : (target >= 4 ? 1 : 0);
                    actor.state.mark = target;
                    actor.state.position = actor.path.points.front();
                    beginSwim(actor, actor.path, "switch", 0.0, 1.6);
                    emit("switch_swim", actor, 0.5);
                } else if (!actor.path.points.empty()) {
                    beginSwim(actor, actor.path, "gone", 0.0, 1.6);
                    emit("leave_swim", actor, 0.5);
                } else {
                    actor.state.visible = false;
                    actor.dead = !actor.state.local;
                    actor.motion = Motion::Launched;
                }
            }
        } else if (actor.motion == Motion::Land) {
            actor.state.squash = 1.0 - 0.12 * std::sin(actor.time * 14.0) * std::exp(-actor.time * 3.0);
            if (actor.time >= 0.55) {
                actor.motion = Motion::Idle;
                actor.time = 0.0;
                actor.state.squash = 1.0;
                actor.state.position = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.state.mark, 0, 7))].position;
                actor.state.yaw = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.state.mark, 0, 7))].yaw;
            }
        } else if (actor.motion == Motion::Jump) {
            actor.time=std::max(0.,roomTime-actor.actionAt);
            if (actor.time < actor.jumpDelay) {
                actor.state.velocityWorld = glm::dvec3(0.0);
                actor.state.speed = 0.0;
                return;
            }
            const double t = actor.time - actor.jumpDelay;
            constexpr double anticipation = 0.13;
            if (t < anticipation) {
                actor.state.squash = 1.0 - 0.22 * std::sin(kPi * 0.5 * t / anticipation);
            } else {
                const double x = t - anticipation;
                if (!actor.takeoffSent) {
                    actor.takeoffSent = true;
                    actor.state.form = "squid";
                    emit("superjump", actor, 0.4);
                }
                actor.state.position.y = layout.lineupSpots[static_cast<std::size_t>(std::clamp(actor.state.mark, 0, 7))].position.y + 7.0 * x + 26.0 * x * x;
                actor.state.velocityY = 7.0 + 52.0 * x;
                actor.state.pitch = -kPi * 0.5;
                actor.state.squash = 1.4;
                if (x > 0.55) {
                    actor.state.visible = false;
                    actor.motion = Motion::Launched;
                    actor.state.form = "squid";
                }
            }
        } else if (actor.motion == Motion::HubReturn) {
            actor.state.position = layout.hubSpot.position;
            actor.state.yaw = layout.hubSpot.yaw;
            actor.state.visible = true;
            actor.state.mark = -1;
            actor.state.form = "kid";
            actor.state.dance = "menu_idle";
            actor.motion = Motion::Idle;
        }

        actor.state.velocityWorld = step > 1e-5 ? (actor.state.position - before) / step : glm::dvec3(0.0);
        actor.state.speed = horizontalDistance(actor.state.velocityWorld, glm::dvec3(0.0));
        if (actor.motion == Motion::Idle || actor.motion == Motion::Land || actor.motion == Motion::Launched)
            actor.state.speed = 0.0;
        if (actor.motion != Motion::Swim && actor.motion != Motion::Leap && actor.motion != Motion::Pop && actor.motion != Motion::Jump)
            actor.state.velocityY = 0.0;
        (void)baseY;
    }

    void update(const network::RoomView& view, const PlayerProfile& profile, double dt) {
        dt = std::max(0.0, dt);
        reducedMotion = profile.settings.reducedMotion;
        const bool inRoom = isRoomPhase(view.phase);
        const bool initialRoster = inRoom && !roomRosterActive;
        const bool hubShotVisible = initialRoster && hasPhase && hub && setProgress > 0.05 && !reducedMotion;
        const double entranceWait = std::max(0.0, (0.6 - setProgress) * kLobbyFade);
        if(roomCode!=view.code){
            roomCode=view.code;countdownActive=fired=handed=false;launchTime=handoffAt=0;
            cancelledEpoch=std::numeric_limits<std::uint64_t>::max();shownCountdown=-2;
        }
        hub = !inRoom;
        roomRosterActive = inRoom;
        if (hub) nextArrivalAt = 0.0;
        activeCamera = hub ? &layout.hubCamera : &layout.lobbyCamera;
        roomTime += dt;
        const bool phaseChanged = !hasPhase || previousPhase != view.phase;
        if (phaseChanged && view.phase == network::RoomPhase::Lobby &&
            (previousPhase == network::RoomPhase::Match || (previousPhase == network::RoomPhase::Starting && handed))) {
            countdownActive = false;
            fired = handed = false;
            shownCountdown = -2;
            for (auto& [id, actor] : actors) {
                (void)id;
                if (actor->dead || actor->state.mark < 0 || actor->state.mark >= 8) continue;
                const auto& spot = layout.lineupSpots[static_cast<std::size_t>(actor->state.mark)];
                actor->motion = Motion::Idle;
                actor->time = actor->delay = actor->jumpDelay = 0.0;
                actor->state.position = spot.position;
                actor->state.yaw = spot.yaw;
                actor->state.velocityWorld = glm::dvec3(0.0);
                actor->state.velocityY = actor->state.speed = actor->state.pitch = actor->state.twirl = 0.0;
                actor->state.squash = 1.0;
                actor->state.form = "kid";
                actor->state.visible = true;
                actor->state.dance = actor->state.ready ? "lobby_pose" : "menu_idle";
                emit("match_return", *actor, 0.5);
            }
        }
        if (phaseChanged && (view.phase == network::RoomPhase::Starting || view.phase == network::RoomPhase::Match)) startCountdown(view);
        updateCountdown(view, dt);
        previousPhase = view.phase;
        hasPhase = true;

        const network::Member* self = nullptr;
        for (const auto& member : view.members) if (member.id == view.selfId) { self = &member; break; }
        localTeam = self && self->team >= 0 ? self->team : 0;

        if (hub) {
            const network::MemberId id = view.selfId;
            if (local && local->state.id != id) {
                actors.erase(local->state.id);local->state.id=id;actors[id]=local;
                local->state.ready=false;local->state.host=false;local->state.team=0;
            }
            if (!local) local = makeActor(id, profile.name, profile.weapon, profile.sub, profile.style, true);
            updateAttributesFromProfile(*local, profile);
            local->state.id = id;
            if (local->state.mark >= 0 && (local->motion == Motion::Idle || local->motion == Motion::Land)) {
                if (reducedMotion) {
                    local->motion = Motion::HubReturn;
                    local->state.position = layout.hubSpot.position;
                    local->state.yaw = layout.hubSpot.yaw;
                    local->state.mark = -1;
                } else beginDepart(*local, -1, true);
            } else if (!local->dead && (local->motion == Motion::Launched || !local->state.visible)) {
                    local->motion = Motion::Idle;
                    local->state.position = layout.hubSpot.position;
                    local->state.yaw = layout.hubSpot.yaw;
                    local->state.mark = -1;
                    local->state.visible = true;
            } else if (local->state.mark < 0 && local->motion==Motion::Idle) {
                local->state.position = layout.hubSpot.position;
                local->state.yaw = layout.hubSpot.yaw;
                local->state.visible = true;
            }
            for (auto& [id, actor] : actors) {
                if (actor == local || actor->dead) continue;
                if(actor->motion==Motion::Launched){actor->dead=true;continue;}
                if (reducedMotion) { actor->dead = true; actor->state.visible = false; }
                else if (actor->motion == Motion::Idle || actor->motion == Motion::Land) beginDepart(*actor, -1, false);
            }
        } else if (view.phase == network::RoomPhase::Lobby || view.phase == network::RoomPhase::Starting ||
                   view.phase == network::RoomPhase::Match) {
            if (local && local->state.id != view.selfId) {
                actors.erase(local->state.id);
                local->state.id = view.selfId;
                actors[view.selfId] = local;
            }
            if (!local) {
                local = makeActor(view.selfId, profile.name, profile.weapon, profile.sub, profile.style, true);
                local->state.position = layout.hubSpot.position;
                local->state.yaw = layout.hubSpot.yaw;
            }
            std::vector<const network::Member*> ordered;
            ordered.reserve(view.members.size() + 1);
            for (const auto& member : view.members) ordered.push_back(&member);
            if (!self && view.selfId != 0) ordered.push_back(nullptr);
            std::unordered_set<network::MemberId> liveIds;
            std::array<bool, 8> taken{};
            std::unordered_map<network::MemberId, int> desired;
            const bool boss = view.settings.mode == "boss";
        bossMode = boss;
            if (self) { desired[self->id] = 0; taken[0] = true; }
            std::vector<const network::Member*> others;
            for (auto* member : ordered) if (member && member->id != view.selfId) others.push_back(member);
            for (auto* member : others) {
                int side = boss ? 0 : (member->team == localTeam ? 0 : 1);
                auto existing = actors.find(member->id);
                if (existing != actors.end()) {
                    const int old = existing->second->state.mark;
                    if (old >= 0 && old < 8 && (boss || (old / 4 == side)) && !taken[static_cast<std::size_t>(old)]) {
                        desired[member->id] = old;
                        taken[static_cast<std::size_t>(old)] = true;
                        continue;
                    }
                }
                int slot = -1;
                if (boss) {
                    for (int i = 0; i < 8; ++i) if (!taken[static_cast<std::size_t>(i)]) { slot = i; break; }
                } else {
                    const int begin = side ? 4 : 1;
                    for (int i = begin; i < begin + (side ? 4 : 3); ++i) if (!taken[static_cast<std::size_t>(i)]) { slot = i; break; }
                }
                if (slot >= 0) { desired[member->id] = slot; taken[static_cast<std::size_t>(slot)] = true; }
            }
            if (!self && view.selfId != 0) desired[view.selfId] = 0;

            std::size_t initialRemote = 0;
            for (auto* member : ordered) {
                const network::MemberId id = member ? member->id : view.selfId;
                const bool isLocal = id == view.selfId;
                liveIds.insert(id);
                auto it = actors.find(id);
                std::shared_ptr<Actor> actor = it == actors.end() ? nullptr : it->second;
                if (!actor) {
                    actor = makeActor(id, isLocal ? profile.name : member->name,
                                      isLocal ? profile.weapon : member->main,
                                      isLocal ? profile.sub : member->sub,
                                      isLocal ? profile.style : member->style, isLocal);
                    actor->state.position = layout.hubSpot.position;
                    actor->state.yaw = layout.hubSpot.yaw;
                }
                const int mark = desired.count(id) ? desired[id] : -1;
                if (isLocal) {
                    updateAttributesFromProfile(*actor, profile);
                    if (member && actor->state.ready != member->ready) {
                        emit(member->ready ? "ready" : "unready", *actor, 1.0);
                        actor->idleAction=member->ready?"ready":"unready";actor->actionAt=roomTime;actor->actionLanded=false;
                        actor->state.ready = member->ready;
                    }
                    if (member) actor->state.host = member->id == view.hostId;
                }
                else setAttributes(*actor, member->name, member->main, member->sub, member->style,
                                   boss ? 0 : (member->team == localTeam ? 0 : 1), mark,
                                   member->ready, member->id == view.hostId, false, member->emote);
                if (member) updateEmote(*actor, *member);
                actor->targetMark = mark;
                if (actor->state.mark < 0 && mark >= 0 && actor->motion == Motion::Idle) {
                    if (initialRoster && isLocal) {
                        beginInitialPop(*actor, mark, entranceWait + (setProgress > 0.05 ? 0.2 : 0.0) + 0.30);
                    } else if (initialRoster && !isLocal) {
                        const std::size_t k = initialRemote++;
                        if (hubShotVisible)
                            beginInitialPop(*actor, mark, entranceWait + 0.95 + 0.13 * static_cast<double>(k) + 0.30);
                        else beginStand(*actor, mark);
                    } else {
                        const double delay = std::max(0.12, nextArrivalAt - roomTime);
                        beginArrival(*actor, mark, entranceWait + delay);
                        nextArrivalAt = roomTime + delay + 0.62;
                    }
                } else if (actor->state.mark >= 0 && mark >= 0 && actor->state.mark != mark) {
                    if (actor->motion == Motion::Idle || actor->motion == Motion::Land) beginDepart(*actor, mark, false);
                } else if (actor->motion == Motion::HubReturn && mark >= 0) {
                    beginStand(*actor, mark);
                }
                if (actor->motion == Motion::Idle && actor->state.mark == mark && mark >= 0)
                    actor->state.team = boss ? 0 : (mark >= 4 ? 1 : 0);
                if (reducedMotion && mark >= 0 && actor->motion != Motion::Jump && !(countdownActive && fired)) {
                    actor->state.position = layout.lineupSpots[static_cast<std::size_t>(mark)].position;
                    actor->state.yaw = layout.lineupSpots[static_cast<std::size_t>(mark)].yaw;
                    actor->state.visible = true;
                    actor->state.mark = mark;
                    actor->state.team = boss ? 0 : (mark >= 4 ? 1 : 0);
                    actor->motion = Motion::Idle;
                    actor->state.form = "kid";
                }
                if (isLocal) local = actor;
            }
            for (auto& [id, actor] : actors) {
                if (liveIds.count(id) || actor->dead || actor == local) continue;
                if(actor->motion==Motion::Launched){actor->dead=true;continue;}
                if (reducedMotion) { actor->dead = true; actor->state.visible = false; }
                else if (actor->motion == Motion::Idle || actor->motion == Motion::Land) beginDepart(*actor, -1, false);
            }
        } else if (view.phase == network::RoomPhase::Match && countdownActive && fired) {
            for (auto& [id, actor] : actors) {
                (void)id;
                if (actor->motion == Motion::Idle || actor->motion == Motion::Land) {
                    actor->state.visible = false;
                    actor->motion = Motion::Launched;
                }
            }
        }

        for (auto it = actors.begin(); it != actors.end();) {
            if(bossMode)it->second->state.team=0;
            updateMotion(*it->second, dt);
            if (it->second->dead) it = actors.erase(it); else ++it;
        }
        setProgress = std::min(1.0, setProgress + dt / kLobbyFade);
        snapshot.clear();
        snapshot.reserve(actors.size());
        for (const auto& [id, actor] : actors) { (void)id; if (!actor->dead) snapshot.push_back(actor->state); }
        std::sort(snapshot.begin(), snapshot.end(), [](const auto& a, const auto& b) {
            if (a.mark != b.mark) return a.mark < b.mark;
            return a.id < b.id;
        });
    }
};

LobbyPresentation::LobbyPresentation(const LobbyLayout& layout) : impl_(std::make_unique<Impl>(layout)) {}
LobbyPresentation::~LobbyPresentation() = default;
void LobbyPresentation::Update(const network::RoomView& view, const PlayerProfile& profile, double dt) { impl_->update(view, profile, dt); }
const std::vector<LobbyActorState>& LobbyPresentation::actors() const { return impl_->snapshot; }
std::vector<LobbyPresentationEvent> LobbyPresentation::takeEvents() {
    auto events = std::move(impl_->events);
    impl_->events.clear();
    return events;
}
const LobbyCamera& LobbyPresentation::camera() const { return *impl_->activeCamera; }
bool LobbyPresentation::hubMode() const { return impl_->hub; }
int LobbyPresentation::countdown() const {
    if (!impl_->countdownActive) return -1;
    const double t = impl_->launchTime;
    if (t < 0.35) return -1;
    if (t < 1.20) return 3;
    if (t < 2.05) return 2;
    if (t < 2.90) return 1;
    return 0;
}
bool LobbyPresentation::launching() const { return impl_->countdownActive && !impl_->handed; }
bool LobbyPresentation::launchComplete() const { return impl_->countdownActive && impl_->fired && impl_->handed; }

} // namespace restoration
