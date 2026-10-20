#pragma once

#include "StageGeometry.hpp"
#include "FacePaint.hpp"
#include "WorldSpecials.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <vector>

namespace restoration {
class PlayerSimulation;
class NavGraph;
class WorldSpecials;
class BossSimulation;

struct ShooterProjectile {
    std::uint64_t id = 0;
    glm::dvec3 pos{0.0}, vel{0.0};
    double age = 0.0;
    PlayerSimulation* owner = nullptr;
    glm::dvec3 start{0.0};
    std::string kind = "shot", weaponId = "shooter";
    double life = 1.2, straight = 0.13, gravity = 28.0, drag = 0.8;
    double radius = 0.85, damage = 36.0, damageFar = 36.0, size = 0.15;
    double trail = -1.45, trailEvery = 1.05, trailRadius = 0.44, seed = 0.0;
    double belowFalloff = 0.0, belowFalloffMax = 0.0;
    double distance = 0.0, range = 0.0, fuse = 0.0, diveGravity = 0.0, stickPaint = 0.0, delay = 0.0;
    double burstRadius = 0.0, burstDamage = 0.0, burstEdge = 0.0, burstPaint = 0.0, burstInner = 0.0;
    std::uint64_t volley = 0;
    int victimHitLimit = 1, tier = 0;
    bool special = false, lodge = false, noHit = false, centerArrow = false;
    glm::dvec3 hitPoint{0.0}, hitNormal{0.0,1.0,0.0}, flightDir{0.0,0.0,1.0};
    std::string state;
    std::vector<PlayerSimulation*> tagged;
};

struct CombatDevice {
    std::uint64_t id = 0;
    PlayerSimulation* owner = nullptr;
    std::string kind;
    int team = 0;
    glm::dvec3 pos{0.0}, vel{0.0}, normal{0.0,1.0,0.0};
    double hp = 0.0, radius = 0.0, age = 0.0, timer = 0.0;
    bool shield = false, grounded = false;
    bool live = true, held = false, blocksActors = false, shootable = false, locked = false, exploding = false;
    std::string state;
    PlayerSimulation* target = nullptr;
    glm::dvec3 C{0.0}, N{0.0,0.0,1.0};
    double R = 0.0, left = 0.0, planeOff = 0.0, halfW = 0.0, speed = 0.0;
    double aimY = 0.0, senseRadius = 0.0, lockRange = 0.0, trigger = 0.0, commitDist = 0.0, creep = 0.0;
    double fuse = -1.0, pulse = 0.0, spin = 0.0, trail = 0.0, heading = 0.0, stuck = 0.0;
    double walkTime = 0.0, travel = 0.0, lostTime = 0.0;
    double repath = 0.0, progressTime = 0.0, progressX = 0.0, progressZ = 0.0;
    double hopTime = 0.0, hopDuration = 0.5;
    double paintAcc = 0.0, h0 = 0.0;
    double hpMax = 0.0;
    bool hopping = false, falling = false, dash = false;
    glm::dvec3 hopStart{0.0}, hopEnd{0.0};
    glm::dvec3 previousC{0.0}, previousN{0.0,0.0,1.0};
    bool hasPreviousShield = false;
    std::vector<int> path;
    std::size_t pathIndex = 0;
    std::vector<std::pair<glm::dvec3,double>> trailPoints;
    int uses = 0;
    bool special = false;
    std::vector<PlayerSimulation*> tagged;
};
struct CombatThreat {
    std::uint64_t id = 0;
    std::string kind;
    int team = 0;
    glm::dvec3 pos{0.0}, vel{0.0};
    double radius = 0.0, timeToImpact = 0.0;
    bool live = true, ground = false, locked = false, shootable = false;
    std::string state;
    PlayerSimulation* target = nullptr;
    double aimY = 0.0, senseRadius = 0.0, lockRange = 0.0, trigger = 0.0, speed = 0.0, left = 0.0;
};

// Read-only logical shield state for combat queries and bot decisions.
struct CombatShield {
    std::uint64_t id = 0;
    std::string kind = "brolly";
    int team = 0;
    PlayerSimulation* owner = nullptr;
    glm::dvec3 C{0.0}, N{0.0,0.0,1.0};
    glm::dvec3 pos{0.0};
    double R = 0.0, hp = 0.0, hpMax = 0.0, left = 0.0;
    double halfW = 0.0, planeOff = 0.0, speed = 0.0;
    bool held = false, live = false, blocksActors = false;
};

struct WorldVisualEvent {
    std::string kind;
    glm::dvec3 pos{0.0}, normal{0.0,1.0,0.0};
    double radius = 0.0;
    int team = 0;
};

// Match owns actors; this owns attacks and applies hit/paint/credit exactly once.
class CombatWorld {
public:
    CombatWorld(const StageData& stage, const StageGeometry& geometry, FacePaint& paint);
    void addActor(PlayerSimulation& actor);
    void setNavigation(const NavGraph& nav) { nav_ = &nav; }
    void setBoss(BossSimulation* boss) { boss_ = boss; }
    void removeActor(PlayerSimulation& actor);
    void cleanup(PlayerSimulation& actor);
    void clear();
    void tick(double dt);
    void launch(ShooterProjectile projectile);
    std::uint64_t allocateId() { return ++serial_; }
    void launchCanopy(PlayerSimulation& owner, double hp, glm::dvec3 direction, double heldHeight);
    bool canDeploy(const PlayerSimulation& owner, const std::string& id) const;
    void deploy(PlayerSimulation& owner, const std::string& id, bool special = false, double charge = 0.0);
    bool blockShot(glm::dvec3 from, glm::dvec3 to, int team, double damage, PlayerSimulation* owner = nullptr);
    double blockRay(glm::dvec3 from, glm::dvec3 dir, double length, int team, double damage, PlayerSimulation* owner = nullptr);
    void blockActor(PlayerSimulation& actor);
    void damageDevices(glm::dvec3 at, double radius, double damage, int team);
    bool beaconAnchor(int team, std::uint64_t id, glm::dvec3& destination) const;
    bool consumeBeacon(PlayerSimulation& actor, std::uint64_t id);
    bool applyHit(PlayerSimulation& attacker, PlayerSimulation& victim, double damage, const std::string& weaponId);
    double paint(PlayerSimulation& owner, glm::dvec3 at, double radius, double seed,
                 const SplatOptions& options = {}, bool special = false);
    void burst(PlayerSimulation& owner, glm::dvec3 at, double radius, double maxDamage,
               double minDamage, double paintRadius, const std::string& weaponId,
               PlayerSimulation* direct = nullptr, bool special = false, bool bossDirect = false);
    void beam(PlayerSimulation& owner, glm::dvec3 from, glm::dvec3 dir, double length,
              double radius, double damage, double paintRadius, bool pierce, const std::string& weaponId);
    const std::vector<ShooterProjectile>& projectiles() const { return projectiles_; }
    const std::vector<CombatDevice>& devices() const { return devices_; }
    const std::vector<PlayerSimulation*>& actors() const { return actors_; }
    std::vector<CombatThreat> threats() const;
    std::vector<CombatShield> shields() const;
    std::size_t attackCount(const PlayerSimulation& actor) const;
    std::uint64_t newVolley() { return ++serial_; }
    const StageGeometry& geometry() const { return geometry_; }
    double time() const { return time_; }
    std::vector<WorldObjectView> visualObjects() const { return specials_.visualObjects(); }
    std::vector<WorldVisualEvent> takeVisualEvents();
    void emitVisual(WorldVisualEvent event);
private:
    friend class WorldSpecials;
    friend class PlayerSimulation;
    friend class WeaponRunner;
    void updateCanopies(double dt);
    void tickArrow(ShooterProjectile& arrow, double dt);
    void tickBladeWave(ShooterProjectile& wave, double dt);
    void tickFist(ShooterProjectile& fist, double dt);
    void burstFist(ShooterProjectile& fist, glm::dvec3 at, PlayerSimulation* direct, const glm::dvec3* normal = nullptr);
    void lodgeArrow(ShooterProjectile& arrow, glm::dvec3 point, glm::dvec3 normal, glm::dvec3 direction);
    void burstArrow(ShooterProjectile& arrow);
    bool blockCanopyShot(glm::dvec3 from, glm::dvec3 to, int team, double damage, const ShooterProjectile* projectile = nullptr, double* hitDistance = nullptr);
    void damageCanopies(glm::dvec3 at, double radius, int team);
    void guardActorsWithCanopies(glm::dvec3 at, double radius, int team);
    void updateDevices(double dt);
    void explodeDevice(CombatDevice& device, PlayerSimulation* direct = nullptr);
    void paintDevice(CombatDevice& device, glm::dvec3 at, double radius);
    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    WorldSpecials specials_;
    const NavGraph* nav_ = nullptr;
    BossSimulation* boss_ = nullptr;
    std::vector<PlayerSimulation*> actors_;
    std::vector<ShooterProjectile> projectiles_;
    std::vector<ShooterProjectile> pendingProjectiles_;
    std::vector<CombatDevice> devices_;
    struct VolleyHit { std::uint64_t volley; PlayerSimulation* victim; };
    std::vector<VolleyHit> volleyHits_;
    struct VolleyBossHit { std::uint64_t volley; int target; };
    std::vector<VolleyBossHit> volleyBossHits_;
    std::uint64_t serial_ = 0;
    double time_ = 0.0;
    bool ticking_ = false;
    std::vector<WorldVisualEvent> visualEvents_;
};
} // namespace restoration
