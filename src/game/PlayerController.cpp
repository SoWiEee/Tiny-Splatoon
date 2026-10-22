#include "PlayerController.hpp"

#include "FollowCamera.hpp"
#include "MatchSimulation.hpp"
#include "ArsenalData.hpp"
#include "PlayerProfile.hpp"
#include "PlayerSimulation.hpp"
#include "../world/StageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;

double clamp(double value, double lo, double hi) {
    return std::clamp(value, lo, hi);
}

double finiteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double responseCurve(double magnitude) {
    if (magnitude < 0.75) return 0.62 * std::pow(magnitude / 0.75, 1.6);
    return 0.62 + ((magnitude - 0.75) / 0.25) * 0.38;
}

struct Stick {
    double x = 0.0, y = 0.0, magnitude = 0.0;
};

Stick readStick(double x, double y, double deadZone, double outer) {
    Stick stick;
    x = finiteOr(x, 0.0);
    y = finiteOr(y, 0.0);
    const double rawMagnitude = std::hypot(x, y);
    if (rawMagnitude <= deadZone) return stick;
    const double scaledMagnitude = std::min(1.0, (rawMagnitude - deadZone) / (outer - deadZone));
    stick.x = x / rawMagnitude * scaledMagnitude;
    stick.y = y / rawMagnitude * scaledMagnitude;
    stick.magnitude = scaledMagnitude;
    return stick;
}

double effectiveRange(const PlayerSimulation& actor) {
    const auto& weapon = actor.weapon();
    if (weapon.kind == "charger" || weapon.kind == "spinner")
        return weapon.number("rangeMax", actor.weaponProfile().range);
    if (weapon.kind == "roller") return 6.0;
    if (weapon.kind == "brush") return 5.0;
    if (weapon.kind == "bucket") return weapon.number("reach", actor.weaponProfile().range);
    return weapon.number("range", 12.0);
}

double angleDifference(double from, double to) {
    return std::remainder(to - from, 2.0 * pi);
}

