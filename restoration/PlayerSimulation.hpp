#pragma once

#include "StageGeometry.hpp"

#include <cstdint>
#include <vector>

namespace restoration {

class FacePaint;

struct PlayerInput {
    glm::dvec3 move{0.0}; // World-space x/z, length at most one.
    bool jump = false, squid = false, fire = false;
    double aimYaw = 0.0, aimPitch = 0.0;
    glm::dvec3 aimPoint{0.0};
    bool hasAimPoint = false;
};

struct PlayerState {
    glm::dvec3 pos{0.0}, vel{0.0};
    double yaw = 0.0, hp = 100.0, ink = 100.0, specialCharge = 0.0;
    double landT = 99.0, landSpeed = 0.0, smoothY = 0.0;
    double respawnTimer = 0.0, invuln = 0.0;
    bool alive = true, grounded = false, climbing = false, submerged = false, squid = false;
    int groundTeam = 0; // 0 dry, 1 own ink, 2 enemy ink.
};

struct ShooterProjectile {
    glm::dvec3 pos{0.0}, vel{0.0};
    double age = 0.0;
};

// Fixed-step local actor and the source Spritzer shooter; team 0/1 ink uses FacePaint.
class PlayerSimulation {
public:
    PlayerSimulation(const StageData& stage, const StageGeometry& geometry, FacePaint& paint, int team = 0);
    void spawnAt(glm::dvec3 pos, double yaw);
    void tick(double dt);
    bool damage(double amount);
    glm::dvec3 visualPos() const;
    const std::vector<ShooterProjectile>& projectiles() const { return projectiles_; }
    double turfPainted() const { return turfPainted_; }
    int shotsFired() const { return shotsFired_; }

    PlayerState state;
    PlayerInput input;

private:
    void horizontal(double dt, bool enemy);
    void climb(double dt);
    void integrate(double dt, bool jumped);
    void railFeet(double lo, double hi);
    void roofSlide(const BlockData& block, double dt);
    void shoot(double dt, bool fire);
    void updateProjectiles(double dt);
    void surface();
    void splat();
    double random();
    glm::dvec3 aimDir() const;

    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    int team_;
    GroundHit ground_;
    glm::dvec3 wallNormal_{0.0, 0.0, 1.0};
    double lastDamage_ = 99.0, lastFire_ = 99.0, damageFromInk_ = 0.0;
    double coyote_ = 0.0, jumpBuffer_ = 0.0, fireBuffer_ = 0.0, kidT_ = 99.0;
    double firePressT_ = -1.0, squidPressT_ = -1.0, time_ = 0.0;
    double cooldown_ = 0.0, bloom_ = 0.0, hardLand_ = 0.0, climbSpeed_ = 0.0, climbExit_ = 0.0;
    double smoothYVelocity_ = 0.0, turfPainted_ = 0.0;
    double yawVelocity_ = 0.0, faceTarget_ = 0.0;
    double firingT_ = 0.0, fireFacing_ = 0.0, roofT_ = 0.0, roofStall_ = 0.0;
    glm::dvec3 roofDirection_{0.0}, roofPrevious_{0.0};
    bool hasFaceTarget_ = false;
    int shotsFired_ = 0;
    bool prevJump_ = false, prevFire_ = false, prevSquid_ = false;
    std::uint32_t seed_ = 1;
    std::vector<ShooterProjectile> projectiles_;
    std::vector<double> projectileTrail_, projectileSeed_;
};

} // namespace restoration
