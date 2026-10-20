#pragma once

#include "AudioEngine.hpp"

#include <string>
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
        std::string specialKind;
    };
    void Play(std::string_view name, const PlayerSimulation* actor = nullptr, double volume = 1, double pitch = 1);
    void UpdateBoss(double dt, MatchSimulation& match);
    void UpdateWorldAudio(double dt, MatchSimulation& match);
    void StopWorldLoops();
    void StopLoops();
    void UpdateActorAudio(PlayerSimulation& actor);

    AudioEngine& engine_;
    MatchSimulation* match_ = nullptr;
    std::unordered_map<const PlayerSimulation*, ActorLoop> actorLoops_;
    std::unordered_map<const PlayerSimulation*, ActorState> actorStates_;
    std::unordered_map<int, bool> crablets_;
    std::string bossLoop_, bossMove_;
    AudioPosition listenerPosition_{};
    std::uint32_t audioRandomState_ = 0x6d2b79f5u;
    double previousRemaining_ = 0, bossHp_ = -1, bossSunkIn_ = 0;
    double gullTimer_ = 4, gustTimer_ = 2, clinkTimer_ = 0, hornTimer_ = 32;
    int pendingHalyardClinks_ = 0;
    int bossPhase_ = 0, matchPhase_ = -1;
    bool harborLoop_ = false, pausedAudio_ = false, resumeLoops_ = false;
    bool hasListenerPosition_ = false, bossDead_ = false, playing_ = false;
};

} // namespace restoration
