#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include "NavGraph.hpp"
#include "BossSimulation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace restoration {

namespace {
void applyActorEffectState(PlayerSimulation& target,const NetworkActorEffect& effect) {
    if(effect.poison>0.0) target.state.poison=std::max(target.state.poison,effect.poison);
    if(effect.track>0.0 && (effect.track>target.state.track || target.state.trackTeam!=effect.effectTeam)) {
        target.state.track=effect.track;
        target.state.trackTeam=effect.effectTeam;
    }
    if(effect.reveal>0.0 && (effect.reveal>target.state.reveal || target.state.revealTeam!=effect.effectTeam)) {
        target.state.reveal=effect.reveal;
        target.state.revealTeam=effect.effectTeam;
    }
    if(effect.shield>0.0) target.state.shield=std::max(target.state.shield,effect.shield);
    target.state.vel+=effect.velocityImpulse;
}
}

void CombatWorld::recordRemoteHudEvent(CombatHudEvent::Kind kind, PlayerSimulation* attacker, PlayerSimulation& victim,
                                      double damage, const std::string& source) {
    recordHudEvent(kind, attacker, victim, damage, source, false);
}

void CombatWorld::recordHudDenial(PlayerSimulation& actor, const std::string& source, bool lowInk) {
    recordHudEvent(CombatHudEvent::Kind::Denied, nullptr, actor, lowInk ? 1.0 : 0.0, source, false);
}

void CombatWorld::recordHudEvent(CombatHudEvent::Kind kind, PlayerSimulation* attacker, PlayerSimulation& victim,
                                double damage, const std::string& source, bool broadcast) {
    if (hudEvents_.size() == 512) hudEvents_.erase(hudEvents_.begin());
    CombatHudEvent event;
    event.serial = ++hudEventSerial_;
    event.time = time_;
    event.kind = kind;
    event.victim = &victim;
    event.victimNid = victim.networkNid();
    event.victimTeam = victim.team();
    event.victimTrackedTeam = victim.state.track>0 ? victim.state.trackTeam : -1;
    event.victimWeapon = victim.mainId();
    event.victimPos = victim.state.pos;
    event.damage = damage;
    event.source = source;
    if (attacker && attacker != &victim) {
        event.attacker = attacker;
        event.attackerNid = attacker->networkNid();
        event.attackerTeam = attacker->team();
        event.attackerWeapon = attacker->mainId();
        event.attackerPos = attacker->state.pos;
    }
    hudEvents_.push_back(std::move(event));
    if (broadcast && !networkApplying_ && networkVisualForwarder_) {
        const auto& recorded = hudEvents_.back();
        WorldVisualEvent visual;
        visual.kind = kind == CombatHudEvent::Kind::Hit ? "hud_hit" : "hud_splat";
        visual.pos = recorded.victimPos;
        visual.normal = recorded.attackerPos;
        visual.team = recorded.victimTeam;
        visual.charge = recorded.damage;
        visual.source = recorded.source;
        visual.id = recorded.attackerNid;
        visual.victimNid = recorded.victimNid;
        networkVisualForwarder_(visual);
    }
}

bool CombatWorld::applyActorEffect(PlayerSimulation& source,PlayerSimulation& target,const NetworkActorEffect& effect) {
    if(networkLocalPeer_ && source.networkOwner()!=networkLocalPeer_) return false;
    if(networkLocalPeer_ && target.networkOwner()!=networkLocalPeer_) {
        if(!networkActorEffectForwarder_) return false;
        networkActorEffectForwarder_(source,target,effect);
        return true;
    }
    applyActorEffectState(target,effect);
    return true;
}

bool CombatWorld::applyNetworkActorEffect(PlayerSimulation& source,PlayerSimulation& target,const NetworkActorEffect& effect) {
    if(!networkLocalPeer_ || target.networkOwner()!=networkLocalPeer_
       || std::find(actors_.begin(),actors_.end(),&source)==actors_.end()
       || std::find(actors_.begin(),actors_.end(),&target)==actors_.end()) return false;
    applyActorEffectState(target,effect);
    return true;
}

