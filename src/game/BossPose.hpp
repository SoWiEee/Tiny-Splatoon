#pragma once

#include "BossModelData.hpp"
#include "BossSimulation.hpp"
#include "BossPoseInput.hpp"
#include <array>
#include <functional>
#include <glm/mat4x4.hpp>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace restoration {
struct BossRigPose {
    glm::dmat4 root{1.0};
    std::vector<glm::dmat4> skin;
};
struct BossCrabletPose {
    int id=0;
    glm::dmat4 root{1.0};
    std::vector<glm::dmat4> skin;
};

struct BossPoseAudioEvent {
    enum class Kind { Footstep, Impact, Fx, Intro };
    Kind kind = Kind::Footstep;
    glm::dvec3 position{0.0};
    double strength = 0.0;
    double time = 0.0;
    int leg = -1;
    std::string socket;
    std::string name;
};

// Source-model FK, animation channels and gameplay sockets on the fixed-step
// side. This class has no GL dependency; its frames can be rendered later.
class BossPose {
public:
    explicit BossPose(const std::filesystem::path& asset="assets/models/boss/model.bin");
    ~BossPose();
    BossPose(const BossPose&)=delete;
    BossPose& operator=(const BossPose&)=delete;
    void Update(BossSimulation& simulation);
    void Update(const BossPoseInput& input);
    void SetAudioEventSink(std::function<void(const BossPoseAudioEvent&)> sink) { audioEventSink_ = std::move(sink); }
    const BossRigPose& hullbreaker() const { return hull_; }
    const std::vector<BossCrabletPose>& crablets() const { return crablets_; }
    const std::array<BossHitShape,10>& hitShapes() const { return hitShapes_; }
    const std::vector<BossPoseSocket>& poseSockets() const { return poseSockets_; }
    const std::map<std::string,std::vector<double>>& materialUniforms() const { return uniforms_; }
    const BossModelAsset& hullAsset() const { return assets_.at(0); }
    const BossModelAsset& crabletAsset() const { return assets_.at(1); }
private:
    struct Rig;
    struct Animator;
    void AnimateHull(Rig& rig,const BossPoseInput& input);
    void AnimateCrablet(Rig& rig,const BossCrablet& crab,double dt);
    static void Evaluate(Rig& rig);
    std::vector<BossModelAsset> assets_;
    BossPoseRigData sourceRig_;
    std::vector<Rig> rigs_;
    std::unique_ptr<Animator> animator_;
    std::map<int,double> simulationAges_;
    double simulationClock_=0;
    bool hasSimulationClock_=false;
    BossRigPose hull_;
    std::vector<BossCrabletPose> crablets_;
    std::array<BossHitShape,10> hitShapes_{};
    std::vector<BossPoseSocket> poseSockets_;
    std::map<std::string,std::vector<double>> uniforms_;
    std::function<void(const BossPoseAudioEvent&)> audioEventSink_;
};
} // namespace restoration
