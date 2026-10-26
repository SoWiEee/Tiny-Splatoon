#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace restoration {
struct LobbyActorState;
struct LobbyPresentationEvent;
struct CharacterLightState;

class LobbyEffects {
public:
    struct Counts { std::size_t trailDomes=0, particles=0, sparkles=0, splats=0; };

    explicit LobbyEffects(std::filesystem::path particleAssets);
    ~LobbyEffects();
    LobbyEffects(const LobbyEffects&) = delete;
    LobbyEffects& operator=(const LobbyEffects&) = delete;

    void Update(const std::vector<LobbyActorState>& actors,
                const std::vector<LobbyPresentationEvent>& events,
                double dt, double time);
    int Draw(const glm::dmat4& view, const glm::dmat4& projection, glm::dvec3 eye,
             const CharacterLightState& lights, double time);
    Counts counts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
