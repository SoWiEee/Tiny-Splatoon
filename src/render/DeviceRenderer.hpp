#pragma once
#include "CharacterArt.hpp"
#include <memory>
#include <cstddef>
namespace restoration {
class CombatWorld;
class PlayerSimulation;
struct DeviceActorPose {const PlayerSimulation* actor;const CharacterPoseFrame* frame;const CharacterStyle* style;glm::dmat4 tank{1};};
class DeviceRenderer {
public:
    explicit DeviceRenderer(std::filesystem::path assets="assets/models/devices");
    ~DeviceRenderer();
    void Update(double dt,const CombatWorld& world,glm::dvec3 eye);
    int Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,
        const std::vector<DeviceActorPose>& actors,const CharacterShadowState* shadow=nullptr);
    int DrawShadow(const glm::dmat4& lightViewProjection,const std::vector<DeviceActorPose>& actors);
    int DrawModel(const std::string& kind,int team,const glm::dmat4& root,double age,const glm::dmat4& view,const glm::dmat4& projection,
        glm::dvec3 eye,const CharacterLightState& lights,bool depthOnly=false,double opacity=1,double phase=0);
    std::size_t Count()const;
    double VisualRadius(const std::string& kind)const;
    std::size_t TracerTailCount()const;
    std::size_t TracerRibbonSegmentCount()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
