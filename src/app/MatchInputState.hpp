#pragma once
#include "../game/MatchSimulation.hpp"
#include "../game/PlayerController.hpp"

namespace restoration {
enum class MatchCursorMode { Free, Captured, Map };
struct MatchInputSample {
    ControllerInput controls;
    double cursorX=0,cursorY=0;
    bool focused=true,pause=false,gear=false,map=false,resumeAllowed=true;
    bool activeSub=false,activeSpecial=false,hasMapTarget=false,padActive=false;
    std::array<bool,10> mapKeys{};
};
struct MatchInputEffects {
    MatchCursorMode cursor=MatchCursorMode::Free;
    bool cursorChanged=false,resetClock=false,pauseAudio=false,openGear=false,confirmMap=false;
};
struct MatchInputTick {
    ControllerInput controls;
    PlayerInput actions;
    bool updateLook=false,suppressMove=false;
    std::array<bool,10> mapKeys{};
};

// Owns interaction transitions and held-input policy; GLFW only supplies snapshots.
class MatchInputState {
public:
    explicit MatchInputState(bool online=false):online_(online){}
    MatchInputEffects Sample(MatchPhase phase,const MatchInputSample& sample);
    MatchInputEffects PhaseChanged(MatchPhase phase);
    MatchInputEffects Resume();
    void RebasePointer(double x,double y);
    MatchInputTick Consume(bool hasMapTarget=false);
    bool paused() const { return paused_; }
    bool mapOpen() const { return mapOpen_; }
    bool simulationPaused() const { return paused_&&!online_; }
    bool shouldTick() const { return !paused_||online_; }
private:
    void Pause(MatchInputEffects& effects);
    void Resume(MatchInputEffects& effects);
    void Cursor(MatchInputEffects& effects,bool force=false);
    void LockLook();
    bool online_=false,paused_=false,mapOpen_=false;
    bool pauseWas_=false,gearWas_=false,confirmWas_=false,cheerWas_=false,leftWas_=false;
    bool fireBlocked_=true,pauseSub_=false,pauseSpecial_=false,subResume_=false,specialResume_=false;
    bool havePointer_=false,usingPad_=false;
    MatchPhase phase_=MatchPhase::Intro;
    MatchCursorMode cursor_=MatchCursorMode::Free;
    double mouseX_=0,mouseY_=0,pendingX_=0,pendingY_=0;
    std::array<bool,10> mapKeysWas_{};
    MatchInputSample sample_;
};
}
