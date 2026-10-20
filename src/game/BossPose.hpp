#pragma once

#include "BossModelData.hpp"
#include "BossSimulation.hpp"
#include "BossPoseInput.hpp"
#include <array>
#include <glm/mat4x4.hpp>
#include <map>
#include <memory>
#include <string>
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

// Source-model FK, animation channels and gameplay sockets on the fixed-step
// side. This class has no GL dependency; its frames can be rendered later.
class BossPose {
public:
    explicit BossPose(const std::filesystem::path& asset="assets/reference/boss/model.bin");
    ~BossPose();
    BossPose(const BossPose&)=delete;
    BossPose& operator=(const BossPose&)=delete;
    void Update(BossSimulation& simulation);
    void Update(const BossPoseInput& input);
    const BossRigPose& hullbreaker() const { return hull_; }
    const std::vector<BossCrabletPose>& crablets() const { return crablets_; }
    const std::array<BossHitShape,10>& hitShapes() const { return hitShapes_; }
    const std::map<std::string,std::vector<double>>& materialUniforms() const { return uniforms_; }
    const BossModelAsset& hullAsset() const { return assets_.at(0); }
    const BossModelAsset& crabletAsset() const { return assets_.at(1); }
private:
    struct Rig;
    void AnimateHull(Rig& rig,const BossPoseInput& input);
    static void AnimateCrablet(Rig& rig,double age,double speed);
    static void Evaluate(Rig& rig);
    std::vector<BossModelAsset> assets_;
    BossPoseRigData sourceRig_;
    std::vector<Rig> rigs_;
    BossRigPose hull_;
    std::vector<BossCrabletPose> crablets_;
    std::array<BossHitShape,10> hitShapes_{};
    std::map<std::string,std::vector<double>> uniforms_;
};
} // namespace restoration
