#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace restoration {
class BossSimulation;
class StageGeometry;
class BossModelData;
struct CharacterLightState;
struct BossPoseAudioEvent;

// Renders the source-built HULLBREAKER mesh and its telegraphed attack geometry.
class BossRenderer {
public:
    explicit BossRenderer(std::filesystem::path assets="assets/reference/boss",bool environment=false);
    ~BossRenderer();
    BossRenderer(const BossRenderer&)=delete;
    BossRenderer& operator=(const BossRenderer&)=delete;
    void Update(BossSimulation& boss);
    void SetAudioEventSink(std::function<void(const BossPoseAudioEvent&)> sink);
    int DrawShadow(const BossSimulation& boss,const glm::mat4& lightViewProjection);
    int Draw(const BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,
             const glm::vec3& camera,const glm::vec3& squadColor,const CharacterLightState* lights=nullptr);
    int DrawHazards(const BossSimulation& boss,const StageGeometry& geometry,
                    const glm::mat4& viewProjection,const glm::vec3& squadColor);
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace restoration
