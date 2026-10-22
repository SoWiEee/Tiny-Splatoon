#include "../src/game/MatchSimulation.hpp"
#include "../src/network/MatchSync.hpp"
#include "../src/world/StageGeometry.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace restoration;
using namespace restoration::network;

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

RosterEntry member(std::uint32_t nid,MemberId owner,bool bot,int team,int slot,const char* name){
    RosterEntry entry;entry.nid=nid;entry.owner=owner;entry.bot=bot;entry.team=team;entry.slot=slot;entry.name=name;
    entry.main="shooter";entry.sub="bomb";entry.special="storm";return entry;
}

const StageData& turfStage(){
    const auto& stages=ReferenceStages();
    const auto found=std::find_if(stages.begin(),stages.end(),[](const auto& stage){return stage.mode=="turf"&&!stage.onlineOnly;});
    if(found==stages.end())throw std::runtime_error("No offline Turf source stage is available");
    return *found;
}

void ownershipAndMigration(){
    const auto& stage=turfStage();StageGeometry geometry(stage);FacePaint paint(stage,geometry);NavGraph nav(stage,geometry);
    MatchOptions options;options.network=true;options.mode="turf";options.duration=60;options.seed=17;options.localPeer=202;options.authorityPeer=101;
    options.networkRoster={member(0,101,false,0,0,"Host"),member(1,202,false,1,0,"Guest"),member(2,101,true,0,1,"Bot")};
    MatchSimulation match(stage,geometry,paint,nav,options);
    require(match.actors().size()==3,"online roster must preserve all stable actor slots");
    require(&match.local()==match.actors()[1].get(),"local actor lookup must use owner identity, not roster position");
    require(match.bots().empty(),"a guest must not run host-owned bots");
    require(match.actors()[0]->networkNid()==0&&match.actors()[1]->networkNid()==1,"network ids must survive actor ordering");

    auto migrated=options.networkRoster;migrated[2].owner=202;
    match.setNetworkAuthorityPeer(202,migrated);
    require(match.networkAuthority(),"new host must become the simulation authority");
    require(match.bots().size()==1&&match.actors()[2]->botControlled(),"new host must create the inherited bot brain");

    auto departed=migrated;departed.erase(departed.begin());
    match.applyNetworkRoster(departed);
    require(match.actors().size()==3,"a departed member must not shift actor indices");
    require(!match.roster()[0].active,"a departed actor must become an inactive stable slot");
    require(match.bots().size()==1,"removing a remote actor must preserve remaining host bots");
}

void bossSlotsAndOnlineDuration(){
    const auto& stage=turfStage();StageGeometry geometry(stage);FacePaint paint(stage,geometry);NavGraph nav(stage,geometry);
    MatchOptions options;options.network=true;options.mode="boss";options.duration=60;options.seed=23;options.localPeer=22;options.authorityPeer=11;
    options.networkRoster={member(0,11,false,0,0,"Host"),member(7,22,false,0,7,"Guest")};
    MatchSimulation match(stage,geometry,paint,nav,options);
    require(match.actors().size()==2,"Boss roster must allow the stable actor id and team slot seven");
    require(match.boss()!=nullptr,"online Boss mode must create its shared authority simulation");
    require(&match.local()==match.actors()[1].get(),"Boss local lookup must use owner identity");
}
}

int main(){
    try{
        ownershipAndMigration();
        bossSlotsAndOnlineDuration();
        std::cout<<"Online roster ownership, stable removal, migration and Boss slots passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
