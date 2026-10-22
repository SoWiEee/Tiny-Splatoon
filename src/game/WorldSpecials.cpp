#include "WorldSpecials.hpp"

#include "ArsenalData.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "SpecialPropData.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;
double horizontalLength(const glm::dvec3& v) { return std::hypot(v.x, v.z); }
glm::dvec3 specialAim(const PlayerSimulation& actor) {
    const double pitch = actor.input.aimPitch;
    const double cp = std::cos(pitch);
    return {std::sin(actor.input.aimYaw) * cp, std::sin(pitch), std::cos(actor.input.aimYaw) * cp};
}
bool supportedSpecial(std::string_view id) {
    return id == "slam" || id == "storm" || id == "bubbler" || id == "zooka"
        || id == "barrage" || id == "barrage_sticky" || id == "barrage_burst"
        || id == "barrage_seeker" || id == "barrage_mist" || id == "sonar" || id == "wail" || id == "kraken"
        || id == "strike" || id == "blower" || id == "jetpack" || id == "stamp" || id == "booyah" || id == "zipcaster" || id == "crab";
}
bool hasTransformEndingCue(std::string_view id) {
    return id=="jetpack"||id=="crab"||id=="kraken"||id=="stamp"||id=="zipcaster"
        ||id=="zooka"||id=="blower"||id=="bubbler";
}
struct WorldTickGuard {
    explicit WorldTickGuard(bool& active) : active_(active), previous_(active) { active_ = true; }
    ~WorldTickGuard() { active_ = previous_; }
    bool& active_;
    bool previous_;
};
void emitSpecialAudio(CombatWorld& world, std::string_view source, glm::dvec3 pos, int team,
                      double charge = 0.0, std::uint64_t id = 0) {
    world.emitVisual({"special_audio", pos, {0.0, 1.0, 0.0}, 0.0, team, charge, 0.08, 0.0, std::string(source), id});
}
}

WorldSpecials::WorldSpecials(CombatWorld& world) : world_(world) {}

bool WorldSpecials::activate(PlayerSimulation& actor, std::string_view id) {
    if (!actor.state.alive) return false;
    const auto& def = GetEquipment(EquipmentCategory::Special, id); // Reject unknown IDs through the locked catalog.
    if (!supportedSpecial(id)) return false;
    if (id != actor.specialId_ || actors_.find(&actor) != actors_.end()) return false;
    const double cost = actor.weapon().number("specialCost", 190.0);
    if (actor.state.specialCharge < cost) return false;
    ActorSpecial special;
    special.id = std::string(id);
    special.duration = def.number("duration", 0.0);

    // Actor._startSpecial's common state transition happens only after the request has passed validation.
    actor.state.specialCharge = 0.0;
    actor.state.ink = 100.0;
    actor.state.squid = false;
    actor.state.submerged = false;
    actor.state.climbing = false;
    actor.runner_.cancelDodge();

    if (id == "slam") {
        actor.triggerPose({"special_leap"});
        special.body = true;
        special.armor = true;
        special.phase = "rise";
        actor.state.vel.x *= 0.3;
        actor.state.vel.y = 11.5;
        actor.state.vel.z *= 0.3;
        actor.state.grounded = false;
    } else if (id == "storm") {
        actor.triggerPose({"throw"});
        special.body = true;
        special.thrown = true;
        special.phase = "throw";
        WorldObject bomb;
        bomb.owner = &actor;
        bomb.kind = "stormBomb";
        bomb.pos = actor.state.pos + glm::dvec3(0.0, 1.45, 0.0);
        const double pitch = std::clamp(actor.input.aimPitch + 0.28, -0.3, 1.1);
        const double cp = std::cos(pitch);
        bomb.vel = {std::sin(actor.input.aimYaw) * cp * def.number("throwSpeed") + actor.state.vel.x * 0.4,
                    std::sin(pitch) * def.number("throwSpeed") + 1.5,
                    std::cos(actor.input.aimYaw) * cp * def.number("throwSpeed") + actor.state.vel.z * 0.4};
        bomb.direction = glm::dvec3(bomb.vel.x, 0.0, bomb.vel.z);
        if (horizontalLength(bomb.direction) > 1e-8) bomb.direction = glm::normalize(bomb.direction);
        else bomb.direction = {0.0, 0.0, 1.0};
        objects_.push_back(std::move(bomb));
    } else if (id == "bubbler") {
        special.bubblerShield = true;
        special.phase = "shield";
        actor.state.shield = special.duration;
        shieldSources_[&actor] = &actor;
    } else if (id == "zooka") {
        actor.runner_.reset();
        special.phase = "ready";
        special.cooldown = 0.25;
        special.noSquid = true;
        special.aimFace = true;
        special.moveSpeed = 4.2;
    } else if (id == "sonar") {
        special.body = true;
        special.casting = true;
        special.duration = def.number("cast", 0.6);
        special.phase = "cast";
        for (auto* enemy : world_.actors_) {
            if (!enemy || enemy->team() == actor.team()) continue;
            enemy->state.reveal = def.number("duration", 8.0);
            enemy->state.revealTeam = actor.team();
            enemy->state.track = def.number("duration", 8.0);
            enemy->state.trackTeam = actor.team();
        }
    } else if (id == "wail") {
        special.noSquid = true;
        special.aimFace = true;
        special.moveSpeed = def.number("holdSpeed", 3.2);
        special.phase = "hold";
        if (actor.botControlled()) special.autoPlacementTime = 0.45 + actor.random() * 0.5;
    } else if (id == "barrage" || id == "barrage_sticky" || id == "barrage_burst"
               || id == "barrage_seeker" || id == "barrage_mist") {
        special.phase = "barrage";
    } else if (id == "strike") {
        const double yaw = actor.input.aimYaw;
        const auto bounds = world_.stage_.bounds;
        special.body = true;
        special.mapTarget = true;
        special.phase = "aim";
        special.duration = 0.0;
        special.target = {std::clamp(actor.state.pos.x + std::sin(yaw) * 16.0, bounds.x + 2.0, bounds.y - 2.0), 0.0,
                          std::clamp(actor.state.pos.z + std::cos(yaw) * 16.0, bounds.z + 2.0, bounds.w - 2.0)};
        if (actor.botControlled()) {
            PlayerSimulation* best = nullptr;
            int bestCluster = 0;
            for (auto* enemy : world_.actors_) {
                if (!enemy || enemy->team() == actor.team() || !enemy->state.alive) continue;
                int cluster = 1;
                for (auto* other : world_.actors_)
                    if (other && other != enemy && other->team() == enemy->team() && other->state.alive
                        && glm::length(other->state.pos - enemy->state.pos) < 5.0) ++cluster;
                if (cluster > bestCluster) { bestCluster = cluster; best = enemy; }
            }
            if (!best || actor.random() < 0.35) {
                double bestScore = -1.0;
                for (int i = 0; i < 24; ++i) {
                    const double x = bounds.x + 3.0 + actor.random() * std::max(0.0, bounds.y - bounds.x - 6.0);
                    const double z = bounds.z + 3.0 + actor.random() * std::max(0.0, bounds.w - bounds.z - 6.0);
                    const double y = world_.geometry_.groundHeight(x, z, 30.0);
                    if (!std::isfinite(y)) continue;
                    const double score = actor.random();
                    if (score > bestScore) { bestScore = score; special.target = {x, 0.0, z}; }
                }
            } else special.target = glm::dvec3(best->state.pos.x, 0.0, best->state.pos.z);
            special.autoPlacementTime = 0.7 + actor.random() * 0.6;
        }
    } else if (id == "kraken") {
        actor.runner_.reset();
        special.noSquid = true;
        special.inkProof = true;
        special.faceMove = true;
        special.moveSpeed = def.number("speed");
        special.accel = 45.0;
        special.phase = "move";
    } else if (id == "blower") {
        actor.runner_.reset();
        special.noSquid = true;
        special.aimFace = true;
        special.phase = "inflate";
    } else if (id == "jetpack") {
        actor.runner_.reset();
        special.body = true;
        special.jumpBack = true;
        special.origin = actor.state.pos;
        special.phase = "hover";
        special.cooldown = 0.3;
        special.jumpWas = true;
        actor.state.grounded = false;
        actor.state.vel.y = 7.0;
    } else if (id == "stamp") {
        actor.runner_.reset();
        special.noSquid = true;
        special.moveSpeed = def.number("moveSpeed");
        special.hullYaw = actor.state.yaw;
        special.faceYaw = special.hullYaw;
        special.hasFaceYaw = true;
        special.phase = "stamp";
    } else if (id == "booyah") {
        special.noSquid = true;
        special.aimFace = true;
        special.moveSpeed = def.number("moveSpeed");
        special.phase = "charge";
    } else if (id == "zipcaster") {
        special.jumpBack = true;
        special.origin = actor.state.pos;
        special.phase = "ready";
    } else if (id == "crab") {
        special.noSquid = true;
        special.inkProof = true;
        special.moveSpeed = def.number("speed");
        special.hitPoints = def.number("hp");
        special.boostCooldown = 0.4;
        special.hullYaw = actor.state.yaw;
        special.faceYaw = special.hullYaw;
        special.hasFaceYaw = true;
        special.phase = "drive";
        special.hitRadius = 1.0;
        special.hitHeight = 2.2;
    }

    actors_.emplace(&actor, std::move(special));
    if (id == "sonar") emitSpecialAudio(world_, "sonar_ping", actor.state.pos, actor.team());
    return true;
}

void WorldSpecials::cheer(PlayerSimulation& actor) {
    const auto previous = cheerTimes_.find(&actor);
    if (!actor.state.alive || world_.time_ - (previous != cheerTimes_.end() ? previous->second : -9.0) < 0.4) return;
    cheerTimes_[&actor] = world_.time_;
    emitSpecialAudio(world_, "booyah_cheer", actor.state.pos, actor.team());
    const auto& def = GetEquipment(EquipmentCategory::Special, "booyah");
    bool helped = false;
    for (auto& [teammate, special] : actors_) {
        if (teammate && teammate->state.alive && teammate->team() == actor.team()
            && special.id == "booyah" && !special.thrown) {
            special.charge = std::min(1.0, special.charge + def.number("cheer"));
            helped = true;
        }
    }
    if (helped && actors_.find(&actor) == actors_.end()) {
        const double cost = actor.weapon().number("specialCost", 190.0);
        actor.state.specialCharge = std::min(cost, actor.state.specialCharge + def.number("cheerSpecial"));
    }
}

