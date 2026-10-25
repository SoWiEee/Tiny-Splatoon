#include "MatchSimulation.hpp"
#include "ArsenalData.hpp"
#include "ZonePlan.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace restoration {
MatchSimulation::MatchSimulation(const StageData& stage,const StageGeometry& geometry,FacePaint& paint,const NavGraph& nav,MatchOptions options)
    :stage_(stage),geometry_(geometry),paint_(paint),nav_(nav),options_(std::move(options)),world_(stage,geometry,paint) {
    if((!options_.network&&stage.onlineOnly)||stage.mode!=(options_.mode=="zones"?"zones":"turf"))throw std::invalid_argument("Match requires a stage variant matching its mode");
    if(options_.mode!="turf"&&options_.mode!="zones"&&options_.mode!="boss")throw std::invalid_argument("Unknown match mode");
    if(options_.network&&(options_.mode=="boss"&&stage.noBoss))throw std::invalid_argument("This stage does not support Boss online");
    if(options_.network&&(options_.mode=="zones"?options_.duration!=300:(options_.duration<60||options_.duration>600)))
        throw std::invalid_argument("Online match duration is invalid");
    if(options_.practice&&options_.mode!="turf")throw std::invalid_argument("Only Turf practice is supported");
    if(options_.mode=="boss"){
        if(options_.duration==0)options_.duration=240;
        if(options_.network){if(options_.duration<60||options_.duration>600)throw std::invalid_argument("Online Boss duration must be 60 to 600 seconds");}
        else if(options_.duration!=180&&options_.duration!=240&&options_.duration!=300)throw std::invalid_argument("Boss duration must be 180, 240, or 300 seconds");
    }
    else if(options_.mode=="zones") {
        if(options_.duration==0)options_.duration=300;
        if(options_.duration<1||options_.duration>600)throw std::invalid_argument("Zones duration must be 1 to 600 seconds");
    }
    else if(!options_.network&&options_.duration!=0&&options_.duration!=90&&options_.duration!=180)
        throw std::invalid_argument("Turf duration must be 90 or 180 seconds");
    world_.setNavigation(nav_);
    if(options_.network)world_.setNetworkLocalPeer(options_.localPeer);
    DifficultyTuning(options_.difficulty);
    GetEquipment(EquipmentCategory::Main,options_.main);
    restart();
}
double MatchSimulation::random(){
    seed_+=0x6d2b79f5u;std::uint32_t t=seed_;t=(t^(t>>15))*(t|1u);t^=t+((t^(t>>7))*(t|61u));
    return double(t^(t>>14))/4294967296.0;
}
std::vector<std::string> MatchSimulation::teamWeapons(const std::string& first){
    std::vector<std::string> pool,out;for(const auto& weapon:MainEquipment())pool.emplace_back(weapon.id);
    if(!first.empty()){out.push_back(first);pool.erase(std::find(pool.begin(),pool.end(),first));}
    while(out.size()<static_cast<std::size_t>(MatchTuning().number("teamSize"))){
        const auto index=static_cast<std::size_t>(random()*pool.size());out.push_back(pool[index]);pool.erase(pool.begin()+index);
    }
    return out;
}
CharacterStyle MatchSimulation::randomStyle(){
    CharacterStyle style;
    style.hair=static_cast<int>(random()*8);style.skin=static_cast<int>(random()*9);
    style.outfit=static_cast<int>(random()*10);style.eyes=static_cast<int>(random()*8);
    style.hat=random()<0.34?1+static_cast<int>(random()*3):0;style.brows=static_cast<int>(random()*4);
    return style;
}
void MatchSimulation::restart(){
    world_.setBoss(nullptr);boss_.reset();zonePlan_.reset();zones_.reset();bossSquad_.clear();botPointers_.clear();bots_.clear();actors_.clear();roster_.clear();world_.clear();paint_.clear();seed_=options_.seed;
    const bool bossMode=options_.mode=="boss";
    phase_=options_.practice?MatchPhase::Playing:MatchPhase::Intro;stateTime_=0;
    remaining_=options_.duration?options_.duration:MatchTuning().number("defaultDuration");paused=false;
    result_.reset();
    std::vector<std::string> names;for(const auto name:BotNames())names.emplace_back(name);
    for(std::size_t count=names.size();count>1;--count)std::swap(names[count-1],names[static_cast<std::size_t>(random()*count)]);
    std::size_t nextName=0;
    const int teams=options_.practice||bossMode?1:2,slots=options_.practice?1:bossMode?8:static_cast<int>(MatchTuning().number("teamSize"));
    if(options_.network){
        if(options_.localPeer==0||options_.authorityPeer==0||options_.networkRoster.empty()||options_.networkRoster.size()>8)
            throw std::invalid_argument("Online match roster is incomplete");
        std::unordered_set<std::uint32_t> nids;
        for(const auto& entry:options_.networkRoster){
            if(entry.nid>7||!nids.insert(entry.nid).second||entry.owner==0||entry.team<0||entry.team>1||
               entry.slot<0||entry.slot>(bossMode?7:3)||(bossMode&&entry.team!=0))
                throw std::invalid_argument("Online match roster is invalid");
            if(entry.bot&&entry.owner!=options_.authorityPeer)throw std::invalid_argument("Online bots must be host-owned");
            if(stage_.noBots&&entry.bot)throw std::invalid_argument("This stage does not support bots");
        }
        for(const auto& networkEntry:options_.networkRoster){
            MatchRosterEntry entry;entry.nid=networkEntry.nid;entry.owner=networkEntry.owner;entry.bot=networkEntry.bot;
            entry.name=networkEntry.name;entry.style=networkEntry.style;entry.team=networkEntry.team;entry.slot=networkEntry.slot;
            entry.local=!entry.bot&&entry.owner==options_.localPeer;
            auto actor=std::make_unique<PlayerSimulation>(stage_,geometry_,paint_,entry.team,&world_);
            actor->setNetworkIdentity(entry.nid,entry.owner);
            auto randomSource=[state=std::make_shared<std::uint32_t>(options_.seed^(0x9e3779b9u*(entry.nid+1)))]() mutable {
                *state+=0x6d2b79f5u;std::uint32_t t=*state;t=(t^(t>>15))*(t|1u);t^=t+((t^(t>>7))*(t|61u));return double(t^(t>>14))/4294967296.0;
            };
            actor->setRandomSource(randomSource);
            actor->setLocal(entry.local);
            actor->setLoadout(networkEntry.main,networkEntry.sub,networkEntry.special);
            actor->state.pos={0,0,0};actor->state.yaw=0;actor->input.aimYaw=0;
            if(entry.bot&&options_.localPeer==options_.authorityPeer){
                auto bot=std::make_unique<BotSimulation>(*actor,stage_,geometry_,paint_,nav_,world_,options_.difficulty,
                    options_.seed^networkEntry.nid,randomSource);
                actor->setBotControlled(true);botPointers_.push_back(bot.get());bots_.push_back(std::move(bot));
            }
            actors_.push_back(std::move(actor));roster_.push_back(std::move(entry));
        }
    }else for(int team=0;team<teams;++team){
        auto weapons=teamWeapons(team==0?options_.main:std::string());
        if(bossMode){auto extra=teamWeapons({});weapons.insert(weapons.end(),extra.begin(),extra.end());}
        for(int slot=0;slot<slots;++slot){
            const bool local=team==0&&slot==0;
            std::string sub=local?options_.sub:std::string(),special=local?options_.special:std::string();
            if(!local&&random()>=0.5)sub=std::string(SubEquipment()[static_cast<std::size_t>(random()*SubEquipment().size())].id);
            if(!local&&random()>=0.5)special=std::string(SpecialEquipment()[static_cast<std::size_t>(random()*SpecialEquipment().size())].id);
            MatchRosterEntry entry;entry.local=local;entry.team=team;entry.slot=slot;
            entry.name=local?(options_.playerName.empty()?"You":options_.playerName):names[nextName++%names.size()];
            entry.style=local?options_.style:randomStyle();
            auto actor=std::make_unique<PlayerSimulation>(stage_,geometry_,paint_,team,&world_);
            actor->setRandomSource([this]{return random();});
            actor->setLocal(local);
            actor->setLoadout(weapons[slot],sub,special);
            // Source constructs every brain at the actor's reset pose, then places the roster.
            actor->state.pos={0,0,0};actor->state.yaw=0;actor->input.aimYaw=0;
            if(!local||options_.autopilot){
                auto bot=std::make_unique<BotSimulation>(*actor,stage_,geometry_,paint_,nav_,world_,options_.difficulty,1,[this]{return random();});
                actor->setBotControlled(true);
                botPointers_.push_back(bot.get());bots_.push_back(std::move(bot));
            }
            actors_.push_back(std::move(actor));roster_.push_back(std::move(entry));
        }
    }
    for(std::size_t i=0;i<actors_.size();++i){
        auto& actor=*actors_[i];const auto& entry=roster_[i];
        const double angle=entry.slot/(bossMode?8.0:4.0)*6.283185307179586+0.6,yaw=entry.team==0?0:3.141592653589793;
        const double radius=bossMode?1.7:1.2;
        actor.spawnAt(stage_.spawnPads[entry.team]+glm::dvec3(std::cos(angle)*radius,0,std::sin(angle)*radius),yaw);
        actor.state.invuln=0;
        for(auto& bot:bots_)if(&bot->actor()==&actor)bot->spawnFacing(yaw);
    }
    if(bossMode){
        for(auto& actor:actors_)bossSquad_.push_back(actor.get());
        boss_=std::make_unique<BossSimulation>(stage_,geometry_,paint_,bossSquad_,options_.difficulty,options_.seed);
        boss_->setNetworkAuthority(networkAuthority());
        boss_->setNetworkLocalPeer(options_.localPeer);
        world_.setBoss(boss_.get());
        for(auto& bot:bots_)bot->setBoss(boss_.get());
        if(bossPoseSink_)bossPoseSink_(*boss_);
    }
    else if(options_.mode=="zones"){
        zones_=std::make_unique<ZoneControl>(stage_,geometry_,paint_,remaining_,[this]{return random();});
        zones_->setNetworkAuthority(networkAuthority());
        zonePlan_=std::make_unique<ZonePlan>(*zones_,stage_,geometry_,paint_,nav_,options_.seed,[this]{return random();});
        for(auto& bot:bots_)bot->setZonePlan(zonePlan_.get());
    }
}
PlayerSimulation& MatchSimulation::local(){
    return const_cast<PlayerSimulation&>(std::as_const(*this).local());
}
const PlayerSimulation& MatchSimulation::local() const{
    if(!options_.network)return *actors_.front();
    for(std::size_t i=0;i<roster_.size();++i)if(roster_[i].local)return *actors_[i];
    throw std::logic_error("Online roster has no local player");
}
void MatchSimulation::setNetworkActorActive(std::uint32_t nid,bool active){
    if(!options_.network)throw std::logic_error("Actor lifecycle is only networked in online matches");
    const auto found=std::find_if(roster_.begin(),roster_.end(),[&](const auto& entry){return entry.nid==nid;});
    if(found==roster_.end())throw std::out_of_range("Unknown online actor id");
    if(found->active==active)return;
    const auto index=static_cast<std::size_t>(std::distance(roster_.begin(),found));
    found->active=active;
    auto& actor=*actors_.at(index);
    if(!active){actor.input={};actor.state.alive=false;actor.state.hp=0;actor.setBotControlled(false);world_.cleanup(actor);}
    else if(found->bot&&networkAuthority()){
        actor.setBotControlled(true);
        auto bot=std::make_unique<BotSimulation>(actor,stage_,geometry_,paint_,nav_,world_,options_.difficulty,
            options_.seed^found->nid,actor.randomSource());
        bot->spawnFacing(actor.state.yaw);
        if(boss_)bot->setBoss(boss_.get());
        if(zonePlan_)bot->setZonePlan(zonePlan_.get());
        botPointers_.push_back(bot.get());bots_.push_back(std::move(bot));
    }
}
void MatchSimulation::setNetworkAuthorityPeer(PeerId peer){
    setNetworkAuthorityPeer(peer,options_.networkRoster);
}
void MatchSimulation::setNetworkAuthorityPeer(PeerId peer,const std::vector<MatchParticipant>& roster){
    if(!options_.network||!peer)throw std::invalid_argument("Invalid online authority peer");
    if(options_.authorityPeer==peer){applyNetworkRoster(roster);return;}
    options_.authorityPeer=peer;
    if(boss_){boss_->setNetworkAuthority(networkAuthority());boss_->setNetworkLocalPeer(options_.localPeer);}
    if(zones_)zones_->setNetworkAuthority(networkAuthority());
    if(zonePlan_)zonePlan_->resetAfterAuthorityChange();
    applyNetworkRoster(roster);
}
void MatchSimulation::updateNetworkBossPresentation(){
    if(boss_&&bossPoseSink_)bossPoseSink_(*boss_);
}
void MatchSimulation::applyNetworkRoster(const std::vector<MatchParticipant>& incoming){
    if(!options_.network)throw std::logic_error("Roster ownership updates require an online match");
    if(incoming.size()>8)throw std::invalid_argument("Online roster exceeds actor limit");
    options_.networkRoster=incoming;
    std::unordered_set<std::uint32_t> seen;
    const bool bossMode=options_.mode=="boss";
    bool rebuildBots=false;
    std::size_t expectedBots=0;
    if(networkAuthority())for(const auto& wire:incoming)if(wire.bot)++expectedBots;
    if(expectedBots!=bots_.size())rebuildBots=true;
    for(const auto& wire:incoming){
        if(wire.nid>7||wire.owner==0||wire.team<0||wire.team>1||wire.slot<0||wire.slot>(bossMode?7:3)||
           (bossMode&&wire.team!=0)||!seen.insert(wire.nid).second)
            throw std::invalid_argument("Online ownership roster is invalid");
        if(wire.bot&&wire.owner!=options_.authorityPeer)throw std::invalid_argument("Online bots must be host-owned");
        const auto found=std::find_if(roster_.begin(),roster_.end(),[&](const auto& entry){return entry.nid==wire.nid;});
        if(found==roster_.end())throw std::invalid_argument("Online roster introduced an unknown actor");
        const auto index=static_cast<std::size_t>(std::distance(roster_.begin(),found));
        auto& player=*actors_[index];
        const bool becameBot=!found->bot&&wire.bot;
        const bool ceasedBot=found->bot&&!wire.bot;
        if(becameBot||ceasedBot)rebuildBots=true;
        const bool ownershipChanged=found->owner!=wire.owner;
        if(becameBot){
            player.setLoadout(wire.main,wire.sub,wire.special);
        }
        if(becameBot||(ownershipChanged&&wire.owner==options_.localPeer))player.prepareNetworkAdoption();
        found->owner=wire.owner;found->bot=wire.bot;found->active=true;
        found->team=wire.team;found->slot=wire.slot;found->name=wire.name;found->style=wire.style;
        found->local=!wire.bot&&wire.owner==options_.localPeer;
        player.setNetworkIdentity(wire.nid,wire.owner);player.setLocal(found->local);
        player.setBotControlled(wire.bot);
        if(ownershipChanged&&!wire.bot){player.input={};}
        if(ceasedBot){
            player.input={};
        }
    }
    for(auto& entry:roster_)if(!seen.count(entry.nid)){if(entry.active&&entry.bot)rebuildBots=true;setNetworkActorActive(entry.nid,false);}
    // Rebuild only the host-owned brains. Actor storage and indices remain stable.
    if(!rebuildBots)return;
    bots_.clear();botPointers_.clear();
    if(networkAuthority())for(std::size_t i=0;i<roster_.size();++i){
        auto& entry=roster_[i];auto& player=*actors_[i];
        if(!entry.active||!entry.bot)continue;
        auto bot=std::make_unique<BotSimulation>(player,stage_,geometry_,paint_,nav_,world_,options_.difficulty,
            options_.seed^entry.nid,player.randomSource());
        bot->spawnFacing(player.state.yaw);
        if(boss_)bot->setBoss(boss_.get());
        if(zonePlan_)bot->setZonePlan(zonePlan_.get());
        botPointers_.push_back(bot.get());bots_.push_back(std::move(bot));
    }
}
void MatchSimulation::applyNetworkState(MatchPhase phase,double remaining,std::optional<MatchResult> result){
    if(!options_.network||!std::isfinite(remaining)||remaining<0)throw std::invalid_argument("Invalid online match state");
    const bool changed=phase_!=phase;
    phase_=phase;remaining_=std::min(remaining,static_cast<double>(options_.duration));result_=std::move(result);
    if(changed)stateTime_=0;
}
void MatchSimulation::startPlaying(){phase_=MatchPhase::Playing;stateTime_=0;}
void MatchSimulation::setBossPoseSink(std::function<void(BossSimulation&)> sink){
    bossPoseSink_=std::move(sink);
    if(boss_&&bossPoseSink_)bossPoseSink_(*boss_);
}
void MatchSimulation::judge(){
    const auto coverage=paint_.coverage();
    if(boss_){
        const auto& state=boss_->state();
        const double elapsed=options_.duration-remaining_;
        result_=MatchResult{coverage,state.dead?0:1,BossResult{state.dead,std::round(elapsed*10)/10,state.hp,state.maxHp,state.phase}};
        phase_=MatchPhase::Judge;stateTime_=0;return;
    }
    if(zones_){
        const int winner=zones_->winner().value_or(zones_->counts()[0]==zones_->counts()[1]?(random()<0.5?0:1):(zones_->counts()[0]<zones_->counts()[1]?0:1));
        ZoneResult zoneResult;zoneResult.counts=zones_->counts();zoneResult.penalty=zones_->penalty();
        zoneResult.overtime=zones_->overtime();zoneResult.reason=zones_->reason();zoneResult.events=zones_->events();
        zoneResult.log=zones_->penaltyLog();
        zoneResult.zoneTurf.reserve(actors_.size());for(const auto& actor:actors_)zoneResult.zoneTurf.push_back(actor->zoneTurf());
        result_=MatchResult{coverage,winner,std::nullopt,std::move(zoneResult)};phase_=MatchPhase::Judge;stateTime_=0;return;
    }
    const int winner=coverage[0]==coverage[1]?(random()<0.5?0:1):(coverage[0]>coverage[1]?0:1);
    result_=MatchResult{coverage,winner};phase_=MatchPhase::Judge;stateTime_=0;
}
void MatchSimulation::finishJudging(){
    if(phase_!=MatchPhase::Judge)throw std::logic_error("Results require completed Turf judging");
    phase_=MatchPhase::Results;stateTime_=0;
}
void MatchSimulation::tick(double dt){
    if(!std::isfinite(dt)||dt<=0||dt>0.1)throw std::invalid_argument("Invalid match step");
    if(paused&&!options_.network)return;
    const bool authority=!options_.network||options_.localPeer==options_.authorityPeer;
    if(authority)stateTime_+=dt;
    if(authority&&phase_==MatchPhase::Intro&&stateTime_>(boss_?7.2:4.2))startPlaying();
    else if(authority&&phase_==MatchPhase::Playing&&!options_.practice){
        if(zones_){
            zones_->update(dt,actors_);
            if(zones_->ended()){phase_=MatchPhase::Finish;stateTime_=0;}
            else if(!zones_->overtime()){
                remaining_=std::max(0.0,remaining_-dt);
                if(remaining_<=0){remaining_=0;if(zones_->timeUp()){phase_=MatchPhase::Finish;stateTime_=0;}}
            }
        }else{
            remaining_=std::max(0.0,remaining_-dt);if(remaining_<=0){phase_=MatchPhase::Finish;stateTime_=0;if(boss_)boss_->finish();}
        }
    }
    else if(authority&&phase_==MatchPhase::Finish&&stateTime_>(boss_?(boss_->state().dead?5.2:3.2):2.6))judge();
    const bool live=phase_==MatchPhase::Playing;
    if(authority&&live&&zonePlan_)zonePlan_->update(dt,botPointers_);
    std::vector<double> turfBefore;
    if(zones_&&live){turfBefore.reserve(actors_.size());for(const auto& actor:actors_)turfBefore.push_back(actor->turfPainted());}
    for(auto& bot:bots_){
        const auto found=options_.network?std::find_if(roster_.begin(),roster_.end(),[&](const auto& entry){return entry.nid==bot->actor().networkNid();}):roster_.end();
        if(found==roster_.end()||found->active)bot->tick(dt,live,botPointers_);
    }
    if(!live)for(std::size_t i=0;i<actors_.size();++i){
        if(options_.network&&!roster_[i].local&&!(roster_[i].bot&&authority))continue;
        const double yaw=actors_[i]->input.aimYaw,pitch=actors_[i]->input.aimPitch;actors_[i]->input={};actors_[i]->input.aimYaw=yaw;actors_[i]->input.aimPitch=pitch;
    }
    for(std::size_t i=0;i<actors_.size();++i){
        auto& actor=actors_[i];const bool simulating=!options_.network||roster_[i].local||(roster_[i].bot&&authority);
        if(!roster_[i].active){actor->updatePresentation(dt);continue;}
        if(!simulating){actor->updatePresentation(dt);continue;}
        actor->setCanRespawn(live);actor->tick(dt);
    }
    if(boss_){
        boss_->tick(dt,live);
        if(bossPoseSink_)bossPoseSink_(*boss_);
        if(authority&&boss_->state().dead&&phase_==MatchPhase::Playing){phase_=MatchPhase::Finish;stateTime_=0;}
    }
    const double radius=PlayerTuning().number("radius")*1.7;
    for(std::size_t i=0;i<actors_.size();++i)for(std::size_t j=i+1;j<actors_.size();++j){
        const bool simA=!options_.network||roster_[i].local||(roster_[i].bot&&authority),simB=!options_.network||roster_[j].local||(roster_[j].bot&&authority);
        if(!simA&&!simB)continue;
        auto& a=actors_[i]->state;auto& b=actors_[j]->state;if(!a.alive||!b.alive)continue;
        const double dx=b.pos.x-a.pos.x,dz=b.pos.z-a.pos.z,dy=b.pos.y-a.pos.y,d2=dx*dx+dz*dz;
        if(d2<radius*radius&&std::abs(dy)<1.2&&d2>1e-5){const double d=std::sqrt(d2),push=(radius-d)*0.5;if(simA){a.pos.x-=dx/d*push;a.pos.z-=dz/d*push;}if(simB){b.pos.x+=dx/d*push;b.pos.z+=dz/d*push;}}
    }
    world_.tick(dt);
    if(authority&&zones_&&live&&phase_==MatchPhase::Playing){
        for(std::size_t i=0;i<actors_.size();++i){
            const double painted=actors_[i]->turfPainted()-turfBefore[i];
            if(painted>0.0&&(zones_->contains(actors_[i]->state.pos)||(actors_[i]->input.hasAimPoint&&zones_->contains(actors_[i]->input.aimPoint))))
                actors_[i]->addZoneTurf(painted);
        }
    }
}
}
