#pragma once
#include "InkStream.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace restoration {
class CombatWorld;
struct ShooterProjectile;

struct ProjectileVisualLook {
    double size=0,tail0=1,tailK=1.2,wobble=.04,frequency=20,nose=0;
    int satellites=0;
};
struct ProjectileSnapshot {
    std::uint64_t id=0;
    glm::dvec3 pos{0},vel{0},start{0},flightDir{0,0,1},hitPoint{0},hitNormal{0,1,0};
    double age=0,life=1.2,delay=0,seed=0,distance=0,fuse=0,size=.15,straight=.13,gravity=28,drag=.8;
    int team=0,tier=0;
    std::string kind="shot",weaponId="shooter",state;
    ProjectileVisualLook look;
};
struct ProjectileChargerSight {
    glm::dvec3 muzzle{0},direction{0,0,1};
    double range=0,charge=0;
    int team=0;
};
// Owned render inputs. Contains no simulation actors or owner pointers.
struct ProjectileFrame {
    double time=0;
    std::vector<ProjectileSnapshot> projectiles;
    std::vector<InkStreamEmitter> streamEmitters;
    std::vector<ProjectileChargerSight> chargerSights;
};

ProjectileSnapshot SnapshotProjectile(const ShooterProjectile& projectile);
ProjectileFrame BuildProjectileFrame(const CombatWorld& world);
}
