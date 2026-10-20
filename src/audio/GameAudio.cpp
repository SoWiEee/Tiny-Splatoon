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
    if (sub == "seeker") return "torpedo_throw";
    if (sub == "tracer") return "tracer_hit";
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
AudioPosition BossPosition(const BossSimulation& boss) {
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
    const double amount = event.amount.value_or(1.0);
    if (event.name == "spawn") {
        auto& state = actorStates_[&actor];
        if (state.alive == false && actor.deaths() > 0) Play("respawn", &actor, actor.isLocal() ? 0.85 : 0.55);
        state.alive = true;
        return;
    }
    if (event.name == "jump" || event.name == "special_leap") { Play(event.name == "jump" ? "jump" : "super_jump", &actor, actor.isLocal() ? 0.9 : 0.6); return; }
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
    if (event.name == "throw") { Play(SubThrowSound(actor), &actor); return; }
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
    match_ = &match; playing_ = false; bossPhase_ = 0; matchPhase_ = -1; bossHp_ = -1; bossDead_ = false;
    bossMove_.clear(); crablets_.clear(); bossSunkIn_ = 0;
    actorLoops_.clear(); actorStates_.clear();
    pausedAudio_ = false; resumeLoops_ = false; hasListenerPosition_ = false;
    gullTimer_ = 4.0; gustTimer_ = 2.0; clinkTimer_ = 0.0; pendingHalyardClinks_ = 0;
    hornTimer_ = 28.0 + 10.0 * AudioRandom(audioRandomState_);
    previousRemaining_ = match.remaining();
    engine_.Play("ready");
    engine_.PlayMusic("menu", 0.4);
    if (match.boss()) Play("boss_title");
}

void GameAudio::StopLoops() {
    for (const auto& [actor, loop] : actorLoops_) engine_.StopLoop(loop.key, 0.2);
    actorLoops_.clear();
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
    StopLoops();
}

void GameAudio::EndMatch() {
    StopLoops(); StopWorldLoops(); engine_.StopMusic(0.6);
    match_ = nullptr; playing_ = false; bossMove_.clear(); crablets_.clear(); actorStates_.clear();
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
    if (inserted) {
        previous.alive = actor.state.alive;
        previous.squid = actor.state.squid;
        previous.lowInk = lowInk;
        previous.specialReady = specialReady;
        previous.specialActive = pose.active;
        previous.specialKind = pose.kind;
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
        previous.alive = actor.state.alive;
        previous.squid = actor.state.squid;
        previous.lowInk = lowInk;
        previous.specialReady = specialReady;
        previous.specialActive = pose.active;
        previous.specialKind = pose.kind;
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
    if (desired.empty() && actor.state.alive && actor.weaponProfile().kind == "spinner" && actor.combatPose().streaming) desired = "splatling_spin";
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
                    const char* cue = move->id == BossMoveId::Sweep ? "boss_cannon_charge" : "boss_tele";
                    AudioPlayOptions options; options.pos = pos;
                    engine_.Play(cue, options);
                    if (move->id == BossMoveId::Charge) { options.volume = 0.45; options.pitch = 1.2; engine_.Play("boss_roar", options); }
                }
            } else if (movePhase == BossMovePhase::Active) {
                if (move->id == BossMoveId::Slam) {
                    if (!suppressResumedEdges) { AudioPlayOptions options; options.pos = pos; engine_.Play("boss_slam", options); }
                }
                else if (move->id == BossMoveId::Barrage) {
                    if (!suppressResumedEdges) {
                        AudioPlayOptions options; options.pos = pos; options.volume = 0.7; engine_.Play("boss_whistle", options);
                        for (const auto& barrel : move->barrels) { options.pos = {barrel.x, barrel.y, barrel.z}; options.volume = 0.55; engine_.Play("boss_barrel", options); }
                    }
                } else if (move->id == BossMoveId::Crablets) {
                    if (!suppressResumedEdges) { AudioPlayOptions options; options.pos = pos; engine_.Play("crablet_chitter", options); }
                }
                else {
                    const char* cue = move->id == BossMoveId::Sweep ? "boss_cannon_sweep" : move->id == BossMoveId::Charge ? "boss_gallop" : "boss_frenzy";
                    const std::string loop = move->id == BossMoveId::Sweep ? "boss:sweep" : move->id == BossMoveId::Charge ? "boss:gallop" : "boss:frenzy";
                    AudioLoopOptions options; options.pos = pos; options.volume = 0.7;
                    if (engine_.StartLoop(loop, cue, options)) bossLoop_ = loop;
                }
            }
        }
        if (!bossLoop_.empty()) { AudioLoopOptions options; options.pos = pos; options.pitch = 1; options.volume = 0.7; engine_.UpdateLoop(bossLoop_, options); }
    } else if (!bossMove_.empty()) {
        bossMove_.clear(); if (!bossLoop_.empty()) engine_.StopLoop(bossLoop_, 0.2); bossLoop_.clear();
    }

    if (state.stunned && bossLoop_ != "boss:dizzy") {
        if (!bossLoop_.empty()) engine_.StopLoop(bossLoop_, 0.1);
        AudioPlayOptions options; options.pos = pos; engine_.Play("boss_crash", options);
        AudioLoopOptions loop; loop.pos = pos; loop.volume = 0.4;
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

    const auto phase = match.phase();
    const int phaseValue = static_cast<int>(phase);
    const bool enteredPhase = phaseValue != matchPhase_;
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
        if (live.find(it->first) == live.end()) { engine_.StopLoop(it->second.key, 0.15); actorStates_.erase(it->first); it = actorLoops_.erase(it); }
        else ++it;
    }

    for (const auto& event : events) {
        AudioPlayOptions options; options.pos = Position(event.pos);
        if (event.kind == "explosion") { options.volume = std::clamp(0.45 + event.radius * 0.08, 0.5, 1.0); engine_.Play("bomb_explode", options); }
        else if (event.kind == "bulletImpact") { options.volume = 0.35; engine_.Play("ink_hit_wall", options); }
        else if (event.kind == "beam") {
            // ActorTrigger handles the source weapon/special shot at the firing edge; beam geometry has no owner ID.
            // Keep it spatially silent here to avoid playing a Wail sound for charger beams.
        }
        else if (event.kind == "slamWave") { options.volume = 0.8; engine_.Play("special_slam", options); }
        else if (event.kind == "stormStart") { options.volume = 0.65; engine_.Play("storm_thunder", options); }
        else if (event.kind == "burst") { options.volume = 0.55; engine_.Play("splat_big", options); }
    }
    UpdateBoss(dt, match);
    resumeLoops_ = false;
    matchPhase_ = phaseValue;
    previousRemaining_ = match.remaining();
}

} // namespace restoration
