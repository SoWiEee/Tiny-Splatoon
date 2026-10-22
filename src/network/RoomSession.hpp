#pragma once

#include "GnsTransport.hpp"
#include "RoomTypes.hpp"

#include <memory>
#include <string>
#include <vector>

namespace restoration::network {

class RoomSession {
public:
    explicit RoomSession(GnsRuntime& runtime);
    ~RoomSession();
    RoomSession(const RoomSession&) = delete;
    RoomSession& operator=(const RoomSession&) = delete;

    void Create(const SteamNetworkingIPAddr& address, Member me);
    void Join(const SteamNetworkingIPAddr& address, std::string code, Member me);
    void Leave();
    void Update(double now);
    void SetMe(Member me);
    void SetTeam(int team);
    void SetReady(bool ready);
    void SetSettings(RoomSettings settings);
    void Emote(std::string name);
    bool StartMatch();
    void MarkLoaded();
    void ReturnToLobby();
    bool SendGame(std::vector<std::uint8_t> payload, MemberId toMemberId = 0, bool reliable = true);
    std::vector<GamePacket> TakeGamePackets();

    const RoomView& view() const;
    const std::vector<RosterEntry>& startRoster() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration::network
