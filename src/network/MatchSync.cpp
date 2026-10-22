#include "MatchSync.hpp"

#include "../game/ArsenalData.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace restoration::network {
namespace {
constexpr std::uint32_t kMagic = 0x4e595354; // TSYN
constexpr std::size_t kMaxBodyChunk = 40 * 1024;
constexpr std::size_t kMaxLogicalBytes = MaxWirePacketBytes - 23;
constexpr std::uint16_t kMaxChunks = 8;
constexpr double kSnapshotPeriod = 0.05;
constexpr double kAuthorityPeriod = 0.25;
constexpr std::uint32_t kUnattributedNid = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kMaxReliableEventBytes = 56 * 1024;

void writeVec(Writer& w, const glm::dvec3& v) { w.f64(v.x).f64(v.y).f64(v.z); }
glm::dvec3 readVec(Reader& r) {
    const glm::dvec3 value{r.f64(),r.f64(),r.f64()};
    if(std::max({std::abs(value.x),std::abs(value.y),std::abs(value.z)})>1e6)throw WireError("match vector exceeds world bounds");
    return value;
}
void writeI32Vec(Writer& w, const std::vector<int>& values, std::size_t limit) {
    if(values.size()>limit)throw WireError("match vector exceeds its limit");
    w.u16(static_cast<std::uint16_t>(values.size()));for(int value:values)w.i32(value);
}
std::vector<int> readI32Vec(Reader& r, std::size_t limit) {
    const auto count=r.u16();if(count>limit)throw WireError("match vector exceeds its limit");
    std::vector<int> out;out.reserve(count);for(std::uint16_t i=0;i<count;++i)out.push_back(r.i32());return out;
}

void writeState(Writer& w, const PlayerState& s) {
    writeVec(w,s.pos);writeVec(w,s.vel);
    for(double v:{s.yaw,s.hp,s.ink,s.specialCharge,s.hurtFlash,s.landT,s.landSpeed,s.smoothY,s.respawnTimer,s.invuln})w.f64(v);
    w.boolean(s.alive).boolean(s.grounded).boolean(s.climbing).boolean(s.submerged).boolean(s.squid).i32(s.groundTeam);
    for(double v:{s.track,s.poison,s.reveal,s.shield})w.f64(v);
    w.i32(s.trackTeam).i32(s.revealTeam);
}
PlayerState readState(Reader& r) {
    PlayerState s;s.pos=readVec(r);s.vel=readVec(r);
    s.yaw=r.f64();s.hp=r.f64();s.ink=r.f64();s.specialCharge=r.f64();s.hurtFlash=r.f64();s.landT=r.f64();s.landSpeed=r.f64();s.smoothY=r.f64();s.respawnTimer=r.f64();s.invuln=r.f64();
    s.alive=r.boolean();s.grounded=r.boolean();s.climbing=r.boolean();s.submerged=r.boolean();s.squid=r.boolean();s.groundTeam=r.i32();
    s.track=r.f64();s.poison=r.f64();s.reveal=r.f64();s.shield=r.f64();s.trackTeam=r.i32();s.revealTeam=r.i32();
    if(s.hp<0||s.hp>100||s.ink<0||s.ink>100||s.groundTeam<0||s.groundTeam>2||s.trackTeam < -1||s.trackTeam>1||s.revealTeam < -1||s.revealTeam>1)
        throw WireError("invalid actor snapshot state");
    return s;
}
void writeInput(Writer& w,const PlayerInput& i){
    writeVec(w,i.move);w.boolean(i.jump).boolean(i.squid).boolean(i.fire).boolean(i.sub).boolean(i.special).boolean(i.cheer);
    w.f64(i.aimYaw).f64(i.aimPitch);writeVec(w,i.aimPoint);w.boolean(i.hasAimPoint).boolean(i.specialConfirm).boolean(i.hasMapTarget);writeVec(w,i.mapTarget);
}
PlayerInput readInput(Reader& r){
    PlayerInput i;i.move=readVec(r);i.jump=r.boolean();i.squid=r.boolean();i.fire=r.boolean();i.sub=r.boolean();i.special=r.boolean();i.cheer=r.boolean();
    i.aimYaw=r.f64();i.aimPitch=r.f64();i.aimPoint=readVec(r);i.hasAimPoint=r.boolean();i.specialConfirm=r.boolean();i.hasMapTarget=r.boolean();i.mapTarget=readVec(r);
    if(glm::length(i.move)>1.02||std::abs(i.aimPitch)>1.7)throw WireError("invalid actor input snapshot");
    return i;
}
void writeCombat(Writer& w,const CombatPose& p){
    for(bool v:{p.firing,p.charging,p.rolling,p.subAim,p.special,p.guard,p.streaming,p.turret,p.leaping,p.specialThrown,p.canopyOpening,p.shakerCharging})w.boolean(v);
    for(double v:{p.charge,p.firingTime,p.dodgeTime,p.specialTime,p.burstTime,p.actionYaw,p.lockTime,p.canopyOpen,p.canopyGrow,p.canopyHp,p.canopyHpMax,p.canopyShake,p.canopyFlash,p.canopyRegrowK,p.bladeFlashTime,p.mittsLandTime,p.shakerCharge})w.f64(v);
    w.i32(p.bowRing).i32(p.bowRingEvents).i32(p.mittsLeapEvents).i32(p.hand);
    w.string(p.specialKind,64);w.f64(p.sinceHand[0]).f64(p.sinceHand[1]).string(p.canopyState,32);
}
CombatPose readCombat(Reader& r){
    CombatPose p;p.firing=r.boolean();p.charging=r.boolean();p.rolling=r.boolean();p.subAim=r.boolean();p.special=r.boolean();p.guard=r.boolean();p.streaming=r.boolean();p.turret=r.boolean();p.leaping=r.boolean();p.specialThrown=r.boolean();p.canopyOpening=r.boolean();p.shakerCharging=r.boolean();
    p.charge=r.f64();p.firingTime=r.f64();p.dodgeTime=r.f64();p.specialTime=r.f64();p.burstTime=r.f64();p.actionYaw=r.f64();p.lockTime=r.f64();p.canopyOpen=r.f64();p.canopyGrow=r.f64();p.canopyHp=r.f64();p.canopyHpMax=r.f64();p.canopyShake=r.f64();p.canopyFlash=r.f64();p.canopyRegrowK=r.f64();p.bladeFlashTime=r.f64();p.mittsLandTime=r.f64();p.shakerCharge=r.f64();
    p.bowRing=r.i32();p.bowRingEvents=r.i32();p.mittsLeapEvents=r.i32();p.hand=r.i32();p.specialKind=r.string(64);p.sinceHand[0]=r.f64();p.sinceHand[1]=r.f64();p.canopyState=r.string(32);
    if(p.bowRing<0||p.bowRing>8||p.hand < -1||p.hand>2)throw WireError("invalid actor combat pose");return p;
}
void writeSpecial(Writer& w,const SpecialPose& p){
    for(bool v:{p.active,p.body,p.casting,p.thrown,p.noSquid,p.aimFace,p.inkProof,p.faceMove,p.mapTarget,p.jumpBack,p.roll,p.raise})w.boolean(v);
    for(double v:{p.elapsed,p.remaining,p.moveSpeed,p.accel,p.charge,p.hitPoints,p.hitRadius,p.hitHeight,p.hang,p.firing,p.seat,p.guard})w.f64(v);
    w.boolean(p.faceYaw.has_value());if(p.faceYaw)w.f64(*p.faceYaw);writeVec(w,p.target);writeVec(w,p.origin);w.string(p.kind,64).string(p.phase,64);
}
SpecialPose readSpecial(Reader& r){
    SpecialPose p;p.active=r.boolean();p.body=r.boolean();p.casting=r.boolean();p.thrown=r.boolean();p.noSquid=r.boolean();p.aimFace=r.boolean();p.inkProof=r.boolean();p.faceMove=r.boolean();p.mapTarget=r.boolean();p.jumpBack=r.boolean();p.roll=r.boolean();p.raise=r.boolean();
    p.elapsed=r.f64();p.remaining=r.f64();p.moveSpeed=r.f64();p.accel=r.f64();p.charge=r.f64();p.hitPoints=r.f64();p.hitRadius=r.f64();p.hitHeight=r.f64();p.hang=r.f64();p.firing=r.f64();p.seat=r.f64();p.guard=r.f64();
    if(r.boolean())p.faceYaw=r.f64();p.target=readVec(r);p.origin=readVec(r);p.kind=r.string(64);p.phase=r.string(64);return p;
}

void writeActorResult(Writer& w,const std::optional<MatchResult>& result){
    w.boolean(result.has_value());if(!result)return;
    w.f64(result->coverage[0]).f64(result->coverage[1]).i32(result->winner).boolean(result->boss.has_value());
    if(result->boss){const auto& b=*result->boss;w.boolean(b.win).f64(b.time).f64(b.hp).f64(b.maxHp).i32(b.phase);}
    w.boolean(result->zones.has_value());if(!result->zones)return;
    const auto& z=*result->zones;w.f64(z.counts[0]).f64(z.counts[1]).f64(z.penalty[0]).f64(z.penalty[1]).boolean(z.overtime).string(z.reason,256);
    w.u16(static_cast<std::uint16_t>(z.zoneTurf.size()));for(double value:z.zoneTurf)w.f64(value);
    if(z.events.size()>128||z.log.size()>512)throw WireError("match result exceeds its limit");
    w.u16(static_cast<std::uint16_t>(z.events.size()));
    for(const auto& e:z.events){
        w.u8(static_cast<std::uint8_t>(e.type)).i32(e.zone).i32(e.owner).i32(e.previousOwner).i32(e.team).i32(e.winner).string(e.objective,128).string(e.reason,256);
        writeI32Vec(w,e.activeZones,32);w.f64(e.counts[0]).f64(e.counts[1]).f64(e.penalties[0]).f64(e.penalties[1]);
        w.f64(e.time).f64(e.share).f64(e.penalty).f64(e.start).f64(e.end).i32(e.losing).boolean(e.final).boolean(e.moved);
    }
    w.u16(static_cast<std::uint16_t>(z.log.size()));for(const auto& e:z.log)w.f64(e.time).f64(e.start).f64(e.end).f64(e.penalty).f64(e.total).i32(e.team);
}
std::optional<MatchResult> readActorResult(Reader& r){
    if(!r.boolean())return std::nullopt;MatchResult result;result.coverage={r.f64(),r.f64()};result.winner=r.i32();if(result.winner<0||result.winner>1)throw WireError("invalid match winner");
    if(r.boolean()){BossResult b;b.win=r.boolean();b.time=r.f64();b.hp=r.f64();b.maxHp=r.f64();b.phase=r.i32();result.boss=b;}
    if(!r.boolean())return result;ZoneResult z;z.counts={r.f64(),r.f64()};z.penalty={r.f64(),r.f64()};z.overtime=r.boolean();z.reason=r.string(256);
    const auto turfCount=r.u16();if(turfCount>8)throw WireError("match result exceeds its limit");z.zoneTurf.reserve(turfCount);for(std::uint16_t i=0;i<turfCount;++i)z.zoneTurf.push_back(r.f64());
    const auto eventCount=r.u16();if(eventCount>128)throw WireError("match result exceeds its limit");z.events.reserve(eventCount);
    for(std::uint16_t i=0;i<eventCount;++i){
        ZoneEvent e;const auto type=r.u8();if(type>static_cast<std::uint8_t>(ZoneEventType::MatchEnded))throw WireError("invalid Zone event type");e.type=static_cast<ZoneEventType>(type);
        e.zone=r.i32();e.owner=r.i32();e.previousOwner=r.i32();e.team=r.i32();e.winner=r.i32();e.objective=r.string(128);e.reason=r.string(256);e.activeZones=readI32Vec(r,32);
        e.counts={r.f64(),r.f64()};e.penalties={r.f64(),r.f64()};e.time=r.f64();e.share=r.f64();e.penalty=r.f64();e.start=r.f64();e.end=r.f64();e.losing=r.i32();e.final=r.boolean();e.moved=r.boolean();z.events.push_back(std::move(e));
    }
    const auto logCount=r.u16();if(logCount>512)throw WireError("match result exceeds its limit");z.log.reserve(logCount);
    for(std::uint16_t i=0;i<logCount;++i){ZonePenaltyRecord e;e.time=r.f64();e.start=r.f64();e.end=r.f64();e.penalty=r.f64();e.total=r.f64();e.team=r.i32();z.log.push_back(e);}
    result.zones=std::move(z);return result;
}

glm::dvec3 hermite(const glm::dvec3& a,const glm::dvec3& va,const glm::dvec3& b,const glm::dvec3& vb,double t,double dt){
    const auto clampVelocity=[](glm::dvec3 v){const double n=glm::length(v);return n>35.0?v*(35.0/n):v;};
    const double t2=t*t,t3=t2*t;return (2*t3-3*t2+1)*a+(t3-2*t2+t)*dt*clampVelocity(va)+(-2*t3+3*t2)*b+(t3-t2)*dt*clampVelocity(vb);
}
}