void WorldSpecials::throwBarrage(PlayerSimulation& actor, ActorSpecial& special) {
    const auto& def = GetEquipment(EquipmentCategory::Special, special.id);
    const std::string kind(def.text("bomb"));
    const auto& sub = GetEquipment(EquipmentCategory::Sub, kind);
    CombatDevice device;
    device.id = world_.allocateId();
    device.owner = &actor;
    device.team = actor.team();
    device.kind = kind;
    device.special = true;
    device.state = "fly";
    device.pos = actor.state.pos + glm::dvec3(0.0, 1.35, 0.0);
    const double pitch = std::clamp(actor.input.aimPitch + 0.28, -0.3, 1.1);
    const double cp = std::cos(pitch);
    device.vel = {std::sin(actor.input.aimYaw) * cp * sub.number("throwSpeed") + actor.state.vel.x * 0.4,
                  std::sin(pitch) * sub.number("throwSpeed") + 1.5,
                  std::cos(actor.input.aimYaw) * cp * sub.number("throwSpeed") + actor.state.vel.z * 0.4};
    device.radius = sub.number("radius", 0.0);
    device.hp = sub.number("hp", 0.0);
    device.shootable = device.hp > 0.0;
    device.timer = sub.number("life", sub.number("fuse", -1.0));
    if (kind == "bomb") device.fuse = -1.0;
    else if (kind == "sticky") device.fuse = sub.number("fuse");
    else if (kind == "burst") device.timer = -1.0;
    else if (kind == "seeker") {
        device.senseRadius = sub.number("seekRange");
        device.trigger = sub.number("triggerDist");
        device.speed = sub.number("speed");
        device.commitDist = sub.number("commitDist");
        device.creep = sub.number("creep");
    } else if (kind == "mist") device.fuse = sub.number("fuse");
    world_.devices_.push_back(std::move(device));
    emitSpecialAudio(world_, "bomb_throw", actor.state.pos, actor.team(), kind == "burst" ? 1.2 : 1.0);
    special.cooldown = def.number("gap");
}

bool WorldSpecials::body(PlayerSimulation& actor, double dt) {
    if(!bodyStep(actor,dt)) return false;
    const auto active=actors_.find(&actor);
    if(active!=actors_.end() && active->second.duration>0 && active->second.elapsed>=active->second.duration
       && active->second.id!="slam" && active->second.id!="storm") {
        if(actor.state.alive)emitSpecialAudio(world_,"special_end",actor.state.pos,actor.team());
        const bool jumpBack=active->second.jumpBack;const auto origin=active->second.origin;
        actors_.erase(active);
        if(jumpBack && actor.state.alive && glm::length(actor.state.pos-origin)>2.5) actor.superJump(origin,true,true);
    }
    if(actor.state.alive && actor.state.pos.y<PlayerTuning().number("fallDeathY")
       && !std::isfinite(world_.geometry_.groundHeight(actor.state.pos.x,actor.state.pos.z,actor.state.pos.y+0.6))) actor.splat();
    return true;
}

bool WorldSpecials::bodyStep(PlayerSimulation& actor, double dt) {
    auto found = actors_.find(&actor);
    if (found == actors_.end() || !found->second.body) return false;
    auto& special = found->second;
    special.elapsed += dt;
    special.firing = std::max(0.0, special.firing-dt);
    if(!special.endingCued&&special.duration>0&&hasTransformEndingCue(special.id)
       &&special.elapsed>=special.duration-2.0) {
        special.endingCued=true;emitSpecialAudio(world_,"special_ending",actor.state.pos,actor.team());
    }
    if (special.id == "strike") {
        actor.state.vel.x *= std::exp(-8.0 * dt);
        actor.state.vel.z *= std::exp(-8.0 * dt);
        if (!actor.state.grounded)
            actor.state.vel.y = std::max(-PlayerTuning().number("maxFall", 40.0),
                actor.state.vel.y - PlayerTuning().number("gravity", 25.0) * dt);
        else actor.state.vel.y = 0.0;
        const double prevY = actor.state.pos.y;
        actor.state.pos += actor.state.vel * dt;
        actor.resolveMotion(prevY, actor.state.grounded);
        if (actor.input.hasMapTarget) {
            const auto bounds = world_.stage_.bounds;
            special.target.x = std::clamp(actor.input.mapTarget.x, bounds.x + 2.0, bounds.y - 2.0);
            special.target.z = std::clamp(actor.input.mapTarget.z, bounds.z + 2.0, bounds.w - 2.0);
        }
        if ((actor.input.specialConfirm && special.elapsed > 0.25)
            || (special.autoPlacementTime >= 0.0 && special.elapsed >= special.autoPlacementTime)
            || special.elapsed >= GetEquipment(EquipmentCategory::Special, "strike").number("aimTime")) {
            actor.triggerPose({"throw"});
            const auto ground = world_.geometry_.raycast({special.target.x, 40.0, special.target.z},
                {0.0, -1.0, 0.0}, 80.0, true);
            WorldObject missile;
            missile.owner = &actor;
            missile.kind = "strikeMissile";
            missile.pos = actor.state.pos + glm::dvec3(0.0, 1.6, 0.0);
            missile.target = ground.hit ? ground.point : glm::dvec3(special.target.x, 0.0, special.target.z);
            missile.timer = GetEquipment(EquipmentCategory::Special, "strike").number("flight");
            missile.radius = GetEquipment(EquipmentCategory::Special, "strike").number("radius");
            objects_.push_back(std::move(missile));
            actors_.erase(found);
        }
        return true;
    }
    if (special.id == "jetpack") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "jetpack");
        const auto& tuning = PlayerTuning();
        const glm::dvec3 move(actor.input.move.x, 0.0, actor.input.move.z);
        const double tvx = move.x * def.number("speed"), tvz = move.z * def.number("speed");
        const double ex = tvx - actor.state.vel.x, ez = tvz - actor.state.vel.z;
        const double el = std::hypot(ex, ez), step = def.number("accel") * dt;
        if (el <= step) { actor.state.vel.x = tvx; actor.state.vel.z = tvz; }
        else { actor.state.vel.x += ex / el * step; actor.state.vel.z += ez / el * step; }
        const double gy = world_.geometry_.groundHeight(actor.state.pos.x, actor.state.pos.z, actor.state.pos.y + 0.5);
        const double floor = !std::isfinite(gy) ? special.origin.y : std::max(gy, special.origin.y - 1.0);
        const double wantY = floor + def.number("height") + std::sin(special.elapsed * 2.2) * 0.12;
        special.boostCooldown -= dt;
        special.boostTime -= dt;
        if (actor.input.jump && !special.jumpWas && special.boostCooldown <= 0.0) {
            special.boostCooldown = def.number("boostGap");
            special.boostTime = 0.35;
            actor.state.vel.y = def.number("boost");
            emitSpecialAudio(world_, "zip_pull", actor.state.pos, actor.team());
        }
        special.jumpWas = actor.input.jump;
        if (special.boostTime <= 0.0) {
            const double vy = std::clamp((wantY - actor.state.pos.y) * 3.0, -5.0, 6.0);
            actor.state.vel.y += (vy - actor.state.vel.y) * std::min(1.0, dt * (actor.state.pos.y > wantY + 1.0 ? 2.5 : 6.0));
        } else actor.state.vel.y -= tuning.number("gravity", 25.0) * 0.35 * dt;
        const double prevY = actor.state.pos.y;
        actor.state.pos += actor.state.vel * dt;
        actor.resolveMotion(prevY, false);
        actor.state.grounded = false;
        special.faceYaw = actor.input.aimYaw;
        special.hasFaceYaw = true;

        special.cooldown -= dt;
        if (actor.input.fire && special.cooldown <= 0.0) {
            special.cooldown = def.number("interval");
            special.firing=0.4; actor.triggerPose({"shoot"});
            const auto aim = specialAim(actor);
            const auto muzzle = actor.state.pos + glm::dvec3(0.0, actor.state.smoothY + 1.05, 0.0) + aim * 0.3;
            auto direction = aim;
            if (actor.input.hasAimPoint) {
                const auto to = actor.input.aimPoint - muzzle;
                const double distance = glm::length(to);
                if (distance >= 2.0 && glm::dot(to, aim) >= 0.0) direction = to / distance;
            }
            WorldObject shot;
            shot.owner = &actor;
            shot.kind = "jetpackShot";
            shot.pos = shot.previous = muzzle;
            shot.vel = direction * def.number("projSpeed");
            shot.timer = def.number("range") / def.number("projSpeed");
            shot.directDamage = def.number("directDamage");
            shot.radius = def.number("splashRadius");
            shot.maxDamage = def.number("splashMax");
            shot.minDamage = def.number("splashMin");
            shot.paintRadius = def.number("paintRadius");
            shot.accumulator=-1.5;
            objects_.push_back(std::move(shot));
        }
        special.trail -= dt;
        if (special.trail <= 0.0) {
            special.trail=0.28;
            const auto ground=world_.geometry_.raycast(actor.state.pos+glm::dvec3(0,0.3,0),{0,-1,0},9.0,true);
            if(ground.hit) world_.paint(actor,ground.point+glm::dvec3(0,0.2,0),1.0,actor.random(),{},true);
        }
        if (special.elapsed >= special.duration) {
            const auto origin = special.origin;
            if (actor.state.alive) {
                emitSpecialAudio(world_, "jet_end", actor.state.pos, actor.team());
                emitSpecialAudio(world_, "special_end", actor.state.pos, actor.team());
            }
            actors_.erase(found);
            if (actor.state.alive && glm::length(actor.state.pos - origin) > 2.5) actor.superJump(origin, true, true);
        }
        return true;
    }
    if (special.id == "zipcaster") {
        if (special.phase != "zip") return false;
        const auto& def = GetEquipment(EquipmentCategory::Special, "zipcaster");
        special.zipElapsed += dt;
        const glm::dvec3 to = special.target - actor.state.pos;
        const double distance = glm::length(to), step = def.number("speed") * dt;
        for (auto* enemy : world_.actors_) {
            if (!enemy || enemy == &actor || enemy->team() == actor.team() || !enemy->state.alive) continue;
            if (horizontalLength(enemy->state.pos - actor.state.pos) < 0.38 * 2.0 + 0.2
                && std::abs(enemy->state.pos.y - actor.state.pos.y) < 1.4) {
                actor.state.vel = glm::dvec3(0.0);
                special.phase = "hang";
                special.body = false;
                special.target = actor.state.pos;
                special.hang = 0.0;
                special.hasFaceYaw = false;
                zipImpact(actor, special, enemy);
                return true;
            }
        }
        if (distance <= step || special.zipElapsed > 1.6 || special.stuck > 0.15) {
            actor.state.vel = glm::dvec3(0.0);
            if (distance <= step) actor.state.pos = special.target;
            special.body = false;
            special.phase = "hang";
            special.hang = std::abs(special.zipNormal.y) < 0.55 ? def.number("hang") : 0.0;
            special.hasFaceYaw = false;
            if (special.hang > 0.0) actor.state.grounded = false;
            zipImpact(actor, special, nullptr);
            return true;
        }
        actor.state.vel = to * (def.number("speed") / distance);
        const auto before = actor.state.pos;
        const double prevY = actor.state.pos.y;
        actor.state.pos += actor.state.vel * dt;
        actor.resolveMotion(prevY, false);
        if (glm::length(actor.state.pos - before) < step * 0.3) special.stuck += dt;
        else special.stuck = 0.0;
        special.faceYaw = std::atan2(actor.state.vel.x, actor.state.vel.z);
        special.hasFaceYaw = true;
        return true;
    }
    if (special.id == "storm") {
        if (special.elapsed > 0.35) {
            special.phase = "ended";
            actors_.erase(found);
        }
        actor.state.vel.x *= std::exp(-6.0 * dt);
        actor.state.vel.z *= std::exp(-6.0 * dt);
        const bool stick = actor.state.grounded;
        if (!stick) actor.state.vel.y -= PlayerTuning().number("gravity", 25.0) * dt;
        else actor.state.vel.y = 0.0;
        const double prevY = actor.state.pos.y;
        actor.state.pos += actor.state.vel * dt;
        actor.resolveMotion(prevY, stick);
        return true;
    }
    if (special.id == "sonar") {
        actor.state.vel.x *= std::exp(-8.0 * dt);
        actor.state.vel.z *= std::exp(-8.0 * dt);
        if (!actor.state.grounded)
            actor.state.vel.y = std::max(-PlayerTuning().number("maxFall", 40.0),
                actor.state.vel.y - PlayerTuning().number("gravity", 25.0) * dt);
        else actor.state.vel.y = 0.0;
        const double prevY = actor.state.pos.y;
        actor.state.pos += actor.state.vel * dt;
        actor.resolveMotion(prevY, actor.state.grounded);
        if (special.elapsed >= special.duration) {
            special.phase = "ended";
            actors_.erase(found);
        }
        return true;
    }

    const auto& def = GetEquipment(EquipmentCategory::Special, "slam");
    if (special.phase == "rise") {
        actor.state.vel.y -= PlayerTuning().number("gravity", 25.0) * 0.9 * dt;
        actor.state.vel.x = actor.state.vel.x * std::exp(-6.0 * dt) + actor.input.move.x * 2.5 * (1.0 - std::exp(-6.0 * dt));
        actor.state.vel.z = actor.state.vel.z * std::exp(-6.0 * dt) + actor.input.move.z * 2.5 * (1.0 - std::exp(-6.0 * dt));
        if (special.elapsed > def.number("rise")) {
            special.phase = "hang";
            special.elapsed = 0.0;
            actor.state.vel = {0.0, 0.6, 0.0};
        }
    } else if (special.phase == "hang") {
        actor.state.vel.y = 0.4;
        if (special.elapsed > def.number("hang")) {
            special.phase = "fall";
            special.elapsed = 0.0;
            actor.state.vel = {0.0, -34.0, 0.0};
            actor.triggerPose({"special_slam"});
        }
    } else if (special.phase == "fall") {
        actor.state.vel.y = -34.0;
    }

    const double prevY = actor.state.pos.y;
    actor.state.pos += actor.state.vel * dt;
    actor.state.grounded = false;
    actor.resolveMotion(prevY, false);
    if (special.phase == "fall" && (actor.state.grounded || special.elapsed > 1.2)) {
        slamImpact(actor, def);
        actor.state.invuln = 0.3;
        actors_.erase(&actor);
    }
    return true;
}

