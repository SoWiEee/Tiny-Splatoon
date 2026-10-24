#include "../src/app/MatchInputState.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace restoration;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
void frame(MatchInputState& state,MatchInputSample& sample){state.Sample(MatchPhase::Playing,sample);}
}
int main(){try{
    MatchInputState state;MatchInputSample sample;
    auto effects=state.Sample(MatchPhase::Intro,sample);
    check(effects.cursorChanged&&effects.cursor==MatchCursorMode::Captured,"Intro must establish pointer capture without enabling look");
    for(int i=0;i<5;++i){sample.cursorX+=100;sample.controls.fire=true;sample.controls.rightStickX=1;sample.controls.gamepadConnected=true;sample.padActive=true;state.Sample(MatchPhase::Intro,sample);auto tick=state.Consume();check(tick.controls.mouseDX==0&&!tick.controls.gamepadConnected&&!tick.actions.fire,"Intro must discard look and action input");}
    effects=state.PhaseChanged(MatchPhase::Playing);
    check(effects.cursorChanged,"Playing must rebase even if Intro already captured the cursor");
    state.RebasePointer(500,0);sample.cursorX=500;frame(state,sample);
    auto tick=state.Consume();check(tick.controls.mouseDX==0&&!tick.actions.fire,"Intro motion and held start-click must not leak into play");
    sample.controls.fire=false;sample.controls.rightStickX=0;frame(state,sample);state.Consume();
    sample.cursorX+=7;sample.cursorY-=3;sample.controls.fire=true;frame(state,sample);
    tick=state.Consume(true);check(near(tick.controls.mouseDX,7)&&near(tick.controls.mouseDY,-3)&&tick.actions.fire&&tick.actions.specialConfirm,"Fresh play input must retain deltas and special confirm");
    tick=state.Consume(true);check(tick.controls.mouseDX==0&&!tick.actions.specialConfirm,"Fixed ticks must consume look and press edges once");
    sample.controls.cheer=true;frame(state,sample);check(state.Consume().actions.cheer&&!state.Consume().actions.cheer,"Cheer is a rising edge across fixed ticks");

    sample.map=true;sample.mapKeys[1]=true;sample.controls.gamepadButtons[0]=true;effects=state.Sample(MatchPhase::Playing,sample);
    check(state.mapOpen()&&effects.cursor==MatchCursorMode::Map&&effects.confirmMap,"Opening the map must use its hidden free cursor and pad-confirm edge");
    tick=state.Consume(true);check(tick.mapKeys[1]&&!tick.updateLook&&!tick.actions.fire&&!tick.actions.hasMapTarget&&tick.controls.mouseDX==0,"Map selection must suppress all gameplay actions");
    check(!state.Consume().mapKeys[1],"Map digit press cannot repeat on later fixed ticks");
    check(!state.Sample(MatchPhase::Playing,sample).confirmMap,"Held pad confirm cannot repeat each frame");
    sample.cursorX=9000;sample.map=false;effects=state.Sample(MatchPhase::Playing,sample);
    check(!state.mapOpen()&&effects.cursorChanged&&effects.cursor==MatchCursorMode::Captured,"Closing map must restore capture");
    check(state.Consume().controls.mouseDX==0&&!state.Consume().actions.fire,"Map pointer movement and held fire must not leak on close");
    sample.map=true;sample.hasMapTarget=true;frame(state,sample);check(!state.mapOpen(),"Special targeting must exclude the jump map");
    sample.map=false;sample.hasMapTarget=false;

    sample.pause=true;sample.controls.sub=true;sample.controls.special=true;effects=state.Sample(MatchPhase::Playing,sample);
    check(state.paused()&&state.simulationPaused()&&!state.shouldTick()&&effects.resetClock&&effects.pauseAudio,"Offline pause must stop simulation, reset accumulator and pause audio");
    check(!state.Consume().actions.sub,"Pause must yield neutral actor input");
    sample.pause=false;sample.controls.sub=false;sample.controls.special=true;sample.cursorX=12000;frame(state,sample);
    effects=state.Resume();check(!state.paused()&&effects.resetClock&&effects.cursor==MatchCursorMode::Captured,"UI resume must use the same transition as Escape");
    state.RebasePointer(12000,0);tick=state.Consume();
    check(tick.suppressMove&&tick.actions.squid&&!tick.actions.fire&&!tick.actions.sub&&!tick.actions.special,"Resuming after releasing sub must cancel release and suppress held special");
    check(!state.Consume().suppressMove,"Sub release synchronization occurs only once");
    sample.controls.special=false;sample.controls.fire=false;frame(state,sample);state.Consume();
    sample.controls.special=true;sample.controls.fire=true;frame(state,sample);tick=state.Consume();check(tick.actions.special&&tick.actions.fire,"Releasing and pressing again must unblock actions");

    sample.focused=false;effects=state.Sample(MatchPhase::Playing,sample);check(state.paused()&&effects.resetClock,"Focus loss must enter the same pause state");
    sample.focused=true;sample.resumeAllowed=false;sample.pause=true;frame(state,sample);check(state.paused(),"Escape must not resume through a nested settings screen");
    sample.pause=false;frame(state,sample);sample.resumeAllowed=true;sample.pause=true;frame(state,sample);check(!state.paused(),"Escape must resume from the actual pause screen");
    sample.pause=false;sample.gear=true;effects=state.Sample(MatchPhase::Playing,sample);check(state.paused()&&effects.openGear,"Practice gear opens through the pause transition");

    MatchInputState online(true);MatchInputSample remote;online.Sample(MatchPhase::Playing,remote);remote.pause=true;effects=online.Sample(MatchPhase::Playing,remote);
    check(online.paused()&&!online.simulationPaused()&&online.shouldTick()&&!effects.pauseAudio,"Online menu must retain live simulation and audio");
    check(!online.Consume().actions.fire,"Online paused actor remains neutral while match ticks");
    online.Resume();remote.pause=false;remote.map=true;online.Sample(MatchPhase::Playing,remote);check(online.mapOpen(),"Online map opens after resume");
    effects=online.PhaseChanged(MatchPhase::Results);check(!online.mapOpen()&&effects.cursor==MatchCursorMode::Free,"Results must close map and release pointer");
    tick=online.Consume();check(!tick.updateLook&&!tick.actions.fire,"Terminal phases must not accept gameplay input");

    MatchInputState subHeld;MatchInputSample held;subHeld.Sample(MatchPhase::Playing,held);held.activeSub=true;held.pause=true;subHeld.Sample(MatchPhase::Playing,held);subHeld.Resume();
    check(subHeld.Consume().suppressMove,"Pause must remember actor-held sub even if physical input was just released");
    MatchInputState heldAcrossPause;MatchInputSample charging;charging.controls.sub=true;
    heldAcrossPause.Sample(MatchPhase::Playing,charging);charging.pause=true;heldAcrossPause.Sample(MatchPhase::Playing,charging);heldAcrossPause.Resume();
    tick=heldAcrossPause.Consume();check(tick.actions.sub&&!tick.suppressMove,"Held sub should continue charging after resume until physical release");
    charging.pause=false;charging.controls.sub=false;heldAcrossPause.Sample(MatchPhase::Playing,charging);
    check(heldAcrossPause.Consume().suppressMove,"Releasing the held sub later must still synchronize cancellation");
    MatchInputState accumulated;MatchInputSample motion;accumulated.Sample(MatchPhase::Playing,motion);accumulated.RebasePointer(0,0);
    motion.cursorX=2;accumulated.Sample(MatchPhase::Playing,motion);motion.cursorX=9;accumulated.Sample(MatchPhase::Playing,motion);
    check(near(accumulated.Consume().controls.mouseDX,9)&&accumulated.Consume().controls.mouseDX==0,"Mouse deltas must accumulate across render frames and drain once per fixed step");
    std::cout<<"MATCH_INPUT intro/map/pause/focus/gear/online transitions and fixed-step input gating passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
