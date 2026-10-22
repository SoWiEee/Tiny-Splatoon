#pragma once

#include "../game/BossSimulation.hpp"
#include "../game/ZoneControl.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace restoration::network {

struct BossAuthorityState {
    BossState state;
    std::optional<BossMove> move;
    std::vector<BossMove> moves;
    std::vector<BossCrablet> crablets;
    std::array<BossHitShape, 10> hitShapes{};
    std::vector<BossPoseSocket> poseSockets;
    bool externalHitShapes = false;
    std::array<double, 8> damage{};
    std::array<int, 8> weakHits{}, splats{};
    std::vector<std::string> hitGuards;
    bool finished = false;
};

struct ZoneAuthorityEntry {
    int id = -1;
    int owner = -1;
    std::array<double, 2> share{};
    double hold = 0.0, warnAt = -99.0;
    bool warnArmed = true;
    std::optional<ZoneFloodState> flood;
};

struct ZoneAuthorityFloodRun {
    int zone = -1, team = -1;
    std::size_t next = 0;
    double elapsed = 0.0, duration = 0.0, reach = 0.0;
    bool easeOut = false;
};

struct ZoneAuthorityState {
    double duration = 300.0, clock = 0.0, sampleT = 0.0, neutralT = 0.0, overtimeT = 0.0;
    std::vector<ZoneAuthorityEntry> zones;
    std::vector<ZoneAuthorityFloodRun> floods;
    int activeObjective = 0, owner = -1, lastOwner = -1, overtimeLosing = -1;
    std::array<double, 2> count{{100.0, 100.0}}, penalty{}, start{{100.0, 100.0}};
    std::array<std::optional<double>, 2> tieEnd{};
    std::array<double, 2> hold{};
    std::vector<ZonePenaltyRecord> penaltyLog;
    double nextSwap = 0.0;
    bool final = false, overtime = false;
    std::optional<int> winner;
    std::string reason;
};

inline constexpr std::size_t MaxAuthorityMoves = 128;
inline constexpr std::size_t MaxAuthorityCrablets = 256;
inline constexpr std::size_t MaxAuthoritySockets = 64;
inline constexpr std::size_t MaxAuthorityFloods = 32;
inline constexpr std::size_t MaxAuthorityHitGuards = 1024;

std::vector<std::uint8_t> EncodeBossAuthorityState(const BossAuthorityState& state);
bool DecodeBossAuthorityState(const std::vector<std::uint8_t>& bytes, BossAuthorityState& state,
                              std::string* error = nullptr);
std::vector<std::uint8_t> EncodeZonesAuthorityState(const ZoneAuthorityState& state);
bool DecodeZonesAuthorityState(const std::vector<std::uint8_t>& bytes, ZoneAuthorityState& state,
                               std::string* error = nullptr);

} // namespace restoration::network