bool isFiniteVector(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

void PlayerController::Update(double dt, const ControllerInput& input, const PlayerSettings& settings,
                              MatchSimulation& match, FollowCamera& camera, const StageGeometry& geometry) {
    if (!(std::isfinite(dt) && dt > 0.0)) return;

    auto& actor = match.local();
    const double sensitivity = clamp(finiteOr(settings.sensitivity, 1.0), 0.2, 3.0);
    const double padSensitivity = clamp(finiteOr(settings.padSensitivity, 1.0), 0.2, 3.0);
    const double aimAssistSetting = clamp(finiteOr(settings.aimAssist, 1.0), 0.0, 1.0);
    const double invert = settings.invertY ? -1.0 : 1.0;

    // Assist is acquired from the previous camera pose, as in player.js. It
    // changes look speed near an enemy and carries a small share of continuing
    // target motion while the player is actively looking, moving, or firing.
    const auto previousAssist = assist_;
    const bool usingPad = input.gamepadConnected && input.usingPad;
    const double strength = usingPad ? aimAssistSetting : (settings.aimAssistMouse ? 0.5 : 0.0);
    assist_ = {};
    if (strength > 0.0) {
        const auto& cameraPose = camera.pose();
        glm::dvec3 forward = cameraPose.forward;
        if (isFiniteVector(forward) && glm::dot(forward, forward) > 1e-12) {
            forward = glm::normalize(forward);
            const auto eye = cameraPose.eye;
            const double maxRange = std::min(32.0, effectiveRange(actor) * 1.15 + 2.0);
            const PlayerSimulation* best = nullptr;
            double bestScore = std::numeric_limits<double>::infinity();
            double bestYaw = 0.0, bestPitch = 0.0, bestCloseness = 0.0;
            for (const auto& candidate : match.actors()) {
                const auto& state = candidate->state;
                if (candidate.get() == &actor || candidate->team() == actor.team() || !state.alive ||
                    state.submerged || state.invuln > 0.0) continue;

                const glm::dvec3 target(state.pos.x,
                    state.pos.y + state.smoothY + (state.squid ? 0.3 : 0.95), state.pos.z);
                glm::dvec3 toTarget = target - eye;
                const double distance = glm::length(toTarget);
                if (distance > maxRange + 4.0 || distance < 0.5 ||
                    glm::distance(actor.state.pos, state.pos) > maxRange) continue;
                toTarget /= distance;
                const double angle = std::acos(clamp(glm::dot(toTarget, forward), -1.0, 1.0));
                const double cone = clamp(std::atan2(1.0, distance), 2.5 * pi / 180.0, 10.0 * pi / 180.0);
                if (angle > cone || !geometry.los(eye, target)) continue;
                const double score = angle / cone + distance * 0.01;
                if (score < bestScore) {
                    best = candidate.get();
                    bestScore = score;
                    bestYaw = std::atan2(toTarget.x, toTarget.z);
                    bestPitch = std::asin(clamp(toTarget.y, -1.0, 1.0));
                    bestCloseness = clamp((1.0 - angle / cone) * 1.3, 0.0, 1.0);
                }
            }
            if (best) {
                assist_.target = best;
                assist_.yaw = bestYaw;
                assist_.pitch = bestPitch;
                assist_.closeness = bestCloseness;
                assist_.strength = strength;
                assist_.has = true;
            }
        }
    }

    const double friction = assist_.has
        ? 1.0 + (0.58 - 1.0) * assist_.closeness * assist_.strength
        : 1.0;
    bool lookActive = false;
    const double mouseDX = finiteOr(input.mouseDX, 0.0);
    const double mouseDY = finiteOr(input.mouseDY, 0.0);
    if (mouseDX != 0.0 || mouseDY != 0.0) {
        const double mouseFriction = settings.aimAssistMouse ? friction : 1.0;
        const double gain = 0.0021 * sensitivity * mouseFriction;
        camera.yaw -= mouseDX * gain;
        camera.pitch -= mouseDY * gain * invert;
        lookActive = true;
    }

    Stick lookStick;
    if (input.gamepadConnected) {
        lookStick = readStick(input.rightStickX, input.rightStickY, 0.11, 0.96);
        if (lookStick.magnitude > 0.93) edgeTime_ = std::min(0.5, edgeTime_ + dt);
        else edgeTime_ = std::max(0.0, edgeTime_ - dt * 3.0);
        const double boost = 1.0 + 0.55 * clamp((edgeTime_ - 0.16) / 0.3, 0.0, 1.0);
        const double curved = lookStick.magnitude > 0.0
            ? responseCurve(lookStick.magnitude) / lookStick.magnitude : 0.0;
        const double filter = 1.0 - std::exp(-60.0 * dt);
        padLookX_ += (lookStick.x * curved - padLookX_) * filter;
        padLookY_ += (lookStick.y * curved - padLookY_) * filter;
        if (lookStick.magnitude > 0.0) lookActive = true;
        camera.yaw -= padLookX_ * 3.6 * padSensitivity * boost * friction * dt;
        camera.pitch -= padLookY_ * 2.4 * padSensitivity * friction * dt * invert;
    } else {
        edgeTime_ = 0.0;
        padLookX_ = 0.0;
        padLookY_ = 0.0;
    }

    double moveX = (input.right ? 1.0 : 0.0) - (input.left ? 1.0 : 0.0);
    double moveZ = (input.forward ? 1.0 : 0.0) - (input.backward ? 1.0 : 0.0);
    if (input.gamepadConnected) {
        const auto moveStick = readStick(input.leftStickX, input.leftStickY, 0.14, 0.95);
        moveX += moveStick.x;
        moveZ -= moveStick.y;
    }
    const double moveLength = std::hypot(moveX, moveZ);
    if (moveLength > 1.0) {
        moveX /= moveLength;
        moveZ /= moveLength;
    }

    if (assist_.has && previousAssist.has && previousAssist.target == assist_.target &&
        (lookActive || moveLength > 0.2 || input.fire)) {
        const double share = 0.42 * assist_.strength * assist_.closeness;
        camera.yaw += angleDifference(previousAssist.yaw, assist_.yaw) * share;
        camera.pitch += (assist_.pitch - previousAssist.pitch) * share * 0.7;
    }

    camera.pitch = clamp(camera.pitch, -1.05, 1.15);
    actor.input.aimYaw = camera.yaw;
    actor.input.aimPitch = camera.pitch;

    const double sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
    actor.input.move = {sy * moveZ - cy * moveX, 0.0, cy * moveZ + sy * moveX};
}

std::vector<SuperJumpMapTarget> PlayerController::SuperJumpMapTargets(MatchSimulation& match) const {
    std::vector<SuperJumpMapTarget> targets;
    auto& local=match.local();
    const bool canStart=local.state.alive&&local.canRespawn()&&!local.superJumpState()&&!local.specialPose().active;
    int allyKey=1;
    for(std::size_t index=0;index<match.actors().size()&&allyKey<=3;++index){
        auto* actor=match.actors()[index].get();
        if(actor==&local||actor->team()!=local.team())continue;
        SuperJumpMapTarget target;
        target.key=allyKey++;
        target.kind=SuperJumpMapTargetKind::Ally;
        target.actor=actor;
        target.x=actor->state.pos.x;target.y=actor->state.pos.y;target.z=actor->state.pos.z;
        target.available=actor->state.alive&&(!local.state.alive?local.canRespawn():!actor->superJumpState()&&canStart);
        target.label="隊友 "+match.roster()[index].name;
        targets.push_back(std::move(target));
    }
    SuperJumpMapTarget base;
    base.key=4;base.kind=SuperJumpMapTargetKind::Base;
    base.x=match.stage().spawnPads[local.team()].x;base.y=match.stage().spawnPads[local.team()].y;base.z=match.stage().spawnPads[local.team()].z;
    base.available=local.state.alive?canStart:local.canRespawn();base.label="隊伍生點";targets.push_back(std::move(base));

    std::vector<const CombatDevice*> beacons;
    for(const auto& device:match.world().devices())
        if(device.live&&device.kind=="beacon"&&device.team==local.team()&&device.uses>0)beacons.push_back(&device);
    std::sort(beacons.begin(),beacons.end(),[](const auto* a,const auto* b){return a->id<b->id;});
    if(beacons.size()>6)beacons.resize(6);
    for(std::size_t i=0;i<beacons.size();++i){
        const auto& device=*beacons[i];
        SuperJumpMapTarget target;
        target.key=static_cast<int>((i+5)%10);target.kind=SuperJumpMapTargetKind::Beacon;target.beaconId=device.id;
        target.x=device.pos.x;target.y=device.pos.y;target.z=device.pos.z;
        target.available=local.state.alive?canStart:local.canRespawn();
        target.label="隊伍訊標 "+std::to_string(i+1)+"（"+std::to_string(device.uses)+"）";
        targets.push_back(std::move(target));
    }
    return targets;
}

bool PlayerController::SelectSuperJumpMapTarget(MatchSimulation& match,int key) {
    if(match.phase()!=MatchPhase::Playing||key<0||key>9)return false;
    auto& local=match.local();
    if(!local.canRespawn())return false;
    const auto targets=SuperJumpMapTargets(match);
    const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto& target){return target.key==key;});
    if(found==targets.end())return false;
    if(found->kind==SuperJumpMapTargetKind::Base){
        if(!local.state.alive){ClearQueuedSuperJump();return true;}
        if(!found->available)return false;
        return local.superJump({found->x,found->y,found->z});
    }
    if(!local.state.alive){
        if(found->kind==SuperJumpMapTargetKind::Ally){
            if(!found->actor||found->actor==&local||found->actor->team()!=local.team()||!found->actor->state.alive)return false;
        }else{
            glm::dvec3 anchor;
            if(!match.world().beaconAnchor(local.team(),found->beaconId,anchor))return false;
        }
        queuedJump_=*found;hasQueuedJump_=true;return true;
    }
    if(!found->available)return false;
    if(found->kind==SuperJumpMapTargetKind::Ally&&found->actor)return local.superJumpToActor(*found->actor);
    if(found->kind==SuperJumpMapTargetKind::Beacon)return local.superJumpToBeacon(found->beaconId);
    return false;
}

