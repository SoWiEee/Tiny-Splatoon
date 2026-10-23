#pragma once

#include "AudioEngine.hpp"

#include <string>
#include <array>
#include <unordered_map>

namespace restoration {

class MatchSimulation;
class PlayerSimulation;
struct CharacterTrigger;
struct WorldVisualEvent;

class GameAudio {
public:
    explicit GameAudio(AudioEngine& engine) : engine_(engine) {}

    void ActorTrigger(PlayerSimulation& actor, const CharacterTrigger& event);
    void Footstep(PlayerSimulation& actor, AudioPosition position, double speed);
    void Update(double dt, MatchSimulation& match, const std::vector<WorldVisualEvent>& events,
        AudioPosition listenerPos, AudioPosition forward, AudioPosition up = {0, 1, 0});
    void Menu(bool active);
    void BeginMatch(MatchSimulation& match);
    void EndMatch();
    void Pause();

private:
    struct ActorLoop {
        std::string key, cue;
    };
    struct ActorState {
        bool alive = false, squid = false, lowInk = false, specialReady = false, specialActive = false;
        bool chargeFull = false, streaming = false;
        int bowRingEvents = 0;
        std::string specialKind;
        std::string canopy;
        double canopyOpen = 0, canopyHp = 0;
    };
    struct DeviceState {std::string state,loop;bool hopping=false;int uses=0;};
    void UpdateDeviceAudio(MatchSimulation& match);
    void Play(std::string_view name, const PlayerSimulation* actor = nullptr, double volume = 1, double pitch = 1);
    void UpdateBoss(double dt, MatchSimulation& match);
    void ClearBossPendingAudio();
    void UpdateZones(double dt,MatchSimulation& match);
    void StopZoneLoops();
    void UpdateWorldAudio(double dt, MatchSimulation& match);
    void StopWorldLoops();
    void StopLoops(bool preserveWail = false);
    void UpdateWailAudio(MatchSimulation& match);
    void UpdateActorAudio(PlayerSimulation& actor);

    AudioEngine& engine_;
    MatchSimulation* match_ = nullptr;
    std::unordered_map<const PlayerSimulation*, ActorLoop> actorLoops_;
    std::unordered_map<const PlayerSimulation*, ActorLoop> weaponLoops_;
    std::unordered_map<const PlayerSimulation*, ActorState> actorStates_;
    std::unordered_map<std::uint64_t,DeviceState> deviceStates_;
    std::unordered_map<std::uint64_t,AudioPosition> wailLoops_;
    std::unordered_map<int, bool> crablets_;
    std::string bossLoop_, bossMove_;
    AudioPosition listenerPosition_{};
    std::uint32_t audioRandomState_ = 0x6d2b79f5u;
    std::array<double,8> bossDamage_{};
    std::array<int,8> bossWeakHits_{};
    std::array<const PlayerSimulation*,8> bossDamageActors_{};
    double previousRemaining_ = 0, bossSunkIn_ = 0, bossHitAt_=-99, bossCritAt_=-99;
    double bossIntroSlamAt_=-1,bossIntroRoarAt_=-1,bossSlamAt_=-1,bossChargeRoarAt_=-1,bossCrabletSecondAt_=-1;
    double bossPhaseRoarAt_=-1,bossDizzyAt_=-1;
    AudioPosition bossIntroSlamPos_{},bossIntroRoarPos_{},bossSlamPos_{},bossChargeRoarPos_{},bossCrabletPos_{},bossBarragePos_{},bossPhaseRoarPos_{},bossDizzyPos_{};
    double bossBarrageStartedAt_=-1;
    int bossBarrageCount_=0,bossBarrageWhistles_=0,bossBarrageBarrels_=0;
    double gullTimer_ = 4, gustTimer_ = 2, clinkTimer_ = 0, hornTimer_ = 32;
    int pendingHalyardClinks_ = 0;
    int bossPhase_ = 0, matchPhase_ = -1;
    bool harborLoop_ = false, pausedAudio_ = false, resumeLoops_ = false;
    bool hasListenerPosition_ = false, bossDead_ = false, playing_ = false,bossIntroSeen_=false,bossStunSeen_=false,bossDamageSeen_=false;
    std::size_t zoneEventCursor_=0;
    std::unordered_map<int,double> zoneHums_;
    std::array<int,2> zoneTicks_{{100,100}};
    double zoneHumAccumulator_=0,zoneWarnAt_=-99;
};

} // namespace restoration