enum class MatchSync::PacketKind : std::uint8_t {
    ActorSnapshot=1, MatchState=2, BossState=3, ZoneState=4, Paint=5, Cells=6,
    Visual=7, Hit=8, DeviceHit=9, BossHit=10, BossSplash=11, EventBatch=12,
    Trigger=13, ActorEffect=14, BeaconUse=15, Fragment=255
};

MatchOptions MatchSync::BuildOptions(RoomSession& session,const PlayerProfile& profile){
    const auto& view=session.view();if(view.phase!=RoomPhase::Starting&&view.phase!=RoomPhase::Match)throw std::logic_error("A match can only be built from a starting room");
    if(!view.selfId||!view.hostId||view.matchEpoch==0)throw std::invalid_argument("Room match identity is incomplete");
    auto roster=session.startRoster();if(roster.empty()||roster.size()>8)throw std::invalid_argument("Room match roster is invalid");
    std::sort(roster.begin(),roster.end(),[](const auto& a,const auto& b){return a.nid<b.nid;});
    if(std::none_of(roster.begin(),roster.end(),[&](const auto& e){return !e.bot&&e.owner==view.selfId;}))throw std::invalid_argument("Room match has no local actor");
    MatchOptions options;options.network=true;options.mode=view.settings.mode;options.duration=options.mode=="zones"?300:static_cast<int>(view.settings.duration);
    options.seed=view.seed?view.seed:1;options.difficulty=view.settings.difficulty;options.localPeer=view.selfId;options.authorityPeer=view.hostId;options.networkRoster=std::move(roster);
    const auto local=std::find_if(options.networkRoster.begin(),options.networkRoster.end(),[&](const auto& e){return !e.bot&&e.owner==view.selfId;});
    options.playerName=local->name.empty()?profile.name:local->name;options.main=local->main.empty()?profile.weapon:local->main;
    options.sub=local->sub.empty()?profile.sub:local->sub;options.special=local->special.empty()?profile.special:local->special;options.style=local->style;
    return options;
}

