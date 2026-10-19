#include "MatchSimulation.hpp"
#include "ArsenalData.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace restoration {
MatchSimulation::MatchSimulation(const StageData& stage,const StageGeometry& geometry,FacePaint& paint,const NavGraph& nav,MatchOptions options)
    :stage_(stage),geometry_(geometry),paint_(paint),nav_(nav),options_(std::move(options)),world_(stage,geometry,paint) {
    if(stage.onlineOnly||stage.mode!="turf")throw std::invalid_argument("Offline Turf requires an offline Turf stage");
    world_.setNavigation(nav_);
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
    botPointers_.clear();bots_.clear();actors_.clear();roster_.clear();world_.clear();paint_.clear();seed_=options_.seed;
    phase_=options_.practice?MatchPhase::Playing:MatchPhase::Intro;stateTime_=0;remaining_=MatchTuning().number("defaultDuration");paused=false;
    result_.reset();
    std::vector<std::string> names;for(const auto name:BotNames())names.emplace_back(name);
    for(std::size_t count=names.size();count>1;--count)std::swap(names[count-1],names[static_cast<std::size_t>(random()*count)]);
    std::size_t nextName=0;
    const int teams=options_.practice?1:2,slots=options_.practice?1:static_cast<int>(MatchTuning().number("teamSize"));
    for(int team=0;team<teams;++team){
        const auto weapons=teamWeapons(team==0?options_.main:std::string());
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
        const double angle=entry.slot/4.0*6.283185307179586+0.6,yaw=entry.team==0?0:3.141592653589793;
        actor.spawnAt(stage_.spawnPads[entry.team]+glm::dvec3(std::cos(angle)*1.2,0,std::sin(angle)*1.2),yaw);
        actor.state.invuln=0;
        for(auto& bot:bots_)if(&bot->actor()==&actor)bot->spawnFacing(yaw);
    }
}
void MatchSimulation::startPlaying(){phase_=MatchPhase::Playing;stateTime_=0;}
void MatchSimulation::judge(){
    const auto coverage=paint_.coverage();
    const int winner=coverage[0]==coverage[1]?(random()<0.5?0:1):(coverage[0]>coverage[1]?0:1);
    result_=MatchResult{coverage,winner};phase_=MatchPhase::Judge;stateTime_=0;
}
void MatchSimulation::finishJudging(){
    if(phase_!=MatchPhase::Judge)throw std::logic_error("Results require completed Turf judging");
    phase_=MatchPhase::Results;stateTime_=0;
}
void MatchSimulation::tick(double dt){
    if(!std::isfinite(dt)||dt<=0||dt>0.1)throw std::invalid_argument("Invalid match step");
    if(paused)return;
    stateTime_+=dt;
    if(phase_==MatchPhase::Intro&&stateTime_>4.2)startPlaying();
    else if(phase_==MatchPhase::Playing&&!options_.practice){remaining_=std::max(0.0,remaining_-dt);if(remaining_<=0){phase_=MatchPhase::Finish;stateTime_=0;}}
    else if(phase_==MatchPhase::Finish&&stateTime_>2.6)judge();
    const bool live=phase_==MatchPhase::Playing;
    for(auto& bot:bots_)bot->tick(dt,live,botPointers_);
    if(!live)for(auto& actor:actors_){const double yaw=actor->input.aimYaw,pitch=actor->input.aimPitch;actor->input={};actor->input.aimYaw=yaw;actor->input.aimPitch=pitch;}
    for(auto& actor:actors_){actor->setCanRespawn(live);actor->tick(dt);}
    const double radius=PlayerTuning().number("radius")*1.7;
    for(std::size_t i=0;i<actors_.size();++i)for(std::size_t j=i+1;j<actors_.size();++j){
        auto& a=actors_[i]->state;auto& b=actors_[j]->state;if(!a.alive||!b.alive)continue;
        const double dx=b.pos.x-a.pos.x,dz=b.pos.z-a.pos.z,dy=b.pos.y-a.pos.y,d2=dx*dx+dz*dz;
        if(d2<radius*radius&&std::abs(dy)<1.2&&d2>1e-5){const double d=std::sqrt(d2),push=(radius-d)*0.5;a.pos.x-=dx/d*push;a.pos.z-=dz/d*push;b.pos.x+=dx/d*push;b.pos.z+=dz/d*push;}
    }
    world_.tick(dt);
}
}