void WorldSpecials::slamImpact(PlayerSimulation& actor, const EquipmentData& def) {
    const auto center = actor.state.pos;
    const double radius = def.number("radius");
    world_.emitVisual({"slamWave",center,{0,1,0},radius,actor.team()});
    world_.paint(actor, center + glm::dvec3(0.0, 0.3, 0.0), radius * 0.72, actor.random(), {}, true);
    for (int i = 0; i < 9; ++i) {
        const double angleJitter = actor.random();
        const double angle = (static_cast<double>(i) / 9.0) * 2.0 * pi + angleJitter * 0.3;
        const double radiusRoll = actor.random();
        const double r = radius * (0.55 + radiusRoll * 0.3);
        const glm::dvec3 at(center.x + std::cos(angle) * r, center.y + 0.6, center.z + std::sin(angle) * r);
        const double paintRadius = 1.1 + actor.random() * 0.6;
        const double paintSeed = actor.random();
        world_.paint(actor, at, paintRadius, paintSeed, {}, true);
    }
    for (auto* victim : world_.actors_) {
        if (!victim || victim == &actor || victim->team() == actor.team() || !victim->state.alive) continue;
        const double distance = glm::length(victim->state.pos - center);
        if (distance > radius) continue;
        const double killRadius = def.number("killRadius");
        const double damage = distance < killRadius ? def.number("damageMax")
            : def.number("damageMin") + (def.number("damageMax") - def.number("damageMin")) * 0.3
                * (1.0 - (distance - killRadius) / (radius - killRadius));
        const auto from = center + glm::dvec3(0.0, 0.8, 0.0);
        const auto to = victim->state.pos + glm::dvec3(0.0, 0.8, 0.0);
        if (world_.geometry_.los(from, to)) world_.applyHit(actor, *victim, damage, "slam");
    }
}

void WorldSpecials::move(PlayerSimulation& actor, double dt) {
    auto found = actors_.find(&actor);
    if (found == actors_.end() || found->second.id != "stamp") return;
    auto& special = found->second;
    const auto& def = GetEquipment(EquipmentCategory::Special, "stamp");
    const double speed = horizontalLength(actor.state.vel);
    const double rate = (speed > 1.5 ? def.number("turnRate") : def.number("turnRateStill")) * dt;
    const double turn = std::remainder(actor.input.aimYaw - special.hullYaw, 2.0 * pi);
    special.hullYaw += std::clamp(turn, -rate, rate);
    special.faceYaw = special.hullYaw;
    special.hasFaceYaw = true;
    const glm::dvec3 forward(std::sin(special.hullYaw), 0.0, std::cos(special.hullYaw));
    if(special.hasMoveOut && actor.input.move==special.moveOut) actor.input.move=special.moveIn;
    special.moveIn=actor.input.move;
    const double along = actor.input.move.x * forward.x + actor.input.move.z * forward.z;
    actor.input.move.x = forward.x * along + (actor.input.move.x - forward.x * along) * def.number("strafe");
    actor.input.move.z = forward.z * along + (actor.input.move.z - forward.z * along) * def.number("strafe");
    special.moveOut=actor.input.move; special.hasMoveOut=true;
}

