#include "../src/gui/HudViewModel.hpp"
#include "../src/game/MatchSimulation.hpp"
#include "../src/world/ReferenceData.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}

int main(){
    using namespace restoration;
    try{
        const auto& stages=ReferenceStages();
        const auto stageIt=std::find_if(stages.begin(),stages.end(),[](const StageData& stage){return stage.id=="halyard"&&stage.mode=="turf";});
        check(stageIt!=stages.end(),"Missing Halyard Turf stage");
        StageGeometry geometry(*stageIt);FacePaint paint(*stageIt,geometry);NavGraph nav(*stageIt,geometry);
        MatchOptions options;options.practice=true;
        MatchSimulation match(*stageIt,geometry,paint,nav,options);
        match.local().state.ink=43;
        match.world().recordHudDenial(match.local(),"bomb",true);
        const auto frame=BuildHudViewModel(match);
        check(frame.actors.size()==1&&frame.localActor==0,"Snapshot must copy the local actor into its indexed roster");
        check(frame.actors[0].ink==43&&frame.stageId==stageIt->id,"Snapshot must copy actor and stage values");
        check(frame.events.size()==1&&frame.events[0].attacker==-1&&frame.events[0].victim==0,
              "Environmental event source must stay distinct from local actor slot zero");
        const auto epoch=frame.eventEpoch;
        const auto position=frame.actors[0].pos;
        match.local().state.ink=12;
        match.local().state.pos.x+=5;
        check(frame.actors[0].ink==43&&frame.actors[0].pos==position&&frame.events[0].source=="bomb",
              "Previously built HUD values must remain owned after simulation mutation");
        match.restart();
        const auto restarted=BuildHudViewModel(match);
        check(frame.actors[0].ink==43&&frame.actors[0].pos==position&&frame.events[0].source=="bomb",
              "Previously built HUD values must remain owned after match restart");
        check(frame.matchId==restarted.matchId&&frame.eventEpoch==epoch&&restarted.eventEpoch!=epoch,
              "Match identity stays stable while restart advances the HUD event epoch");

        FacePaint bossPaint(*stageIt,geometry);MatchOptions bossOptions;bossOptions.mode="boss";
        MatchSimulation bossMatch(*stageIt,geometry,bossPaint,nav,bossOptions);
        bossMatch.startPlaying();for(int i=0;i<40;++i)bossMatch.tick(.1);
        auto* boss=bossMatch.boss();check(boss!=nullptr,"Boss match must create its boss state");
        boss->forceMove(BossMoveId::Slam);for(int i=0;i<100&&!boss->move();++i)bossMatch.tick(.1);
        auto bossFrame=BuildHudViewModel(bossMatch);
        check(bossFrame.boss&&bossFrame.boss->hasMove&&bossFrame.boss->moveId==HudBossMove::Slam&&
              bossFrame.boss->movePhase==HudBossMovePhase::Telegraph,
              "Boss snapshot must translate the current move and phase into presentation enums");
        BossSegmentHit hit;hit.weak=true;hit.point=boss->state().pos+glm::dvec3(0,3,0);
        boss->hit(bossMatch.local(),120,hit,"shooter");
        bossFrame=BuildHudViewModel(bossMatch);
        check(!bossFrame.boss->hits.empty()&&bossFrame.boss->hits.back().weak,
              "Boss damage feedback events must be copied into the HUD snapshot");

        const StageData* zonesStage=nullptr;
        for(const auto& stage:stages)if(stage.id=="tidewater"&&stage.mode=="zones"&&!stage.onlineOnly){zonesStage=&stage;break;}
        check(zonesStage!=nullptr,"Missing Tidewater Zones stage variant");
        StageGeometry zonesGeometry(*zonesStage);FacePaint zonesPaint(*zonesStage,zonesGeometry);NavGraph zonesNav(*zonesStage,zonesGeometry);
        MatchOptions zonesOptions;zonesOptions.mode="zones";zonesOptions.duration=300;
        MatchSimulation zonesMatch(*zonesStage,zonesGeometry,zonesPaint,zonesNav,zonesOptions);
        auto* zones=const_cast<ZoneControl*>(zonesMatch.zones());check(zones!=nullptr,"Zones match must create its objective state");
        zones->timeUp();
        const auto zonesFrame=BuildHudViewModel(zonesMatch);
        check(zonesFrame.zones&&zonesFrame.zones->objective=="center"&&!zonesFrame.zones->zones.empty()&&
              std::any_of(zonesFrame.zones->events.begin(),zonesFrame.zones->events.end(),[](const HudZoneEvent& event){return event.type==HudZoneEventType::OvertimeStarted;}),
              "Zones snapshot must copy objectives, live zones, and overtime events");
        std::cout<<"HUD snapshot ownership, actor slots, and restart epochs passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
