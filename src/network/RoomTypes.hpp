#pragma once

#include "../game/MatchParticipant.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace restoration::network {

using MemberId = PeerId;

enum class RoomPhase : std::uint8_t { Offline, Connecting, Lobby, Starting, Match, Error };

struct Member {
    MemberId id = 0;
    std::string name = "Player";
    int team = -1;
    bool autoTeam = true;
    bool ready = false;
    std::string main = "shooter", sub, special;
    CharacterStyle style{};
    std::uint32_t ping = 0;
    std::string emote;
    std::uint64_t emoteSerial=0; // Local presentation sequence; not part of the wire profile.
};

struct RoomSettings {
    std::string map = "tidewater", time = "day", mode = "turf";
    std::uint32_t duration = 180;
    bool bots = true;
    std::string difficulty = "normal";
    std::uint32_t palette = 0;
};

struct RoomView {
    RoomPhase phase = RoomPhase::Offline;
    std::string code, status, error;
    MemberId selfId = 0, hostId = 0;
    RoomSettings settings{};
    std::vector<Member> members;
    std::uint64_t matchEpoch = 0;
    std::uint32_t seed = 0;
};

using RosterEntry = MatchParticipant;

struct GamePacket {
    MemberId from = 0;
    std::vector<std::uint8_t> payload;
};

} // namespace restoration::network