bool WorldSpecials::weapon(PlayerSimulation& actor, double dt, bool fire, bool firePressed,
                           bool& sub, bool& subReleased) {
    auto found = actors_.find(&actor);
    if (found == actors_.end()) return false;
    auto& special = found->second;
    if (special.id == "barrage" || special.id == "barrage_sticky" || special.id == "barrage_burst"
        || special.id == "barrage_seeker" || special.id == "barrage_mist") {
        special.cooldown -= dt;
        if (subReleased && special.cooldown <= 0.0) throwBarrage(actor, special);
        if (subReleased) { sub = false; subReleased = false; }
        return false;
    }
    if (special.id == "wail") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "wail");
        if ((firePressed && special.elapsed > def.number("fireMin", 0.2))
            || (special.autoPlacementTime >= 0.0 && special.elapsed >= special.autoPlacementTime)
            || special.elapsed >= def.number("holdTime", 6.0)) {
            WorldObject speaker;
            speaker.owner = &actor;
            speaker.id = world_.allocateId();
            speaker.kind = "speaker";
            speaker.phase = "charge";
            const double yaw = actor.input.aimYaw;
            const glm::dvec3 forward(std::sin(yaw), 0.0, std::cos(yaw));
            const auto at = actor.state.pos + forward * 1.3;
            const auto ground = world_.geometry_.raycast(
                glm::dvec3(at.x, actor.state.pos.y + 1.2, at.z), {0.0, -1.0, 0.0}, 4.0, true);
            speaker.pos = ground.hit && ground.normal.y > 0.6 ? ground.point : actor.state.pos;
            const double pitch = std::clamp(actor.input.aimPitch, -0.3, 0.3);
            const double cp = std::cos(pitch);
            speaker.direction = {std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
            // Source speaker prop defines its beam mouth at local (0, 0.7, 0.509).
            speaker.mouth = speaker.pos + glm::dvec3(forward.x * 0.509, 0.7, forward.z * 0.509);
            speaker.radius = def.number("radius", 1.5);
            speaker.range = def.number("range", 72.0);
            speaker.damagePerSecond = def.number("dps", 260.0);
            objects_.push_back(std::move(speaker));
            special.phase = "placed";
            actors_.erase(found);
        }
        return true;
    }
    if (special.id == "kraken") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "kraken");
        special.cooldown -= dt;
        if (firePressed && special.cooldown <= 0.0) {
            special.cooldown = def.number("cooldown");
            special.attack = true;
            if (actor.state.grounded) {
                const glm::dvec3 forward(std::sin(actor.input.aimYaw), 0.0, std::cos(actor.input.aimYaw));
                actor.state.vel.x += forward.x * def.number("attackFwd") * 0.5;
                actor.state.vel.z += forward.z * def.number("attackFwd") * 0.5;
                actor.state.vel.y = def.number("attackVel");
                actor.state.grounded = false;
            } else actor.state.vel.y = std::min(actor.state.vel.y, -16.0);
        }
        return true;
    }
    if (special.id == "blower") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "blower");
        if (!fire) special.needRelease = false;
        auto bubble = std::find_if(objects_.begin(), objects_.end(), [&](const WorldObject& object) {
            return object.live && object.kind == "bubble" && object.owner == &actor && object.held;
        });
        if (fire && bubble == objects_.end() && special.bubbleCount < static_cast<int>(def.number("max"))
            && special.phase != "done" && !special.needRelease) {
            WorldObject created;
            created.owner = &actor;
            created.kind = "bubble";
            created.held = true;
            created.radius = def.number("rMin");
            created.timer = def.number("life");
            created.accumulator = actor.random() * 6.0;
            objects_.push_back(std::move(created));
            special.cooldown = 0.0;
            special.phase = "inflate";
            bubble = std::prev(objects_.end());
        }
        if (bubble != objects_.end()) {
            special.firing=0.3;
            special.cooldown += dt;
            const double k = std::clamp(special.cooldown / def.number("inflate"), 0.0, 1.0);
            bubble->radius = def.number("rMin") + (def.number("rMax") - def.number("rMin")) * (1.0 - (1.0 - k) * (1.0 - k));
            const auto forward = glm::dvec3(std::sin(actor.input.aimYaw), 0.0, std::cos(actor.input.aimYaw));
            const auto aim = specialAim(actor);
            const auto muzzle = actor.state.pos + glm::dvec3(0.0, actor.state.smoothY + 1.05, 0.0) + aim * 0.3;
            bubble->pos = muzzle + forward * (bubble->radius + 0.2);
            bubble->pos.y = std::max(bubble->pos.y, actor.state.pos.y + bubble->radius * 0.7);
            if (!fire || special.cooldown > def.number("inflate") + 0.6) {
                bubble->held = false;
                bubble->vel = forward * def.number("drift");
                bubble->vel.y = 0.25;
                if (fire) special.needRelease = true;
                special.cooldown = 0.0;
                ++special.bubbleCount;
                if (special.bubbleCount >= static_cast<int>(def.number("max"))) {
                    special.phase = "done";
                    special.duration = special.elapsed + 0.6;
                }
            }
        }
        return true;
    }
    if (special.id == "stamp") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "stamp");
        special.cooldown -= dt;
        const bool subPressed = sub && !special.subWas;
        special.subWas = sub;
        if (subPressed) {
            actor.triggerPose({"throw"});
            const auto aim = specialAim(actor);
            const auto from = actor.state.pos + glm::dvec3(0.0, 1.5, 0.0);
            auto direction = aim;
            if (actor.input.hasAimPoint) {
                const auto to = actor.input.aimPoint - from;
                const double distance = glm::length(to);
                if (distance >= 2.0 && glm::dot(to, aim) >= 0.0) direction = to / distance;
            }
            WorldObject thrown;
            thrown.owner = &actor;
            thrown.kind = "thrownStamp";
            thrown.pos = thrown.previous = from;
            thrown.vel = direction * def.number("throwSpeed");
            thrown.vel.y += 2.5;
            thrown.timer = 3.0;
            objects_.push_back(std::move(thrown));
            actors_.erase(found);
            sub = false;
            subReleased = false;
            return true;
        }
        if (fire && special.cooldown <= 0.0 && special.flipElapsed < 0.0) {
            if (!actor.state.grounded) {
                special.flipElapsed = 0.0;
                special.flipBack=special.flipFront=false;
                CharacterTrigger event{"roll"};event.direction={std::sin(special.hullYaw),0,std::cos(special.hullYaw)};
                event.duration=def.number("flipTime"); actor.triggerPose(event);
                special.guard = def.number("flipTime");
                special.cooldown = def.number("flipTime") + 0.1;
            } else {
                actor.triggerPose({"flick"});
                special.pending = 0.15;
                special.cooldown = def.number("interval");
                special.guard = def.number("guardTime");
            }
            special.firing=0.5;
        }
        if (special.pending >= 0.0) {
            special.pending -= dt;
            if (special.pending < 0.0) {
                stampImpact(actor, special, def.number("radius"), def.number("reach"));
                const glm::dvec3 forward(std::sin(special.hullYaw), 0.0, std::cos(special.hullYaw));
                actor.state.vel.x += forward.x * def.number("lunge");
                actor.state.vel.z += forward.z * def.number("lunge");
            }
        }
        sub = false;
        subReleased = false;
        return true;
    }
    if (special.id == "booyah") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "booyah");
        special.charge = std::min(1.0, special.charge + dt / def.number("charge"));
        if (special.charge >= 1.0) {
            special.fullTime += dt;
            if ((firePressed && special.fullTime > 0.05) || special.fullTime > def.number("autoThrow")) {
                const auto from = actor.state.pos + glm::dvec3(0.0, 1.35, 0.0);
                const double pitch=std::clamp(actor.input.aimPitch+0.28,-0.3,1.1),speed=def.number("throwSpeed");
                const glm::dvec3 velocity(std::sin(actor.input.aimYaw)*std::cos(pitch)*speed+actor.state.vel.x*0.4,
                    std::sin(pitch)*speed+1.5,std::cos(actor.input.aimYaw)*std::cos(pitch)*speed+actor.state.vel.z*0.4);
                WorldObject orb;
                orb.owner = &actor;
                orb.kind = "orb";
                orb.phase = "fly";
                orb.pos = orb.previous = from;
                orb.vel = velocity;
                orb.timer = 4.0;
                orb.radius = def.number("radius");
                orb.range = def.number("fuse");
                orb.maxDamage = def.number("damageMax");
                orb.minDamage = def.number("damageMin");
                orb.killRadius = def.number("killRadius");
                objects_.push_back(std::move(orb));
                special.thrown = true;
                actor.triggerPose({"throw"});
                actors_.erase(found);
                return true;
            }
        }
        special.raise = true;
        return true;
    }
    if (special.id == "zipcaster") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "zipcaster");
        special.cooldown -= dt;
        const bool subPressed = sub && !special.subWas;
        special.subWas = sub;
        sub = false;
        subReleased = false;
        if (subPressed && special.cooldown <= 0.0 && special.phase != "zip") {
            const auto from = actor.state.pos + glm::dvec3(0.0, 1.2, 0.0);
            auto direction = actor.input.hasAimPoint ? actor.input.aimPoint - from : specialAim(actor);
            const double magnitude = glm::length(direction);
            if (magnitude > 1e-8) direction /= magnitude;
            special.cooldown = def.number("cooldown");
            const auto hit = world_.geometry_.raycast(from, direction, def.number("range"), true);
            if (hit.hit) {
                const bool wall = std::abs(hit.normal.y) < 0.55;
                special.zipNormal = hit.normal;
                special.target = hit.point + hit.normal * (0.38 + 0.08);
                if (wall) special.target.y -= 1.0;
                else special.target.y = hit.point.y;
                special.phase = "zip";
                special.body = true;
                special.stuck = 0.0;
                special.zipElapsed=0.0; special.hang=0.0;
                world_.paint(actor, hit.point + hit.normal * 0.1, 1.0, actor.random(), {}, true);
                emitSpecialAudio(world_, "zip_latch", hit.point, actor.team());
                emitSpecialAudio(world_, "zip_pull", actor.state.pos, actor.team());
            }
        }
        return false;
    }
    if (special.id == "crab") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "crab");
        special.roll = actor.input.squid;
        special.moveSpeed = special.roll ? def.number("rollSpeed") : def.number("speed");
        const double speed = horizontalLength(actor.state.vel);
        const double wanted = special.roll
            ? (speed > 0.8 ? std::atan2(actor.state.vel.x, actor.state.vel.z) : special.hullYaw)
            : actor.input.aimYaw;
        const double turn = std::remainder(wanted - special.hullYaw, 2.0 * pi);
        special.hullYaw += std::clamp(turn, -(special.roll ? 8.0 : def.number("turnRate")) * dt,
                                                   (special.roll ? 8.0 : def.number("turnRate")) * dt);
        special.faceYaw = special.hullYaw;
        special.hasFaceYaw = true;
        if (special.roll) return true;
        special.cooldown -= dt;
        special.boostCooldown -= dt;
        if (fire) {
            special.firing=0.3;
            unsigned guard = 0;
            while (special.cooldown <= 0.0 && guard++ < 3) {
                special.cooldown += def.number("gunInterval");
                const double yawSpread = (actor.random() - 0.5) * 2.0 * def.number("gunSpread") * pi / 180.0;
                const double pitchSpread = (actor.random() - 0.5) * def.number("gunSpread") * pi / 180.0;
                const double pitch = std::clamp(actor.input.aimPitch, -0.4, 0.5);
                glm::dvec3 direction(std::sin(special.hullYaw + yawSpread) * std::cos(pitch),
                                     std::sin(pitch + pitchSpread),
                                     std::cos(special.hullYaw + yawSpread) * std::cos(pitch));
                direction = glm::normalize(direction);
                const double cy=std::cos(special.hullYaw),sy=std::sin(special.hullYaw);
                const auto muzzle=actor.state.pos+glm::dvec3(CrabGunMuzzle.x*cy+CrabGunMuzzle.z*sy,
                    CrabGunMuzzle.y,-CrabGunMuzzle.x*sy+CrabGunMuzzle.z*cy);
                ShooterProjectile shot;
                shot.owner = &actor; shot.weaponId = "crab"; shot.kind = "shot";
                shot.pos = shot.start = muzzle; shot.vel = direction * def.number("gunSpeed");
                shot.life = def.number("gunRange") / def.number("gunSpeed") + 0.6;
                shot.straight = 0.4; shot.gravity = 28.0; shot.drag = 0.8;
                shot.radius = 0.55; shot.damage = shot.damageFar = def.number("gunDamage");
                shot.size = 0.14; shot.trailEvery = 1.6; shot.trailRadius = 0.35; shot.special = true;
                world_.launch(std::move(shot));
            }
        } else if (special.cooldown < 0.0) special.cooldown = 0.0;
        const bool subPressed = sub && !special.subWas;
        special.subWas = sub;
        if (subPressed && special.boostCooldown <= 0.0) {
            special.boostCooldown = def.number("cannonGap");
            const double pitch = std::clamp(actor.input.aimPitch + 0.45, 0.1, 1.0);
            const double cy=std::cos(special.hullYaw),sy=std::sin(special.hullYaw);
            const auto muzzle=actor.state.pos+glm::dvec3(CrabCannonMuzzle.x*cy+CrabCannonMuzzle.z*sy,
                CrabCannonMuzzle.y,-CrabCannonMuzzle.x*sy+CrabCannonMuzzle.z*cy);
            WorldObject shell;
            shell.owner = &actor;
            shell.kind = "crabShell";
            shell.pos = shell.previous = muzzle;
            shell.vel = glm::dvec3(std::sin(special.hullYaw) * std::cos(pitch), std::sin(pitch),
                                   std::cos(special.hullYaw) * std::cos(pitch)) * def.number("cannonSpeed");
            shell.timer = 4.0;
            shell.radius = def.number("cannonRadius");
            shell.maxDamage = def.number("cannonDamageMax");
            shell.minDamage = def.number("cannonDamageMin");
            shell.killRadius = 0.8;
            objects_.push_back(std::move(shell));
        }
        return true;
    }
    if (special.id != "zooka") return false;
    const auto& def = GetEquipment(EquipmentCategory::Special, "zooka");
    special.cooldown -= dt;
    if (fire && special.cooldown <= 0.0) {
        special.cooldown = def.number("interval");
        special.firing=0.45;actor.triggerPose({"shoot"});
        const auto aim = specialAim(actor);
        const auto muzzle = actor.state.pos + glm::dvec3(0.0, actor.state.smoothY + 1.05, 0.0) + aim * 0.3;
        auto direction = aim;
        if (actor.input.hasAimPoint) {
            const auto to = actor.input.aimPoint - muzzle;
            const double distance = glm::length(to);
            if (distance >= 2.0 && glm::dot(to, aim) >= 0.0) direction = to / distance;
        }
        WorldObject shot;
        shot.owner = &actor;
        shot.kind = "zooka";
        shot.pos = muzzle;
        shot.direction = direction;
        shot.vel = direction * def.number("speed");
        shot.timer = def.number("range") / def.number("speed");
        shot.radius = def.number("radius");
        objects_.push_back(std::move(shot));
    }
    return true;
}

bool WorldSpecials::shotHit(glm::dvec3 from, glm::dvec3 to, int team, double damage, PlayerSimulation* owner) {
    WorldTickGuard ticking(tickingWorld_);
    for(auto& object:objects_) {
        if(!object.live || object.kind!="bubble") continue;
        const auto hit=StageGeometry::segmentCapsuleDist(from,to,object.pos-glm::dvec3(0,object.radius*0.5,0),object.radius*0.5,object.radius);
        if(hit.dist<object.radius*0.95) { bubbleHit(object,team,damage,owner); return true; }
    }
    return false;
}