MatchSync::MatchSync(RoomSession& session,MatchSimulation& match,FacePaint& paint)
    :session_(session),match_(match),paint_(paint),previousPaintActor_(paint.networkActor()),localPeer_(session.view().selfId),authorityPeer_(session.view().hostId),epoch_(session.view().matchEpoch){
    if(!match.networked()||!localPeer_||!authorityPeer_||!epoch_||match.localPeer()!=localPeer_||match.authorityPeer()!=authorityPeer_)
        throw std::invalid_argument("MatchSync requires a matching online room and simulation");
    lastRoster_=session.startRoster();
    match_.world().setNetworkLocalPeer(localPeer_);
    paint_.setNetworkActor(kUnattributedNid);
    paint_.setPaintJournalSink([this](const auto& event){sendPaint(event);});
    paint_.setCellsJournalSink([this](const auto& cells,int team){sendCells(cells,team);});
    match_.world().setNetworkVisualForwarder([this](const auto& event){sendVisual(event);});
    match_.world().setNetworkHitForwarder([this](auto& attacker,auto& victim,double damage,const auto& source){
        if(victim.networkOwner()==localPeer_)return false;
        if(attacker.networkOwner()!=localPeer_||victim.networkOwner()==0)return false;
        sendHit(attacker,victim,damage,source);return true;
    });
    match_.world().setNetworkDeviceHitForwarder([this](std::uint32_t owner,std::uint64_t id,double damage,int team){sendDeviceHit(owner,id,damage,team);});
    match_.world().setNetworkActorEffectForwarder([this](auto& source,auto& target,const NetworkActorEffect& effect){
        if(applyingNetwork_||source.networkOwner()!=localPeer_)return;
        Writer w;w.u32(source.networkNid()).u32(target.networkNid()).f64(effect.poison).f64(effect.track).f64(effect.reveal).f64(effect.shield).i32(effect.effectTeam);writeVec(w,effect.velocityImpulse);
        queueReliable(PacketKind::ActorEffect,w.take());
    });
    match_.world().setNetworkBeaconUseForwarder([this](auto& consumer,std::uint32_t ownerNid,std::uint64_t id){
        if(applyingNetwork_||consumer.networkOwner()!=localPeer_)return;
        Writer w;w.u32(consumer.networkNid()).u32(ownerNid).u64(id);queueReliable(PacketKind::BeaconUse,w.take());
    });
    for(const auto& a:match_.actors())a->setNetworkPoseTriggerSink([this,actor=a.get()](const auto& event){
        if(actor->networkOwner()==localPeer_){Writer w;w.u32(actor->networkNid()).string(event.name,64).boolean(event.amount.has_value());if(event.amount)w.f64(*event.amount);writeVec(w,event.direction);w.boolean(event.hasDirection).i32(event.hand).i32(event.side).f64(event.duration);queueReliable(PacketKind::Trigger,w.take());}
    });
    if(auto* boss=match_.boss()){
        boss->setHitForwarder([this](auto& attacker,double damage,const auto& hit,const auto& weapon){sendBossHit(attacker,damage,hit,weapon);});
        boss->setSplashForwarder([this](auto& attacker,auto center,double radius,double maxDamage,double minDamage,const auto& weapon){sendBossSplash(attacker,center,radius,maxDamage,minDamage,weapon);});
    }
}

MatchSync::~MatchSync(){
    paint_.setPaintJournalSink({});paint_.setCellsJournalSink({});
    paint_.setNetworkActor(previousPaintActor_);paint_.setNetworkApplying(false);
    match_.world().setNetworkVisualForwarder({});match_.world().setNetworkHitForwarder({});match_.world().setNetworkDeviceHitForwarder({});
    match_.world().setNetworkActorEffectForwarder({});match_.world().setNetworkBeaconUseForwarder({});
    for(const auto& a:match_.actors())a->setNetworkPoseTriggerSink({});
    if(auto* boss=match_.boss()){boss->setHitForwarder({});boss->setSplashForwarder({});}
}

void MatchSync::Tick(double dt){if(session_.view().phase==RoomPhase::Match)match_.tick(dt);}

void MatchSync::Update(double now,double frameDt){
    if(!std::isfinite(now)||!std::isfinite(frameDt)||frameDt<0)throw std::invalid_argument("Invalid MatchSync clock");
    const auto& view=session_.view();
    if(view.matchEpoch!=epoch_)return;
    if(view.hostId&&view.hostId!=authorityPeer_){
        authorityPeer_=view.hostId;
        const auto currentRoster=session_.startRoster();
        match_.setNetworkAuthorityPeer(authorityPeer_,currentRoster);
        if(match_.networkAuthority()){
            if(match_.boss()&&latestBossState_)match_.boss()->adoptAuthorityState(*latestBossState_);
            if(match_.zones()&&latestZoneState_)const_cast<ZoneControl*>(match_.zones())->adoptAuthorityState(*latestZoneState_);
            match_.updateNetworkBossPresentation();
        }
    }
    const auto roster=session_.startRoster();if(roster.size()<=8){
        for(const auto& previous:lastRoster_){
            const auto current=std::find_if(roster.begin(),roster.end(),[&](const auto& entry){return entry.nid==previous.nid;});
            if(current==roster.end()||current->owner!=previous.owner){
                samples_.erase(previous.nid);
                match_.world().updateRemoteWorld(previous.owner,{},{},{});
            }
        }
        match_.applyNetworkRoster(roster);lastRoster_=roster;
    }
    for(const auto& packet:session_.TakeGamePackets())receive(packet,now);
    interpolateGhosts(now,frameDt);
    if(view.phase==RoomPhase::Match){
        if(now>=nextSnapshot_){flushReliableEvents();sendOwnedSnapshots(now);if(match_.networkAuthority())sendBossState();nextSnapshot_=now+kSnapshotPeriod;}
        if(match_.networkAuthority()&&now>=nextAuthority_){sendAuthorityState();nextAuthority_=now+kAuthorityPeriod;}
    }
}

