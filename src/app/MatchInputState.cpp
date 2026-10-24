#include "MatchInputState.hpp"
#include <utility>

namespace restoration {
void MatchInputState::LockLook(){pendingX_=pendingY_=0;fireBlocked_=true;}
void MatchInputState::RebasePointer(double x,double y){mouseX_=x;mouseY_=y;havePointer_=true;pendingX_=pendingY_=0;}
void MatchInputState::Cursor(MatchInputEffects& effects,bool force){
    const auto desired=paused_?MatchCursorMode::Free:mapOpen_?MatchCursorMode::Map:
        (phase_==MatchPhase::Intro||phase_==MatchPhase::Playing)?MatchCursorMode::Captured:MatchCursorMode::Free;
    effects.cursor=desired;effects.cursorChanged=force||desired!=cursor_;
    if(effects.cursorChanged)LockLook();
    cursor_=desired;
}
void MatchInputState::Pause(MatchInputEffects& effects){
    pauseSub_=sample_.controls.sub||sample_.activeSub;
    pauseSpecial_=sample_.controls.special||sample_.activeSpecial;
    paused_=true;mapOpen_=false;LockLook();effects.resetClock=true;effects.pauseAudio=!online_;
}
void MatchInputState::Resume(MatchInputEffects& effects){
    if(!paused_)return;
    paused_=false;subResume_=pauseSub_;specialResume_=pauseSpecial_;
    LockLook();effects.resetClock=true;
}
MatchInputEffects MatchInputState::Resume(){MatchInputEffects effects;Resume(effects);Cursor(effects);return effects;}
MatchInputEffects MatchInputState::PhaseChanged(MatchPhase phase){
    const bool changed=phase_!=phase;phase_=phase;
    if(changed)LockLook();
    if(phase_!=MatchPhase::Playing)mapOpen_=false;
    MatchInputEffects effects;Cursor(effects,changed&&phase_==MatchPhase::Playing);return effects;
}
MatchInputEffects MatchInputState::Sample(MatchPhase phase,const MatchInputSample& sample){
    const bool phaseChanged=phase_!=phase;
    auto effects=PhaseChanged(phase);sample_=sample;
    if(phase_==MatchPhase::Playing&&sample.pause&&!pauseWas_){
        if(!paused_)Pause(effects);else if(sample.resumeAllowed)Resume(effects);
    }
    pauseWas_=sample.pause;
    if(sample.gear&&!gearWas_&&!paused_){Pause(effects);effects.openGear=true;}
    gearWas_=sample.gear;
    if(!paused_&&phase_==MatchPhase::Playing&&!sample.focused)Pause(effects);
    const bool map=!paused_&&phase_==MatchPhase::Playing&&!sample.hasMapTarget&&sample.map;
    if(map!=mapOpen_){mapOpen_=map;LockLook();}
    const bool phaseCursorChanged=effects.cursorChanged;
    Cursor(effects,phaseCursorChanged);
    if(sample.padActive)usingPad_=true;
    if(havePointer_&&!phaseChanged&&!effects.cursorChanged&&!paused_&&!mapOpen_&&phase_==MatchPhase::Playing){
        pendingX_+=sample.cursorX-mouseX_;pendingY_+=sample.cursorY-mouseY_;
        if(sample.cursorX!=mouseX_||sample.cursorY!=mouseY_)usingPad_=false;
    }else pendingX_=pendingY_=0;
    mouseX_=sample.cursorX;mouseY_=sample.cursorY;havePointer_=true;
    const bool confirm=sample.controls.gamepadConnected&&sample.controls.gamepadButtons[0];
    effects.confirmMap=mapOpen_&&confirm&&!confirmWas_;confirmWas_=mapOpen_&&confirm;
    return effects;
}
MatchInputTick MatchInputState::Consume(bool hasMapTarget){
    MatchInputTick result;
    if(paused_){pendingX_=pendingY_=0;return result;}
    if(mapOpen_||phase_!=MatchPhase::Playing){
        result.updateLook=phase_==MatchPhase::Intro;
        pendingX_=pendingY_=0;
        leftWas_=!mapOpen_&&sample_.controls.fire;cheerWas_=!mapOpen_&&sample_.controls.cheer;
        for(std::size_t i=0;i<mapKeysWas_.size();++i){result.mapKeys[i]=mapOpen_&&sample_.mapKeys[i]&&!mapKeysWas_[i];mapKeysWas_[i]=sample_.mapKeys[i];}
        return result;
    }
    result.updateLook=true;
    result.controls=sample_.controls;result.controls.usingPad=usingPad_;
    result.controls.mouseDX=std::exchange(pendingX_,0.0);result.controls.mouseDY=std::exchange(pendingY_,0.0);
    auto& actions=result.actions;
    actions.jump=sample_.controls.jump;actions.squid=sample_.controls.squid;
    const bool left=sample_.controls.fire;
    if(!left)fireBlocked_=false;
    actions.fire=left&&!fireBlocked_;actions.sub=sample_.controls.sub;
    if(subResume_&&!actions.sub){actions.squid=true;actions.fire=false;result.suppressMove=true;subResume_=false;}
    actions.special=sample_.controls.special&&!specialResume_;
    if(specialResume_&&!sample_.controls.special)specialResume_=false;
    actions.cheer=sample_.controls.cheer&&!cheerWas_;cheerWas_=sample_.controls.cheer;
    actions.hasMapTarget=hasMapTarget;
    actions.specialConfirm=hasMapTarget&&left&&!fireBlocked_&&!leftWas_;leftWas_=left;
    for(std::size_t i=0;i<mapKeysWas_.size();++i)mapKeysWas_[i]=sample_.mapKeys[i];
    return result;
}
}
