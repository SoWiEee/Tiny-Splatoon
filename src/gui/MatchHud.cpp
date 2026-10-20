#include "MatchHud.hpp"
#include "FacePaint.hpp"
#include "ArsenalData.hpp"
#include "MatchSimulation.hpp"
#include "StageGeometry.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace restoration {
MatchHudAction DrawTurfHud(const StageData& stage,const FacePaint& paint,
                           MatchSimulation& match,int width,int height,bool paused,bool nativeApp) {
    MatchHudAction action=MatchHudAction::None;
    if(const auto* boss=match.boss()){
        const auto coverage=paint.coverage();
        if(match.phase()==MatchPhase::Results){
            ImGui::SetNextWindowPos(ImVec2(width*.5f,height*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
            ImGui::SetNextWindowSize(ImVec2(650,0));
            ImGui::Begin("Boss Battle results",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
            const auto& result=match.result();const bool win=result&&result->boss&&result->boss->win;
            ImGui::TextColored(win?ImVec4(.35f,1.0f,.57f,1):ImVec4(1.0f,.42f,.25f,1),"%s",win?"HULLBREAKER DEFEATED":"SQUAD DEFEATED");
            if(result&&result->boss){
                ImGui::Text("Fight time %02d:%04.1f   Boss HP %.0f / %.0f   Final phase %d",
                    static_cast<int>(result->boss->time/60),std::fmod(result->boss->time,60),result->boss->hp,result->boss->maxHp,result->boss->phase);
            }
            ImGui::Text("Turf  %4.1f%%   /   %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
            ImGui::Separator();ImGui::TextUnformatted("SQUAD                 DAMAGE    WEAK    SPLATS   DEATHS    TURF");
            const auto& damage=boss->squadDamage();const auto& weak=boss->squadWeakHits();const auto& splats=boss->squadSplats();
            for(std::size_t i=0;i<match.actors().size();++i){
                const auto& actor=*match.actors()[i];const auto& roster=match.roster()[i];
                ImGui::Text("%-18s  %7.0f  %5d  %6d  %6d  %6.0f",roster.name.c_str(),damage[i],weak[i],splats[i],actor.deaths(),actor.turfPainted());
            }
            const double localDamage=std::round(damage[0]);const int localTurf=static_cast<int>(std::round(match.local().turfPainted()));
            const int xp=static_cast<int>(std::round((win?1200:500)+localTurf+splats[0]*40+localDamage*.04));
            ImGui::Separator();ImGui::Text("XP earned this round: %d  (win/loss + turf + crablets + Boss damage)",xp);
            ImGui::TextUnformatted("Offline profile saving is not implemented yet; this is the source-rule award estimate.");
            if(nativeApp&&ImGui::Button("Replay / choose stage",ImVec2(-1,36)))action=MatchHudAction::StageSelect;
            if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36)))action=MatchHudAction::Close;
            ImGui::End();return action;
        }
        if(paused){
            ImGui::SetNextWindowPos(ImVec2(width*.5f,height*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));ImGui::SetNextWindowSize(ImVec2(420,0));
            ImGui::Begin("Paused",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
            ImGui::TextUnformatted("The match clock and simulation are paused.");
            if(ImGui::Button("Resume",ImVec2(-1,36)))action=MatchHudAction::Resume;
            if(nativeApp&&ImGui::Button("Return to stage select",ImVec2(-1,36)))action=MatchHudAction::StageSelect;
            if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36)))action=MatchHudAction::Close;
            ImGui::End();return action;
        }
        if(match.phase()==MatchPhase::Finish||match.phase()==MatchPhase::Judge){
            ImGui::SetNextWindowPos(ImVec2(width*.5f,height*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));ImGui::SetNextWindowSize(ImVec2(430,0));
            ImGui::Begin("Boss finish",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted(boss->state().dead?"HULLBREAKER is down!":"Time is up. The squad is defeated.");
            ImGui::Text("Boss HP %.0f / %.0f   Phase %d",boss->state().hp,boss->state().maxHp,boss->state().phase);
            ImGui::Text("Turf  %4.1f%%   /   %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
            ImGui::End();return action;
        }
        const auto& state=boss->state();const int seconds=static_cast<int>(std::ceil(std::max(0.0,match.remaining())));
        ImGui::SetNextWindowPos(ImVec2(18,18));ImGui::Begin("Boss squad",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs);
        ImGui::Text("%s   %02d:%02d",std::string(stage.name).c_str(),seconds/60,seconds%60);
        ImGui::Text("SQUAD  %d / 8   Turf %4.1f%%",static_cast<int>(std::count_if(match.actors().begin(),match.actors().end(),[](const auto& a){return a->state.alive;})),coverage[0]*100.0);
        const auto& local=match.local();ImGui::Text("HP %3.0f   Ink %3.0f   Special %3.0f / %.0f",local.state.hp,local.state.ink,local.state.specialCharge,local.weapon().number("specialCost",190.0));
        if(!local.state.alive)ImGui::Text("Splatted - respawning in %.1f",local.state.respawnTimer);
        ImGui::TextUnformatted("WASD / Mouse   Fire LMB   Sub RMB or E   Special F or Q");ImGui::TextUnformatted("Shift squid   Space jump   C cheer   Esc pause");
        ImGui::End();
        const float hp=state.maxHp>0?static_cast<float>(std::clamp(state.hp/state.maxHp,0.0,1.0)):0.0f;
        auto* draw=ImGui::GetForegroundDrawList();const float barWidth=std::min(620.0f,width*.58f),left=(width-barWidth)*.5f,top=26.0f;
        draw->AddRectFilled(ImVec2(left,top),ImVec2(left+barWidth,top+28),IM_COL32(14,19,28,225),8);
        const ImU32 phaseColor=state.phase>=3?IM_COL32(255,103,66,255):state.phase==2?IM_COL32(244,176,61,255):IM_COL32(58,150,236,255);
        draw->AddRectFilled(ImVec2(left+3,top+3),ImVec2(left+3+(barWidth-6)*hp,top+25),phaseColor,6);
        for(const float notch:{.66f,.33f}){const float x=left+barWidth*(1-notch);draw->AddLine(ImVec2(x,top+1),ImVec2(x,top+27),IM_COL32(255,255,255,235),2);}
        draw->AddText(ImVec2(left+10,top+7),IM_COL32(255,255,255,255),"HULLBREAKER");
        draw->AddText(ImVec2(left+barWidth-72,top+7),IM_COL32(255,255,255,255),(std::to_string(static_cast<int>(std::round(hp*100)))+"%").c_str());
        char phaseText[80];std::snprintf(phaseText,sizeof(phaseText),"PHASE %d   HP %.0f / %.0f",state.phase,state.hp,state.maxHp);
        draw->AddText(ImVec2(left,top+34),IM_COL32(238,242,250,255),phaseText);
        const bool open=std::any_of(boss->hitShapes().begin(),boss->hitShapes().end(),[](const auto& hit){return hit.weak&&hit.active;});
        if(open)draw->AddText(ImVec2(width*.5f-35,top+72),IM_COL32(255,159,42,255),"OPEN!");
        if(boss->move()){
            const char* names[]={"CRUSHER SLAM","CONTAINER BARRAGE","INK CANNON SWEEP","HULL CHARGE","BROOD","SHELL FRENZY"};
            const auto id=static_cast<int>(boss->move()->id);if(id>=0&&id<6){const char* phases[]={"TELEGRAPH","ACTIVE","RECOVERY"};
                const auto mp=boss->move()->phase(state.clock);int pi=mp==BossMovePhase::Telegraph?0:mp==BossMovePhase::Active?1:2;
                draw->AddText(ImVec2(width*.5f-84,top+50),IM_COL32(255,225,183,255),names[id]);draw->AddText(ImVec2(width*.5f-40,top+68),IM_COL32(255,148,68,255),phases[pi]);}
        }
        const ImVec2 center(width*.5f,height*.5f);draw->AddCircle(center,7,IM_COL32(255,255,255,230),24,1.5f);draw->AddCircleFilled(center,1.5f,IM_COL32(255,255,255,255));
        return action;
    }
    if(match.phase()==MatchPhase::Results) {
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(440,0));
        ImGui::Begin("Turf results",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        if(const auto& result=match.result()) {
            ImGui::Text("Team %s wins",result->winner==0?"Orange":"Blue");
            ImGui::Text("Final turf  Orange %4.1f%%  /  Blue %4.1f%%",
                result->coverage[0]*100.0,result->coverage[1]*100.0);
        }
        if(nativeApp && ImGui::Button("Replay / choose stage",ImVec2(-1,36)))action=MatchHudAction::StageSelect;
        if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36)))action=MatchHudAction::Close;
        ImGui::End();
    } else if(paused) {
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(420,0));
        ImGui::Begin("Paused",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("The match clock and simulation are paused.");
        if(ImGui::Button("Resume",ImVec2(-1,36)))action=MatchHudAction::Resume;
        if(nativeApp && ImGui::Button("Return to stage select",ImVec2(-1,36)))action=MatchHudAction::StageSelect;
        if(ImGui::Button(nativeApp?"Close game":"Close match",ImVec2(-1,36)))action=MatchHudAction::Close;
        ImGui::End();
    } else if(match.phase()==MatchPhase::Finish || match.phase()==MatchPhase::Judge) {
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(ImVec2(440,0));
        ImGui::Begin("Match tally",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoInputs);
        const auto coverage=paint.coverage();
        if(match.phase()==MatchPhase::Finish)ImGui::TextUnformatted("Time is up. Counting final turf...");
        else if(const auto& result=match.result()) {
            ImGui::Text("Team %s wins",result->winner==0?"Orange":"Blue");
            ImGui::TextUnformatted("Final turf tally");
        }
        ImGui::Text("Orange %4.1f%%  /  Blue %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
        ImGui::End();
    } else {
        ImGui::SetNextWindowPos(ImVec2(18,18));
        ImGui::Begin("Turf Riot",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs);
        const auto coverage=paint.coverage();
        const auto& local=match.local();
        const int seconds=static_cast<int>(std::ceil(std::max(0.0,match.remaining())));
        ImGui::Text("%s",std::string(stage.name).c_str());
        ImGui::Text("%02d:%02d",seconds/60,seconds%60);
        ImGui::Text("Turf  %4.1f%%  /  %4.1f%%",coverage[0]*100.0,coverage[1]*100.0);
        ImGui::Text("HP %3.0f   Ink %3.0f   Special %3.0f / %.0f",local.state.hp,local.state.ink,
            local.state.specialCharge,local.weapon().number("specialCost",190.0));
        if(!local.state.alive)ImGui::Text("Splatted - respawning in %.1f",local.state.respawnTimer);
        ImGui::Text("WASD / Mouse   Fire LMB   Sub RMB or E   Special F or Q");
        ImGui::Text("Shift squid   Space jump   C cheer   Esc pause");
        if(local.specialPose().mapTarget)ImGui::TextUnformatted("Aim at a strike target; click to confirm after the ready delay.");
        ImGui::End();
        auto* draw=ImGui::GetForegroundDrawList();
        const ImVec2 center(width*0.5f,height*0.5f);
        draw->AddCircle(center,7,IM_COL32(255,255,255,230),24,1.5f);
        draw->AddCircleFilled(center,1.5f,IM_COL32(255,255,255,255));
    }
    return action;
}
} // namespace restoration