void MatchSync::send(PacketKind kind,std::vector<std::uint8_t> body,bool reliable){
    if(!failure_.empty())return;
    if(body.size()>kMaxLogicalBytes){failure_="遊戲同步資料過大，請返回房間後重試";return;}
    const auto sequence=++sendSequence_;
    const auto writeHeader=[&](Writer& w,PacketKind type,std::uint64_t seq){w.u32(kMagic).u16(WireVersion).u8(static_cast<std::uint8_t>(type)).u64(epoch_).u64(seq);};
    if(body.size()<=kMaxBodyChunk){Writer w;writeHeader(w,kind,sequence);for(auto b:body)w.u8(b);if(!session_.SendGame(w.take(),0,reliable))failure_="無法傳送遊戲同步資料，請重新加入房間";return;}
    const auto count=(body.size()+kMaxBodyChunk-1)/kMaxBodyChunk;if(count>kMaxChunks){failure_="遊戲同步資料分段過多，請返回房間後重試";return;}
    for(std::size_t i=0;i<count;++i){
        const auto begin=i*kMaxBodyChunk;const auto size=std::min(kMaxBodyChunk,body.size()-begin);
        Writer w;writeHeader(w,PacketKind::Fragment,sequence);w.u8(static_cast<std::uint8_t>(kind)).u16(static_cast<std::uint16_t>(i)).u16(static_cast<std::uint16_t>(count)).u32(static_cast<std::uint32_t>(body.size())).bytes(body.data()+begin,size,kMaxBodyChunk);
        if(!session_.SendGame(w.take(),0,reliable)){failure_="遊戲同步傳送失敗，請重新加入房間";return;}
    }
}

void MatchSync::queueReliable(PacketKind kind,std::vector<std::uint8_t> body){
    if(!failure_.empty())return;
    constexpr std::size_t entryOverhead=5;
    if(reliableEvents_.size()>=1024||body.size()>kMaxReliableEventBytes-entryOverhead||
       reliableEventBytes_>kMaxReliableEventBytes-entryOverhead-body.size()){
        failure_="遊戲事件佇列已滿，同步已停止，請重新加入房間";
        return;
    }
    reliableEventBytes_+=entryOverhead+body.size();
    reliableEvents_.push_back({kind,std::move(body)});
}

void MatchSync::flushReliableEvents(){
    if(reliableEvents_.empty()||!failure_.empty())return;
    Writer w;w.u16(static_cast<std::uint16_t>(reliableEvents_.size()));
    try{
        for(const auto& event:reliableEvents_)w.u8(static_cast<std::uint8_t>(event.kind)).bytes(event.body,kMaxReliableEventBytes);
    }catch(const WireError&){failure_="遊戲事件資料過大，請返回房間後重試";reliableEvents_.clear();reliableEventBytes_=0;return;}
    reliableEvents_.clear();reliableEventBytes_=0;
    send(PacketKind::EventBatch,w.take(),true);
}

void MatchSync::sendOwnedSnapshots(double now){
    std::vector<std::size_t> owned;
    for(std::size_t i=0;i<match_.roster().size();++i)if(match_.roster()[i].active&&match_.roster()[i].owner==localPeer_)owned.push_back(i);
    if(owned.empty())return;
    Writer w;w.f64(now).u8(static_cast<std::uint8_t>(owned.size()));
    for(const auto index:owned){
        const auto& roster=match_.roster()[index];const auto& actor=*match_.actors()[index];
        w.u32(roster.nid).boolean(roster.active);writeState(w,actor.state);writeInput(w,actor.input);writeCombat(w,actor.combatPose());writeSpecial(w,actor.specialPose());
        w.f64(actor.turfPainted()).f64(actor.zoneTurf()).i32(actor.deaths()).i32(actor.splats()).i32(actor.shotsFired());
        const auto& jump=actor.superJumpState();w.boolean(jump.has_value());
        if(jump){w.boolean(jump->flight).boolean(jump->home).f64(jump->elapsed).f64(jump->duration);writeVec(w,jump->from);writeVec(w,jump->to);writeVec(w,jump->target);w.u32(jump->actorTarget?jump->actorTarget->networkNid():kUnattributedNid).u64(jump->beaconId);}
    }
    auto projectiles=std::vector<const ShooterProjectile*>{};
    for(const auto& p:match_.world().projectiles())if(p.owner&&p.owner->networkOwner()==localPeer_)projectiles.push_back(&p);
    if(projectiles.size()>24)projectiles.erase(projectiles.begin(),projectiles.end()-24);
    w.u16(static_cast<std::uint16_t>(projectiles.size()));
    for(const auto* p:projectiles){
        w.u64(p->id).u32(p->owner->networkNid());writeVec(w,p->pos);writeVec(w,p->vel);w.f64(p->age).string(p->kind,32).string(p->weaponId,32).f64(p->size).f64(p->trail).f64(p->radius).f64(p->damage).boolean(p->special).string(p->state,32);writeVec(w,p->start);writeVec(w,p->hitPoint);writeVec(w,p->hitNormal);writeVec(w,p->flightDir);w.f64(p->look.size).f64(p->look.tail0).f64(p->look.tailK).f64(p->look.wobble).f64(p->look.frequency).f64(p->look.nose).i32(p->look.satellites);
    }
    std::vector<const CombatDevice*> devices;
    for(const auto& d:match_.world().devices())if(d.owner&&d.owner->networkOwner()==localPeer_)devices.push_back(&d);
    if(devices.size()>12)devices.erase(devices.begin(),devices.end()-12);
    w.u16(static_cast<std::uint16_t>(devices.size()));
    for(const auto* d:devices){
        w.u64(d->id).u32(d->owner->networkNid()).string(d->kind,32).i32(d->team);writeVec(w,d->pos);writeVec(w,d->vel);writeVec(w,d->normal);w.f64(d->hp).f64(d->radius).f64(d->age).f64(d->timer).boolean(d->shield).boolean(d->grounded).boolean(d->live).boolean(d->held).boolean(d->blocksActors).boolean(d->shootable).boolean(d->locked).boolean(d->exploding).string(d->state,32);writeVec(w,d->C);writeVec(w,d->N);w.f64(d->R).f64(d->left).f64(d->planeOff).f64(d->halfW).f64(d->speed).f64(d->hpMax).i32(d->uses).boolean(d->special).boolean(d->hasPreviousShield);writeVec(w,d->previousC);writeVec(w,d->previousN);
        w.f64(d->heading).f64(d->fuse).f64(d->pulse).f64(d->pulsePeriod).f64(d->spin).f64(d->trigger);
        const auto points=std::min<std::size_t>(64,d->trailPoints.size());w.u8(static_cast<std::uint8_t>(points));
        for(auto i=d->trailPoints.size()-points;i<d->trailPoints.size();++i){writeVec(w,d->trailPoints[i].first);w.f64(std::max(0.0,match_.world().time()-d->trailPoints[i].second));}
    }
    std::vector<WorldObjectView> objects;
    for(const auto& object:match_.world().visualObjects())if(ownsActor(localPeer_,object.ownerNid))objects.push_back(object);
    if(objects.size()>12)objects.erase(objects.begin(),objects.end()-12);
    w.u16(static_cast<std::uint16_t>(objects.size()));
    for(const auto& object:objects){
        w.u32(object.ownerNid).u64(object.id).string(object.kind,32).string(object.phase,32).i32(object.team);writeVec(w,object.pos);writeVec(w,object.previous);writeVec(w,object.target);writeVec(w,object.mouth);writeVec(w,object.vel);writeVec(w,object.direction);w.f64(object.age).f64(object.timer).f64(object.radius).f64(object.range).f64(object.charge).boolean(object.held);
    }
    send(PacketKind::ActorSnapshot,w.take(),false);
}