double WorldSpecials::rayHit(glm::dvec3 from, glm::dvec3 direction, double length, int team, double damage, PlayerSimulation* owner) {
    WorldTickGuard ticking(tickingWorld_);
    double best=length; WorldObject* bubble=nullptr;
    for(auto& object:objects_) {
        if(!object.live || object.kind!="bubble") continue;
        const auto delta=object.pos-from; const double t=glm::dot(delta,direction);
        if(t<=0 || t>best) continue;
        const double d2=glm::dot(delta,delta)-t*t;
        if(d2>object.radius*object.radius) continue;
        const double entry=t-std::sqrt(object.radius*object.radius-d2);
        if(entry<best) { best=std::max(0.1,entry); bubble=&object; }
    }
    if(bubble) bubbleHit(*bubble,team,damage,owner);
    return best;
}

void WorldSpecials::areaHit(glm::dvec3 center, double radius, double damage, int team, PlayerSimulation* owner) {
    WorldTickGuard ticking(tickingWorld_);
    for(auto& object:objects_)
        if(object.live && object.kind=="bubble" && glm::length(object.pos-center)<radius+object.radius*0.8)
            bubbleHit(object,team,damage*1.5,owner);
}

void WorldSpecials::bubbleHit(WorldObject& object, int team, double damage, PlayerSimulation* owner) {
    const auto& def=GetEquipment(EquipmentCategory::Special,"blower");
    if(!object.live) return;
    if(team==object.owner->team()) {
        if(object.held) return;
        object.charge+=damage;
        if(object.charge>=def.number("popDamage")) explodeBubble(object,owner);
    } else object.radius=std::max(def.number("rMin")*0.75,object.radius-damage*0.004);
}

void WorldSpecials::tickActor(PlayerSimulation& actor, double dt) {
    auto found = actors_.find(&actor);
    if (found == actors_.end()) return;
    auto& special = found->second;
    special.elapsed += dt;
    special.firing=std::max(0.0,special.firing-dt);
    if(!special.endingCued&&special.duration>0&&hasTransformEndingCue(special.id)
       &&special.elapsed>=special.duration-2.0) {
        special.endingCued=true;emitSpecialAudio(world_,"special_ending",actor.state.pos,actor.team());
    }
    if (special.id == "kraken") {
        const auto& def = GetEquipment(EquipmentCategory::Special, "kraken");
        if (!actor.state.grounded && !special.air && actor.state.vel.y > 3.0 && !special.attack)
            actor.state.vel.y = std::max(actor.state.vel.y, def.number("hopVel"));
        const bool landed = special.air && actor.state.grounded;
        special.air = !actor.state.grounded;
        if (landed && special.attack) {
            special.attack = false;
            const auto center = actor.state.pos;
            world_.paint(actor, center + glm::dvec3(0.0, 0.3, 0.0), def.number("radius") * 0.8, actor.random(), {}, true);
            specialBlast(actor, center + glm::dvec3(0.0, 0.4, 0.0), def.number("radius"),
                         def.number("damage"), def.number("damage") * 0.6, def.number("radius") * 0.7, "kraken");
        }
        if (actor.state.grounded) {
            special.trail += horizontalLength(actor.state.vel) * dt;
            if (special.trail > 0.5) {
                special.trail = 0.0;
                world_.paint(actor, actor.state.pos + glm::dvec3(0.0, 0.2, 0.0), def.number("paintRadius"), actor.random(), {}, true);
            }
        }
    }
    if(special.id=="zipcaster") {
        if(special.phase!="zip") special.hasFaceYaw=false;
        if(special.hang>0) {
            special.hang-=dt;
            if(actor.input.jump || glm::dot(actor.input.move,special.zipNormal)>0.5) {
                special.hang=0; actor.state.vel={special.zipNormal.x*4,8,special.zipNormal.z*4};actor.state.grounded=false;
            }
        }
    } else if(special.id=="stamp") {
        const auto& def=GetEquipment(EquipmentCategory::Special,"stamp");
        if(special.flipElapsed>=0) {
            special.flipElapsed+=dt;
            if(!special.flipBack && special.flipElapsed>=def.number("flipTime")*0.3) {
                special.flipBack=true;stampImpact(actor,special,def.number("flipBackRadius"),-def.number("flipBackReach"));
            }
            if(!special.flipFront && special.flipElapsed>=def.number("flipTime")*0.75) {
                special.flipFront=true;stampImpact(actor,special,def.number("flipRadius"),def.number("flipReach"));
            }
            if(special.flipElapsed>=def.number("flipTime")) special.flipElapsed=-1;
        }
        if(special.guard>0) {
            special.guard-=dt;
            const auto front=[&](glm::dvec3 at) {
                const auto delta=at-actor.state.pos;const double distance=horizontalLength(delta);
                return distance<0.3 || (delta.x*std::sin(special.hullYaw)+delta.z*std::cos(special.hullYaw))/distance>=std::cos(def.number("deflectArc")*pi/180.0);
            };
            world_.projectiles_.erase(std::remove_if(world_.projectiles_.begin(),world_.projectiles_.end(),[&](const ShooterProjectile& p) {
                return p.owner && p.owner->team()!=actor.team() && std::abs(p.pos.y-actor.state.pos.y-1)<=2
                    && horizontalLength(p.pos-actor.state.pos)<=def.number("deflectR") && front(p.pos);
            }),world_.projectiles_.end());
            for(auto& device:world_.devices_) {
                const bool smash=device.kind=="bomb" || device.kind=="sticky" || device.kind=="burst" || device.kind=="seeker" || device.kind=="mist" || device.kind=="mine";
                if(device.live && device.team!=actor.team() && smash && glm::length(device.pos-actor.state.pos-glm::dvec3(0,1,0))<=def.number("bombClearR") && front(device.pos)) device.live=false;
            }
        }
    } else if(special.id=="crab") {
        if(special.hitPoints<=0) {
            emitSpecialAudio(world_, "crab_break", actor.state.pos, actor.team());
            actors_.erase(found);
            return;
        }
        if(special.roll) {
            if(actor.state.grounded && horizontalLength(actor.state.vel)>2) {
                special.trail+=dt;
                if(special.trail>0.08) {special.trail=0;world_.paint(actor,actor.state.pos+glm::dvec3(0,0.2,0),0.9,actor.random(),{},true);}
            }
        } else special.trail+=dt*(1+horizontalLength(actor.state.vel)*2.2);
    }
    if (special.duration > 0.0 && special.elapsed >= special.duration) {
        special.phase = "ended";
        const auto origin=special.origin; const bool jumpBack=special.jumpBack;
        if(actor.state.alive)emitSpecialAudio(world_,"special_end",actor.state.pos,actor.team());
        if (special.id == "crab") emitSpecialAudio(world_, "crab_break", actor.state.pos, actor.team());
        if(special.id=="blower") for(auto& object:objects_) if(object.live && object.owner==&actor && object.kind=="bubble" && object.held) {
            const auto& def=GetEquipment(EquipmentCategory::Special,"blower");
            object.held=false;object.vel={std::sin(actor.input.aimYaw)*def.number("drift"),0.25,std::cos(actor.input.aimYaw)*def.number("drift")};
        }
        actors_.erase(found);
        if(jumpBack && actor.state.alive && glm::length(actor.state.pos-origin)>2.5) actor.superJump(origin,true,true);
    }
}

void WorldSpecials::tickStorm(WorldObject& object, double dt) {
    const auto& def = GetEquipment(EquipmentCategory::Special, "storm");
    if (object.kind == "stormBomb") {
        const glm::dvec3 previous = object.pos;
        object.age += dt;
        object.vel.y -= 24.0 * dt;
        object.pos += object.vel * dt;
        const auto hit = world_.geometry_.segment(previous, object.pos, false);
        const double waterY = PlayerTuning().number("waterY", -20.0);
        if (hit.hit || object.age > 1.1) {
            const auto ground = world_.geometry_.raycast(object.pos + glm::dvec3(0.0, 0.5, 0.0), {0.0, -1.0, 0.0}, 12.0);
            const double groundY = ground.hit ? ground.point.y : object.pos.y;
            object.kind = "stormCloud";
            object.pos.y = groundY + 4.6;
            object.previous = object.pos; // immutable seed origin for the visual-only source cloud layout
            world_.emitVisual({"stormStart",{object.pos.x,groundY,object.pos.z},{0,1,0},def.number("radius"),object.owner->team()});
            object.age = 0.0;
            object.timer = def.number("duration");
            object.accumulator = 0.0;
        } else if (object.pos.y < waterY - 1.8) {
            object.live = false;
            return;
        }
    }
    if (object.kind != "stormCloud") return;

    object.age += dt;
    object.pos += object.direction * (def.number("driftSpeed") * dt);
    if (object.age < object.timer - 0.3) {
        object.accumulator -= dt;
        while (object.accumulator <= 0.0) {
            object.accumulator += 0.045;
            const double angle = object.owner->random() * 2.0 * pi;
            const double r = std::sqrt(object.owner->random()) * def.number("radius");
            const glm::dvec3 rayOrigin(object.pos.x + std::cos(angle) * r, object.pos.y - 0.8, object.pos.z + std::sin(angle) * r);
            const auto ground = world_.geometry_.raycast(rayOrigin, {0.0, -1.0, 0.0}, 12.0);
            if (ground.hit) {
                const double radius = 0.45 + object.owner->random() * 0.35;
                world_.paint(*object.owner, ground.point + ground.normal * 0.1, radius, object.owner->random(), {}, true);
            }
        }
        for (auto* victim : world_.actors_) {
            if (!victim || victim->team() == object.owner->team() || !victim->state.alive) continue;
            const double dx = victim->state.pos.x - object.pos.x, dz = victim->state.pos.z - object.pos.z;
            if (dx * dx + dz * dz > def.number("radius") * def.number("radius") || victim->state.pos.y > object.pos.y) continue;
            const auto from = victim->state.pos + glm::dvec3(0.0, 1.2, 0.0);
            const glm::dvec3 to(victim->state.pos.x, object.pos.y - 0.6, victim->state.pos.z);
            if (!world_.geometry_.los(from, to)) continue;
            world_.applyHit(*object.owner,*victim,def.number("dps") * dt,"storm");
        }
    }
    if (object.age >= object.timer) object.live = false;
}

