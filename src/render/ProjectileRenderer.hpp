#pragma once
#include "ProjectileVisuals.hpp"
#include <filesystem>
#include <memory>

namespace restoration {
class CombatWorld;
struct WorldVisualEvent;
struct CharacterLightState;
class ProjectileRenderer {
public:
    explicit ProjectileRenderer(const std::filesystem::path& assets="assets/reference/projectiles",bool environment=false);
    ~ProjectileRenderer();
    void Update(double dt,const CombatWorld& world,const std::vector<WorldVisualEvent>& events);
    int Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights);
    const std::vector<ProjectileBlob>& blobs() const;
    std::size_t ribbons() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