void MatchSync::sendAuthorityState(){
    Writer matchState;matchState.u8(1).u8(static_cast<std::uint8_t>(match_.phase())).f64(match_.remaining());writeActorResult(matchState,match_.result());send(PacketKind::MatchState,matchState.take(),true);
    if(const auto* zones=match_.zones()){Writer w;w.u8(3).bytes(EncodeZonesAuthorityState(const_cast<ZoneControl*>(zones)->captureAuthorityState()));send(PacketKind::ZoneState,w.take(),true);}
}
void MatchSync::sendBossState(){
    if(auto* boss=match_.boss()){Writer w;w.u8(2).bytes(EncodeBossAuthorityState(boss->captureAuthorityState()));send(PacketKind::BossState,w.take(),true);}
}

void MatchSync::sendPaint(const PaintJournalEvent& e){
    if(!match_.networked()||applyingNetwork_||session_.view().phase!=RoomPhase::Match)return;
    if(e.actorNid==kUnattributedNid&&!match_.networkAuthority())return;
    Writer w;w.u64(++paintSequence_).u32(e.actorNid);writeVec(w,e.center);writeVec(w,e.stretch);w.f64(e.radius).f64(e.seed).f64(e.stretchAmt).i32(e.team).u8(static_cast<std::uint8_t>(e.kind)).boolean(e.hasStretch).boolean(e.cosmetic);queueReliable(PacketKind::Paint,w.take());
}
void MatchSync::sendCells(const std::vector<int>& cells,int team){
    if(!match_.networkAuthority()||applyingNetwork_||session_.view().phase!=RoomPhase::Match)return;
    if(cells.size()>(kMaxReliableEventBytes-32)/sizeof(std::int32_t)){failure_="區域塗地資料過大，請返回房間後重試";return;}
    Writer w;w.u64(++paintSequence_).i32(team).u32(static_cast<std::uint32_t>(cells.size()));for(const int cell:cells)w.i32(cell);queueReliable(PacketKind::Cells,w.take());
}
void MatchSync::sendVisual(const WorldVisualEvent& e){
    if(applyingNetwork_||session_.view().phase!=RoomPhase::Match)return;
    Writer w;w.string(e.kind,64);writeVec(w,e.pos);writeVec(w,e.normal);w.f64(e.radius).i32(e.team).f64(e.charge).f64(e.width).f64(e.seed).string(e.source,64).u64(e.id).boolean(e.sourceLocal).boolean(e.sourceVictimLocal).u32(e.victimNid);queueReliable(PacketKind::Visual,w.take());
}
void MatchSync::sendHit(PlayerSimulation& attacker,PlayerSimulation& victim,double damage,const std::string& source){
    if(applyingNetwork_||attacker.networkOwner()!=localPeer_||victim.networkOwner()==localPeer_)return;
    Writer w;w.u64(++hitSequence_).u32(attacker.networkNid()).u32(victim.networkNid()).f64(damage).string(source,64);queueReliable(PacketKind::Hit,w.take());
}
void MatchSync::sendDeviceHit(std::uint32_t ownerNid,std::uint64_t id,double damage,int team){
    if(applyingNetwork_)return;const auto local=std::find_if(match_.roster().begin(),match_.roster().end(),[&](const auto& e){return e.active&&e.owner==localPeer_&&e.team==team;});if(local==match_.roster().end())return;
    Writer w;w.u32(local->nid).u32(ownerNid).u64(id).f64(damage).i32(team);queueReliable(PacketKind::DeviceHit,w.take());
}
void MatchSync::sendBossHit(PlayerSimulation& attacker,double damage,const BossSegmentHit& hit,const std::string& weapon){
    if(attacker.networkOwner()!=localPeer_||match_.networkAuthority())return;Writer w;w.u32(attacker.networkNid()).f64(damage).u32(static_cast<std::uint32_t>(hit.shape+1)).u32(static_cast<std::uint32_t>(hit.crablet+1)).boolean(hit.weak).f64(hit.t).f64(hit.distance);writeVec(w,hit.point);w.string(weapon,64);queueReliable(PacketKind::BossHit,w.take());
}
void MatchSync::sendBossSplash(PlayerSimulation& attacker,glm::dvec3 center,double radius,double maxDamage,double minDamage,const std::string& weapon){
    if(attacker.networkOwner()!=localPeer_||match_.networkAuthority())return;Writer w;w.u32(attacker.networkNid());writeVec(w,center);w.f64(radius).f64(maxDamage).f64(minDamage).string(weapon,64);queueReliable(PacketKind::BossSplash,w.take());
}

PlayerSimulation* MatchSync::actor(std::uint32_t nid)const{for(const auto& a:match_.actors())if(a->networkNid()==nid)return a.get();return nullptr;}
const MatchRosterEntry* MatchSync::rosterEntry(std::uint32_t nid)const{for(const auto& e:match_.roster())if(e.nid==nid)return &e;return nullptr;}
bool MatchSync::ownsActor(MemberId owner,std::uint32_t nid)const{const auto* e=rosterEntry(nid);return e&&e->active&&e->owner==owner;}

void MatchSync::receive(const GamePacket& packet,double now){
    if(packet.from==0||packet.from==localPeer_||packet.payload.size()>MaxWirePacketBytes)return;
    try{
        Reader r(packet.payload);if(r.u32()!=kMagic||r.u16()!=WireVersion)return;const auto rawKind=r.u8();const auto epoch=r.u64();const auto seq=r.u64();if(epoch!=epoch_)return;
        const auto kind=static_cast<PacketKind>(rawKind);
        if(kind==PacketKind::Fragment){applyChunk(packet,r,now,seq);return;}
        if(kind==PacketKind::ActorSnapshot){if(seq<=lastSequence_[packet.from])return;applySnapshot(packet,r,now);r.finish();lastSequence_[packet.from]=seq;}
        else {if(seq<=lastReliableSequence_[packet.from])return;
            if(kind==PacketKind::EventBatch){
                const auto count=r.u16();if(count>1024)throw WireError("too many batched gameplay events");
                std::vector<std::pair<PacketKind,std::vector<std::uint8_t>>> events;events.reserve(count);
                for(std::uint16_t i=0;i<count;++i){const auto eventKind=static_cast<PacketKind>(r.u8());events.emplace_back(eventKind,r.bytes(kMaxReliableEventBytes));}
                r.finish();
                for(const auto& [eventKind,bytes]:events){
                    try{Reader eventReader(bytes);applyReliable(packet,eventKind,eventReader);eventReader.finish();}
                    catch(const WireError&){}
                    catch(const std::out_of_range&){}
                }
            }else applyReliable(packet,kind,r);
            r.finish();lastReliableSequence_[packet.from]=seq;
        }
        r.finish();
    }catch(const WireError&){return;}catch(const std::out_of_range&){return;}
}

