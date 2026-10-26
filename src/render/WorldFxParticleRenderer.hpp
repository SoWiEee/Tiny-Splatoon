#pragma once

#include "WorldFxRecipes.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace restoration {
struct CharacterLightState;

// Draws the six short-lived pools from splat-ref/src/fx/fx.js with their
// exported source GLSL and source base meshes.
class WorldFxParticleRenderer {
public:
    explicit WorldFxParticleRenderer(std::filesystem::path assets = "assets/effects/particles",
                                    std::filesystem::path bossAssets = "assets/models/boss/shaders");
    ~WorldFxParticleRenderer();
    WorldFxParticleRenderer(const WorldFxParticleRenderer&) = delete;
    WorldFxParticleRenderer& operator=(const WorldFxParticleRenderer&) = delete;

    int Draw(const std::vector<WorldFxParticle>& live,
             const std::vector<WorldFxParticle>& world,
             const glm::mat4& view,
             const glm::mat4& projection,
             const glm::vec3& eye,
             const CharacterLightState* lights,
             double time);

private:
    struct Pool;
    struct BossBlobPool;
    void initialize();
    void initializeBossBlob();
    std::filesystem::path assets_;
    std::filesystem::path bossAssets_;
    std::array<std::unique_ptr<Pool>, 6> pools_;
    std::unique_ptr<BossBlobPool> bossBlobPool_;
};
}