bool CombatWorld::applyHit(PlayerSimulation& attacker,PlayerSimulation& victim,double damage,const std::string& source) {
    if(attacker.team()==victim.team() || !victim.state.alive) return false;
    if(networkHitForwarder_&&!networkApplying_&&networkLocalPeer_&&attacker.networkOwner()==networkLocalPeer_&&victim.networkOwner()!=networkLocalPeer_)
        return networkHitForwarder_(attacker,victim,damage,source);
    return victim.damageFromCombat(damage,&attacker,source);
}

void CombatWorld::burst(PlayerSimulation& owner,glm::dvec3 at,double radius,double maxDamage,double minDamage,double paintRadius,const std::string& id,PlayerSimulation* direct,bool special,bool bossDirect) {
    emitVisual({"explosion",at,{0,1,0},radius,owner.team()});
    guardActorsWithCanopies(at,radius,owner.team());
    damageCanopies(at,radius,owner.team());
    const auto floor=geometry_.raycast(at+glm::dvec3(0,0.2,0),{0,-1,0},3.5);
    if(floor.hit) paint(owner,floor.point+floor.normal*0.1,paintRadius,owner.random(),{},special);
    for(auto* e:actors_) {
        if (!e) continue;
        if(e==direct || e->team()==owner.team() || !e->state.alive) continue;
        const auto target=e->state.pos+glm::dvec3(0,0.7,0); const double d=glm::length(target-at);
        if(d<=radius && geometry_.los(at,target)) applyHit(owner,*e,maxDamage+(minDamage-maxDamage)*d/radius,id);
    }
    damageDevices(at,radius,maxDamage,owner.team());
    specials_.areaHit(at,radius,minDamage,owner.team(),&owner);
    if(boss_&&!bossDirect&&owner.team()==0)boss_->splash(owner,at,radius,maxDamage,minDamage,id);
}

void CombatWorld::beam(PlayerSimulation& owner,glm::dvec3 from,glm::dvec3 dir,double length,double radius,double damage,double paintRadius,bool pierce,const std::string& id) {
    const double blocked=blockRay(from,dir,length,owner.team(),damage,&owner);
    const auto hit=geometry_.raycast(from,dir,blocked,true); length=hit.hit?hit.dist:blocked;
    const auto end=from+dir*length;
    bool struckBoss=false, struckActor=false;
    if(boss_&&owner.team()==0){
        if(const auto bossHit=boss_->segmentHit(from,end,radius)){
            struckBoss=true;
            boss_->hit(owner,damage,*bossHit,id);
            if(!pierce)length=bossHit->distance;
        }
    }
    for(auto* e:actors_) {
        if (!e) continue;
        if(e->team()==owner.team() || !e->state.alive) continue;
        const auto h=StageGeometry::segmentCapsuleDist(from,end,e->visualPos(),e->hitRadius(),e->hitHeight());
        if(h.dist<e->hitRadius()+radius) { struckActor=true;applyHit(owner,*e,damage,id); if(!pierce) {length*=h.t;break;} }
    }
    for(double d=0;d<length;d+=0.55) {
        const auto floor=geometry_.raycast(from+dir*d,{0,-1,0},4,true);
        if(floor.hit) paint(owner,floor.point+floor.normal*0.1,paintRadius,owner.random());
    }
    if(hit.hit) paint(owner,hit.point+hit.normal*0.1,paintRadius,owner.random());
    if(id=="charger" && hit.hit && !struckBoss && !struckActor) {
        WorldVisualEvent wall;wall.kind="inkWallImpact";wall.pos=hit.point;wall.normal=hit.normal;
        wall.radius=radius;wall.team=owner.team();wall.source=id;wall.id=owner.networkNid();wall.sourceLocal=owner.isLocal();
        emitVisual(std::move(wall));
    }
    const double charge=std::max(.12,owner.combatPose().charge);
    emitVisual({"beam",from,dir,length,owner.team(),charge,(.035+charge*.05)*2.3,time_*17});
}

} // namespace restoration