void MatchSync::applyReliable(const GamePacket& packet,PacketKind kind,Reader& r){
    switch(kind){
        case PacketKind::MatchState:case PacketKind::BossState:case PacketKind::ZoneState:applyAuthorityState(packet,r);break;
        case PacketKind::Paint:applyPaint(packet,r);break;
        case PacketKind::Cells:applyCells(packet,r);break;
        case PacketKind::Visual:applyVisual(packet,r);break;
        case PacketKind::Hit:applyHit(packet,r);break;
        case PacketKind::DeviceHit:applyDeviceHit(packet,r);break;
        case PacketKind::BossHit:applyBossHit(packet,r);break;
        case PacketKind::BossSplash:applyBossSplash(packet,r);break;
        case PacketKind::Trigger:applyTrigger(packet,r);break;
        case PacketKind::ActorEffect:applyActorEffect(packet,r);break;
        case PacketKind::BeaconUse:applyBeaconUse(packet,r);break;
        default:break;
    }
}

void MatchSync::applyChunk(const GamePacket& packet,Reader& r,double now,std::uint64_t sequence){
    const auto kind=static_cast<PacketKind>(r.u8());const auto index=r.u16(),count=r.u16();const auto total=r.u32();auto chunk=r.bytes(kMaxBodyChunk);
    r.finish();
    if(kind==PacketKind::Fragment||count==0||count>kMaxChunks||index>=count||total==0||total>kMaxLogicalBytes||chunk.empty())return;
    const auto key=std::make_pair(packet.from,sequence);
    for(auto it=assemblies_.begin();it!=assemblies_.end();){if(now-it->second.started>3.0)it=assemblies_.erase(it);else ++it;}
    auto found=assemblies_.find(key);
    if(found==assemblies_.end()){
        if(assemblies_.size()>=32)assemblies_.erase(assemblies_.begin());
        PendingAssembly assembly;assembly.sequence=sequence;assembly.kind=kind;assembly.count=count;assembly.total=total;assembly.started=now;assembly.chunks.resize(count);
        found=assemblies_.emplace(key,std::move(assembly)).first;
    }
    auto& assembly=found->second;
    if(assembly.kind!=kind||assembly.count!=count||assembly.total!=total){assemblies_.erase(found);return;}
    auto& slot=assembly.chunks[index];
    if(slot){if(*slot!=chunk)assemblies_.erase(found);return;}
    if(assembly.received>assembly.total||chunk.size()>assembly.total-assembly.received){assemblies_.erase(found);return;}
    assembly.received+=chunk.size();slot=std::move(chunk);
    if(std::any_of(assembly.chunks.begin(),assembly.chunks.end(),[](const auto& value){return !value.has_value();}))return;
    if(assembly.received!=assembly.total){assemblies_.erase(found);return;}
    std::vector<std::uint8_t> body;body.reserve(assembly.total);for(const auto& part:assembly.chunks)body.insert(body.end(),part->begin(),part->end());
    assemblies_.erase(found);
    Writer completed;completed.u32(kMagic).u16(WireVersion).u8(static_cast<std::uint8_t>(kind)).u64(epoch_).u64(sequence);
    for(const auto byte:body)completed.u8(byte);
    receive(GamePacket{packet.from,completed.take()},now);
}

void MatchSync::applySnapshot(const GamePacket& packet,Reader& r,double now){
    const double remoteTime=r.f64();if(remoteTime<0)throw WireError("invalid snapshot clock");
    const auto offsetIt=senderClockOffset_.find(packet.from);
    double clockOffset=offsetIt==senderClockOffset_.end()?now-remoteTime:offsetIt->second;
    if(offsetIt!=senderClockOffset_.end()){
        const double error=(now-remoteTime)-clockOffset;
        clockOffset+=std::clamp(error*0.02,-0.01,0.01);
    }
    const auto count=r.u8();if(count>8)throw WireError("too many actor snapshots");
    std::vector<std::pair<std::uint32_t,ActorSample>> actorSamples;actorSamples.reserve(count);
    for(std::uint8_t i=0;i<count;++i){
        const auto nid=r.u32();ActorSample sample;sample.remoteTime=remoteTime;sample.received=remoteTime+clockOffset;sample.active=r.boolean();sample.state=readState(r);sample.input=readInput(r);sample.combat=readCombat(r);sample.special=readSpecial(r);
        sample.turf=r.f64();sample.zoneTurf=r.f64();sample.deaths=r.i32();sample.splats=r.i32();sample.shots=r.i32();
        if(r.boolean()){
            SuperJumpState jump;jump.flight=r.boolean();jump.home=r.boolean();jump.elapsed=r.f64();jump.duration=r.f64();jump.from=readVec(r);jump.to=readVec(r);jump.target=readVec(r);
            const auto target=r.u32();jump.beaconId=r.u64();
            if(jump.elapsed<0||jump.elapsed>30||jump.duration<0||jump.duration>30||(target!=kUnattributedNid&&!actor(target)))throw WireError("invalid super jump snapshot");
            jump.actorTarget=target==kUnattributedNid?nullptr:actor(target);sample.jump=jump;
        }
        if(sample.turf<0||sample.turf>1e9||sample.zoneTurf<0||sample.zoneTurf>1e9||sample.deaths<0||sample.splats<0||sample.shots<0)throw WireError("invalid actor statistics");
        if(ownsActor(packet.from,nid))actorSamples.emplace_back(nid,std::move(sample));
    }
    std::vector<ShooterProjectile> projectiles;const auto pc=r.u16();if(pc>192)throw WireError("too many projectile ghosts");projectiles.reserve(pc);
    for(std::uint16_t i=0;i<pc;++i){ShooterProjectile p;p.id=r.u64();const auto ownerNid=r.u32();p.owner=actor(ownerNid);if(!ownsActor(packet.from,ownerNid))throw WireError("forged projectile owner");p.pos=readVec(r);p.vel=readVec(r);p.age=r.f64();p.kind=r.string(32);p.weaponId=r.string(32);p.size=r.f64();p.trail=r.f64();p.radius=r.f64();p.damage=r.f64();p.special=r.boolean();p.state=r.string(32);p.start=readVec(r);p.hitPoint=readVec(r);p.hitNormal=readVec(r);p.flightDir=readVec(r);p.look.size=r.f64();p.look.tail0=r.f64();p.look.tailK=r.f64();p.look.wobble=r.f64();p.look.frequency=r.f64();p.look.nose=r.f64();p.look.satellites=r.i32();p.networkRemote=true;p.networkOwner=packet.from;projectiles.push_back(std::move(p));}
    std::vector<CombatDevice> devices;const auto dc=r.u16();if(dc>96)throw WireError("too many device ghosts");devices.reserve(dc);
    for(std::uint16_t i=0;i<dc;++i){CombatDevice d;d.id=r.u64();const auto ownerNid=r.u32();d.owner=actor(ownerNid);if(!ownsActor(packet.from,ownerNid))throw WireError("forged device owner");d.kind=r.string(32);d.team=r.i32();d.pos=readVec(r);d.vel=readVec(r);d.normal=readVec(r);d.hp=r.f64();d.radius=r.f64();d.age=r.f64();d.timer=r.f64();d.shield=r.boolean();d.grounded=r.boolean();d.live=r.boolean();d.held=r.boolean();d.blocksActors=r.boolean();d.shootable=r.boolean();d.locked=r.boolean();d.exploding=r.boolean();d.state=r.string(32);d.C=readVec(r);d.N=readVec(r);d.R=r.f64();d.left=r.f64();d.planeOff=r.f64();d.halfW=r.f64();d.speed=r.f64();d.hpMax=r.f64();d.uses=r.i32();d.special=r.boolean();if(d.uses<0||d.uses>100)throw WireError("invalid beacon uses");d.hasPreviousShield=r.boolean();d.previousC=readVec(r);d.previousN=readVec(r);d.heading=r.f64();d.fuse=r.f64();d.pulse=r.f64();d.pulsePeriod=r.f64();d.spin=r.f64();d.trigger=r.f64();const auto points=r.u8();if(points>64)throw WireError("too many tracer trail points");for(std::uint8_t point=0;point<points;++point){const auto pos=readVec(r);const double age=r.f64();if(age<0||age>600)throw WireError("invalid tracer trail age");d.trailPoints.emplace_back(pos,match_.world().time()-age);}d.networkRemote=true;d.networkOwner=packet.from;devices.push_back(std::move(d));}
    std::vector<WorldObjectView> objects;const auto oc=r.u16();if(oc>96)throw WireError("too many special ghosts");objects.reserve(oc);
    for(std::uint16_t i=0;i<oc;++i){WorldObjectView o;o.ownerNid=r.u32();if(!ownsActor(packet.from,o.ownerNid))throw WireError("forged special owner");o.id=r.u64();o.kind=r.string(32);o.phase=r.string(32);o.team=r.i32();o.pos=readVec(r);o.previous=readVec(r);o.target=readVec(r);o.mouth=readVec(r);o.vel=readVec(r);o.direction=readVec(r);o.age=r.f64();o.timer=r.f64();o.radius=r.f64();o.range=r.f64();o.charge=r.f64();o.held=r.boolean();objects.push_back(std::move(o));}
    r.finish();
    senderClockOffset_[packet.from]=clockOffset;
    for(auto& [nid,sample]:actorSamples)applyActorSample(nid,std::move(sample),now);
    match_.world().updateRemoteWorld(packet.from,std::move(projectiles),std::move(devices),std::move(objects));
    if(lastSnapshotReceive_>0){const auto interval=now-lastSnapshotReceive_;if(interval>0.12)jitterBuffer_=std::min(0.30,jitterBuffer_+0.02);else jitterBuffer_=std::max(0.085,jitterBuffer_-0.002);}lastSnapshotReceive_=now;
}