void WorldSpecials::tickZooka(WorldObject& object, double dt) {
    const auto& def = GetEquipment(EquipmentCategory::Special, "zooka");
    object.age += dt;
    const auto previous = object.pos;
    object.pos += object.vel * dt;
    bool done = false;
    auto burst = [&](const glm::dvec3& at, const glm::dvec3& normal, bool soft) {
        world_.emitVisual({"burst",at,normal,soft?1.1:1.6,object.owner->team()});
        world_.paint(*object.owner, at + normal * 0.15, soft ? 1.1 : 1.6, object.owner->random(), {}, true);
        object.live = false;
        done = true;
    };
    const auto wall = world_.geometry_.segment(previous, object.pos, true);
    if (wall.hit) burst(wall.point, wall.normal, false);
    if (!done && world_.blockShot(previous, object.pos, object.owner->team(), 120.0)) burst(object.pos, {0.0, 1.0, 0.0}, false);
    if (!done) {
        for (auto* victim : world_.actors_) {
            if (!victim || victim->team() == object.owner->team() || !victim->state.alive
                || std::find(object.tagged.begin(), object.tagged.end(), victim) != object.tagged.end()) continue;
            const double top = victim->state.pos.y + (victim->state.squid ? 0.6 : 1.6);
            if (top < object.pos.y - def.number("height") * 0.5 || victim->state.pos.y > object.pos.y + def.number("height") * 0.5) continue;
            const glm::dvec3 horizontalEnd(object.pos.x, previous.y, object.pos.z);
            const glm::dvec3 base(victim->state.pos.x, previous.y - 0.5, victim->state.pos.z);
            const auto distance = StageGeometry::segmentCapsuleDist(previous, horizontalEnd, base, 0.01, 1.0);
            if (distance.dist < def.number("radius") + 0.38) {
                object.tagged.push_back(victim);
                world_.applyHit(*object.owner, *victim, def.number("damage"), "zooka");
            }
        }
    }
    if (done) return;
    object.accumulator += glm::length(object.vel) * dt;
    if (object.accumulator > def.number("paintEvery")) {
        object.accumulator = 0.0;
        const auto ground = world_.geometry_.raycast(object.pos, {0.0, -1.0, 0.0}, 3.5, true);
        if (ground.hit) {
            const double radius = def.number("paintRadius") * (0.85 + object.owner->random() * 0.3);
            world_.paint(*object.owner, ground.point + ground.normal * 0.15, radius, object.owner->random(), {}, true);
        }
    }
    if (object.age > object.timer) burst(object.pos, {0.0, 1.0, 0.0}, true);
}

void WorldSpecials::tickWail(WorldObject& object, double dt) {
    const auto& def = GetEquipment(EquipmentCategory::Special, "wail");
    object.age += dt;
    if (object.phase == "charge") {
        if (object.age >= def.number("charge", 1.3)) {
            object.phase = "blast";
            object.age = 0.0;
            emitSpecialAudio(world_, "wail_blast_start", object.pos, object.owner->team(), 0.0, object.id);
        }
        return;
    }
    if (object.phase == "blast") {
        for (auto* victim : world_.actors_) {
            if (!victim || victim->team() == object.owner->team() || !victim->state.alive) continue;
            const glm::dvec3 to = victim->state.pos + glm::dvec3(0.0, 0.8, 0.0) - object.mouth;
            const double along = glm::dot(to, object.direction);
            if (along < -0.5 || along > object.range) continue;
            const auto perpendicular = to - object.direction * along;
            if (glm::length(perpendicular) < object.radius + 0.45)
                world_.applyHit(*object.owner, *victim, object.damagePerSecond * dt, "wail");
        }
        if (object.age >= def.number("blast", 3.2)) {
            object.phase = "fade";
            object.age = 0.0;
            emitSpecialAudio(world_, "wail_blast_stop", object.pos, object.owner->team(), 0.0, object.id);
        }
        return;
    }
    if (object.phase == "fade" && object.age >= def.number("fade", 0.6)) object.live = false;
}

void WorldSpecials::specialBlast(PlayerSimulation& owner, glm::dvec3 center, double radius, double maxDamage,
                                 double minDamage, double killRadius, std::string_view weaponId) {
    world_.emitVisual({"explosion",center,{0,1,0},radius,owner.team(),0,.08,0,std::string(weaponId)});
    for (auto* victim : world_.actors_) {
        if (!victim || victim->team() == owner.team() || !victim->state.alive) continue;
        const glm::dvec3 target = victim->state.pos + glm::dvec3(0.0, 0.8, 0.0);
        const double distance = glm::length(target - center);
        if (distance > radius + 0.3 || !world_.geometry_.los(center + glm::dvec3(0.0, 0.35, 0.0), target)) continue;
        const double k = distance <= killRadius ? 0.0
            : std::clamp((distance - killRadius) / std::max(0.01, radius - killRadius), 0.0, 1.0);
        world_.applyHit(owner, *victim, maxDamage + (minDamage - maxDamage) * k * k, std::string(weaponId));
    }
    areaHit(center, radius, 60.0, owner.team(), &owner);
    world_.damageDevices(center, radius, 60.0, owner.team());
}

void WorldSpecials::jetpackBlast(WorldObject& object, glm::dvec3 center, PlayerSimulation* direct) {
    world_.emitVisual({"explosion",center,{0,1,0},object.radius,object.owner->team(),0,.08,0,"jetpack"});
    const auto ground = world_.geometry_.raycast(center + glm::dvec3(0.0, 0.2, 0.0), {0.0, -1.0, 0.0}, 3.5);
    if (ground.hit) world_.paint(*object.owner, ground.point + ground.normal * 0.1,
        object.paintRadius, object.owner->random(), {}, true);
    for (auto* victim : world_.actors_) {
        if (!victim || victim == direct || victim->team() == object.owner->team() || !victim->state.alive) continue;
        const glm::dvec3 target = victim->state.pos + glm::dvec3(0.0, 0.7, 0.0);
        const double distance = glm::length(target - center);
        if (distance > object.radius || !world_.geometry_.los(center, target)) continue;
        const double damage = object.maxDamage + (object.minDamage - object.maxDamage) * distance / object.radius;
        world_.applyHit(*object.owner, *victim, damage, "jetpack");
    }
    areaHit(center, object.radius, object.minDamage, object.owner->team(), object.owner);
}

void WorldSpecials::stampImpact(PlayerSimulation& actor, const ActorSpecial& special, double radius, double reach) {
    const glm::dvec3 forward(std::sin(special.hullYaw), 0.0, std::cos(special.hullYaw));
    const glm::dvec3 center = actor.state.pos + forward * reach;
    const auto ground = world_.geometry_.raycast(center + glm::dvec3(0.0, 1.2, 0.0), {0.0, -1.0, 0.0}, 3.0, true);
    const glm::dvec3 at = ground.hit ? ground.point : glm::dvec3(center.x, actor.state.pos.y, center.z);
    world_.emitVisual({"explosion",at,{0,1,0},radius,actor.team(),0,.08,0,"stamp"});
    world_.paint(actor, at + glm::dvec3(0.0, 0.2, 0.0), radius * 0.95, actor.random(), {}, true);
    world_.paint(actor, at + forward * (radius * 0.7) + glm::dvec3(0.0, 0.2, 0.0),
        radius * 0.6, actor.random(), {}, true);
    for (auto* victim : world_.actors_) {
        if (!victim || victim->team() == actor.team() || !victim->state.alive) continue;
        if (horizontalLength(victim->state.pos - at) > radius + 0.38
            || std::abs(victim->state.pos.y - at.y) > 1.8) continue;
        if (world_.geometry_.los(at + glm::dvec3(0.0, 0.5, 0.0), victim->state.pos + glm::dvec3(0.0, 0.6, 0.0)))
            world_.applyHit(actor, *victim, GetEquipment(EquipmentCategory::Special, "stamp").number("damage"), "stamp");
    }
    areaHit(at, radius, 80.0, actor.team(), &actor);
    world_.damageDevices(at, radius, 80.0, actor.team());
}

void WorldSpecials::zipImpact(PlayerSimulation& actor, ActorSpecial& special, PlayerSimulation* direct) {
    const auto& def = GetEquipment(EquipmentCategory::Special, "zipcaster");
    const glm::dvec3 center = actor.state.pos + glm::dvec3(0.0, 0.7, 0.0);
    world_.emitVisual({"explosion",center,{0,1,0},def.number("impactRadius"),actor.team(),0,.08,0,"zipcaster"});
    for (auto* victim : world_.actors_) {
        if (!victim || victim->team() == actor.team() || !victim->state.alive) continue;
        const glm::dvec3 target = victim->state.pos + glm::dvec3(0.0, 0.8, 0.0);
        const double distance = glm::length(target - center);
        const bool directHit = victim == direct || distance <= def.number("impactDirectR");
        if (!directHit && distance > def.number("impactRadius") + 0.3) continue;
        if (victim != direct && !world_.geometry_.los(center, target)) continue;
        world_.applyHit(actor, *victim, directHit ? def.number("impactDirect") : def.number("impactSplash"), "zipcaster");
    }
    areaHit(center, def.number("impactRadius"), def.number("impactSplash"), actor.team(), &actor);
    world_.damageDevices(center, def.number("impactRadius"), def.number("impactSplash"), actor.team());
    const auto ground=world_.geometry_.raycast(center+glm::dvec3(0,0.3,0),{0,-1,0},3.0,true);
    const glm::dvec3 paintAt=(ground.hit?ground.point:center)+glm::dvec3(0,0.2,0);
    world_.paint(actor, paintAt, def.number("impactRadius") * 0.75, actor.random(), {}, true);
    special.phase = special.hang > 0.0 ? "hang" : "ready";
}

void WorldSpecials::tickStrike(WorldObject& object, double dt) {
    if (object.kind == "strikeMissile") {
        object.age += dt;
        if(!object.audioWhistled&&object.timer>0&&object.age>=object.timer*0.35) {
            object.audioWhistled=true;
            const double progress=std::clamp(object.age/object.timer,0.0,1.0);
            emitSpecialAudio(world_,"strike_whistle",object.pos+(object.target-object.pos)*progress,object.owner->team());
        }
        if (object.age < object.timer) return;
        const auto& def = GetEquipment(EquipmentCategory::Special, "strike");
        object.kind = "tornado";
        object.pos = object.target;
        world_.emitVisual({"explosion",object.pos+glm::dvec3(0,0.6,0),{0,1,0},def.number("radius")*0.8,object.owner->team(),0,.08,0,"strike"});
        object.age = 0.0;
        object.timer = def.number("duration");
        object.radius = def.number("radius");
        object.damagePerSecond = def.number("dps");
        world_.paint(*object.owner, object.pos + glm::dvec3(0.0, 0.4, 0.0), object.radius * 0.7,
            object.owner->random(), {}, true);
        return;
    }
    const auto& def = GetEquipment(EquipmentCategory::Special, "strike");
    object.age += dt;
    const double fade = std::max(0.0, std::min(1.0, (object.timer - object.age) / 0.6));
    object.accumulator -= dt;
    if (object.accumulator <= 0.0) {
        object.accumulator = 0.1;
        for (int i = 0; i < 2; ++i) {
            const double angle = object.owner->random() * 2.0 * pi;
            const double r = std::sqrt(object.owner->random()) * object.radius;
            const auto at = object.pos + glm::dvec3(std::cos(angle) * r, 3.0, std::sin(angle) * r);
            const auto ground = world_.geometry_.raycast(at, {0.0, -1.0, 0.0}, 8.0, true);
            if (ground.hit) world_.paint(*object.owner, ground.point + ground.normal * 0.2,
                0.9 + object.owner->random() * 0.8, object.owner->random(), {}, true);
        }
    }
    for (auto* victim : world_.actors_) {
        if (!victim || victim->team() == object.owner->team() || !victim->state.alive) continue;
        const double dx = object.pos.x - victim->state.pos.x, dz = object.pos.z - victim->state.pos.z;
        const double distance = std::hypot(dx, dz);
        if (distance > object.radius || victim->state.pos.y < object.pos.y - 1.5
            || victim->state.pos.y > object.pos.y + 7.0) continue;
        world_.applyHit(*object.owner, *victim, object.damagePerSecond * dt * fade, "strike");
        if (distance > 0.3) {
            victim->state.vel.x += dx / distance * def.number("pull") * dt;
            victim->state.vel.z += dz / distance * def.number("pull") * dt;
        }
    }
    if (object.age >= object.timer) object.live = false;
}