void PlayerController::ValidateSuperJumpQueue(MatchSimulation& match) {
    if(!hasQueuedJump_)return;
    if(match.phase()!=MatchPhase::Playing){ClearQueuedSuperJump();return;}
    auto& local=match.local();
    if(queuedJump_.kind==SuperJumpMapTargetKind::Ally){
        if(!queuedJump_.actor||queuedJump_.actor==&local||queuedJump_.actor->team()!=local.team()||!queuedJump_.actor->state.alive)
            ClearQueuedSuperJump();
    }else if(queuedJump_.kind==SuperJumpMapTargetKind::Beacon){
        glm::dvec3 anchor;
        if(!match.world().beaconAnchor(local.team(),queuedJump_.beaconId,anchor))ClearQueuedSuperJump();
    }else ClearQueuedSuperJump();
}

bool PlayerController::LaunchQueuedSuperJump(MatchSimulation& match) {
    if(!hasQueuedJump_||match.phase()!=MatchPhase::Playing||!match.local().state.alive||!match.local().canRespawn())return false;
    const auto queued=queuedJump_;
    ClearQueuedSuperJump();
    auto& local=match.local();
    if(queued.kind==SuperJumpMapTargetKind::Ally&&queued.actor&&queued.actor->state.alive&&queued.actor->team()==local.team())
        return local.superJumpToActor(*queued.actor,true);
    if(queued.kind==SuperJumpMapTargetKind::Beacon)return local.superJumpToBeacon(queued.beaconId);
    return false;
}

} // namespace restoration