void MatchSync::applyActorSample(std::uint32_t nid,ActorSample sample,double){auto& history=samples_[nid];if(!history.empty()&&sample.received<=history.back().received)return;history.push_back(std::move(sample));while(history.size()>12)history.pop_front();}

void MatchSync::interpolateGhosts(double now,double){
    for(auto& [nid,history]:samples_){
        const auto* roster=rosterEntry(nid);auto* target=actor(nid);if(!roster||!target||!roster->active||roster->owner==localPeer_||history.empty())continue;
        const double sampleTime=now-jitterBuffer_;const ActorSample* a=&history.front();const ActorSample* b=a;
        for(std::size_t i=1;i<history.size();++i){if(history[i].received>=sampleTime){b=&history[i];a=&history[i-1];break;}a=b=&history[i];}
        PlayerState state=a->state;PlayerInput input=a->input;const double span=b->received-a->received;
        if(a!=b&&span>1e-4){const double t=std::clamp((sampleTime-a->received)/span,0.0,1.0);if(glm::length(b->state.pos-a->state.pos)>8.0)state=t<.5?a->state:b->state;else state.pos=hermite(a->state.pos,a->state.vel,b->state.pos,b->state.vel,t,span);state.vel=glm::mix(a->state.vel,b->state.vel,t);state.hp=glm::mix(a->state.hp,b->state.hp,t);state.ink=glm::mix(a->state.ink,b->state.ink,t);state.specialCharge=glm::mix(a->state.specialCharge,b->state.specialCharge,t);state.hurtFlash=glm::mix(a->state.hurtFlash,b->state.hurtFlash,t);state.yaw=a->state.yaw+std::remainder(b->state.yaw-a->state.yaw,6.283185307179586)*t;input=t<.5?a->input:b->input;}
        else if(sampleTime>b->received){const double dt=std::min(0.18,sampleTime-b->received);state=b->state;state.pos=b->state.pos+b->state.vel*dt;input=b->input;}
        else {state=b->state;input=b->input;}
        target->state=state;target->input=input;target->setNetworkGhostPose(b->combat,b->special);
        target->setNetworkSuperJump(b->jump);
        target->applyNetworkStats(b->turf,b->zoneTurf,b->deaths,b->splats,b->shots);
    }
}

void MatchSync::applyAuthorityState(const GamePacket& packet,Reader& r){
    if(packet.from!=authorityPeer_){r.finish();return;}
    // The packet kind is recovered from the framed payload by the caller. A small
    // discriminator is prefixed to all state-body messages below.
    const auto subtype=r.u8();
    if(subtype==1){const auto phase=r.u8();if(phase>static_cast<std::uint8_t>(MatchPhase::Results))throw WireError("invalid match phase");const auto remaining=r.f64();auto result=readActorResult(r);r.finish();match_.applyNetworkState(static_cast<MatchPhase>(phase),remaining,std::move(result));return;}
    const auto bytes=r.bytes(MaxWirePacketBytes);
    r.finish();
    if(subtype==2){BossAuthorityState next;if(!DecodeBossAuthorityState(bytes,next))throw WireError("invalid Boss authority snapshot");if(auto* boss=match_.boss()){if(!match_.networkAuthority()&&!boss->applyAuthorityState(next))return;match_.updateNetworkBossPresentation();}latestBossState_=std::move(next);return;}
    if(subtype==3){ZoneAuthorityState next;if(!DecodeZonesAuthorityState(bytes,next))throw WireError("invalid Zones authority snapshot");if(const auto* zones=match_.zones()){if(!const_cast<ZoneControl*>(zones)->applyAuthorityState(next))return;}latestZoneState_=std::move(next);return;}
    throw WireError("invalid authority state subtype");
}

