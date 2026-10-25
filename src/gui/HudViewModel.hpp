#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include "../game/WeaponRunner.hpp"

namespace restoration {
class MatchSimulation;

enum class HudMatchPhase { Intro, Playing, Finish, Judge, Results };
enum class HudEventKind { Hit, Splat, Denied };
enum class HudBossMove { Slam, Barrage, Sweep, Charge, Crablets, Frenzy };
enum class HudBossMovePhase { None, Telegraph, Active, Recovery };
enum class HudZoneEventType { OwnerChanged, ControlChanged, PenaltyAdded, ObjectiveChanged, Contested, FinalStarted, OvertimeStarted, MatchEnded };

struct HudActor {
    std::string name,mainId,subId,specialId,weaponKind;
    int team=0,slot=0;
    bool local=false,bot=false,active=true,alive=true,squid=false,grounded=false,climbing=false,canRespawn=false;
    bool subAim=false,specialActive=false,specialBarrage=false,specialMapTarget=false;
    CombatPose combatPose{};
    glm::dvec3 pos{0},visualPos{0},velocity{0};
    double ink=0,specialCharge=0,respawnTimer=0,invulnerability=0,aimYaw=0;
    double turf=0,zoneTurf=0,spreadDegrees=0,streamDuration=0,specialCost=190,subCost=70;
    int shotsFired=0,splats=0,deaths=0;
};

struct HudCombatEvent {
    std::uint64_t serial=0;
    double time=0,damage=0;
    HudEventKind kind=HudEventKind::Hit;
    int attacker=-1,victim=-1,attackerTeam=-1,victimTeam=-1,victimTrackedTeam=-1;
    std::string attackerWeapon,victimWeapon,source;
    glm::dvec3 attackerPos{0},victimPos{0};
};

struct HudBossHit {
    std::uint64_t serial=0;
    double time=0,damage=0;
    int attackerSlot=-1;
    glm::dvec3 point{0};
    bool weak=false;
};

struct HudBoss {
    glm::dvec3 pos{0},moveAnchor{0};
    double clock=0,hp=0,maxHp=0,moveT0=0,moveYaw=0;
    int phase=1,anim=0;
    HudBossMove moveId=HudBossMove::Slam;
    HudBossMovePhase movePhase=HudBossMovePhase::None;
    bool dead=false,stunned=false,hasMove=false;
    std::array<double,8> squadDamage{};
    std::array<int,8> squadWeakHits{},squadSplats{};
    std::vector<HudBossHit> hits;
};

struct HudZone {
    std::string kind;
    int home=-1,owner=-1;
    std::array<double,2> share{};
};

struct HudZoneEvent {
    HudZoneEventType type=HudZoneEventType::ControlChanged;
    int owner=-1,team=-1;
    std::string objective,reason;
    double penalty=0;
    bool final=false,moved=false;
};

struct HudZones {
    int owner=-1;
    std::array<double,2> counts{},penalty{};
    std::string objective;
    int objectiveHome=-1;
    double nextSwap=0,neutralTime=0;
    bool overtime=false,final=false,ended=false;
    std::vector<int> activeIndices;
    std::vector<HudZone> zones;
    std::vector<HudZoneEvent> events;
};

struct HudBossResult { bool win=false; double time=0,hp=0,maxHp=0; int phase=1; };
struct HudZoneResult { std::array<double,2> counts{},penalty{}; std::string reason; };
struct HudResult {
    std::array<double,2> coverage{};
    int winner=0;
    std::optional<HudBossResult> boss;
    std::optional<HudZoneResult> zones;
};

// Owned frame snapshot. Consumers receive it as const and never retain simulation objects.
struct HudViewModel {
    std::uint64_t matchId=0,eventEpoch=0;
    std::string stageId,stageName;
    HudMatchPhase phase=HudMatchPhase::Intro;
    double worldTime=0,remaining=0;
    int localTeam=0,localActor=-1;
    bool networkAuthority=true;
    std::vector<HudActor> actors;
    std::vector<HudCombatEvent> events;
    std::optional<HudBoss> boss;
    std::optional<HudZones> zones;
    std::optional<HudResult> result;
};

HudViewModel BuildHudViewModel(const MatchSimulation& match);
}
