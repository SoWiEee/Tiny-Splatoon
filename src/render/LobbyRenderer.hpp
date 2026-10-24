#pragma once

#include "CharacterArt.hpp"
#include "../game/LobbyLayout.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace restoration {

// Loads only generated native assets; the read-only source tree is never a runtime dependency.
class LobbyRenderer {
public:
    explicit LobbyRenderer(std::filesystem::path assets);
    ~LobbyRenderer();
    LobbyRenderer(const LobbyRenderer&) = delete;
    LobbyRenderer& operator=(const LobbyRenderer&) = delete;

    const LobbyLayout& Layout() const;
    const CharacterLightState& Lights() const;
    // Applies the source key spotlight's cone and distance attenuation at a world-space actor position.
    CharacterLightState LightingAt(glm::dvec3 worldPosition) const;
    // Records source ground-shader ripples as x/z, absolute lobby time and strength.
    void AddRipple(glm::dvec3 position, double absoluteTime, double strength=1.0);
    void ResetRipples();
    void SetReflectionsEnabled(bool enabled);
    void SetKeyShadowEnabled(bool enabled);
    using ReflectionDraw = std::function<void(const glm::dmat4&,const glm::dmat4&,glm::dvec3)>;
    using ShadowDraw = std::function<void(const glm::dmat4&,const glm::dmat4&,glm::dvec3,const CharacterShadowState&)>;
    CharacterShadowState KeyShadowState() const;
    int Draw(const glm::dmat4& view, const glm::dmat4& projection, glm::dvec3 eye,
             double time, glm::dvec3 teamA, glm::dvec3 teamB,const ReflectionDraw& reflectionDraw = {},
             const ShadowDraw& shadowDraw = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