void MatchSync::applyHit(const GamePacket& packet,Reader& r){
    const auto seq=r.u64();const auto attackerNid=r.u32(),victimNid=r.u32();const double damage=r.f64();const auto source=r.string(64);auto* attacker=actor(attackerNid);auto* victim=actor(victimNid);
    r.finish();
    if(seq<=lastAppliedHit_[packet.from]||!ownsActor(packet.from,attackerNid)||!victim||!rosterEntry(victimNid)||rosterEntry(victimNid)->owner!=localPeer_||!rosterEntry(victimNid)->active||!attacker||attacker->team()==victim->team()||damage<=0||damage>1000)return;
    lastAppliedHit_[packet.from]=seq;victim->damageFromNetwork(damage,attacker,source);
}
void MatchSync::applyDeviceHit(const GamePacket& packet,Reader& r){
    const auto attackerNid=r.u32(),ownerNid=r.u32();const auto id=r.u64();const double damage=r.f64();const int team=r.i32();
    r.finish();
    const auto* attacker=actor(attackerNid);const auto* owner=actor(ownerNid);
    if(!ownsActor(packet.from,attackerNid)||!attacker||!owner||!ownsActor(localPeer_,ownerNid)||
       attacker->team()!=team||owner->team()==team||damage<=0||damage>1000)return;
    match_.world().applyNetworkDeviceHit(ownerNid,id,damage,team);
}
void MatchSync::applyActorEffect(const GamePacket& packet,Reader& r){
    const auto sourceNid=r.u32(),targetNid=r.u32();NetworkActorEffect effect;
    effect.poison=r.f64();effect.track=r.f64();effect.reveal=r.f64();effect.shield=r.f64();effect.effectTeam=r.i32();effect.velocityImpulse=readVec(r);r.finish();
    if(!ownsActor(packet.from,sourceNid)||!ownsActor(localPeer_,targetNid))return;
    for(const double value:{effect.poison,effect.track,effect.reveal,effect.shield})if(value<0||value>600)return;
    auto* source=actor(sourceNid);auto* target=actor(targetNid);
    if(!source||!target||effect.effectTeam< -1||effect.effectTeam>1||glm::length(effect.velocityImpulse)>100)return;
    if(effect.shield>0&&source->team()!=target->team())return;
    if((effect.poison>0||effect.track>0||effect.reveal>0)&&source->team()==target->team())return;
    if((effect.track>0||effect.reveal>0)&&effect.effectTeam!=source->team())return;
    match_.world().applyNetworkActorEffect(*source,*target,effect);
}
void MatchSync::applyBeaconUse(const GamePacket& packet,Reader& r){
    const auto consumerNid=r.u32(),ownerNid=r.u32();const auto id=r.u64();r.finish();
    auto* consumer=actor(consumerNid);
    if(!consumer||!ownsActor(packet.from,consumerNid)||!ownsActor(localPeer_,ownerNid))return;
    match_.world().applyNetworkBeaconUse(*consumer,ownerNid,id);
}
void MatchSync::applyBossHit(const GamePacket& packet,Reader& r){
    const auto nid=r.u32();const double damage=r.f64();BossSegmentHit hit;hit.shape=static_cast<int>(r.u32())-1;hit.crablet=static_cast<int>(r.u32())-1;hit.weak=r.boolean();hit.t=r.f64();hit.distance=r.f64();hit.point=readVec(r);const auto weapon=r.string(64);auto* a=actor(nid);auto* boss=match_.boss();
    r.finish();
    if(!match_.networkAuthority()||!ownsActor(packet.from,nid)||!a||!boss||a->team()!=0||damage<=0||damage>1000||
       hit.shape< -1||hit.shape>9||hit.crablet< -1||hit.crablet>=static_cast<int>(boss->crablets().size())||
       hit.t<0||hit.t>1||hit.distance<0||hit.distance>100)return;
    boss->hit(*a,damage,hit,weapon);
}
void MatchSync::applyBossSplash(const GamePacket& packet,Reader& r){
    const auto nid=r.u32();const auto center=readVec(r);const double radius=r.f64(),maxDamage=r.f64(),minDamage=r.f64();const auto weapon=r.string(64);auto* a=actor(nid);auto* boss=match_.boss();
    r.finish();
    if(!match_.networkAuthority()||!ownsActor(packet.from,nid)||!a||!boss||a->team()!=0||radius<=0||radius>100||maxDamage<0||maxDamage>1000||minDamage<0||minDamage>maxDamage)return;
    boss->splash(*a,center,radius,maxDamage,minDamage,weapon);
}
void MatchSync::applyPaint(const GamePacket& packet,Reader& r){
    const auto seq=r.u64();const auto nid=r.u32();PaintJournalEvent e;e.actorNid=nid;e.center=readVec(r);e.stretch=readVec(r);e.radius=r.f64();e.seed=r.f64();e.stretchAmt=r.f64();e.team=r.i32();const auto kind=r.u8();e.hasStretch=r.boolean();e.cosmetic=r.boolean();
    r.finish();
    if(seq<=lastAppliedPaint_[packet.from]||kind>=8||e.radius<0||e.radius>100||e.stretchAmt<0||e.stretchAmt>100||e.team<0||e.team>1)return;
    if(nid==kUnattributedNid){if(packet.from!=authorityPeer_)return;}else if(!ownsActor(packet.from,nid))return;
    lastAppliedPaint_[packet.from]=seq;
    struct ApplyGuard { bool& applying; FacePaint& paint; std::uint32_t previous; ~ApplyGuard(){paint.setNetworkActor(previous);paint.setNetworkApplying(false);applying=false;} } guard{applyingNetwork_,paint_,paint_.networkActor()};
    applyingNetwork_=true;paint_.setNetworkApplying(true);paint_.setNetworkActor(nid);
    SplatOptions options;options.stretch=e.stretch;options.stretchAmt=e.stretchAmt;options.hasStretch=e.hasStretch;options.cosmetic=e.cosmetic;options.kind=static_cast<SplatKind>(kind);
    paint_.splat(e.center,e.radius,e.team,e.seed,options);
}
void MatchSync::applyCells(const GamePacket& packet,Reader& r){
    if(packet.from!=authorityPeer_)return;const auto seq=r.u64();const int team=r.i32();const auto count=r.u32();if(seq<=lastAppliedPaint_[packet.from]||count>(kMaxReliableEventBytes-32)/sizeof(std::int32_t)||team< -1||team>1)return;
    std::vector<int> cells;cells.reserve(count);for(std::uint32_t i=0;i<count;++i)cells.push_back(r.i32());r.finish();
    if(std::any_of(cells.begin(),cells.end(),[&](int cell){return cell<0||static_cast<std::size_t>(cell)>=paint_.grid().size();}))return;
    lastAppliedPaint_[packet.from]=seq;
    struct ApplyGuard { bool& applying; FacePaint& paint; ~ApplyGuard(){paint.setNetworkApplying(false);applying=false;} } guard{applyingNetwork_,paint_};
    applyingNetwork_=true;paint_.setNetworkApplying(true);paint_.setCells(cells,team);
}
void MatchSync::applyTrigger(const GamePacket& packet,Reader& r){
    const auto nid=r.u32();CharacterTrigger event;event.name=r.string(64);if(r.boolean())event.amount=r.f64();event.direction=readVec(r);event.hasDirection=r.boolean();event.hand=r.i32();event.side=r.i32();event.duration=r.f64();
    r.finish();
    auto* target=actor(nid);
    if(!ownsActor(packet.from,nid)||!target||event.name.empty()||event.hand< -2||event.hand>2||event.side< -1||event.side>1||event.duration<0||event.duration>10||
       (event.amount&&(*event.amount< -1000||*event.amount>1000))||glm::length(event.direction)>100)return;
    target->applyNetworkPoseTrigger(event);
}
void MatchSync::applyVisual(const GamePacket&,Reader& r){WorldVisualEvent e;e.kind=r.string(64);e.pos=readVec(r);e.normal=readVec(r);e.radius=r.f64();e.team=r.i32();e.charge=r.f64();e.width=r.f64();e.seed=r.f64();e.source=r.string(64);e.id=r.u64();e.sourceLocal=r.boolean();e.sourceVictimLocal=r.boolean();e.victimNid=r.u32();r.finish();if(e.team< -1||e.team>1||e.radius<0||e.radius>100||e.width<0||e.width>100||e.charge<0||e.charge>1000)return;e.sourceLocal=false;e.sourceVictimLocal=ownsActor(localPeer_,e.victimNid);if(e.sourceVictimLocal)e.pos=glm::dvec3(0);match_.world().queueRemoteVisual(std::move(e));}

} // namespace restoration::network
