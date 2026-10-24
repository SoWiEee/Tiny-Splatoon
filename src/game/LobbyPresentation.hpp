#pragma once

#include "LobbyLayout.hpp"
#include "PlayerProfile.hpp"
#include "../network/RoomTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace restoration {

struct LobbyActorState {
    network::MemberId id = 0;
    std::string name;
    CharacterStyle style{};
    std::string main = "shooter", sub;
    int team = 0, mark = -1;
    glm::dvec3 position{0}, velocityWorld{0};
    double speed = 0, velocityY = 0, yaw = 0, pitch = 0, twirl = 0, squash = 1;
    std::string form = "kid", dance;
    bool visible = false, ready = false, host = false, local = false;
};

struct LobbyPresentationEvent {
    std::string kind;
    network::MemberId id = 0;
    glm::dvec3 position{0};
    double strength = 0;
};

class LobbyPresentation {
public:
    explicit LobbyPresentation(const LobbyLayout& layout);
    ~LobbyPresentation();
    LobbyPresentation(const LobbyPresentation&) = delete;
    LobbyPresentation& operator=(const LobbyPresentation&) = delete;

    void Update(const network::RoomView& view, const PlayerProfile& profile, double dt);
    const std::vector<LobbyActorState>& actors() const;
    std::vector<LobbyPresentationEvent> takeEvents();
    const LobbyCamera& camera() const;
    bool hubMode() const;
    int countdown() const;
    bool launching() const;
    bool launchComplete() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