void WorldSpecials::explodeBubble(WorldObject& object, PlayerSimulation* shooter) {
    if (!object.live || object.held) return;
    object.live = false;
    const auto& def = GetEquipment(EquipmentCategory::Special, "blower");
    const double radius = object.radius * def.number("blastMul");
    auto& owner = shooter && shooter->team() == object.owner->team() ? *shooter : *object.owner;
    const auto center = object.pos;
    for (int i = 0; i < 6; ++i) {
        const double angle = (static_cast<double>(i) / 6.0) * 2.0 * pi + object.owner->random() * 0.4;
        const double r = radius * (0.35 + object.owner->random() * 0.5);
        const auto ground = world_.geometry_.raycast(center + glm::dvec3(std::cos(angle) * r, 1.0, std::sin(angle) * r),
            {0.0, -1.0, 0.0}, 6.0, true);
        if (ground.hit) world_.paint(owner, ground.point + ground.normal * 0.2,
            1.2 + object.owner->random() * 0.6, object.owner->random(), {}, true);
    }
    const auto ground = world_.geometry_.raycast(center, {0.0, -1.0, 0.0}, 6.0, true);
    if (ground.hit) world_.paint(owner, ground.point + ground.normal * 0.2, radius * 0.55,
        object.owner->random(), {}, true);
    specialBlast(owner, center, radius, def.number("damageMax"), def.number("damageMin"),
        radius * 0.45, "blower");
}

void WorldSpecials::tickBubble(WorldObject& object, double dt) {
    object.age += dt;
    if (!object.held) {
        object.timer -= dt;
        object.vel *= std::exp(-0.45 * dt);
        object.vel.y += std::sin(object.age * 1.7 + object.accumulator) * 0.15 * dt;
        const auto previous = object.pos;
        object.pos += object.vel * dt;
        const double groundY = world_.geometry_.groundHeight(object.pos.x, object.pos.z, object.pos.y + 1.0);
        if (std::isfinite(groundY) && object.pos.y - object.radius * 0.85 < groundY) {
            object.pos.y = groundY + object.radius * 0.85;
            object.vel.y = std::max(0.0, object.vel.y);
        }
        const double speed = glm::length(object.vel);
        if (speed > 1e-8) {
            const auto wall = world_.geometry_.raycast(previous, object.vel / speed,
                object.radius * 0.8 + speed * dt, true);
            if (wall.hit && std::abs(wall.normal.y) < 0.6) {
                object.pos = previous;
                object.vel.x = object.vel.z = 0.0;
            }
        }
        for (auto* enemy : world_.actors_) {
            if (!enemy || enemy->team() == object.owner->team() || !enemy->state.alive) continue;
            const double dy = enemy->state.pos.y + 0.8 - object.pos.y;
            const double r = std::sqrt(std::max(0.0, object.radius * object.radius - dy * dy)) + 0.38;
            double dx = enemy->state.pos.x - object.pos.x, dz = enemy->state.pos.z - object.pos.z;
            const double distance = std::hypot(dx, dz);
            if (distance >= r || distance <= 1e-3) continue;
            dx /= distance; dz /= distance;
            enemy->state.pos.x = object.pos.x + dx * r;
            enemy->state.pos.z = object.pos.z + dz * r;
            const double vn = enemy->state.vel.x * dx + enemy->state.vel.z * dz;
            if (vn < 0.0) { enemy->state.vel.x -= dx * vn; enemy->state.vel.z -= dz * vn; }
        }
        if (object.timer <= 0.0) {
            object.live = false;
            world_.emitVisual({"bubble_pop",object.pos,{0,1,0},object.radius,object.owner->team(),0,.08,0,"blower"});
            world_.paint(*object.owner, object.pos - glm::dvec3(0.0, object.radius * 0.5, 0.0),
                object.radius * 0.6, object.owner->random(), {}, true);
        }
    }
}

void WorldSpecials::tickThrownStamp(WorldObject& object, double dt) {
    object.age += dt;
    object.previous = object.pos;
    object.vel.y -= 12.0 * dt;
    object.pos += object.vel * dt;
    const auto wall = world_.geometry_.segment(object.previous, object.pos, true);
    glm::dvec3 impact(0.0);
    bool hit = wall.hit;
    if (hit) impact = wall.point + wall.normal * 0.3;
    if (!hit) {
        for (auto* victim : world_.actors_) {
            if (!victim || victim->team() == object.owner->team() || !victim->state.alive) continue;
            const auto distance = StageGeometry::segmentCapsuleDist(object.previous, object.pos,
                victim->state.pos, 0.7, 1.6);
            if (distance.dist < 0.8) {
                impact = object.previous + (object.pos - object.previous) * distance.t;
                hit = true;
                break;
            }
        }
    }
    if (!hit && shotHit(object.previous, object.pos, object.owner->team(), 200.0, object.owner)) {
        impact = object.pos;
        hit = true;
    }
    if (hit || object.age > object.timer || object.pos.y < PlayerTuning().number("waterY", -20.0) - 2.0) {
        object.live = false;
        if (!hit) return;
        const auto ground = world_.geometry_.raycast(impact, {0.0, -1.0, 0.0}, 4.0, true);
        const glm::dvec3 base = ground.hit ? ground.point : impact;
        const auto& def = GetEquipment(EquipmentCategory::Special, "stamp");
        world_.paint(*object.owner, base + glm::dvec3(0.0, 0.2, 0.0), def.number("throwRadius") * 0.8,
            object.owner->random(), {}, true);
        for (int i = 0; i < 4; ++i) {
            const double angle = object.owner->random() * 2.0 * pi;
            const double r = def.number("throwRadius") * (0.5 + object.owner->random() * 0.4);
            world_.paint(*object.owner, impact + glm::dvec3(std::cos(angle) * r, 0.5, std::sin(angle) * r),
                0.9, object.owner->random(), {}, true);
        }
        specialBlast(*object.owner, impact, def.number("throwRadius"), def.number("throwDamageMax"),
            def.number("throwDamageMin"), 1.2, "stamp");
    }
}

void WorldSpecials::tickShell(WorldObject& object, double dt) {
    object.age += dt;
    object.previous = object.pos;
    object.vel.y -= 20.0 * dt;
    object.pos += object.vel * dt;
    const auto wall = world_.geometry_.segment(object.previous, object.pos, true);
    glm::dvec3 impact(0.0);
    bool hit = wall.hit;
    if (hit) impact = wall.point + wall.normal * 0.25;
    if (!hit) {
        for (auto* victim : world_.actors_) {
            if (!victim || victim->team() == object.owner->team() || !victim->state.alive) continue;
            const auto victimPose = pose(*victim);
            const double radius = victimPose.hitRadius > 0.0 ? victimPose.hitRadius : 0.38;
            const double height = victimPose.hitHeight > 0.0 ? victimPose.hitHeight : 1.45;
            const auto distance = StageGeometry::segmentCapsuleDist(object.previous, object.pos,
                victim->state.pos, radius, height);
            if (distance.dist < radius + 0.25) {
                impact = object.previous + (object.pos - object.previous) * distance.t;
                hit = true;
                break;
            }
        }
    }
    if (!hit && shotHit(object.previous, object.pos, object.owner->team(), 150.0, object.owner)) {
        impact = object.pos;
        hit = true;
    }
    if (hit || object.age >= object.timer || object.pos.y < PlayerTuning().number("waterY", -20.0) - 2.0) {
        object.live = false;
        if (!hit) return;
        world_.paint(*object.owner, impact + glm::dvec3(0.0, 0.1, 0.0), object.radius * 0.75,
            object.owner->random(), {}, true);
        specialBlast(*object.owner, impact, object.radius, object.maxDamage, object.minDamage,
            object.killRadius, "crab");
    }
}

void WorldSpecials::tickOrb(WorldObject& object, double dt) {
    object.age += dt;
    if (object.phase == "fly") {
        object.previous = object.pos;
        object.vel.y -= 24.0 * dt;
        object.pos += object.vel * dt;
        const auto hit = world_.geometry_.segment(object.previous, object.pos, true);
        if (hit.hit) {
            object.pos = hit.point + hit.normal * 0.5;
            object.phase = "fuse";
            object.age = 0.0;
            world_.emitVisual({"special_warning",object.pos,{0,1,0},object.radius,object.owner->team(),0,.08,0,"booyah"});
        } else if (object.age > object.timer || object.pos.y < PlayerTuning().number("waterY", -20.0) - 2.0)
            object.live = false;
        return;
    }
    if (object.age < object.range) return;
    object.live = false;
    const auto ground = world_.geometry_.raycast(object.pos, {0.0, -1.0, 0.0}, 6.0, true);
    const glm::dvec3 base = ground.hit ? ground.point : object.pos;
    world_.paint(*object.owner, base + glm::dvec3(0.0, 0.3, 0.0), object.radius * 0.72,
        object.owner->random(), {}, true);
    for (int i = 0; i < 12; ++i) {
        const double angle = (static_cast<double>(i) / 12.0) * 2.0 * pi + object.owner->random() * 0.3;
        const double r = object.radius * (0.5 + object.owner->random() * 0.45);
        const auto radial = object.pos + glm::dvec3(std::cos(angle) * r, 2.0, std::sin(angle) * r);
        const auto ground = world_.geometry_.raycast(radial, {0.0, -1.0, 0.0}, 8.0, true);
        if (ground.hit) world_.paint(*object.owner, ground.point + ground.normal * 0.2,
            1.3 + object.owner->random() * 0.7, object.owner->random(), {}, true);
    }
    specialBlast(*object.owner, object.pos, object.radius, object.maxDamage, object.minDamage,
        object.killRadius, "booyah");
}

