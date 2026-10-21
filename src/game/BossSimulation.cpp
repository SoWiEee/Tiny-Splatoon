#include "BossSimulation.hpp"
#include "ArsenalData.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr std::array<double,4> phasePace{1,1,.88,.78};
constexpr std::array<double,4> walkSpeed{0,2.9,3.3,3.8};
constexpr std::array<double,4> turnSpeed{0,1.25,1.45,1.75};
constexpr std::array<double,4> coolTime{0,1.05,.8,.6};
constexpr std::array<double,4> coolRandom{0,.8,.6,.45};
constexpr std::array<double,4> preferred{0,10,9,8};
constexpr std::array<double,4> chargeSpeed{0,13,14.5,16};
constexpr std::array<double,4> stunTime{0,4.2,3.7,3.3};
constexpr std::array<double,4> crabSpeed{0,4.4,4.4,5.2};
constexpr std::array<double,4> sweepSpan{0,1.9,2.2,2.5};
double angleDiff(double from,double to){return std::atan2(std::sin(to-from),std::cos(to-from));}
double round3(double v){return std::floor(v*1000+0.5)/1000;}
double flat(glm::dvec3 a,glm::dvec3 b){return std::hypot(a.x-b.x,a.z-b.z);}
double ease(double v){v=std::clamp(v,0.0,1.0);return v*v*(3-2*v);}
double moveTele(BossMoveId id){switch(id){case BossMoveId::Slam:return 1.15;case BossMoveId::Barrage:return .85;case BossMoveId::Sweep:return 1.15;case BossMoveId::Charge:return 1.15;case BossMoveId::Crablets:return .9;case BossMoveId::Frenzy:return 1.2;}return 1;}
double moveAct(BossMoveId id){switch(id){case BossMoveId::Slam:return 1.9;case BossMoveId::Sweep:return 2.1;case BossMoveId::Crablets:return .8;case BossMoveId::Frenzy:return 3.0;default:return 0;}}
double moveRec(BossMoveId id){switch(id){case BossMoveId::Slam:return 1.0;case BossMoveId::Barrage:return .8;case BossMoveId::Sweep:return .9;case BossMoveId::Charge:return .9;case BossMoveId::Crablets:return .6;case BossMoveId::Frenzy:return 1.7;}return 1;}
const char* moveName(BossMoveId id){switch(id){case BossMoveId::Slam:return "slam";case BossMoveId::Barrage:return "barrage";case BossMoveId::Sweep:return "sweep";case BossMoveId::Charge:return "charge";case BossMoveId::Crablets:return "crablets";case BossMoveId::Frenzy:return "frenzy";}return "";}
}
BossMovePhase BossMove::phase(double time)const{
    const double u=time-t0;
    if(u<0)return BossMovePhase::None;
    if(u<tele)return BossMovePhase::Telegraph;
    if(u<tele+act)return BossMovePhase::Active;
    if(u<end())return BossMovePhase::Recovery;
    return BossMovePhase::None;
}
BossSimulation::BossSimulation(const StageData& stage,const StageGeometry& geometry,FacePaint& paint,
    const std::vector<PlayerSimulation*>& squad,const std::string& difficulty,std::uint32_t seed)
    :stage_(stage),geometry_(geometry),paint_(paint),squad_(squad),nav_(stage,geometry),difficulty_(difficulty),
      seed_(seed),initialSeed_(seed){
    if(stage.mode!="turf"||stage.onlineOnly||squad.empty())throw std::invalid_argument("Boss requires a legal offline Turf stage and squad");
    damageMultiplier_=difficulty=="easy"?.7:difficulty=="hard"?1.2:1;
    reset();
}
double BossSimulation::random(){
    seed_+=0x6d2b79f5u;std::uint32_t t=seed_;t=(t^(t>>15))*(t|1u);t^=t+((t^(t>>7))*(t|61u));
    return double(t^(t>>14))/4294967296.0;
}
double BossSimulation::pace()const{return difficulty_=="easy"?1.25:difficulty_=="hard"?.85:1;}
void BossSimulation::reset(){
    seed_=initialSeed_;state_={};move_.reset();moves_.clear();crabs_.clear();oneShotHits_.clear();poseSockets_.clear();
    poseHitPulse_={};
    forcedMove_.reset();lastMove_.reset();attention_.fill(0);damageBy_.fill(0);totalDamageBy_.fill(0);weakHitsBy_.fill(0);splatsBy_.fill(0);
    externalHitShapes_=false;spawnedCrabs_=false;finished_=false;pendingPhase_=0;crabSeq_=1;
    cool_=2.4;retarget_=goalTime_=stuckTime_=speed_=crabletCooldown_=frenzyCooldown_=0;
    hasGoal_=false;path_.clear();pathIndex_=0;
    double units=0;for(const auto* actor:squad_)units+=actor->botControlled()&&!actor->isLocal()?0.6:1.0;
    const double diff=difficulty_=="easy"?.75:difficulty_=="hard"?1.3:1.0;
    state_.maxHp=std::round(18000*units*diff);state_.hp=state_.maxHp;state_.phase=1;state_.target=-1;
    double cx=0,cz=0;for(const int i:nav_.planIds()){const auto [x,z]=nav_.xz(i);cx+=x;cz+=z;}
    const double n=static_cast<double>(std::max<std::size_t>(1,nav_.planIds().size()));cx/=n;cz/=n;
    int best=-1;double score=-std::numeric_limits<double>::infinity();
    for(const int i:nav_.planIds()){
        const auto [x,z]=nav_.xz(i);
        const double s=std::min(nav_.wallDistance(i),6.0)*1.5+std::min(nav_.dropDistance(i),5.0)-std::hypot(x-cx,z-cz)*.25;
        if(s>score){score=s;best=i;}
    }
    const auto [x,z]=best>=0?nav_.xz(best):std::pair<double,double>{0,0};
    state_.pos={x,nav_.floorAt(x,z),z};home_={x,z};
    state_.yaw=std::atan2(stage_.spawnPads[0].x-x,stage_.spawnPads[0].z-z);
    for(int k=0;k<24&&!nav_.turnOk(x,z,state_.yaw);++k)state_.yaw+=(k%2?1:-1)*k*.13;
    updateDefaultHitShapes();
}
void BossSimulation::finish(){finished_=true;move_.reset();moves_.clear();state_.stunned=false;for(auto& crab:crabs_)crab.dead=true;}
void BossSimulation::forceMove(BossMoveId id){forcedMove_=id;cool_=0;}
PlayerSimulation* BossSimulation::targetActor()const{
    return state_.target>=0&&state_.target<static_cast<int>(squad_.size())&&squad_[state_.target]->state.alive?squad_[state_.target]:nullptr;
}
std::optional<glm::dvec3> BossSimulation::poseAim()const{
    if(state_.dead)return std::nullopt;
    if(state_.anim==0&&move_&&move_->phase(state_.clock)!=BossMovePhase::None){
        const auto& m=*move_;const double u=state_.clock-m.t0;
        switch(m.id){
        case BossMoveId::Slam:return m.point;
        case BossMoveId::Sweep:{const double a=u<m.tele?m.a0:beamAngle(m,state_.clock);return glm::dvec3(m.origin.x+std::sin(a)*12,m.point.y,m.origin.z+std::cos(a)*12);}
        case BossMoveId::Charge:return m.point+glm::dvec3(std::sin(m.yaw)*(m.length+3),1,std::cos(m.yaw)*(m.length+3));
        case BossMoveId::Barrage:if(!m.barrels.empty()){const auto it=std::find_if(m.barrels.begin(),m.barrels.end(),[u](const auto& b){return b.throwAt+.2>u;});const auto& b=it==m.barrels.end()?m.barrels.back():*it;return glm::dvec3(b.x,b.y,b.z);}break;
        default:break;
        }
    }
    if(const auto* target=targetActor())return target->state.pos+glm::dvec3(0,1,0);
    return std::nullopt;
}
BossPoseHitPulse BossSimulation::takePoseHitPulse(){const auto pulse=poseHitPulse_;poseHitPulse_={};return pulse;}
void BossSimulation::chooseTarget(){
    retarget_=3+random()*2.5;
    int best=-1;double score=-std::numeric_limits<double>::infinity();
    for(int i=0;i<static_cast<int>(squad_.size());++i){
        const auto& a=*squad_[i];
        if(!a.state.alive||a.superJumpState()||nav_.inPad(a.state.pos.x,a.state.pos.z,3))continue;
        const double s=-flat(a.state.pos,state_.pos)*.1+damageBy_[i]*.004-attention_[i]*1.4+random()*1.3;
        if(s>score){score=s;best=i;}
    }
    for(auto& v:attention_)v*=.8;
    if(best>=0){attention_[best]+=1;if(state_.target!=best)goalTime_=0;state_.target=best;}
    else state_.target=-1;
}
void BossSimulation::chooseGoal(PlayerSimulation* target){
    goalTime_=3+random()*2.5;
    double score=-std::numeric_limits<double>::infinity();glm::dvec2 best(0);
    bool chosen=false;
    for(int i=0;i<16;++i){
        const glm::dvec2 p=nav_.randomPoint(random());
        const int cell=nav_.cell(p.x,p.y);
        if(cell<0)continue;
        double s=std::min(nav_.wallDistance(cell),6.0)*.7+std::min(nav_.dropDistance(cell),4.0)*.4-
            std::hypot(p.x-state_.pos.x,p.y-state_.pos.z)*.07+random()*1.5;
        if(target)s-=std::abs(std::hypot(p.x-target->state.pos.x,p.y-target->state.pos.z)-preferred[state_.phase]);
        else s-=glm::length(p-home_)*.2;
        if(std::hypot(p.x-stage_.spawnPads[0].x,p.y-stage_.spawnPads[0].z)<18)s-=12;
        if(s>score){score=s;best=p;chosen=true;}
    }
    if(!chosen)return;
    goal_=best;hasGoal_=true;path_=nav_.path(state_.pos.x,state_.pos.z,best.x,best.y);pathIndex_=1;
}
bool BossSimulation::turnToward(double want,double dt){
    const double diff=angleDiff(state_.yaw,want);
    if(std::abs(diff)<1e-3)return true;
    const double next=state_.yaw+std::clamp(diff,-turnSpeed[state_.phase]*dt,turnSpeed[state_.phase]*dt);
    if(nav_.turnOk(state_.pos.x,state_.pos.z,next)){state_.yaw=next;return true;}
    const auto room=[this](double x,double z){return nav_.wallClear(x,z)+std::min(nav_.floorClear(x,z),3.0)*.5;};
    const double r=walkSpeed[state_.phase]*.55*dt;double best=room(state_.pos.x,state_.pos.z)+1e-3;
    glm::dvec2 point;bool found=false;
    for(int k=0;k<8;++k){const double a=k/8.0*2*pi,x=state_.pos.x+std::sin(a)*r,z=state_.pos.z+std::cos(a)*r;
        if(!nav_.turnOk(x,z,state_.yaw))continue;
        const double score=room(x,z);if(score>best){best=score;point={x,z};found=true;}
    }
    if(!found)return false;
    state_.pos.x=point.x;state_.pos.z=point.y;
    if(nav_.turnOk(point.x,point.y,next))state_.yaw=next;
    return true;
}
void BossSimulation::walk(double dt,PlayerSimulation* target){
    if(!hasGoal_||goalTime_<=0||(!path_.empty()&&pathIndex_>=path_.size()&&goalTime_<1.5))chooseGoal(target);
    double want=state_.yaw;bool go=false;
    if(pathIndex_<path_.size()){
        glm::dvec2 wp=path_[pathIndex_];
        while(std::hypot(wp.x-state_.pos.x,wp.y-state_.pos.z)<1.1&&pathIndex_<path_.size()-1)wp=path_[++pathIndex_];
        const double d=std::hypot(wp.x-state_.pos.x,wp.y-state_.pos.z);
        if(pathIndex_>=path_.size()-1&&d<1.1)pathIndex_=path_.size();
        else{want=std::atan2(wp.x-state_.pos.x,wp.y-state_.pos.z);go=true;}
    }
    if(!go&&target)want=std::atan2(target->state.pos.x-state_.pos.x,target->state.pos.z-state_.pos.z);
    const double dy=angleDiff(state_.yaw,want);
    turnToward(want,dt);
    const double vmax=walkSpeed[state_.phase]*(go?std::clamp(1-std::abs(dy)/.9,0.0,1.0):0.0);
    speed_+=std::clamp(vmax-speed_,-4*dt,3*dt);
    if(speed_>.01){
        const double x=state_.pos.x+std::sin(state_.yaw)*speed_*dt,z=state_.pos.z+std::cos(state_.yaw)*speed_*dt;
        if(nav_.poseOk(x,z,state_.yaw)){state_.pos.x=x;state_.pos.z=z;stuckTime_=0;}
        else{speed_=0;stuckTime_+=dt;if(stuckTime_>.8){stuckTime_=0;goalTime_=0;path_.clear();}}
    }
}
std::optional<std::tuple<double,double,bool>> BossSimulation::chargeLane(double yaw)const{
    double score=-std::numeric_limits<double>::infinity();std::optional<std::tuple<double,double,bool>> best;
    for(const double offset:{0.0,.25,-.25,.5,-.5,.8,-.8,1.1,-1.1,1.45,-1.45,1.8,-1.8}){
        const double heading=yaw+offset;
        if(!nav_.turnOk(state_.pos.x,state_.pos.z,heading))continue;
        const auto cast=nav_.cast(state_.pos.x,state_.pos.z,heading,34);
        if(cast.dist<6)continue;
        const double s=std::min(cast.dist,22.0)*.2-std::abs(offset)*1.6+(cast.wall?1:0);
        if(s>score){score=s;best=std::make_tuple(heading,cast.dist,cast.wall);}
    }
    return best;
}
void BossSimulation::chooseMove(PlayerSimulation* target,double dt){
    if(!target)return;
    const double dx=target->state.pos.x-state_.pos.x,dz=target->state.pos.z-state_.pos.z;
    const double aim=std::atan2(dx,dz);
    const double fx=state_.pos.x+std::sin(state_.yaw)*2,fz=state_.pos.z+std::cos(state_.yaw)*2;
    const double distance=std::hypot(target->state.pos.x-fx,target->state.pos.z-fz);
    int near=0,far=0;
    for(const auto* a:squad_)if(a->state.alive&&!nav_.inPad(a->state.pos.x,a->state.pos.z,1)){
        const double d=flat(a->state.pos,state_.pos);near+=d<10.5;far+=d>13;
    }
    auto lane=chargeLane(aim);
    std::vector<std::pair<BossMoveId,double>> weights{
        {BossMoveId::Slam,distance<11?3+near*.6:.25},
        {BossMoveId::Barrage,1.3+far*.45+(distance>12?1:0)},
        {BossMoveId::Sweep,distance>5&&distance<17?2.1:.4},
        {BossMoveId::Charge,lane?(distance>5.5?2.2:1.2):0}
    };
    const int livingCrabs=static_cast<int>(std::count_if(crabs_.begin(),crabs_.end(),[](const auto& c){return !c.dead;}));
    if(state_.phase>=2&&crabletCooldown_<=0&&livingCrabs<3)weights.push_back({BossMoveId::Crablets,2.4});
    if(state_.phase>=3&&frenzyCooldown_<=0)weights.push_back({BossMoveId::Frenzy,1.1+near*.9});
    for(auto& w:weights)if(lastMove_&&w.first==*lastMove_)w.second*=.3;
    double sum=0;for(const auto& w:weights)sum+=w.second;
    double choice=random()*sum;BossMoveId id=BossMoveId::Barrage;
    for(const auto& w:weights){choice-=w.second;if(choice<=0){id=w.first;break;}}
    if(forcedMove_){id=*forcedMove_;forcedMove_.reset();if(id==BossMoveId::Charge&&!lane)id=BossMoveId::Barrage;}
    if(id==BossMoveId::Slam||id==BossMoveId::Sweep||id==BossMoveId::Charge){
        const double want=id==BossMoveId::Charge?std::get<0>(*lane):aim;
        const double dy=angleDiff(state_.yaw,want);
        if(std::abs(dy)>=.16){
            if(turnToward(want,dt))return;
            cool_=.3;lastMove_=id;return;
        }
    }
    startMove(id,target);
}
double BossSimulation::chargeTime(double length,double speed){
    constexpr double ramp=.35;
    const double s0=.5*speed*ramp;
    return length<=s0?std::sqrt(2*length*ramp/speed):ramp+(length-s0)/speed;
}
double BossSimulation::chargeDistance(const BossMove& move,double time){
    const double t=time-move.t0-move.tele;
    if(t<=0)return 0;
    constexpr double ramp=.35;
    return std::min(move.length,t<ramp?.5*move.speed/ramp*t*t:.5*move.speed*ramp+move.speed*(t-ramp));
}
double BossSimulation::beamAngle(const BossMove& move,double time){
    return move.a0+(move.a1-move.a0)*ease((time-move.t0-move.tele)/move.act);
}
void BossSimulation::startMove(BossMoveId id,PlayerSimulation* target){
    BossMove m;m.id=id;m.t0=round3(state_.clock);m.seed=static_cast<std::uint32_t>(random()*1e9);
    const int ph=state_.phase;const double factor=phasePace[ph];
    m.tele=std::max(.7,moveTele(id)*factor);m.act=moveAct(id)*factor;m.rec=moveRec(id)*factor;
    const double sx=std::sin(state_.yaw),sz=std::cos(state_.yaw),y=state_.pos.y;
    switch(id){
    case BossMoveId::Slam:{
        const double x=state_.pos.x+sx*4.3,z=state_.pos.z+sz*4.3;
        m.point={round3(x),round3(nav_.floorAt(x,z)),round3(z)};
        m.rings=ph>=3?std::vector<double>{0,.6}:ph>=2?std::vector<double>{0,.9}:std::vector<double>{0};
        m.act=1.2;break;
    }
    case BossMoveId::Barrage:{
        const int count=ph==1?4:ph==2?5:7;
        std::vector<PlayerSimulation*> candidates;
        for(auto* a:squad_)if(a->state.alive&&!a->superJumpState()&&!nav_.inPad(a->state.pos.x,a->state.pos.z,3.4))candidates.push_back(a);
        std::stable_sort(candidates.begin(),candidates.end(),[this](const auto* a,const auto* b){return flat(a->state.pos,state_.pos)>flat(b->state.pos,state_.pos);});
        for(int i=0;i<count&&!candidates.empty();++i){
            const auto* a=candidates[i%candidates.size()];const bool extra=i>=static_cast<int>(candidates.size());
            const double angle=random()*2*pi,off=extra?3+random()*2.5:0;
            double x=a->state.pos.x+a->state.vel.x*.3+std::cos(angle)*off,z=a->state.pos.z+a->state.vel.z*.3+std::sin(angle)*off;
            double g=geometry_.groundHeight(x,z,a->state.pos.y+1);
            if(!(g>PlayerTuning().number("waterY"))||nav_.inPad(x,z,2.9)){x=a->state.pos.x;z=a->state.pos.z;}
            g=geometry_.groundHeight(x,z,a->state.pos.y+1);
            if(!(g>PlayerTuning().number("waterY"))||nav_.inPad(x,z,2.9))continue;
            m.barrels.push_back({round3(x),round3(g),round3(z),round3(m.tele+m.barrels.size()*.26)});
        }
        if(m.barrels.empty()){cool_=.5;lastMove_=id;return;}
        m.origin={round3(state_.pos.x-sx*.6),round3(y+5.4),round3(state_.pos.z-sz*.6)};
        m.act=m.barrels.back().throwAt-m.tele+1.3+.1;break;
    }
    case BossMoveId::Sweep:{
        const double aim=std::atan2(target->state.pos.x-state_.pos.x,target->state.pos.z-state_.pos.z);
        const double dir=random()<.5?1:-1;
        m.origin={round3(state_.pos.x+sx*3.3),round3(y+2.3),round3(state_.pos.z+sz*3.3)};
        m.point.y=round3(y);m.a0=round3(aim-dir*sweepSpan[ph]/2);m.a1=round3(aim+dir*sweepSpan[ph]/2);
        m.yaw=round3(state_.yaw);break;
    }
    case BossMoveId::Charge:{
        const auto cast=nav_.cast(state_.pos.x,state_.pos.z,state_.yaw,34);
        if(cast.dist<6){cool_=.4;lastMove_=id;return;}
        m.point={round3(state_.pos.x),round3(y),round3(state_.pos.z)};m.yaw=round3(state_.yaw);
        m.length=round3(cast.dist);m.wall=cast.wall;m.speed=chargeSpeed[ph];m.stun=true;
        m.act=chargeTime(m.length,m.speed);m.rec=stunTime[ph];break;
    }
    case BossMoveId::Crablets:m.count=ph>=3?5:3;crabletCooldown_=14*pace();break;
    case BossMoveId::Frenzy:
        m.point={round3(state_.pos.x),round3(y),round3(state_.pos.z)};
        m.rot0=round3(random()*2*pi);m.spin=nav_.wallClear(state_.pos.x,state_.pos.z)>=5&&nav_.floorClear(state_.pos.x,state_.pos.z)>=3.9;
        m.stun=true;frenzyCooldown_=11*pace();break;
    }
    m.tele=round3(m.tele);m.act=round3(m.act);m.rec=round3(m.rec);
    move_=m;moves_.push_back(m);spawnedCrabs_=false;speed_=0;path_.clear();
}
void BossSimulation::runMove(double dt){
    const auto& m=*move_;const double u=state_.clock-m.t0;
    if(u>=m.end()){
        lastMove_=m.id;move_.reset();cool_=(coolTime[state_.phase]+random()*coolRandom[state_.phase])*pace();goalTime_=0;return;
    }
    switch(m.id){
    case BossMoveId::Charge:{
        const double distance=chargeDistance(m,state_.clock);
        state_.pos.x=m.point.x+std::sin(m.yaw)*distance;state_.pos.z=m.point.z+std::cos(m.yaw)*distance;state_.yaw=m.yaw;break;
    }
    case BossMoveId::Sweep:
        if(u>=m.tele&&u<=m.tele+m.act){
            const double want=m.yaw+angleDiff(m.yaw,beamAngle(m,state_.clock))*.5;
            if(nav_.turnOk(state_.pos.x,state_.pos.z,want))state_.yaw=want;
        }break;
    case BossMoveId::Frenzy:
        if(m.spin&&u>=m.tele&&u<=m.tele+m.act)
            state_.yaw+=dt*4.2*std::min({1.0,(u-m.tele)/.4,(m.tele+m.act-u)/.4});
        break;
    case BossMoveId::Crablets:
        if(!spawnedCrabs_&&u>=m.tele){spawnedCrabs_=true;spawnCrablets(m.count);}break;
    default:break;
    }
}
void BossSimulation::tick(double dt,bool playing){
    if(!std::isfinite(dt)||dt<=0||dt>.1)throw std::invalid_argument("Invalid boss step");
    const double previous=state_.clock,px=state_.pos.x,pz=state_.pos.z,pyaw=state_.yaw;
    state_.clock+=dt;
    crabletCooldown_-=dt;frenzyCooldown_-=dt;
    for(auto& d:damageBy_)d*=std::exp(-dt/8);
    if(!state_.visible&&state_.clock>=1.8){state_.visible=true;state_.anim=1;state_.animT0=state_.clock;}
    if(state_.anim==1&&state_.clock-state_.animT0>=3.4)state_.anim=0;
    if(state_.anim==2&&state_.clock-state_.animT0>=1.9){state_.anim=0;state_.invulnerable=false;cool_=std::min(cool_,.9*pace());}
    if(!state_.dead&&state_.visible){
        if(playing&&!finished_){
            updateCrablets(dt);
            if(move_)runMove(dt);
            else if(state_.anim==2){}
            else if(pendingPhase_>state_.phase){
                state_.phase=pendingPhase_;pendingPhase_=0;state_.anim=2;state_.animT0=state_.clock;
                state_.invulnerable=true;path_.clear();hasGoal_=false;
                if(state_.phase>=2)crabletCooldown_=std::min(crabletCooldown_,2.0);
                if(state_.phase>=3)frenzyCooldown_=std::min(frenzyCooldown_,3.0);
            }else{
                retarget_-=dt;goalTime_-=dt;cool_-=dt;
                if(retarget_<=0||!targetActor())chooseTarget();
                auto* target=targetActor();
                if(cool_<=0&&target)chooseMove(target,dt);
                if(!move_)walk(dt,target);
            }
        }else if(move_){move_.reset();state_.stunned=false;}
    }
    state_.stunned=move_&&move_->stun&&move_->phase(state_.clock)==BossMovePhase::Recovery;
    state_.pos.y+=(nav_.floorAt(state_.pos.x,state_.pos.z)-state_.pos.y)*std::min(1.0,dt*5);
    state_.vel={(state_.pos.x-px)/dt,0,(state_.pos.z-pz)/dt};state_.turn=angleDiff(pyaw,state_.yaw)/dt;
    if(playing&&!state_.dead&&!finished_&&state_.visible)advanceHazards(previous,state_.clock,dt,true);
    else advanceHazards(previous,state_.clock,dt,false);
    if(!externalHitShapes_)updateDefaultHitShapes();
}
void BossSimulation::spawnCrablets(int count){
    const double bx=-std::sin(state_.yaw),bz=-std::cos(state_.yaw);
    for(int i=0;i<count;++i){
        const double side=(i-(count-1)*.5)*1.1;
        double x=state_.pos.x+bx*3.6+bz*side,z=state_.pos.z+bz*3.6-bx*side;
        double ground=geometry_.groundHeight(x,z,state_.pos.y+1.5);
        if(!(ground>PlayerTuning().number("waterY"))||nav_.inPad(x,z,.8)){
            x=state_.pos.x+bz*side;z=state_.pos.z-bx*side;ground=state_.pos.y;
        }
        BossCrablet crab;crab.id=crabSeq_++;crab.pos={x,ground,z};crab.yaw=state_.yaw+pi+side*.4;
        crabs_.push_back(crab);
    }
}
void BossSimulation::popCrablet(BossCrablet& crab,bool killed){
    if(crab.dead)return;
    crab.dead=true;
    if(!killed){
        for(auto* actor:squad_){
            const auto& p=actor->state.pos;
            if(!actor->state.alive||nav_.inPad(p.x,p.z,.6))continue;
            if(std::hypot(p.x-crab.pos.x,p.z-crab.pos.z)<2.1&&std::abs(p.y-crab.pos.y)<1.6)
                damage(*actor,40*damageMultiplier_);
        }
        paintAt(crab.pos.x,crab.pos.y,crab.pos.z,1.8);
    }
}
void BossSimulation::updateCrablets(double dt){
    const double velocity=crabSpeed[state_.phase];
    for(auto& crab:crabs_){
        if(crab.dead)continue;
        crab.age+=dt;crab.retarget-=dt;
        if(crab.age>12){popCrablet(crab,false);continue;}
        if(crab.retarget<=0){
            crab.retarget=.4;crab.target=-1;double best=30;
            for(int i=0;i<static_cast<int>(squad_.size());++i){
                const auto& p=squad_[i]->state.pos;
                if(!squad_[i]->state.alive||nav_.inPad(p.x,p.z,.5))continue;
                const double d=flat(p,crab.pos);if(d<best){best=d;crab.target=i;}
            }
        }
        auto* target=crab.target>=0?squad_[crab.target]:nullptr;
        if(target&&target->state.alive&&flat(target->state.pos,crab.pos)<1.05&&
            std::abs(target->state.pos.y-crab.pos.y)<1.3){popCrablet(crab,false);continue;}
        bool moved=false;
        if(target&&target->state.alive){
            const double want=std::atan2(target->state.pos.x-crab.pos.x,target->state.pos.z-crab.pos.z);
            for(const double offset:{0.0,.5,-.5,1.0,-1.0,1.6,-1.6}){
                const double yaw=want+offset,x=crab.pos.x+std::sin(yaw)*velocity*dt,z=crab.pos.z+std::cos(yaw)*velocity*dt;
                if(nav_.inPad(x,z,.8))continue;
                const double ground=geometry_.groundHeight(x,z,crab.pos.y+.7);
                if(!(ground>PlayerTuning().number("waterY")+.3)||ground<crab.pos.y-1.4||
                    geometry_.pointInside({x,ground+.3,z}))continue;
                crab.pos.x=x;crab.pos.z=z;crab.pos.y+=(ground-crab.pos.y)*std::min(1.0,dt*12);
                crab.yaw+=angleDiff(crab.yaw,yaw)*std::min(1.0,dt*10);moved=true;break;
            }
        }
        crab.speed+=((moved?velocity:0)-crab.speed)*std::min(1.0,dt*8);
    }
}
void BossSimulation::damage(PlayerSimulation& actor,double amount){if(amount>0)actor.damage(amount);}
void BossSimulation::paintAt(double x,double y,double z,double radius){
    if(nav_.inPad(x,z,.3))return;
    const auto hit=geometry_.raycast({x,y+1.5,z},{0,-1,0},4,true);
    if(!hit.hit||hit.point.y<PlayerTuning().number("waterY"))return;
    paint_.splat(hit.point+hit.normal*.08,radius,1,random());
}
double BossSimulation::reachAt(const BossMove& move,double x,double z,double radius)const{
    constexpr int headings=96;
    double angle=std::atan2(x-move.point.x,z-move.point.z);
    if(angle<0)angle+=2*pi;
    const int bucket=static_cast<int>(std::floor(angle/(2*pi)*headings))%headings;
    const double a=(bucket+.5)/headings*2*pi,sx=std::sin(a),sz=std::cos(a);
    double r=0;
    while(r<radius&&(r<1.2||nav_.kindAt(move.point.x+sx*r,move.point.z+sz*r)==0))r+=.35;
    return std::min(r,radius);
}
double BossSimulation::beamLength(const BossMove& move,double angle)const{
    const double quant=std::round(angle*90)/90;
    const auto hit=geometry_.raycast({move.origin.x,move.point.y+1,move.origin.z},
        {std::sin(quant),0,std::cos(quant)},17,true);
    return hit.hit?hit.dist:17;
}
void BossSimulation::hurtSquad(const BossMove& m,double from,double to,double dt){
    const double u0=from-m.t0,u1=to-m.t0;
    if(u1<0||u1>m.end()+4)return;
    for(int actorId=0;actorId<static_cast<int>(squad_.size());++actorId){
        auto& a=*squad_[actorId];const auto& p=a.state.pos;
        if(!a.state.alive||nav_.inPad(p.x,p.z,.6))continue;
        const auto once=[&](const char* kind,int index,double amount){
            const std::string key=std::to_string(m.t0)+"|"+kind+"|"+std::to_string(index)+"|"+std::to_string(actorId);
            if(std::find(oneShotHits_.begin(),oneShotHits_.end(),key)!=oneShotHits_.end())return;
            oneShotHits_.push_back(key);damage(a,amount*damageMultiplier_);
        };
        switch(m.id){
        case BossMoveId::Slam:{
            const double d=flat(p,m.point);
            if(u0<m.tele&&u1>=m.tele&&d<3.3+.35&&p.y<m.point.y+2)once("impact",0,48);
            for(int k=0;k<static_cast<int>(m.rings.size());++k){
                const double start=m.tele+m.rings[k];if(u1<start)continue;
                const double r0=2.4+9.5*(std::max(u0,start)-start),r1=2.4+9.5*(u1-start);
                if(r0>19)continue;
                if(d>r0-.55-.25&&d<r1+.55+.25&&p.y<m.point.y+.5&&p.y>m.point.y-.8&&
                    d<reachAt(m,p.x,p.z,19)+.3)once("ring",k,32);
            }
            break;
        }
        case BossMoveId::Barrage:
            for(int k=0;k<static_cast<int>(m.barrels.size());++k){
                const auto& b=m.barrels[k];const double land=b.throwAt+1.3;
                if(u0<land&&u1>=land&&std::hypot(p.x-b.x,p.z-b.z)<2.4+.35&&std::abs(p.y-b.y)<2.2)
                    once("barrel",k,52);
            }break;
        case BossMoveId::Sweep:{
            if(u1<m.tele||u1>m.tele+m.act||a.state.submerged)break;
            const double angle=beamAngle(m,to),length=beamLength(m,angle),sx=std::sin(angle),sz=std::cos(angle);
            const double dx=p.x-m.origin.x,dz=p.z-m.origin.z,along=dx*sx+dz*sz,lat=std::abs(dx*sz-dz*sx);
            if(along>1.5&&along<length+.4&&lat<.95+a.hitRadius()&&p.y<m.point.y+2.4&&
                geometry_.los(m.origin,p+glm::dvec3(0,.9,0)))damage(a,170*damageMultiplier_*dt);
            break;
        }
        case BossMoveId::Charge:{
            if(u1<m.tele||u1>m.tele+m.act)break;
            const double distance=chargeDistance(m,to),bx=m.point.x+std::sin(m.yaw)*distance,bz=m.point.z+std::cos(m.yaw)*distance;
            for(const auto [offset,radius]:{std::pair<double,double>{2.3,2.5},{-1.8,2.6}}){
                const double cx=bx+std::sin(m.yaw)*offset,cz=bz+std::cos(m.yaw)*offset;
                if(std::hypot(p.x-cx,p.z-cz)<radius+.35&&p.y<m.point.y+3.5){
                    const std::string key=std::to_string(m.t0)+"|ram|0|"+std::to_string(actorId);
                    if(std::find(oneShotHits_.begin(),oneShotHits_.end(),key)==oneShotHits_.end()){
                        const double side=(p.x-bx)*std::cos(m.yaw)-(p.z-bz)*std::sin(m.yaw)>=0?1:-1;
                        a.state.vel.x+=std::cos(m.yaw)*side*9+std::sin(m.yaw)*5;
                        a.state.vel.z+=-std::sin(m.yaw)*side*9+std::cos(m.yaw)*5;
                        a.state.vel.y=7;a.state.grounded=false;
                        once("ram",0,55);
                    }
                    break;
                }
            }
            break;
        }
        case BossMoveId::Frenzy:
            if(u1>=m.tele&&u1<=m.tele+m.act&&flat(p,m.point)<8.8&&p.y<m.point.y+3&&
                geometry_.los(m.point+glm::dvec3(0,1.6,0),p+glm::dvec3(0,.8,0)))
                damage(a,52*damageMultiplier_*dt);
            break;
        default:break;
        }
    }
}
void BossSimulation::paintHazards(const BossMove& m,double from,double to){
    const double u0=from-m.t0,u1=to-m.t0;
    const auto steps=[&](double start,double every,auto paint){
        const int k0=static_cast<int>(std::floor((u0-start)/every)),k1=static_cast<int>(std::floor((u1-start)/every));
        for(int k=std::max(0,k0+1);k<=k1;++k)paint(k,start+k*every);
    };
    switch(m.id){
    case BossMoveId::Slam:
        if(u0<m.tele&&u1>=m.tele){paintAt(m.point.x,m.point.y,m.point.z,2.8);
            for(int i=0;i<7;++i){const double a=i*.9;paintAt(m.point.x+std::cos(a)*2.6,m.point.y,m.point.z+std::sin(a)*2.6,1.3);}}
        for(int ring=0;ring<static_cast<int>(m.rings.size());++ring){
            steps(m.tele+m.rings[ring],2.2/9.5,[&,ring](int k,double){
                const double radius=2.4+2.2*(k+1);if(radius>18)return;
                const int count=static_cast<int>(std::round(radius*1.4));
                for(int i=0;i<count;++i){const double a=i/double(count)*2*pi+ring*.4+k;
                    const double x=m.point.x+std::cos(a)*radius,z=m.point.z+std::sin(a)*radius;
                    if(radius<reachAt(m,x,z,19))paintAt(x,m.point.y,z,.75);
                }
            });
        }break;
    case BossMoveId::Barrage:
        for(const auto& b:m.barrels)if(u0<b.throwAt+1.3&&u1>=b.throwAt+1.3){
            paintAt(b.x,b.y,b.z,2.2);
            for(int i=0;i<4;++i){const double a=i*1.6+b.x;paintAt(b.x+std::cos(a)*1.8,b.y,b.z+std::sin(a)*1.8,1);}
        }break;
    case BossMoveId::Sweep:
        steps(m.tele,.06,[&](int k,double t){
            if(t>m.tele+m.act)return;
            const double angle=beamAngle(m,m.t0+t),length=beamLength(m,angle);
            paintAt(m.origin.x+std::sin(angle)*length,m.point.y,m.origin.z+std::cos(angle)*length,1.05);
            if(k%2){const double f=.45+.4*std::fmod(k*.37,1.0);
                paintAt(m.origin.x+std::sin(angle)*length*f,m.point.y,m.origin.z+std::cos(angle)*length*f,.8);}
        });break;
    case BossMoveId::Charge:
        steps(m.tele,.09,[&](int k,double t){
            if(t>m.tele+m.act)return;
            const double d=chargeDistance(m,m.t0+t),sx=std::sin(m.yaw),sz=std::cos(m.yaw),side=k%2?1.3:-1.3;
            paintAt(m.point.x+sx*(d-1)+sz*side,m.point.y,m.point.z+sz*(d-1)-sx*side,1);
        });break;
    case BossMoveId::Frenzy:
        steps(m.tele,.07,[&](int k,double t){
            if(t>m.tele+m.act)return;
            for(int j=0;j<2;++j){
                const double a=m.rot0+k*.07*2.6+j*pi+(k%3)*.7;
                const double r=8.8*(.35+.6*std::fmod(k*.618+j*.3,1.0));
                paintAt(m.point.x+std::sin(a)*r,m.point.y,m.point.z+std::cos(a)*r,.9);
            }
        });break;
    default:break;
    }
}
void BossSimulation::advanceHazards(double from,double to,double dt,bool live){
    if(!live)return;
    for(const auto& m:moves_){
        const double u=to-m.t0;
        const double extra=m.id==BossMoveId::Slam?(19-2.4)/9.5+(m.rings.empty()?0:m.rings.back()):0;
        if(u<0||u>=m.end()+extra+.2)continue;
        hurtSquad(m,from,to,dt);paintHazards(m,from,to);
    }
    moves_.erase(std::remove_if(moves_.begin(),moves_.end(),[to](const auto& m){return to-m.t0>m.end()+4;}),moves_.end());
}
void BossSimulation::updateDefaultHitShapes(){
    // Socket layout is updated by the presentation's animated rig when available.
    // The source rest-pose radii remain authoritative for headless combat queries.
    const double s=std::sin(state_.yaw),c=std::cos(state_.yaw);
    const auto place=[&](double x,double y,double z){return state_.pos+glm::dvec3(c*x+s*z,y,-s*x+c*z);};
    hitShapes_={{
        {"shellF",place(0,3.5,2.1),1.8,false,true},
        {"shellR",place(0,3.5,-2.1),1.8,false,true},
        {"body",place(0,2.2,2.3),1.05,false,true},
        {"clawL",place(-2.1,1.1,3.0),.85,false,true},
        {"clawR",place(2.1,1.1,3.0),.55,false,true},
        {"eyeL",place(-.65,2.9,3.5),.4,true,true},
        {"eyeR",place(.65,2.9,3.5),.4,true,true},
        {"belly",place(0,1.5,2.1),.8,true,state_.stunned||state_.phase>=3},
        {"crackL",place(-.7,4.9,1.6),.7,true,state_.phase>=2},
        {"crackR",place(.7,4.9,1.6),.7,true,state_.phase>=2}
    }};
}
void BossSimulation::setHitShapes(const std::array<BossHitShape,10>& shapes){hitShapes_=shapes;externalHitShapes_=true;}
std::optional<BossSegmentHit> BossSimulation::segmentHit(glm::dvec3 from,glm::dvec3 to,double pad)const{
    const glm::dvec3 delta=to-from;const double length2=std::max(glm::dot(delta,delta),1e-9);
    double score=std::numeric_limits<double>::infinity();std::optional<BossSegmentHit> best;
    if(state_.visible&&!state_.dead&&!finished_)for(int i=0;i<static_cast<int>(hitShapes_.size());++i){
        const auto& shape=hitShapes_[i];if(!shape.active)continue;
        const double t=std::clamp(glm::dot(shape.pos-from,delta)/length2,0.0,1.0);
        const glm::dvec3 miss=from+delta*t-shape.pos;
        const double r=shape.radius+pad,d2=glm::dot(miss,miss);
        if(d2>r*r)continue;
        const double entry=std::max(0.0,t-std::sqrt(std::max(0.0,r*r-d2)/length2));
        const double key=entry-(shape.weak?.02:0);
        if(key<score){score=key;best=BossSegmentHit{from+delta*entry,entry,std::sqrt(length2)*entry,i,-1,shape.weak};}
    }
    if(!finished_)for(int i=0;i<static_cast<int>(crabs_.size());++i){
        const auto& crab=crabs_[i];if(crab.dead)continue;
        const glm::dvec3 center=crab.pos+glm::dvec3(0,.35,0);
        const double t=std::clamp(glm::dot(center-from,delta)/length2,0.0,1.0);
        const glm::dvec3 miss=from+delta*t-center;
        if(glm::dot(miss,miss)>(.55+pad)*(.55+pad))continue;
        if(best&&t>=best->t)continue;
        best=BossSegmentHit{from+delta*t,t,std::sqrt(length2)*t,-1,i,false};
    }
    return best;
}
double BossSimulation::applyDamage(PlayerSimulation& attacker,double amount,bool weak){
    if(state_.dead||state_.invulnerable||!state_.visible||finished_)return 0;
    poseHitPulse_.flash=std::max(poseHitPulse_.flash,std::clamp(amount/90.0,.25,1.0));
    if(weak)poseHitPulse_.weakFlash=std::max(poseHitPulse_.weakFlash,std::clamp(amount/90.0,.25,1.0));
    if(amount>=60)poseHitPulse_.hurt=std::max(poseHitPulse_.hurt,std::clamp(amount/160.0,.3,1.0));
    const double landed=std::min(amount,state_.hp);state_.hp-=landed;
    const int index=static_cast<int>(std::distance(squad_.begin(),std::find(squad_.begin(),squad_.end(),&attacker)));
    if(index>=0&&index<static_cast<int>(damageBy_.size())){
        damageBy_[index]+=landed;totalDamageBy_[index]+=landed;if(weak)++weakHitsBy_[index];
    }
    if(state_.hp<=.001){state_.hp=0;state_.dead=true;state_.killer=index;finish();for(auto& crab:crabs_)crab.dead=true;}
    else{
        const double fraction=state_.hp/state_.maxHp;
        const int want=fraction<=.33?3:fraction<=.66?2:1;
        if(want>state_.phase&&want>pendingPhase_)pendingPhase_=want;
    }
    return landed;
}
double BossSimulation::hit(PlayerSimulation& attacker,double amount,const BossSegmentHit& hit,const std::string& weapon){
    if(amount<=0||state_.dead||finished_)return 0;
    if(hit.crablet>=0){
        auto& crab=crabs_.at(hit.crablet);if(crab.dead)return 0;
        const double landed=std::min(amount,crab.hp);crab.hp-=landed;
        if(crab.hp<=0){const int index=static_cast<int>(std::distance(squad_.begin(),std::find(squad_.begin(),squad_.end(),&attacker)));if(index>=0&&index<static_cast<int>(splatsBy_.size()))++splatsBy_[index];popCrablet(crab,true);}
        return landed;
    }
    double multiplier=1;
    if(weapon=="roller")multiplier=.3;
    else if(weapon=="slam")multiplier=.6;
    else if(weapon=="bomb"||weapon=="sticky"||weapon=="seeker"||weapon=="burst"||weapon=="shaker"||weapon=="waddle"||weapon=="torpedo")multiplier=.75;
    return applyDamage(attacker,amount*(hit.weak?2.5:1)*(state_.stunned?1.25:1)*multiplier,hit.weak);
}
double BossSimulation::splash(PlayerSimulation& attacker,glm::dvec3 center,double radius,double maxDamage,double minDamage,const std::string& weapon){
    if(finished_)return 0;
    double total=0;
    for(int i=0;i<static_cast<int>(crabs_.size());++i){
        const auto& crab=crabs_[i];
        if(!crab.dead&&glm::length(crab.pos+glm::dvec3(0,.3,0)-center)<radius+.4){
            BossSegmentHit hit;hit.crablet=i;total+=this->hit(attacker,maxDamage,hit,weapon);
        }
    }
    if(!state_.visible)return total;
    const BossHitShape* nearest=nullptr;double distance=std::numeric_limits<double>::infinity();
    for(const auto& shape:hitShapes_){
        if(!shape.active||shape.weak)continue;
        const double d=std::max(0.0,glm::length(shape.pos-center)-shape.radius);
        if(d<distance){distance=d;nearest=&shape;}
    }
    if(!nearest||distance>radius)return total;
    const double length=glm::length(nearest->pos-center);
    const glm::dvec3 to=glm::mix(nearest->pos,center,std::min(.9,nearest->radius/std::max(nearest->radius,length)));
    if(!geometry_.los(center+glm::dvec3(0,.3,0),to))return total;
    BossSegmentHit h;h.shape=static_cast<int>(nearest-hitShapes_.data());
    total+=hit(attacker,maxDamage+(minDamage-maxDamage)*std::clamp(distance/radius,0.0,1.0),h,weapon);
    return total;
}
BossThreat BossSimulation::threat(double x,double y,double z,double horizon)const{
    BossThreat out;
    if(nav_.inPad(x,z,.6))return out;
    const auto push=[&](double level,double dx,double dz){
        const double d=std::max(1e-9,std::hypot(dx,dz));out.ax+=dx/d*level;out.az+=dz/d*level;out.level=std::max(out.level,level);
    };
    for(const auto& m:moves_){
        const double u=state_.clock-m.t0;if(u<0||u>m.end()+4)continue;
        const double dx=x-m.point.x,dz=z-m.point.z,d=std::hypot(dx,dz);
        switch(m.id){
        case BossMoveId::Slam:
            if(u<m.tele+.1&&d<4.5)push(1,dx,dz);
            for(const double offset:m.rings){
                const double r=2.4+9.5*(u-m.tele-offset),in=(d-.55-r)/9.5;
                if(d>reachAt(m,x,z,19)+.3||std::abs(y-m.point.y)>.8)continue;
                if(r<19&&in>-.1&&in<horizon&&(out.ringIn<0||in<out.ringIn))out.ringIn=std::max(0.0,in);
            }break;
        case BossMoveId::Barrage:
            for(const auto& b:m.barrels){const double land=b.throwAt+1.3;
                if(u<land+.05&&u>b.throwAt-.6&&std::hypot(x-b.x,z-b.z)<3.4)push(.9,x-b.x,z-b.z);}
            break;
        case BossMoveId::Sweep:
            if(u>m.tele+m.act||std::hypot(x-m.origin.x,z-m.origin.z)>18.5)break;
            {const double ang=std::atan2(x-m.origin.x,z-m.origin.z),from=u<m.tele?m.a0:beamAngle(m,state_.clock);
            const double lo=std::min(from,m.a1)-.2,hi=std::max(from,m.a1)+.2;
            double a=ang;while(a<lo-pi)a+=2*pi;while(a>hi+pi)a-=2*pi;
            if(a>=lo&&a<=hi){out.cover=out.beam=true;push(.8,-std::sin(state_.yaw),-std::cos(state_.yaw));}}
            break;
        case BossMoveId::Charge:
            if(u>m.tele+m.act)break;
            {const double distance=chargeDistance(m,std::max(state_.clock,m.t0+m.tele)),sx=std::sin(m.yaw),sz=std::cos(m.yaw);
            const double lx=x-m.point.x,lz=z-m.point.z,along=lx*sx+lz*sz,lat=lx*sz-lz*sx;
            if(along>distance-3&&along<m.length+5&&std::abs(lat)<3.6){out.lane=true;push(1,sz*(lat>=0?1:-1),-sx*(lat>=0?1:-1));}}
            break;
        case BossMoveId::Frenzy:
            if(u<m.tele+m.act&&d<10)push(.8,dx,dz);
            break;
        default:break;
        }
    }
    return out;
}
} // namespace restoration
