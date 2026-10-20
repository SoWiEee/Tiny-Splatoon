#include "BossPose.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace restoration {
struct BossPose::Rig {
    std::vector<int> parent;
    std::vector<glm::dmat4> restGlobal, inverseRest, currentGlobal, skin;
};

BossPose::BossPose(const std::filesystem::path& path)
    : assets_(LoadBossModels(path)),
      sourceRig_(LoadBossPoseRig(path.parent_path() / "pose-rig.bin")) {
    rigs_.resize(assets_.size());
    for (std::size_t r = 0; r < rigs_.size(); ++r) {
        auto& rig = rigs_[r];
        const auto& asset = assets_[r];
        rig.parent.reserve(asset.bones.size());
        rig.restGlobal.resize(asset.bones.size());
        rig.inverseRest.resize(asset.bones.size());
        rig.currentGlobal.resize(asset.bones.size());
        rig.skin.resize(asset.bones.size());
        for (std::size_t i = 0; i < asset.bones.size(); ++i) {
            const auto& bone = asset.bones[i];
            rig.parent.push_back(bone.parent);
            const auto local = glm::translate(glm::dmat4(1.0), glm::dvec3(bone.position));
            rig.restGlobal[i] = bone.parent < 0
                ? local
                : rig.restGlobal[static_cast<std::size_t>(bone.parent)] * local;
            rig.inverseRest[i] = glm::inverse(rig.restGlobal[i]);
        }
        Evaluate(rig);
    }
    crablets_.reserve(5);
    Update(BossPoseInput{});
}

BossPose::~BossPose() = default;

void BossPose::Evaluate(Rig& rig) {
    for (std::size_t i = 0; i < rig.restGlobal.size(); ++i) {
        rig.currentGlobal[i] = rig.restGlobal[i];
        rig.skin[i] = rig.currentGlobal[i] * rig.inverseRest[i];
    }
}

void BossPose::AnimateHull(Rig& rig, const BossPoseInput&) {
    Evaluate(rig);
}

void BossPose::AnimateCrablet(Rig& rig, double, double) {
    Evaluate(rig);
}

void BossPose::Update(const BossPoseInput& input) {
    if (input.dt < 0 || !std::isfinite(input.dt))
        throw std::invalid_argument("Invalid Boss pose delta");

    auto& rig = rigs_.at(0);
    AnimateHull(rig, input);
    const auto translation = glm::translate(glm::dmat4(1.0), input.position);
    const auto rotation = glm::rotate(glm::dmat4(1.0), input.yaw, glm::dvec3(0, 1, 0));
    hull_.root = translation * rotation;
    hull_.skin = rig.skin;

    const auto& asset = assets_.at(0);
    if (asset.sockets.size() != hitShapes_.size())
        throw std::runtime_error("Boss source socket count changed");
    const auto transform = [&](const glm::dmat4& matrix, glm::dvec3 point) {
        const auto p = matrix * glm::dvec4(point, 1.0);
        return glm::dvec3(p) / p.w;
    };
    for (std::size_t i = 0; i < hitShapes_.size(); ++i) {
        const auto& socket = asset.sockets[i];
        if (socket.bone >= rig.currentGlobal.size())
            throw std::runtime_error("Boss socket references an absent bone");
        const auto matrix = hull_.root * rig.currentGlobal[socket.bone];
        hitShapes_[i] = {socket.name, transform(matrix, glm::dvec3(socket.offset)),
            socket.radius, socket.weak, socket.active};
    }

    // The production BossModel material uniforms and clip/IK/spring state are
    // not yet ported. Keep the API explicit so callers cannot mistake an empty
    // snapshot for a source-validated animated material state.
    uniforms_.clear();
    crablets_.clear();
}

void BossPose::Update(BossSimulation& simulation) {
    const auto& state = simulation.state();
    BossPoseInput input;
    input.position = state.pos;
    input.yaw = state.yaw;
    input.phase = state.phase;
    input.stunned = state.stunned;
    input.dead = state.dead;
    if (simulation.move()) {
        const auto& move = *simulation.move();
        switch (move.id) {
        case BossMoveId::Slam: input.move = "slam"; break;
        case BossMoveId::Barrage: input.move = "barrage"; break;
        case BossMoveId::Sweep: input.move = "sweep"; break;
        case BossMoveId::Charge: input.move = "charge"; break;
        case BossMoveId::Crablets: input.move = "crablets"; break;
        case BossMoveId::Frenzy: input.move = "frenzy"; break;
        }
        switch (move.phase(state.clock)) {
        case BossMovePhase::Telegraph: input.movePhase = "tele"; break;
        case BossMovePhase::Active: input.movePhase = "act"; break;
        case BossMovePhase::Recovery: input.movePhase = "rec"; break;
        default: break;
        }
        input.moveTime = std::max(0.0, state.clock - move.t0);
        input.phaseDuration = move.end();
        input.hasParams = true;
        input.wall = move.wall;
        input.count = move.count;
        input.rings = move.rings;
        for (const auto& barrel : move.barrels)
            input.barrels.push_back({barrel.x, barrel.y, barrel.z, barrel.throwAt});
    }
    Update(input);
    simulation.setHitShapes(hitShapes_);

    const auto& crabRig = rigs_.at(1);
    crablets_.clear();
    for (const auto& crab : simulation.crablets()) {
        if (crab.dead) continue;
        AnimateCrablet(rigs_.at(1), crab.age, crab.speed);
        BossCrabletPose pose;
        pose.id = crab.id;
        pose.root = glm::translate(glm::dmat4(1.0), crab.pos) *
            glm::rotate(glm::dmat4(1.0), crab.yaw, glm::dvec3(0, 1, 0));
        pose.skin = crabRig.skin;
        crablets_.push_back(std::move(pose));
    }
}
} // namespace restoration
