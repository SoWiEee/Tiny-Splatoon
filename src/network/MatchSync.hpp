#pragma once

#include "RoomSession.hpp"
#include "Wire.hpp"
#include "AuthorityState.hpp"
#include "../game/MatchSimulation.hpp"
#include "../game/PlayerProfile.hpp"

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>

namespace restoration::network {

// Online gameplay transport for one RoomSession / MatchSimulation pair. The room
// layer remains responsible for polling GNS; this class only consumes and emits
// room game packets.
class MatchSync {
public:
    static MatchOptions BuildOptions(RoomSession& session, const PlayerProfile& profile);

    MatchSync(RoomSession& session, MatchSimulation& match, FacePaint& paint);
    ~MatchSync();
    MatchSync(const MatchSync&) = delete;
    MatchSync& operator=(const MatchSync&) = delete;

    // Call after RoomSession::Update. This handles packets, ownership changes,
    // ghost interpolation and the 20 Hz outgoing snapshot cadence.
    void Update(double now, double frameDt);
    // Advances gameplay only while the room is in GO/Match.
    void Tick(double dt);
    const std::string& failure() const { return failure_; }

private:
    enum class PacketKind : std::uint8_t;
    struct ActorSample {
        double received = 0.0, remoteTime = 0.0;
        PlayerState state{};
        PlayerInput input{};
        CombatPose combat{};
        SpecialPose special{};
        std::optional<SuperJumpState> jump;
        bool active = true;
        double turf = 0.0, zoneTurf = 0.0;
        int deaths = 0, splats = 0, shots = 0;
    };
    struct PendingAssembly {
        std::uint64_t sequence = 0;
        PacketKind kind{};
        std::uint16_t count = 0;
        std::uint32_t total = 0;
        double started = 0.0;
        std::vector<std::optional<std::vector<std::uint8_t>>> chunks;
        std::size_t received = 0;
    };
    struct QueuedEvent { PacketKind kind{}; std::vector<std::uint8_t> body; };

    void receive(const GamePacket& packet, double now);
    void send(PacketKind kind, std::vector<std::uint8_t> body, bool reliable);
    void queueReliable(PacketKind kind, std::vector<std::uint8_t> body);
    void flushReliableEvents();
    void applyReliable(const GamePacket& packet, PacketKind kind, Reader& reader);
    void sendOwnedSnapshots(double now);
    void sendAuthorityState();
    void sendBossState();
    void sendPaint(const PaintJournalEvent& event);
    void sendCells(const std::vector<int>& cells, int team);
    void sendVisual(const WorldVisualEvent& event);
    void sendHit(PlayerSimulation& attacker, PlayerSimulation& victim, double damage, const std::string& source);
    void sendDeviceHit(std::uint32_t ownerNid, std::uint64_t id, double damage, int team);
    void sendBossHit(PlayerSimulation& attacker, double damage, const BossSegmentHit& hit, const std::string& weapon);
    void sendBossSplash(PlayerSimulation& attacker, glm::dvec3 center, double radius, double maxDamage, double minDamage, const std::string& weapon);
    void applyRoster(const std::vector<RosterEntry>& roster);
    void applySnapshot(const GamePacket& packet, Reader& reader, double now);
    void applyActorSample(std::uint32_t nid, ActorSample sample, double now);
    void interpolateGhosts(double now, double frameDt);
    void applyAuthorityState(const GamePacket& packet, Reader& reader);
    void applyHit(const GamePacket& packet, Reader& reader);
    void applyDeviceHit(const GamePacket& packet, Reader& reader);
    void applyBossHit(const GamePacket& packet, Reader& reader);
    void applyBossSplash(const GamePacket& packet, Reader& reader);
    void applyPaint(const GamePacket& packet, Reader& reader);
    void applyCells(const GamePacket& packet, Reader& reader);
    void applyVisual(const GamePacket& packet, Reader& reader);
    void applyTrigger(const GamePacket& packet, Reader& reader);
    void applyActorEffect(const GamePacket& packet, Reader& reader);
    void applyBeaconUse(const GamePacket& packet, Reader& reader);
    void applyChunk(const GamePacket& packet, Reader& reader, double now, std::uint64_t sequence);
    PlayerSimulation* actor(std::uint32_t nid) const;
    const MatchRosterEntry* rosterEntry(std::uint32_t nid) const;
    bool ownsActor(MemberId owner, std::uint32_t nid) const;

    RoomSession& session_;
    MatchSimulation& match_;
    FacePaint& paint_;
    std::uint32_t previousPaintActor_ = 0;
    MemberId localPeer_ = 0, authorityPeer_ = 0;
    std::uint64_t epoch_ = 0;
    double nextSnapshot_ = 0.0, nextAuthority_ = 0.0;
    double lastSnapshotReceive_ = 0.0, jitterBuffer_ = 0.12;
    std::uint64_t sendSequence_ = 0;
    std::uint64_t paintSequence_ = 0, hitSequence_ = 0;
    std::unordered_map<MemberId, double> senderClockOffset_;
    std::unordered_map<MemberId, std::uint64_t> lastSequence_;
    std::unordered_map<MemberId, std::uint64_t> lastReliableSequence_;
    std::unordered_map<MemberId, std::uint64_t> lastAppliedHit_;
    std::unordered_map<MemberId, std::uint64_t> lastAppliedPaint_;
    std::unordered_map<MemberId, std::uint64_t> lastAppliedVisual_;
    std::unordered_map<std::uint32_t, std::deque<ActorSample>> samples_;
    std::map<std::pair<MemberId, std::uint64_t>, PendingAssembly> assemblies_;
    std::unordered_map<std::uint32_t, std::vector<ShooterProjectile>> remoteProjectiles_;
    std::unordered_map<std::uint32_t, std::vector<CombatDevice>> remoteDevices_;
    std::unordered_map<std::uint32_t, std::vector<WorldObjectView>> remoteObjects_;
    std::optional<BossAuthorityState> latestBossState_;
    std::optional<ZoneAuthorityState> latestZoneState_;
    std::vector<RosterEntry> lastRoster_;
    std::vector<QueuedEvent> reliableEvents_;
    std::size_t reliableEventBytes_ = 0;
    std::string failure_;
    bool applyingNetwork_ = false;
};

} // namespace restoration::network
