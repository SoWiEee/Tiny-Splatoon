#include "GameAudio.hpp"

#include "BossSimulation.hpp"
#include "ArsenalData.hpp"
#include "CharacterPose.hpp"
#include "CombatWorld.hpp"
#include "MatchSimulation.hpp"
#include "PlayerSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

#include <glm/geometric.hpp>

namespace restoration {
namespace {
AudioPosition Position(const glm::dvec3& p) { return {p.x, p.y, p.z}; }
const char* MusicTrack(int phase) { return phase >= 3 ? "boss_3" : phase == 2 ? "boss_2" : "boss"; }
const char* BossMoveName(BossMoveId id) {
    switch (id) {
    case BossMoveId::Slam: return "slam";
    case BossMoveId::Barrage: return "barrage";
    case BossMoveId::Sweep: return "sweep";
    case BossMoveId::Charge: return "charge";
    case BossMoveId::Crablets: return "crablets";
    case BossMoveId::Frenzy: return "frenzy";
    }
    return "slam";
}
const char* SpecialWeaponSound(const PlayerSimulation& actor) {
    const auto special = actor.specialPose();
    if (!special.active) return nullptr;
    if (special.kind == "crab") return actor.input.sub ? "crab_cannon" : "crab_gatling";
    if (special.kind == "jetpack") return "jet_fire";
    if (special.kind == "zooka") return "zooka_fire";
    if (special.kind == "wail") return "wail_blast";
    if (special.kind == "stamp") return "stamp_swing";
    if (special.kind == "booyah") return "booyah_blast";
    if (special.kind == "zipcaster") return "zip_fire";
    if (special.kind == "strike") return "strike_launch";
    if (special.kind == "kraken") return special.roll ? "kraken_slam" : "kraken_jump";
    if (special.kind == "blower") return "bubble_release";
    return nullptr;
}

const char* WeaponSound(const PlayerSimulation& actor) {
    if (const auto* special = SpecialWeaponSound(actor)) return special;
    const auto kind = actor.weaponProfile().kind;
    if (kind == "twins") return "shoot_dualies";
    if (kind == "spinner") return "shoot_splatling";
    if (kind == "blaster") return "shoot_blaster";
    if (kind == "charger") return "shoot_charger";
    if (kind == "bucket") return "slosh_throw";
    if (kind == "roller" || kind == "brush") return "roller_flick";
    if (kind == "bow") return "bow_loose";
    if (kind == "blade") return "blade_swish";
    if (kind == "brolly") return "brolly_blast";
    if (kind == "mitts") return "mitts_punch";
    return "shoot_shooter";
}
const char* SubThrowSound(const PlayerSimulation& actor) {
    const auto special = actor.specialPose();
    if (special.active) {
        if (special.kind == "booyah") return "booyah_throw";
        if (special.kind == "stamp") return "stamp_throw";
        if (special.kind == "blower") return "bubble_release";
    }
    const auto& sub = actor.subId();
    if (sub == "tracer") return "tracer_zap";
    if (sub == "boomerang") return "boomerang_throw";
    if (sub == "torpedo") return "torpedo_throw";
    return "bomb_throw";
}
const char* SpecialActivationSound(std::string_view kind) {
    if (kind == "kraken") return "kraken_on";
    if (kind == "jetpack") return "jet_fire";
    if (kind == "crab") return "crab_move";
    if (kind == "stamp") return "stamp_swing";
    if (kind == "wail") return "wail_charge";
    if (kind == "blower") return "blower_inflate";
    if (kind == "booyah") return "booyah_charge";
    if (kind == "zipcaster") return "zip_fire";
    if (kind == "strike") return "strike_launch";
    if (kind == "zooka") return "zooka_fire";
    if (kind == "bubbler") return "shield_up";
    return nullptr;
}
AudioPosition BossPosition(const BossSimulation& boss, std::string_view socket = {}) {
    if (!socket.empty()) if (const auto position = boss.poseSocket(socket)) return Position(*position);
    const auto& p = boss.state().pos;
    return {p.x, p.y + 3.0, p.z};
}
double AudioRandom(std::uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return static_cast<double>(state) / 4294967296.0;
}
double DistanceSquared(AudioPosition a, AudioPosition b) {
    const double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
}

void GameAudio::Play(std::string_view name, const PlayerSimulation* actor, double volume, double pitch) {
    AudioPlayOptions options;
    options.volume = volume; options.pitch = pitch;
    if (actor && !actor->isLocal()) {
        auto position = actor->muzzle();
        if (position == glm::dvec3(0.0)) position = actor->state.pos + glm::dvec3(0, 1, 0);
        options.pos = Position(position);
    }
    engine_.Play(name, options);
}

void GameAudio::Footstep(PlayerSimulation& actor, AudioPosition position, double speed) {
    if (!engine_.ready() || !actor.state.alive || actor.state.squid) return;
    if (!actor.isLocal() && hasListenerPosition_ && DistanceSquared(position, listenerPosition_) > 18.0 * 18.0) return;
    const char* cue = actor.state.groundTeam == 1 ? "step_ink" : actor.state.groundTeam == 2 ? "step_enemy" : "step_dry";
    const double horizontalSpeed = speed > 0 ? speed : std::hypot(actor.state.vel.x, actor.state.vel.z);
    AudioPlayOptions options;
    options.volume = (actor.isLocal() ? 0.7 : 0.45) * std::min(1.0, 0.45 + std::max(0.0, horizontalSpeed) / 8.0);
    if (!actor.isLocal()) options.pos = position;
    engine_.Play(cue, options);
}

void GameAudio::ActorTrigger(PlayerSimulation& actor, const CharacterTrigger& event) {
    const auto kind = actor.weaponProfile().kind;
    const auto pose = actor.specialPose();
    const double amount = event.amount.value_or(1.0);
    if (event.name == "spawn") {
        auto& state = actorStates_[&actor];
        if (state.alive == false && actor.deaths() > 0) Play("respawn", &actor, actor.isLocal() ? 0.85 : 0.55);
        state.alive = true;
        return;
    }
    if (event.name == "jump" || event.name == "special_leap") {
        if (event.name == "jump" && pose.active && pose.kind == "kraken") Play("kraken_jump", &actor, 0.8);
        else Play(event.name == "jump" ? "jump" : "super_jump", &actor, actor.isLocal() ? 0.9 : 0.6);
        return;
    }
    if (event.name == "land") { Play("land", &actor, 1, std::clamp(0.75 + amount * 0.12, 0.7, 1.25)); return; }
    if (event.name == "hit") { Play(actor.isLocal() ? "hurt" : "ink_hit_body", &actor, actor.isLocal() ? 0.7 : 0.55); return; }
    if (event.name == "shoot" || event.name == "stream") {
        Play(WeaponSound(actor), &actor, actor.isLocal() ? 0.48 : 0.34, event.hand ? 1.05 : 1.0);
        return;
    }
    if (event.name == "charge_release") {
        const char* sound = kind == "bow" ? "bow_loose" : kind == "charger" ? "shoot_charger" : "shoot_splatling";
        Play(sound, &actor, 0.65, std::clamp(1.08 - 0.16 * amount, 0.8, 1.15)); return;
    }
    if (event.name == "throw") {
        const auto special = actor.specialPose();
        if (special.active && special.kind == "booyah") Play("booyah_throw", &actor, 0.9);
        else if (special.active && special.kind == "stamp") Play("stamp_throw", &actor, 0.9);
        else if (special.active && special.kind == "blower") Play("bubble_release", &actor, 0.7);
        else if (actor.subId() == "torpedo") {
            Play("bomb_throw", &actor, 0.6, 0.92); Play("torpedo_throw", &actor, 0.7);
        } else if (actor.subId() == "tracer") Play("tracer_zap", &actor, 0.8);
        else if (actor.subId() == "boomerang") Play("boomerang_throw", &actor, 0.8);
        else if (actor.subId() == "waddle") Play("bomb_throw", &actor, 0.6, 1.08);
        else if (actor.subId() == "burst") Play("bomb_throw", &actor, 0.65, 1.2);
        else if (actor.subId() == "shaker")
            Play("bomb_throw", &actor, 0.65, 0.95 + 0.06 * std::max(0.0, event.amount.value_or(0.0)));
        else Play(SubThrowSound(actor), &actor, 0.65);
        return;
    }
    if (event.name == "flick") { Play("roller_flick", &actor, 0.8, event.side < 0 ? 1.08 : 1.0); return; }
    if (event.name == "slosh") { Play("slosh_throw", &actor, 0.65); return; }
    if (event.name == "blade_slash") { Play("blade_swish", &actor, 0.55, event.side < 0 ? 1.09 : 1.0); return; }
    if (event.name == "blade_heavy") { Play("blade_heavy", &actor, 0.7); return; }
    if (event.name == "dodge") { Play("dualies_roll", &actor, 0.75); return; }
    if (event.name == "roll") { Play("roll", &actor, 0.75); return; }
}

void GameAudio::Menu(bool active) {
    if (!active) { if (engine_.ready()) engine_.StopMusic(0.5); return; }
    if (!engine_.ready() && !engine_.Initialize()) return;
    engine_.PlayMusic("title", 0.8);
}

void GameAudio::BeginMatch(MatchSimulation& match) {
    if (!engine_.ready() && !engine_.Initialize()) return;
    StopLoops(); StopWorldLoops(); engine_.StopMusic(0.05);
    StopZoneLoops();zoneEventCursor_=0;zoneTicks_={100,100};zoneHumAccumulator_=0;zoneWarnAt_=-99;
    match_ = &match; playing_ = false; bossPhase_ = 0; matchPhase_ = -1; bossHp_ = -1; bossDead_ = false;
    bossMove_.clear(); crablets_.clear(); bossSunkIn_ = 0; bossIntroSeen_=false; ClearBossPendingAudio();
    actorLoops_.clear(); actorStates_.clear();
    pausedAudio_ = false; resumeLoops_ = false; hasListenerPosition_ = false;
    gullTimer_ = 4.0; gustTimer_ = 2.0; clinkTimer_ = 0.0; pendingHalyardClinks_ = 0;
    hornTimer_ = 28.0 + 10.0 * AudioRandom(audioRandomState_);
    previousRemaining_ = match.remaining();
    engine_.Play("ready");
    engine_.PlayMusic("menu", 0.4);
    if (match.boss()) Play("boss_title");
}

void GameAudio::StopLoops(bool preserveWail) {
    for (const auto& [actor, loop] : actorLoops_) engine_.StopLoop(loop.key, 0.2);
    actorLoops_.clear();
    for (const auto& [actor, loop] : weaponLoops_) engine_.StopLoop(loop.key, 0.2);
    weaponLoops_.clear();
    for(const auto& [id,state]:deviceStates_)if(!state.loop.empty())engine_.StopLoop("device:"+std::to_string(id),.15);
    deviceStates_.clear();
    for (const auto& item : wailLoops_) engine_.StopLoop("wail:" + std::to_string(item.first), 0.3);
    if (!preserveWail) wailLoops_.clear();
    for (const auto* key : {"boss:sweep", "boss:gallop", "boss:frenzy", "boss:dizzy"}) engine_.StopLoop(key, 0.2);
    bossLoop_.clear();
}

void GameAudio::StopWorldLoops() {
    if (harborLoop_) engine_.StopLoop("world:harbor", 0.25);
    harborLoop_ = false;
}

void GameAudio::Pause() {
    if (pausedAudio_) return;
    pausedAudio_ = true;
    ClearBossPendingAudio();
    StopLoops(true);
    StopZoneLoops();
}

void GameAudio::ClearBossPendingAudio() {
    bossIntroSlamAt_=bossIntroRoarAt_=bossSlamAt_=bossChargeRoarAt_=bossCrabletSecondAt_=-1;
    bossBarrageStartedAt_=-1;bossBarrageCount_=bossBarrageWhistles_=bossBarrageBarrels_=0;
}

void GameAudio::EndMatch() {
    StopLoops(); StopWorldLoops(); engine_.StopMusic(0.6);
    StopZoneLoops();zoneEventCursor_=0;
    match_ = nullptr; playing_ = false; bossMove_.clear(); crablets_.clear(); actorStates_.clear();
    ClearBossPendingAudio();bossIntroSeen_=false;
    pausedAudio_ = false; resumeLoops_ = false; hasListenerPosition_ = false;
    bossPhase_ = 0; matchPhase_ = -1; bossHp_ = -1; bossDead_ = false; bossSunkIn_ = 0;
}

void GameAudio::UpdateActorAudio(PlayerSimulation& actor) {
    const auto pose = actor.specialPose();
    const double specialCost = actor.weapon().number("specialCost", 190.0);
    const bool lowInk = actor.state.ink <= 20.0;
    const bool specialReady = actor.state.specialCharge >= specialCost;
    auto [stateIt, inserted] = actorStates_.try_emplace(&actor);
    auto& previous = stateIt->second;
    const auto combat=actor.combatPose();
    const auto canopy=combat.canopyState;
    const auto kind = actor.weaponProfile().kind;
    const bool nearActor = actor.isLocal() || !hasListenerPosition_ ||
        DistanceSquared(Position(actor.state.pos), listenerPosition_) <= 40.0 * 40.0;
    if(!inserted&&actor.mainId()=="brolly"&&previous.canopy!=canopy&&!resumeLoops_){
        if(canopy=="ready"&&actor.state.alive)Play("brolly_regrow",&actor,.6);
        else if(canopy=="launched")Play("brolly_launch",&actor,.8);
        else if(canopy=="broken")Play("brolly_break",&actor,.8);
    }
    if(!inserted&&actor.mainId()=="brolly"&&!resumeLoops_){
        if(previous.canopyOpen<=0&&combat.canopyOpen>0)Play("brolly_open",&actor,.6);
        else if(previous.canopyOpen>.4&&combat.canopyOpen<previous.canopyOpen&&canopy=="ready")Play("brolly_close",&actor,.5);
        if(canopy!="broken"&&previous.canopyHp>combat.canopyHp+.01){
            const double damage=previous.canopyHp-combat.canopyHp;
            Play("brolly_hit",&actor,std::clamp(.35+damage/90.,.35,.9)*(actor.isLocal()?.8:.65),damage>60?.8:1.);
        }
    }
    previous.canopy=canopy;
    previous.canopyOpen=combat.canopyOpen;previous.canopyHp=combat.canopyHp;
    if (inserted) {
        previous.alive = actor.state.alive;
        previous.squid = actor.state.squid;
        previous.lowInk = lowInk;
        previous.specialReady = specialReady;
        previous.specialActive = pose.active;
        previous.specialKind = pose.kind;
        previous.bowRingEvents = combat.bowRingEvents;
        previous.chargeFull = combat.charging && combat.charge >= 0.999;
        previous.streaming = combat.streaming;
    } else {
        if (previous.alive && !actor.state.alive) {
            Play(actor.isLocal() ? "splatted_self" : (actor.team() == match_->local().team() ? "ally_splatted" : "splat_enemy"), &actor);
        } else if (!previous.alive && actor.state.alive) {
            Play("respawn", &actor, actor.isLocal() ? 0.85 : 0.55);
        }
        if (previous.squid != actor.state.squid) Play(actor.state.squid ? "squid_in" : "squid_out", &actor, 0.8);
        if (!previous.lowInk && lowInk) Play("low_ink", &actor, 0.65);
        if (previous.lowInk && !lowInk && actor.state.ink > 94.0) Play("refill_full", &actor, 0.7);
        if (!previous.specialReady && specialReady) Play("special_ready", &actor, 0.8);
        if (!previous.specialActive && pose.active) {
            Play("special_activate", &actor, 0.75);
            if (const auto* cue = SpecialActivationSound(pose.kind)) Play(cue, &actor, 0.8);
        } else if (previous.specialActive && !pose.active && actor.state.alive) {
            Play("special_end", &actor, 0.7);
        }
        if (!resumeLoops_) {
            if (kind == "bow") {
                if (previous.bowRingEvents < 1 && combat.bowRingEvents >= 1 && actor.isLocal()) Play("bow_ring1", &actor, 0.65);
                if (previous.bowRingEvents < 2 && combat.bowRingEvents >= 2 && actor.isLocal()) Play("bow_full", &actor, 0.7);
            }
            if (!previous.chargeFull && combat.charging && combat.charge >= 0.999) {
                if (kind == "charger" && actor.isLocal()) Play("charger_full", &actor, 0.7);
                else if (kind == "spinner" && actor.isLocal()) Play("splatling_ready", &actor, 0.7);
                else if (kind == "blade" && (actor.isLocal() || !hasListenerPosition_ || DistanceSquared(Position(actor.state.pos), listenerPosition_) <= 40.0 * 40.0))
                    Play("blade_ready", &actor, actor.isLocal() ? 0.6 : 0.4);
                else if (kind == "mitts" && (actor.isLocal() || !hasListenerPosition_ || DistanceSquared(Position(actor.state.pos), listenerPosition_) <= 40.0 * 40.0))
                    Play("mitts_full", &actor, actor.isLocal() ? 0.6 : 0.4);
            }
            if (kind == "spinner" && previous.streaming && !combat.streaming && actor.state.alive)
                Play("splatling_wind", &actor, actor.isLocal() ? 0.6 : 0.42);
        }
        previous.alive = actor.state.alive;
        previous.squid = actor.state.squid;
        previous.lowInk = lowInk;
        previous.specialReady = specialReady;
        previous.specialActive = pose.active;
        previous.specialKind = pose.kind;
        previous.bowRingEvents = combat.bowRingEvents;
        previous.chargeFull = combat.charging && combat.charge >= 0.999;
        previous.streaming = combat.streaming;
    }

    std::string desiredWeapon;
    if (actor.state.alive && actor.isLocal() && combat.shakerCharging) desiredWeapon = "shaker_fizz";
    else if (actor.state.alive && nearActor && combat.charging) {
        if (kind == "charger") desiredWeapon = "charger_charge";
        else if (kind == "spinner") desiredWeapon = "splatling_spin";
        else if (kind == "bow") desiredWeapon = "bow_draw";
        else if (kind == "blade") desiredWeapon = "blade_charge";
        else if (kind == "mitts") desiredWeapon = "mitts_charge";
    }
    if (actor.state.alive && nearActor && kind == "spinner" && combat.streaming) desiredWeapon = "splatling_spin";
    auto weaponLoop = weaponLoops_.find(&actor);
    if (weaponLoop != weaponLoops_.end() && weaponLoop->second.cue != desiredWeapon) {
        engine_.StopLoop(weaponLoop->second.key, weaponLoop->second.cue == "shaker_fizz" ? 0.1 : 0.18);
        weaponLoops_.erase(weaponLoop);
        weaponLoop = weaponLoops_.end();
    }
    if (!desiredWeapon.empty()) {
        const std::string key = "weapon:" + std::to_string(reinterpret_cast<std::uintptr_t>(&actor)) + ':' + desiredWeapon;
        AudioLoopOptions options;
        if (desiredWeapon == "shaker_fizz") {
            const double charge = std::clamp(combat.shakerCharge, 0.0, 1.0);
            const bool full = charge >= 1.0;
            options.volume = 0.3 + 0.8 * charge + (full ? 0.25 : 0.0);
            options.pitch = 0.85 + 0.55 * charge + (full ? 0.15 * std::sin(match_->world().time() * 9.0) : 0.0);
        } else {
            options.volume = kind == "charger" ? (actor.isLocal() ? 0.55 : 0.35) :
                kind == "spinner" ? (actor.isLocal() ? 0.6 : 0.4) :
                kind == "mitts" ? (actor.isLocal() ? 0.5 : 0.35) : (actor.isLocal() ? 0.5 : 0.32);
        }
        if (desiredWeapon == "charger_charge") options.pitch = 1.0 + 1.5 * combat.charge;
        else if (desiredWeapon == "splatling_spin") options.pitch = combat.streaming ? 1.5 : 0.6 + 0.85 * combat.charge;
        else if (desiredWeapon == "bow_draw") options.pitch = 0.6 + 0.9 * combat.charge;
        else if (desiredWeapon == "blade_charge") options.pitch = 1.0 + 0.6 * combat.charge;
        else if (desiredWeapon == "mitts_charge") options.pitch = 0.8 + 0.9 * combat.charge;
        if (!actor.isLocal()) options.pos = Position(actor.state.pos);
        if (weaponLoop == weaponLoops_.end()) {
            if (engine_.StartLoop(key, desiredWeapon, options)) weaponLoops_.emplace(&actor, ActorLoop{key, desiredWeapon});
        } else engine_.UpdateLoop(weaponLoop->second.key, options);
    }

    std::string desired;
    const double horizontalSpeed = std::hypot(actor.state.vel.x, actor.state.vel.z);
    if (actor.state.alive && pose.active) {
        if (pose.kind == "jetpack") desired = "jet_loop";
        else if (pose.kind == "crab") desired = pose.roll ? "crab_roll" : "crab_move";
        else if (pose.kind == "storm") desired = "storm_rain";
        else if (pose.kind == "booyah") desired = "booyah_charge";
    }
    if (desired.empty() && actor.state.alive && actor.state.squid && actor.state.submerged && horizontalSpeed > 0.5) desired = "swim";
    if (desired.empty() && actor.state.alive && actor.state.climbing) desired = "climb";
    if (desired.empty() && actor.state.alive && actor.state.grounded && actor.state.groundTeam == 2) desired = "enemy_ink_sizzle";
    auto loopIt = actorLoops_.find(&actor);
    if (loopIt != actorLoops_.end() && loopIt->second.cue != desired) {
        engine_.StopLoop(loopIt->second.key, 0.18);
        actorLoops_.erase(loopIt);
        loopIt = actorLoops_.end();
    }
    if (desired.empty()) return;
    const std::string key = "actor:" + std::to_string(reinterpret_cast<std::uintptr_t>(&actor)) + ':' + desired;
    AudioLoopOptions options;
    options.volume = actor.isLocal() ? 0.4 : 0.25;
    const double verticalSpeed = std::abs(actor.state.vel.y);
    if (desired == "swim") {
        options.volume = std::min(0.6, horizontalSpeed / 11.8 * 0.6 + 0.08);
        options.pitch = 0.6 + std::min(1.0, horizontalSpeed / 11.8);
    } else if (desired == "climb") {
        options.volume = 0.5; options.pitch = 0.6 + std::min(1.0, verticalSpeed / 7.5);
    } else if (desired == "enemy_ink_sizzle") {
        options.volume = 0.45; options.pitch = 1.0;
    } else options.pitch = std::clamp(0.7 + horizontalSpeed * 0.025, 0.7, 1.5);
    if (!actor.isLocal()) options.volume *= 0.55;
    if (!actor.isLocal()) options.pos = Position(actor.state.pos + glm::dvec3(0, 0.5, 0));
    if (loopIt == actorLoops_.end()) {
        if (engine_.StartLoop(key, desired, options)) actorLoops_.emplace(&actor, ActorLoop{key, desired});
    } else {
        engine_.UpdateLoop(loopIt->second.key, options);
    }
}

void GameAudio::UpdateDeviceAudio(MatchSimulation& match){
    std::unordered_set<std::uint64_t> live;
    for(const auto& d:match.world().devices()){
        if(!d.live)continue;live.insert(d.id);auto [it,inserted]=deviceStates_.try_emplace(d.id);auto& old=it->second;
        const auto cue=[&](const char* name,double volume){AudioPlayOptions o;o.pos=Position(d.pos);o.volume=volume;engine_.Play(name,o);};
        if(!inserted&&!resumeLoops_){
            if(d.kind=="torpedo"&&old.state!=d.state){
                if(d.state=="unfold"){cue("torpedo_transform",.8);if((d.owner&&d.owner->isLocal())||(d.target&&d.target->isLocal()))engine_.Play("torpedo_lock",d.target&&d.target->isLocal()?.7:.45);}
                else if(d.state=="launch")cue("torpedo_launch",.8);
            }
            if(d.kind=="waddle"){
                if(old.state=="fly"&&d.state=="sense")cue("waddle_land",.6);
                if(old.state!=d.state&&d.state=="wake")cue("waddle_lock",1);
                if(!old.hopping&&d.hopping)cue("waddle_hop",.6);
                if(old.hopping&&!d.hopping)cue("waddle_land",.45);
            }
            if(d.kind=="boomerang"&&old.state!=d.state&&d.state=="return")cue("boomerang_return",.8);
            if(d.kind=="shaker"&&d.uses<old.uses)cue("shaker_hop",.6);
            if(d.kind=="tracer"&&d.uses>old.uses)cue("tracer_bounce",.7);
        }
        std::string loop;
        if(d.kind=="torpedo"&&(d.state=="fly"||d.state=="unfold"||d.state=="launch"))loop="torpedo_whirr";
        else if(d.kind=="tracer"&&d.state=="fly")loop="tracer_hum";
        else if(d.kind=="waddle"&&d.state=="walk")loop="waddle_walk";
        else if(d.kind=="boomerang")loop=d.state=="orbit"?"boomerang_orbit":"boomerang_whirr";
        const auto key="device:"+std::to_string(d.id);
        if(old.loop!=loop&&!old.loop.empty())engine_.StopLoop(key,.1);
        if(!loop.empty()){AudioLoopOptions options;options.pos=Position(d.pos);options.volume=.35;
            if(d.kind=="waddle")options.pitch=.75+std::clamp(d.speed/4.,0.,1.)*.45;
            if(old.loop!=loop)engine_.StartLoop(key,loop,options);else engine_.UpdateLoop(key,options);}
        old={d.state,loop,d.hopping,d.uses};
    }
    for(auto it=deviceStates_.begin();it!=deviceStates_.end();)if(!live.count(it->first)){if(!it->second.loop.empty())engine_.StopLoop("device:"+std::to_string(it->first),.15);it=deviceStates_.erase(it);}else ++it;
}

void GameAudio::UpdateWailAudio(MatchSimulation& match) {
    std::unordered_set<std::uint64_t> active;
    for (const auto& object : match.world().visualObjects()) {
        if (object.kind != "speaker" || object.phase != "blast" || !object.id) continue;
        active.insert(object.id);
        wailLoops_.insert_or_assign(object.id, Position(object.pos));
    }
    for (auto it = wailLoops_.begin(); it != wailLoops_.end();) {
        if (!active.count(it->first)) {
            engine_.StopLoop("wail:" + std::to_string(it->first), 0.3);
            it = wailLoops_.erase(it);
            continue;
        }
        AudioLoopOptions options; options.volume = 1.0; options.pos = it->second;
        engine_.StartLoop("wail:" + std::to_string(it->first), "wail_blast", options);
        ++it;
    }
}

void GameAudio::UpdateWorldAudio(double dt, MatchSimulation& match) {
    if (!harborLoop_) {
        AudioLoopOptions options; options.volume = 0.55;
        harborLoop_ = engine_.StartLoop("world:harbor", "harbor_ambience", options);
    }

    const double step = std::max(0.0, dt);
    const auto& bounds = match.stage().bounds;
    gullTimer_ -= step;
    if (gullTimer_ <= 0) {
        gullTimer_ = 7.0 + AudioRandom(audioRandomState_) * 12.0;
        const double angle = AudioRandom(audioRandomState_) * 6.283185307179586;
        AudioPlayOptions options;
        options.pos = AudioPosition{std::cos(angle) * (bounds.y + 25.0), 12.0 + AudioRandom(audioRandomState_) * 8.0,
            std::sin(angle) * (bounds.w + 20.0)};
        options.volume = 0.6 + AudioRandom(audioRandomState_) * 0.4;
        options.pitch = 0.9 + AudioRandom(audioRandomState_) * 0.25;
        engine_.Play("gull", options);
    }

    if (match.stage().id != "halyard") return;
    gustTimer_ -= step;
    if (gustTimer_ <= 0) {
        gustTimer_ = 2.5 + AudioRandom(audioRandomState_) * 4.0;
        pendingHalyardClinks_ = 2 + static_cast<int>(AudioRandom(audioRandomState_) * 4.0);
        clinkTimer_ = 0;
    }
    if (pendingHalyardClinks_ > 0) {
        clinkTimer_ -= step;
        if (clinkTimer_ <= 0) {
            --pendingHalyardClinks_;
            clinkTimer_ = 0.12 + AudioRandom(audioRandomState_) * 0.45;
            const double side = AudioRandom(audioRandomState_) < 0.5 ? -1.0 : 1.0;
            AudioPlayOptions options;
            options.pos = AudioPosition{side * (27.0 + AudioRandom(audioRandomState_) * 14.0),
                8.0 + AudioRandom(audioRandomState_) * 4.0, (AudioRandom(audioRandomState_) * 2.0 - 1.0) * 40.0};
            options.volume = 0.5 + AudioRandom(audioRandomState_) * 0.5;
            options.pitch = 0.85 + AudioRandom(audioRandomState_) * 0.35;
            engine_.Play("halyard_clink", options);
        }
    }
    hornTimer_ -= step;
    if (hornTimer_ <= 0) {
        hornTimer_ = 55.0 + AudioRandom(audioRandomState_) * 30.0;
        const double side = AudioRandom(audioRandomState_) < 0.5 ? -1.0 : 1.0;
        AudioPlayOptions options;
        options.pos = AudioPosition{side * 95.0, 8.0, (AudioRandom(audioRandomState_) * 2.0 - 1.0) * 70.0};
        options.volume = 0.8;
        engine_.Play("ferry_horn", options);
    }
}

void GameAudio::UpdateBoss(double dt, MatchSimulation& match) {
    auto* boss = match.boss();
    if (!boss) return;
    const auto& state = boss->state();
    const auto pos = BossPosition(*boss);
    const auto socketPos = [&](std::string_view name) { return BossPosition(*boss, name); };
    if(state.dead)ClearBossPendingAudio();
    const bool scheduleAudio = match.phase()!=MatchPhase::Finish&&match.phase()!=MatchPhase::Results&&!resumeLoops_;
    if(state.anim==1&&!bossIntroSeen_){
        bossIntroSeen_=true;
        if(scheduleAudio){bossIntroSlamAt_=state.clock+.1;bossIntroSlamPos_=pos;bossIntroRoarAt_=state.clock+.35;bossIntroRoarPos_=socketPos("mouth");}
    }
    if(bossIntroSlamAt_>=0&&state.clock>=bossIntroSlamAt_){AudioPlayOptions options;options.pos=bossIntroSlamPos_;options.volume=.55;options.pitch=.8;engine_.Play("boss_slam",options);bossIntroSlamAt_=-1;}
    if(bossIntroRoarAt_>=0&&state.clock>=bossIntroRoarAt_){AudioPlayOptions options;options.pos=bossIntroRoarPos_;engine_.Play("boss_roar",options);bossIntroRoarAt_=-1;}
    if(bossSlamAt_>=0&&state.clock>=bossSlamAt_){AudioPlayOptions options;options.pos=bossSlamPos_;engine_.Play("boss_slam",options);bossSlamAt_=-1;}
    if(bossChargeRoarAt_>=0&&state.clock>=bossChargeRoarAt_){AudioPlayOptions options;options.pos=bossChargeRoarPos_;options.volume=.45;options.pitch=1.25;engine_.Play("boss_roar",options);bossChargeRoarAt_=-1;}
    if(bossCrabletSecondAt_>=0&&state.clock>=bossCrabletSecondAt_){AudioPlayOptions options;options.pos=bossCrabletPos_;options.pitch=1.15;engine_.Play("crablet_chitter",options);bossCrabletSecondAt_=-1;}
    if (state.phase != bossPhase_) {
        if (bossPhase_ != 0 && !resumeLoops_) engine_.Play("boss_phase");
        bossPhase_ = state.phase;
        if (match.phase() == MatchPhase::Playing && !state.dead) engine_.PlayMusic(MusicTrack(state.phase), 1.0);
    }
    if (bossHp_ >= 0 && state.hp < bossHp_ - 0.01 && !state.dead && !resumeLoops_) {
        AudioPlayOptions options; options.pos = pos; options.volume = 0.65;
        engine_.Play("boss_hit", options);
    }
    bossHp_ = state.hp;
    if (state.dead && !bossDead_) {
        bossDead_ = true; StopLoops(); engine_.StopMusic(0.5); engine_.Duck(0.7, 3.0);
        if (!resumeLoops_) {
            AudioPlayOptions options; options.pos = pos; options.volume = 1;
            engine_.Play("boss_defeat", options); bossSunkIn_ = 1.35;
        } else bossSunkIn_ = 0;
    }
    if (bossSunkIn_ > 0) {
        bossSunkIn_ -= dt;
        if (bossSunkIn_ <= 0 && !resumeLoops_) { AudioPlayOptions options; options.pos = pos; engine_.Play("boss_sunk", options); }
    }

    if (const auto& move = boss->move(); move && !state.dead) {
        const char* moveName = BossMoveName(move->id);
        const auto movePhase = move->phase(state.clock);
        const char* phaseName = movePhase == BossMovePhase::Telegraph ? "tele" : movePhase == BossMovePhase::Active ? "act" : movePhase == BossMovePhase::Recovery ? "rec" : "none";
        const std::string key = std::string(moveName) + ':' + phaseName;
        if (key != bossMove_) {
            const bool suppressResumedEdges = resumeLoops_;
            bossMove_ = key;
            if (bossLoop_.size()) { engine_.StopLoop(bossLoop_, 0.2); bossLoop_.clear(); }
            if (movePhase == BossMovePhase::Telegraph) {
                if (!suppressResumedEdges) {
                    AudioPlayOptions options;
                    switch (move->id) {
                    case BossMoveId::Slam: options.pos = socketPos("clawL"); engine_.Play("boss_tele", options); break;
                    case BossMoveId::Barrage: options.pos = socketPos("shellTop"); options.pitch = 1.12; engine_.Play("boss_tele", options); break;
                    case BossMoveId::Sweep: options.pos = socketPos("cannon"); engine_.Play("boss_cannon_charge", options); break;
                    case BossMoveId::Charge:
                        options.pos = socketPos("mouth"); options.pitch = 0.82; engine_.Play("boss_tele", options);
                        bossChargeRoarAt_=scheduleAudio?state.clock+.2:-1;bossChargeRoarPos_=socketPos("mouth");break;
                    case BossMoveId::Crablets: options.pos = socketPos("hatch"); options.pitch = 1.25; engine_.Play("boss_tele", options); break;
                    case BossMoveId::Frenzy: options.pos = socketPos("mouth"); options.volume = 0.7; options.pitch = 1.15; engine_.Play("boss_roar", options); break;
                    }
                }
            } else if (movePhase == BossMovePhase::Active) {
                if (move->id == BossMoveId::Slam) {
                    if (!suppressResumedEdges) { bossSlamAt_=state.clock+.12;bossSlamPos_=socketPos("clawL"); }
                }
                else if (move->id == BossMoveId::Barrage) {
                    if (!suppressResumedEdges) {
                        bossBarrageStartedAt_=state.clock;bossBarragePos_=socketPos("shellTop");
                        bossBarrageCount_=3+static_cast<int>(AudioRandom(audioRandomState_)*3.0);
                        bossBarrageWhistles_=bossBarrageBarrels_=0;
                    }
                } else if (move->id == BossMoveId::Crablets) {
                    if (!suppressResumedEdges) {
                        bossCrabletPos_=socketPos("hatch");AudioPlayOptions options;options.pos=bossCrabletPos_;engine_.Play("crablet_chitter",options);
                        bossCrabletSecondAt_=scheduleAudio?state.clock+.3:-1;
                    }
                }
                else {
                    const char* cue = move->id == BossMoveId::Sweep ? "boss_cannon_sweep" : move->id == BossMoveId::Charge ? "boss_gallop" : "boss_frenzy";
                    const std::string loop = move->id == BossMoveId::Sweep ? "boss:sweep" : move->id == BossMoveId::Charge ? "boss:gallop" : "boss:frenzy";
                    AudioLoopOptions options; options.pos = move->id == BossMoveId::Sweep ? socketPos("cannon") : pos; options.volume = 0.7;
                    if (engine_.StartLoop(loop, cue, options)) bossLoop_ = loop;
                }
            }
        }
        if (!bossLoop_.empty()) { AudioLoopOptions options; options.pos = bossLoop_ == "boss:sweep" ? socketPos("cannon") : bossLoop_ == "boss:dizzy" ? socketPos("eyeL") : pos; options.pitch = 1; options.volume = 0.7; engine_.UpdateLoop(bossLoop_, options); }
        if(movePhase!=BossMovePhase::Active||move->id!=BossMoveId::Barrage){bossBarrageStartedAt_=-1;bossBarrageCount_=bossBarrageWhistles_=bossBarrageBarrels_=0;}
        if(movePhase==BossMovePhase::Active&&move->id==BossMoveId::Barrage&&bossBarrageStartedAt_>=0&&scheduleAudio){
            const double elapsed=state.clock-bossBarrageStartedAt_;
            while(bossBarrageWhistles_<bossBarrageCount_&&elapsed+1e-9>=bossBarrageWhistles_*.2){
                AudioPlayOptions options;options.pos=bossBarragePos_;options.pitch=.9+AudioRandom(audioRandomState_)*.25;engine_.Play("boss_whistle",options);++bossBarrageWhistles_;
            }
            while(bossBarrageBarrels_<bossBarrageCount_&&elapsed+1e-9>=.95+bossBarrageBarrels_*.2){
                AudioPlayOptions options;options.volume=.55;
                if(bossBarrageBarrels_<static_cast<int>(move->barrels.size())){const auto& barrel=move->barrels[static_cast<std::size_t>(bossBarrageBarrels_)];options.pos={barrel.x,barrel.y,barrel.z};}
                engine_.Play("boss_barrel",options);++bossBarrageBarrels_;
            }
        }
    } else if (!bossMove_.empty()) {
        bossMove_.clear(); if (!bossLoop_.empty()) engine_.StopLoop(bossLoop_, 0.2); bossLoop_.clear();
        bossBarrageStartedAt_=-1;bossBarrageCount_=bossBarrageWhistles_=bossBarrageBarrels_=0;
    }

    if (state.stunned && bossLoop_ != "boss:dizzy") {
        if (!bossLoop_.empty()) engine_.StopLoop(bossLoop_, 0.1);
        AudioPlayOptions options; options.pos = socketPos("shellTop"); engine_.Play("boss_crash", options);
        AudioLoopOptions loop; loop.pos = socketPos("eyeL"); loop.volume = 0.4;
        if (engine_.StartLoop("boss:dizzy", "boss_dizzy", loop)) bossLoop_ = "boss:dizzy";
    } else if (!state.stunned && bossLoop_ == "boss:dizzy") { engine_.StopLoop("boss:dizzy", 0.2); bossLoop_.clear(); }

    for (const auto& crab : boss->crablets()) {
        const auto found = crablets_.find(crab.id);
        AudioPlayOptions options; options.pos = Position(crab.pos);
        if (found == crablets_.end()) { if (!resumeLoops_) engine_.Play("crablet_chitter", options); }
        else if (!found->second && crab.dead) { if (!resumeLoops_) { options.pitch = 1.1; engine_.Play("crablet_pop", options); } }
        crablets_[crab.id] = crab.dead;
    }
}

void GameAudio::StopZoneLoops(){
    for(const auto& item:zoneHums_)engine_.StopLoop("zone:"+std::to_string(item.first),.6);
    zoneHums_.clear();zoneHumAccumulator_=0;
}

void GameAudio::UpdateZones(double dt,MatchSimulation& match){
    const auto* z=match.zones();if(!z){StopZoneLoops();return;}
    const int me=match.local().team();const auto& zones=z->zones();const auto& events=z->events();
    if(zoneEventCursor_>events.size())zoneEventCursor_=0;
    for(;zoneEventCursor_<events.size();++zoneEventCursor_){const auto& e=events[zoneEventCursor_];AudioPlayOptions options;
        switch(e.type){
        case ZoneEventType::ControlChanged:
            if(e.owner>=0)engine_.Play(e.owner==me?"zone_ours":"zone_theirs");
            else if(e.previousOwner>=0)engine_.Play(e.previousOwner==me?"zone_lost":"zone_broken");break;
        case ZoneEventType::PenaltyAdded:options.volume=e.team==me?.9:.65;options.pitch=e.team==me?.94:1.06;engine_.Play("zone_penalty",options);break;
        case ZoneEventType::ObjectiveChanged:
            engine_.Play(e.final?"zone_final":"zone_shift");
            if(e.moved)for(const int id:z->activeIndices()){const auto& p=zones[id].center;options.pos={p.x,p.y+1,p.z};engine_.Play("zone_wipe",options);}break;
        case ZoneEventType::OvertimeStarted:engine_.Play("zone_overtime");break;
        case ZoneEventType::ZoneOwnerChanged:
            if(e.zone>=0&&e.zone<int(zones.size())&&e.owner>=0){const auto& p=zones[e.zone].center;options.pos={p.x,p.y+1,p.z};options.pitch=e.owner==me?1.06:.9;engine_.Play("zone_flood",options);}break;
        case ZoneEventType::Contested:
            if(z->time()-zoneWarnAt_>=1.5){engine_.Play(e.owner==me?"zone_warn":"zone_chance");zoneWarnAt_=z->time();}break;
        default:break;
        }
    }
    if(match.phase()!=MatchPhase::Playing||match.paused||z->ended()){StopZoneLoops();return;}
    for(int team=0;team<2;++team){const int total=int(std::ceil(z->counts()[team]+z->penalty()[team]));
        if(total!=zoneTicks_[team]&&total<=10&&total>0){AudioPlayOptions o;o.volume=team==me?.55:.4;o.pitch=team==me?1.12:.9;engine_.Play("zone_tick",o);}zoneTicks_[team]=total;
    }
    zoneHumAccumulator_+=dt;if(zoneHumAccumulator_<1./6)return;const double step=std::min(1.,zoneHumAccumulator_);zoneHumAccumulator_=0;
    const auto& active=z->activeIndices();
    for(auto it=zoneHums_.begin();it!=zoneHums_.end();){if(std::find(active.begin(),active.end(),it->first)==active.end()){engine_.StopLoop("zone:"+std::to_string(it->first),.6);it=zoneHums_.erase(it);}else++it;}
    for(const int id:active){const auto& zone=zones[id];auto& tension=zoneHums_[id];
        const double target=zone.owner>=0?(zone.share[1-zone.owner]-.08)/(.4-.08):(std::max(zone.share[0],zone.share[1])-.35)/(.8-.35);
        tension+=(std::clamp(target,0.,1.)-tension)*std::min(1.,step*4);
        AudioLoopOptions o;o.pos={zone.center.x,zone.center.y+1,zone.center.z};o.volume=(zone.owner<0?.8:.95)*(1+.6*tension);
        o.zoneHum=AudioLoopOptions::ZoneHumParameters{zone.owner<0?0:zone.owner==me?1:2,tension};
        engine_.StartLoop("zone:"+std::to_string(id),"zone_hum",o);
    }
}

void GameAudio::Update(double dt, MatchSimulation& match, const std::vector<WorldVisualEvent>& events,
    AudioPosition listenerPos, AudioPosition forward, AudioPosition up) {
    if (!engine_.ready()) return;
    engine_.SetListener(listenerPos, forward, up); engine_.Update();
    if (match_ != &match) BeginMatch(match);
    listenerPosition_ = listenerPos; hasListenerPosition_ = true;
    if (pausedAudio_) {
        pausedAudio_ = false; resumeLoops_ = true; bossMove_.clear();
    }
    UpdateWorldAudio(dt, match);
    UpdateDeviceAudio(match);
    UpdateZones(dt,match);

    const auto phase = match.phase();
    const int phaseValue = static_cast<int>(phase);
    const bool enteredPhase = phaseValue != matchPhase_;
    if ((phase == MatchPhase::Finish || phase == MatchPhase::Results) && enteredPhase) ClearBossPendingAudio();
    if (phase == MatchPhase::Playing && !playing_) {
        playing_ = true;
        if (!match.boss()) engine_.PlayMusic("battle", 0.75);
        else engine_.PlayMusic(MusicTrack(match.boss()->state().phase), 0.75);
        engine_.Play("go_horn");
    }
    if (phase == MatchPhase::Playing && !match.boss() && previousRemaining_ > 60 && match.remaining() <= 60) {
        engine_.Play("one_minute"); engine_.PlayMusic("battle_final", 0.8);
    }
    if (phase == MatchPhase::Finish && enteredPhase) {
        if (!match.boss() || !match.boss()->state().dead) engine_.StopMusic(0.7);
        for (const auto* key : {"boss:sweep", "boss:gallop", "boss:frenzy", "boss:dizzy"}) engine_.StopLoop(key, 0.25);
        if (!match.boss() && previousRemaining_ > 0 && match.remaining() <= 0) engine_.Play("times_up");
    }
    if (phase == MatchPhase::Results && enteredPhase) {
        if (match.result()) {
            const bool won = match.result()->winner == match.local().team();
            engine_.Play(won ? "victory_fanfare" : "defeat_jingle");
            engine_.PlayMusic(won ? "results_win" : "results_lose", 0.7);
        }
    }

    std::unordered_set<const PlayerSimulation*> live;
    for (const auto& owned : match.actors()) {
        auto* actor = owned.get(); live.insert(actor);
        UpdateActorAudio(*actor);
    }
    for (auto it = actorLoops_.begin(); it != actorLoops_.end();) {
        if (live.find(it->first) == live.end()) { engine_.StopLoop(it->second.key, 0.15); it = actorLoops_.erase(it); }
        else ++it;
    }
    for (auto it = weaponLoops_.begin(); it != weaponLoops_.end();) {
        if (live.find(it->first) == live.end()) { engine_.StopLoop(it->second.key, 0.15); it = weaponLoops_.erase(it); }
        else ++it;
    }
    for (auto it = actorStates_.begin(); it != actorStates_.end();) {
        if (live.find(it->first) == live.end()) it = actorStates_.erase(it);
        else ++it;
    }

    for (const auto& event : events) {
        AudioPlayOptions options; options.pos = Position(event.pos);
        if (event.kind == "device_arm") {
            const char* cue = nullptr;
            if (event.source == "mine") { cue = "bomb_beep"; options.volume = 0.5; options.pitch = 0.8; }
            else if (event.source == "beacon") { cue = "bomb_beep"; options.volume = 0.5; options.pitch = 1.3; }
            else if (event.source == "sticky") { cue = "bomb_beep"; options.volume = 0.55; }
            else if (event.source == "sprinkler") { cue = "bomb_beep"; options.volume = 0.55; options.pitch = 1.4; }
            else if (event.source == "shaker") { cue = "bomb_beep"; options.volume = 0.55; options.pitch = 1.25; }
            else if (event.source == "scan") { cue = "special_activate"; options.volume = 0.45; options.pitch = 1.6; }
            else if (event.source == "mist") { cue = "enemy_ink_sizzle"; options.volume = 0.7; options.pitch = 0.7; }
            else if (event.source == "curtain") { cue = "swim_splash"; options.volume = 0.8; options.pitch = 0.8; }
            if (cue) engine_.Play(cue, options);
        }
        else if (event.kind == "device_warning") {
            const double k = std::clamp(event.charge, 0.0, 1.0);
            if (event.source == "mine") { options.volume = 0.8; options.pitch = 1.5; engine_.Play("bomb_beep", options); }
            else if (event.source == "sticky") {
                options.volume = 0.3 + 0.4 * k; options.pitch = 1.0 + 0.3 * k;
                engine_.Play("bomb_beep", options);
            } else if (event.source == "waddle") {
                options.volume = 0.3 + 0.35 * k; options.pitch = 1.05 + 0.3 * k;
                engine_.Play("bomb_beep", options);
            } else if (event.source == "boomerang") {
                options.volume = 0.5 + 0.4 * k; options.pitch = 1.1 + 0.4 * k;
                engine_.Play("bomb_beep", options);
            }
        }
        else if (event.kind == "special_warning" && event.source == "booyah") {
            options.volume = 1.0; options.pitch = 0.6; engine_.Play("bomb_beep", options);
        }
        else if (event.kind == "special_audio") {
            if (event.source == "wail_blast_start" && event.id) {
                wailLoops_.insert_or_assign(event.id, Position(event.pos));
            }
            else if (event.source == "wail_blast_stop" && event.id) {
                engine_.StopLoop("wail:" + std::to_string(event.id), 0.3);
                wailLoops_.erase(event.id);
            }
            else if (event.source == "shaker_fizzle") {
                const bool nearActor = event.sourceLocal || !hasListenerPosition_ ||
                    DistanceSquared(Position(event.pos), listenerPosition_) <= 30.0 * 30.0;
                if (nearActor) {
                    if (event.sourceLocal) options.pos.reset();
                    options.volume = event.sourceLocal ? 0.8 : 0.6;
                    engine_.Play("shaker_fizzle", options);
                }
            }
            else if (event.source == "shaker_clink" || event.source == "shaker_clink_max") {
                const bool nearActor = event.sourceLocal || !hasListenerPosition_ ||
                    DistanceSquared(Position(event.pos), listenerPosition_) <= 30.0 * 30.0;
                if (nearActor && !resumeLoops_) {
                    if (event.sourceLocal) options.pos.reset();
                    options.volume = event.sourceLocal ? 1.0 : 0.75;
                    options.pitch = event.source == "shaker_clink_max" ? 1.0 : 0.94;
                    engine_.Play(event.source, options);
                }
            }
            else if (event.source == "bomb_throw") {
                options.volume = 0.65; options.pitch = event.charge > 0.0 ? event.charge : 1.0;
                engine_.Play("bomb_throw", options);
            }
            else if (event.source == "sonar_ping") { options.volume = 0.9; engine_.Play("sonar_ping", options); }
            else if (event.source == "sonar_mark") { options.volume = 0.8; engine_.Play("sonar_mark", options); }
            else if (event.source == "strike_whistle") { options.volume = 0.9; engine_.Play("strike_whistle", options); }
            else if (event.source == "zip_latch") { options.volume = 0.8; engine_.Play("zip_latch", options); }
            else if (event.source == "zip_pull") { options.volume = 0.7; options.pitch = 0.8; engine_.Play("zip_pull", options); }
            else if (event.source == "shield_hit") { options.volume = 0.6; engine_.Play("shield_hit", options); }
            else if (event.source == "shield_pop") { options.volume = 0.6; engine_.Play("shield_pop", options); }
            else if (event.source == "crab_hit") { options.volume = 0.5; engine_.Play("crab_hit", options); }
            else if (event.source == "crab_break") { options.volume = 0.8; engine_.Play("crab_break", options); }
            else if (event.source == "booyah_cheer") { options.volume = 0.7; engine_.Play("booyah_cheer", options); }
            else if (event.source == "jet_end") { options.volume = 0.7; engine_.Play("jet_end", options); }
        }
        else if (event.kind == "device_hit" && event.source == "tracer") {
            const bool audible = event.sourceLocal || event.sourceVictimLocal || !hasListenerPosition_ ||
                DistanceSquared(Position(event.pos), listenerPosition_) <= 30.0 * 30.0;
            if (audible) {
                if (event.sourceVictimLocal) options.pos.reset();
                options.volume = 0.9; engine_.Play("tracer_hit", options);
            }
        }
        else if (event.kind == "device_hit" && event.source == "tracer_trail") {
            const bool audible = event.sourceLocal || event.sourceVictimLocal;
            if (audible) {
                if (event.sourceVictimLocal) options.pos.reset();
                options.volume = 0.55; options.pitch = 1.3; engine_.Play("tracer_hit", options);
            }
        }
        else if (event.kind == "device_destroy") {
            if (event.source == "torpedo") {
                options.volume = 0.8; options.pitch = 0.75; engine_.Play("splat_small", options);
                options.volume = 0.45; options.pitch = 1.6; engine_.Play("torpedo_pop", options);
            } else if (event.source == "waddle") {
                if (event.charge >= 0.5) { options.volume = 0.8; options.pitch = 1.3; }
                else { options.volume = 0.5; options.pitch = 0.7; }
                engine_.Play("splat_small", options);
            } else if (event.source == "beacon" || event.source == "mine") {
                options.volume = 0.7; options.pitch = 0.8; engine_.Play("splat_small", options);
            } else {
                // Other source subweapons use the shared destructible-device cue.
                options.volume = 0.7; options.pitch = 0.8; engine_.Play("splat_small", options);
            }
        }
        else if (event.kind == "bubble_pop" && event.source == "blower") {
            options.volume = 0.7; engine_.Play("bubble_pop", options);
        }
        else if (event.kind == "explosion") {
            const double genericVolume = std::clamp(0.45 + event.radius * 0.08, 0.5, 1.0);
            options.volume = genericVolume;
            if (event.source == "jetpack") {
                // The source plays jet_fire when the shot is launched; it has no second blast cue.
            } else if (event.source == "burst") {
                options.volume = 0.6; options.pitch = 1.2; engine_.Play("blaster_boom", options);
            } else if (event.source == "boomerang") {
                if (event.charge >= 0.5) engine_.Play("bomb_explode", options);
                else engine_.Play("boomerang_burst", options);
            } else if (event.source == "booyah") {
                options.volume = 1.0; engine_.Play("booyah_blast", options);
            } else if (event.source == "strike") {
                options.volume = 1.0; engine_.Play("strike_impact", options);
            } else if (event.source == "stamp") {
                options.volume = 0.85; engine_.Play("stamp_slam", options);
            } else if (event.source == "kraken") {
                options.volume = 0.9; engine_.Play("kraken_slam", options);
            } else if (event.source == "blower") {
                options.volume = 1.0; engine_.Play("bubble_blast", options);
            } else if (event.source == "zipcaster") {
                options.volume = 0.6; options.pitch = 1.25; engine_.Play("blaster_boom", options);
            } else if (event.source == "shaker") {
                options.volume = 0.62; options.pitch = 1.1 + 0.06 * std::max(0.0, event.charge);
                engine_.Play("bomb_explode", options);
            } else if (event.source == "torpedo") {
                const bool full = event.charge >= 0.5;
                options.volume = full ? 0.75 : 0.6; options.pitch = full ? 1.08 : 1.15;
                engine_.Play("bomb_explode", options);
                options.volume = full ? 0.9 : 0.6; options.pitch = full ? 1.0 : 1.12;
                engine_.Play("torpedo_pop", options);
            } else if (event.source == "crab") {
                // Crab cannon shells have a launch cue but no source impact one-shot.
            } else {
                engine_.Play("bomb_explode", options);
            }
        }
        else if (event.kind == "bulletImpact") { options.volume = 0.35; engine_.Play("ink_hit_wall", options); }
        else if (event.kind == "beam") {
            // ActorTrigger handles the source weapon/special shot at the firing edge; beam geometry has no owner ID.
            // Keep it spatially silent here to avoid playing a Wail sound for charger beams.
        }
        else if (event.kind == "slamWave") { options.volume = 0.8; engine_.Play("special_slam", options); }
        else if (event.kind == "stormStart") { options.volume = 0.65; engine_.Play("storm_thunder", options); }
        else if (event.kind == "burst") { options.volume = 0.55; engine_.Play("splat_big", options); }
    }
    UpdateWailAudio(match);
    UpdateBoss(dt, match);
    resumeLoops_ = false;
    matchPhase_ = phaseValue;
    previousRemaining_ = match.remaining();
}

} // namespace restoration
