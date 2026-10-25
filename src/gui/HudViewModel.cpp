#include "HudViewModel.hpp"
#include "../game/ArsenalData.hpp"
#include "../game/MatchSimulation.hpp"
#include <algorithm>
#include <cstdint>

namespace restoration {
namespace {
HudMatchPhase hudPhase(MatchPhase phase) {
    switch(phase) {
    case MatchPhase::Intro:return HudMatchPhase::Intro;
    case MatchPhase::Playing:return HudMatchPhase::Playing;
    case MatchPhase::Finish:return HudMatchPhase::Finish;
    case MatchPhase::Judge:return HudMatchPhase::Judge;
    case MatchPhase::Results:return HudMatchPhase::Results;
    }
    return HudMatchPhase::Intro;
}
int actorSlot(const MatchSimulation& match,const PlayerSimulation* actor) {
    if(!actor)return -1;
    for(std::size_t i=0;i<match.actors().size();++i)if(match.actors()[i].get()==actor)return static_cast<int>(i);
    return -1;
}
HudZoneEventType hudZoneEvent(ZoneEventType type) {
    switch(type) {
    case ZoneEventType::ZoneOwnerChanged:return HudZoneEventType::OwnerChanged;
    case ZoneEventType::ControlChanged:return HudZoneEventType::ControlChanged;
    case ZoneEventType::PenaltyAdded:return HudZoneEventType::PenaltyAdded;
    case ZoneEventType::ObjectiveChanged:return HudZoneEventType::ObjectiveChanged;
    case ZoneEventType::Contested:return HudZoneEventType::Contested;
    case ZoneEventType::FinalStarted:return HudZoneEventType::FinalStarted;
    case ZoneEventType::OvertimeStarted:return HudZoneEventType::OvertimeStarted;
    case ZoneEventType::MatchEnded:return HudZoneEventType::MatchEnded;
    }
    return HudZoneEventType::ControlChanged;
}
HudBossMove hudBossMove(BossMoveId id) {
    switch(id) {
    case BossMoveId::Slam:return HudBossMove::Slam;
    case BossMoveId::Barrage:return HudBossMove::Barrage;
    case BossMoveId::Sweep:return HudBossMove::Sweep;
    case BossMoveId::Charge:return HudBossMove::Charge;
    case BossMoveId::Crablets:return HudBossMove::Crablets;
    case BossMoveId::Frenzy:return HudBossMove::Frenzy;
    }
    return HudBossMove::Slam;
}
HudBossMovePhase hudBossMovePhase(BossMovePhase phase) {
    switch(phase) {
    case BossMovePhase::None:return HudBossMovePhase::None;
    case BossMovePhase::Telegraph:return HudBossMovePhase::Telegraph;
    case BossMovePhase::Active:return HudBossMovePhase::Active;
    case BossMovePhase::Recovery:return HudBossMovePhase::Recovery;
    }
    return HudBossMovePhase::None;
}
}

HudViewModel BuildHudViewModel(const MatchSimulation& match) {
    HudViewModel view;
    view.matchId=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&match));
    view.phase=hudPhase(match.phase());view.remaining=match.remaining();
    view.stageId=match.stage().id;view.stageName=match.stage().name;
    view.networkAuthority=match.networkAuthority();
    const auto& actors=match.actors();const auto& roster=match.roster();
    if(actors.empty())return view;
    const auto* local=&match.local();view.localActor=actorSlot(match,local);view.localTeam=local->team();
    const auto& world=match.world();view.worldTime=world.time();view.eventEpoch=world.hudEventEpoch();
    view.actors.reserve(actors.size());
    for(std::size_t i=0;i<actors.size();++i) {
        const auto& actor=*actors[i];const auto& state=actor.state;
        const auto pose=actor.combatPose();const auto special=actor.specialPose();
        const auto& entry=roster[i];HudActor item;
        item.name=entry.name;item.mainId=actor.mainId();item.subId=actor.subId();item.specialId=actor.specialId();item.weaponKind=actor.weapon().kind;
        item.team=actor.team();item.slot=entry.slot;item.local=actor.isLocal();item.bot=entry.bot||actor.botControlled();item.active=entry.active;
        item.alive=state.alive;item.squid=state.squid;item.grounded=state.grounded;item.climbing=state.climbing;item.canRespawn=actor.canRespawn();
        item.subAim=pose.subAim;item.combatPose=pose;item.specialActive=special.active;item.specialBarrage=special.active&&special.phase=="barrage";item.specialMapTarget=special.mapTarget;
        item.pos=state.pos;item.visualPos=actor.visualPos();item.velocity=state.vel;
        item.ink=state.ink;item.specialCharge=state.specialCharge;item.respawnTimer=state.respawnTimer;item.invulnerability=state.invuln;item.aimYaw=actor.input.aimYaw;
        item.turf=actor.turfPainted();item.zoneTurf=actor.zoneTurf();item.shotsFired=actor.shotsFired();item.spreadDegrees=actor.crosshairSpreadDegrees();item.streamDuration=actor.streamDuration();
        item.specialCost=actor.weapon().number("specialCost",190);item.subCost=actor.subWeapon().number("inkCost",70);item.splats=actor.splats();item.deaths=actor.deaths();
        if(item.local)view.localActor=static_cast<int>(i);
        view.actors.push_back(std::move(item));
    }
    for(const auto& event:world.hudEvents()) {
        HudCombatEvent item;item.serial=event.serial;item.time=event.time;item.damage=event.damage;
        item.kind=event.kind==CombatHudEvent::Kind::Hit?HudEventKind::Hit:event.kind==CombatHudEvent::Kind::Splat?HudEventKind::Splat:HudEventKind::Denied;
        item.attacker=actorSlot(match,event.attacker);item.victim=actorSlot(match,event.victim);
        item.attackerTeam=event.attackerTeam;item.victimTeam=event.victimTeam;item.victimTrackedTeam=event.victimTrackedTeam;
        item.attackerWeapon=event.attackerWeapon;item.victimWeapon=event.victimWeapon;item.source=event.source;item.attackerPos=event.attackerPos;item.victimPos=event.victimPos;
        view.events.push_back(std::move(item));
    }
    if(const auto* boss=match.boss()) {
        const auto& state=boss->state();HudBoss item;
        item.pos=state.pos;item.moveAnchor=boss->poseSocket("shellTop").value_or(state.pos+glm::dvec3(0,4.2,0));
        item.clock=state.clock;item.hp=state.hp;item.maxHp=state.maxHp;item.phase=state.phase;item.anim=state.anim;item.dead=state.dead;item.stunned=state.stunned;
        item.squadDamage=boss->squadDamage();item.squadWeakHits=boss->squadWeakHits();item.squadSplats=boss->squadSplats();
        if(const auto& move=boss->move()) {
            item.hasMove=true;item.moveId=hudBossMove(move->id);item.movePhase=hudBossMovePhase(move->phase(state.clock));item.moveT0=move->t0;item.moveYaw=move->yaw;
        }
        item.hits.reserve(boss->hudHitEvents().size());
        for(const auto& event:boss->hudHitEvents())item.hits.push_back({event.serial,event.time,event.damage,event.attackerSlot,event.point,event.weak});
        view.boss=std::move(item);
    }
    if(const auto* zones=match.zones()) {
        HudZones item;item.owner=zones->owner();item.counts=zones->counts();item.penalty=zones->penalty();
        item.objective=zones->objective().id;item.objectiveHome=zones->objective().home;item.nextSwap=zones->nextSwap();item.neutralTime=zones->neutralTime();
        item.overtime=zones->overtime();item.final=zones->final();item.ended=zones->ended();item.activeIndices=zones->activeIndices();
        item.zones.reserve(zones->zones().size());
        for(const auto& zone:zones->zones())item.zones.push_back({zone.kind,zone.home,zone.owner,zone.share});
        item.events.reserve(zones->events().size());
        for(const auto& event:zones->events())item.events.push_back({hudZoneEvent(event.type),event.owner,event.team,event.objective,event.reason,event.penalty,event.final,event.moved});
        view.zones=std::move(item);
    }
    if(const auto& result=match.result()) {
        HudResult item;item.coverage=result->coverage;item.winner=result->winner;
        if(result->boss)item.boss=HudBossResult{result->boss->win,result->boss->time,result->boss->hp,result->boss->maxHp,result->boss->phase};
        if(result->zones)item.zones=HudZoneResult{result->zones->counts,result->zones->penalty,result->zones->reason};
        view.result=std::move(item);
    }
    return view;
}
}