void WorldSpecials::tickJetpackShot(WorldObject& object, double dt) {
    object.age += dt;
    object.previous = object.pos;
    object.pos += object.vel * dt;
    for (auto* victim : world_.actors_) {
        if (!victim || victim == object.owner || victim->team() == object.owner->team() || !victim->state.alive) continue;
        const auto victimPose = pose(*victim);
        const double radius = victimPose.hitRadius > 0.0 ? victimPose.hitRadius : 0.38;
        const double height = victimPose.hitHeight > 0.0 ? victimPose.hitHeight : (victim->state.squid ? 0.55 : 1.45);
        const auto hit = StageGeometry::segmentCapsuleDist(object.previous, object.pos, victim->state.pos, radius, height);
        if (hit.dist >= radius * 0.95 + 0.26) continue;
        const auto impact = object.previous + (object.pos - object.previous) * hit.t;
        world_.applyHit(*object.owner, *victim, object.directDamage, "jetpack");
        jetpackBlast(object, impact, victim);
        object.live = false;
        return;
    }
    if (world_.blockShot(object.previous, object.pos, object.owner->team(), object.directDamage, object.owner)) {
        object.live = false; return;
    }
    const double distance = glm::length(object.pos - object.previous);
    const auto wall = distance > 0.0 ? world_.geometry_.segment(object.previous, object.pos, true) : RayHit{};
    if (wall.hit) {
        jetpackBlast(object, wall.point, nullptr);
        object.live = false;
    } else if (object.age > object.timer) {
        jetpackBlast(object, object.pos, nullptr);
        object.live = false;
    }
    if(object.live) {
        object.accumulator+=glm::length(object.vel)*dt;
        if(object.accumulator>1.2) {
            object.accumulator=0;
            const auto ground=world_.geometry_.raycast(object.pos,{0,-1,0},4,true);
            if(ground.hit) world_.paint(*object.owner,ground.point+ground.normal*0.1,0.42*(0.8+object.owner->random()*0.4),object.owner->random(),{},true);
        }
        if(object.pos.y<PlayerTuning().number("waterY")-1.8) object.live=false;
    }
}

void WorldSpecials::tickWorld(double dt) {
    for(auto* actor:world_.actors_) if(actor && actor->input.cheer) {actor->input.cheer=false;cheer(*actor);}
    WorldTickGuard ticking(tickingWorld_);
    for (auto& object : objects_) {
        if (!object.live || !object.owner) continue;
        if (object.kind == "stormBomb" || object.kind == "stormCloud") tickStorm(object, dt);
        else if (object.kind == "zooka") tickZooka(object, dt);
        else if (object.kind == "speaker" && object.owner->state.alive) tickWail(object, dt);
        else if (object.kind == "strikeMissile" || object.kind == "tornado") tickStrike(object, dt);
        else if (object.kind == "bubble") tickBubble(object, dt);
        else if (object.kind == "thrownStamp") tickThrownStamp(object, dt);
        else if (object.kind == "crabShell") tickShell(object, dt);
        else if (object.kind == "orb") tickOrb(object, dt);
        else if (object.kind == "jetpackShot") tickJetpackShot(object, dt);
    }
    tickingWorld_ = false;
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [](const WorldObject& object) {
        return !object.live || !object.owner;
    }), objects_.end());

    const auto& bubbler = GetEquipment(EquipmentCategory::Special, "bubbler");
    for (const auto& [owner, special] : actors_) {
        if (!special.bubblerShield || !owner || !owner->state.alive || !(owner->state.shield > 0.0)) continue;
        for (auto* ally : world_.actors_) {
            if (!ally || ally == owner || ally->team() != owner->team() || !ally->state.alive || ally->state.shield > 0.0) continue;
            if (glm::length(ally->state.pos - owner->state.pos) < bubbler.number("shareRange")) {
                ally->state.shield = owner->state.shield;
                shieldSources_[ally] = const_cast<PlayerSimulation*>(owner);
            }
        }
    }
    for (auto* actor : world_.actors_) {
        if (!actor) continue;
        if (!actor->state.alive || actor->state.shield <= 0.0) {
            actor->state.shield = 0.0;
            shieldSources_.erase(actor);
        } else {
            actor->state.shield = std::max(0.0, actor->state.shield - dt);
            if (actor->state.shield <= 0.0) {
                emitSpecialAudio(world_, "shield_pop", actor->state.pos, actor->team());
                shieldSources_.erase(actor);
            }
        }
    }
}

double WorldSpecials::filterDamage(PlayerSimulation& victim, PlayerSimulation* attacker,
                                   double amount, std::string_view) {
    if (!(amount > 0.0)) return amount;
    const auto active = actors_.find(&victim);
    if (active != actors_.end() && active->second.armor) amount *= 0.25;
    if(active!=actors_.end() && active->second.id=="crab") {
        auto& special=active->second;
        if(!special.roll && attacker && attacker!=&victim) {
            const auto delta=attacker->state.pos-victim.state.pos;const double distance=horizontalLength(delta);
            const double front=(delta.x*std::sin(special.hullYaw)+delta.z*std::cos(special.hullYaw))/(distance>0?distance:1);
            if(front<-0.45 || attacker->state.pos.y>victim.state.pos.y+2.4) return amount;
        }
        special.hitPoints-=amount*(special.roll?GetEquipment(EquipmentCategory::Special,"crab").number("rollArmor"):1);
        if (world_.time_ - special.lastHitAudio > 0.08) {
            special.lastHitAudio = world_.time_;
            emitSpecialAudio(world_, "crab_hit", victim.state.pos, victim.team());
        }
        return 0;
    }
    if(active!=actors_.end() && active->second.id=="stamp" && active->second.guard>0 && attacker && attacker!=&victim) {
        const auto delta=attacker->state.pos-victim.state.pos;const double distance=horizontalLength(delta);
        const double dot=(delta.x*std::sin(active->second.hullYaw)+delta.z*std::cos(active->second.hullYaw))/(distance>0?distance:1);
        if(distance<0.3 || dot>=std::cos(GetEquipment(EquipmentCategory::Special,"stamp").number("deflectArc")*pi/180.0)) {
            emitSpecialAudio(world_, "shield_hit", victim.state.pos, victim.team());
            return 0;
        }
    }
    const bool kraken = active != actors_.end() && active->second.id == "kraken";
    if (!kraken && victim.state.shield <= 0.0) return amount;
    const auto shieldHit = shieldHitTimes_.find(&victim);
    if (!kraken && (shieldHit == shieldHitTimes_.end() || world_.time_ - shieldHit->second > 0.12)) {
        shieldHitTimes_[&victim] = world_.time_;
        emitSpecialAudio(world_, "shield_hit", victim.state.pos, victim.team());
    }
    const auto& def = GetEquipment(EquipmentCategory::Special, kraken ? "kraken" : "bubbler");
    double dx = 0.0, dz = 0.0;
    if (attacker && attacker != &victim) {
        dx = victim.state.pos.x - attacker->state.pos.x;
        dz = victim.state.pos.z - attacker->state.pos.z;
    }
    const double length = std::hypot(dx, dz);
    if (length < 1e-3) {
        dx = -std::sin(victim.state.yaw);
        dz = -std::cos(victim.state.yaw);
    } else { dx /= length; dz /= length; }
    const double knock = std::min(kraken ? 4.0 : def.number("knockMax"), amount * def.number("knockPerDamage"));
    victim.state.vel.x += dx * knock;
    victim.state.vel.z += dz * knock;
    if (victim.state.grounded) {
        victim.state.vel.y = std::max(victim.state.vel.y, 1.4 + knock * 0.18);
        victim.state.grounded = false;
    }
    return 0.0;
}

SpecialPose WorldSpecials::pose(const PlayerSimulation& actor) const {
    const auto found = actors_.find(const_cast<PlayerSimulation*>(&actor));
    if (found == actors_.end()) return {};
    const auto& special = found->second;
    SpecialPose result;
    result.active = true;
    result.body = special.body;
    result.casting = special.casting || special.body;
    result.thrown = special.thrown;
    result.noSquid = special.noSquid;
    result.aimFace = special.aimFace;
    result.moveSpeed = special.moveSpeed;
    result.accel = special.accel;
    result.inkProof = special.inkProof;
    result.faceMove = special.faceMove;
    result.mapTarget = special.mapTarget;
    result.target = special.target;
    result.jumpBack = special.jumpBack;
    result.origin = special.origin;
    result.roll = special.roll;
    result.raise = special.raise;
    result.charge = special.charge;
    result.hitPoints = special.hitPoints;
    result.hitRadius = special.hitRadius;
    result.hitHeight = special.hitHeight;
    result.hang = special.hang;
    result.firing = special.firing;
    result.guard = special.guard;
    if(special.id=="crab" && !special.roll && special.elapsed>0) result.seat=CrabSeatAt.y;
    if (special.hasFaceYaw) result.faceYaw = special.faceYaw;
    result.elapsed = special.elapsed;
    result.remaining = special.duration > 0.0 ? std::clamp(1.0 - special.elapsed / special.duration, 0.0, 1.0) : 1.0;
    result.kind = special.id;
    result.phase = special.phase;
    return result;
}

std::vector<WorldObjectView> WorldSpecials::visualObjects() const {
    std::vector<WorldObjectView> result;
    result.reserve(objects_.size());
    for (const auto& object : objects_) {
        if (!object.live || !object.owner) continue;
        result.push_back({object.kind,object.phase,object.owner->team(),object.pos,object.previous,
                          object.target,object.mouth,object.vel,object.direction,object.age,
                          object.timer,object.radius,object.range,object.charge,object.held,object.id});
    }
    return result;
}

void WorldSpecials::splat(PlayerSimulation& actor) {
    // PlayerSimulation applies COMBAT06 gauge retention before calling this lifecycle cleanup hook.
    cleanup(actor);
}

void WorldSpecials::cleanup(PlayerSimulation& actor) {
    actors_.erase(&actor);
    shieldHitTimes_.erase(&actor);
    for (auto& object : objects_) if (object.live && object.owner == &actor) {
        if (object.kind == "speaker" && object.phase == "blast")
            emitSpecialAudio(world_, "wail_blast_stop", object.pos, actor.team(), 0.0, object.id);
        object.live = false;
    }
    if (!tickingWorld_) {
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [](const WorldObject& object) {
            return !object.live || !object.owner;
        }), objects_.end());
    }
    // Keep the vector stable during CombatWorld's device update; its own cleanup removes dead devices afterward.
    for (auto& device : world_.devices_) if (device.owner == &actor && device.special) device.live = false;
    for (auto it = shieldSources_.begin(); it != shieldSources_.end();) {
        if (it->first == &actor || it->second == &actor) {
            if (it->first) it->first->state.shield = 0.0;
            it = shieldSources_.erase(it);
        } else ++it;
    }
}

void WorldSpecials::clear() {
    shieldHitTimes_.clear();
    for (const auto& [actor, _] : actors_) if (actor) actor->state.shield = 0.0;
    for (const auto& [actor, _] : shieldSources_) if (actor) actor->state.shield = 0.0;
    actors_.clear();
    shieldSources_.clear();
    if (tickingWorld_) {
        for (auto& object : objects_) object.live = false;
    } else objects_.clear();
    for (auto& device : world_.devices_) if (device.special) device.live = false;
}

} // namespace restoration
