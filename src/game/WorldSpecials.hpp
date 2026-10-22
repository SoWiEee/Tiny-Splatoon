#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace restoration {
class CombatWorld;
class PlayerSimulation;
struct EquipmentData;

struct SpecialPose {
    bool active = false, body = false, casting = false, thrown = false, noSquid = false, aimFace = false;
    bool inkProof = false, faceMove = false, mapTarget = false, jumpBack = false;
    bool roll = false, raise = false;
    double elapsed = 0.0, remaining = 0.0, moveSpeed = -1.0, accel = -1.0;
    double charge = 0.0, hitPoints = 0.0, hitRadius = -1.0, hitHeight = -1.0, hang = 0.0, firing = 0.0, seat = 0.0, guard = 0.0;
    std::optional<double> faceYaw;
    glm::dvec3 target{0.0}, origin{0.0};
    std::string kind, phase;
};

// Presentation copy; simulation ownership and mutable object state stay private.
struct WorldObjectView {
    std::string kind, phase;
    int team = 0;
    glm::dvec3 pos{0.0}, previous{0.0}, target{0.0}, mouth{0.0}, vel{0.0}, direction{0.0,0.0,1.0};
    double age = 0.0, timer = 0.0, radius = 0.0, range = 0.0, charge = 0.0;
    bool held = false;
};

class WorldSpecials {
public:
    explicit WorldSpecials(CombatWorld& world);

    bool activate(PlayerSimulation& actor, std::string_view id);
    void cheer(PlayerSimulation& actor);
    bool body(PlayerSimulation& actor, double dt);
    void move(PlayerSimulation& actor, double dt);
    bool weapon(PlayerSimulation& actor, double dt, bool fire, bool firePressed,
                bool& sub, bool& subReleased);
    void tickActor(PlayerSimulation& actor, double dt);
    void tickWorld(double dt);
    bool shotHit(glm::dvec3 from, glm::dvec3 to, int team, double damage, PlayerSimulation* owner);
    double rayHit(glm::dvec3 from, glm::dvec3 dir, double length, int team, double damage, PlayerSimulation* owner);
    void areaHit(glm::dvec3 center, double radius, double damage, int team, PlayerSimulation* owner);
    double filterDamage(PlayerSimulation& victim, PlayerSimulation* attacker,
                        double amount, std::string_view source);
    SpecialPose pose(const PlayerSimulation& actor) const;
    std::vector<WorldObjectView> visualObjects() const;
    void splat(PlayerSimulation& actor);
    void cleanup(PlayerSimulation& actor);
    void clear();

private:
    struct ActorSpecial {
        std::string id, phase;
        glm::dvec3 origin{0.0}, target{0.0}, zipTarget{0.0}, zipNormal{0.0};
        double elapsed = 0.0, duration = 0.0, cooldown = 0.0, autoPlacementTime = -1.0, moveSpeed = -1.0;
        double accel = -1.0, faceYaw = 0.0, hullYaw = 0.0, charge = 0.0, fullTime = 0.0;
        double hitPoints = 0.0, trail = 0.0, boostCooldown = 0.0, boostTime = 0.0, zipElapsed = 0.0, firing = 0.0;
        double lastHitAudio = -1.0;
        glm::dvec3 moveIn{0.0}, moveOut{0.0};
        bool hasMoveOut = false, flipBack = false, flipFront = false;
        double guard = 0.0, pending = -1.0, flipElapsed = -1.0, hang = 0.0, stuck = 0.0;
        int bubbleCount = 0;
        bool body = false, casting = false, thrown = false, bubblerShield = false, armor = false, endingCued = false;
        bool noSquid = false, aimFace = false, inkProof = false, faceMove = false, hasFaceYaw = false;
        bool mapTarget = false, jumpBack = false, attack = false, air = false, roll = false;
        bool subWas = false, fireWas = false, jumpWas = false, needRelease = false, raise = false;
        double hitRadius = -1.0, hitHeight = -1.0;
    };
    struct WorldObject {
        PlayerSimulation* owner = nullptr;
        std::string kind, phase;
        glm::dvec3 pos{0.0}, previous{0.0}, target{0.0}, mouth{0.0}, vel{0.0}, direction{0.0, 0.0, 1.0};
        double age = 0.0, timer = 0.0, accumulator = 0.0, radius = 0.0, range = 0.0, damagePerSecond = 0.0;
        double charge = 0.0, maxDamage = 0.0, minDamage = 0.0, killRadius = 0.0, paintRadius = 0.0;
        double directDamage = 0.0;
        bool live = true, held = false, audioWhistled = false;
        std::vector<PlayerSimulation*> tagged;
    };

    void throwBarrage(PlayerSimulation& actor, ActorSpecial& special);
    bool bodyStep(PlayerSimulation& actor, double dt);
    void slamImpact(PlayerSimulation& actor, const EquipmentData& def);
    void tickStorm(WorldObject& object, double dt);
    void tickZooka(WorldObject& object, double dt);
    void tickWail(WorldObject& object, double dt);
    void tickStrike(WorldObject& object, double dt);
    void tickBubble(WorldObject& object, double dt);
    void tickThrownStamp(WorldObject& object, double dt);
    void tickShell(WorldObject& object, double dt);
    void tickOrb(WorldObject& object, double dt);
    void tickJetpackShot(WorldObject& object, double dt);
    void jetpackBlast(WorldObject& object, glm::dvec3 center, PlayerSimulation* direct);
    void specialBlast(PlayerSimulation& owner, glm::dvec3 center, double radius, double maxDamage,
                      double minDamage, double killRadius, std::string_view weaponId);
    void explodeBubble(WorldObject& object, PlayerSimulation* shooter);
    void bubbleHit(WorldObject& object, int team, double damage, PlayerSimulation* owner);
    void stampImpact(PlayerSimulation& actor, const ActorSpecial& special, double radius, double reach);
    void zipImpact(PlayerSimulation& actor, ActorSpecial& special, PlayerSimulation* direct);

    CombatWorld& world_;
    bool tickingWorld_ = false;
    std::unordered_map<PlayerSimulation*, ActorSpecial> actors_;
    std::unordered_map<PlayerSimulation*, PlayerSimulation*> shieldSources_;
    std::unordered_map<PlayerSimulation*, double> cheerTimes_;
    std::unordered_map<PlayerSimulation*, double> shieldHitTimes_;
    std::vector<WorldObject> objects_;
};
} // namespace restoration
