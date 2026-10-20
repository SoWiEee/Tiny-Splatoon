#pragma once
#include "StageGeometry.hpp"
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace restoration {
class PlayerSimulation;
struct EquipmentData;

struct WeaponProfile {
    std::string kind = "shooter";
    double range = 12.5, projectileSpeed = 34.0;
    bool charges = false, melee = false, painter = true;
};
struct CombatPose {
    bool firing = false, charging = false, rolling = false, subAim = false, special = false, guard = false;
    double charge = 0.0, firingTime = 0.0, dodgeTime = 0.0, specialTime = 0.0;
    double burstTime = 0.0, actionYaw = 0.0, lockTime = 0.0;
    int bowRing = 0, bowRingEvents = 0, mittsLeapEvents = 0;
    bool streaming = false, turret = false, leaping = false;
    bool specialThrown = false;
    int hand = 0;
    std::string specialKind;
    std::array<double,2> sinceHand{99.0,99.0};
    double canopyOpen=0.0,canopyGrow=1.0,canopyHp=0.0,canopyHpMax=0.0;
    double canopyShake=0.0,canopyFlash=0.0,canopyRegrowK=0.0;
    bool canopyOpening=false;
    std::string canopyState="ready";
    double bladeFlashTime=99.0,mittsLandTime=99.0;
};

enum class CanopyState { Ready, Broken, Launched };

struct BrollyCanopy {
    std::uint64_t id = 0;
    CanopyState state = CanopyState::Ready;
    double hp = 0.0, open = 0.0, openTime = 0.0, pressTime = 0.0;
    double grow = 1.0, regrowTime = 0.0, lastHit = -9.0;
    bool want = false, shot = false, lockRelease = false, previousFire = false;
    bool opening = false;
    double flash=0.0,shake=0.0,regrowK=0.0;
    bool previousShield = false;
    glm::dvec3 previousCenter{0.0}, previousNormal{0.0,0.0,1.0};
};

class WeaponRunner {
public:
    explicit WeaponRunner(PlayerSimulation& actor) : actor_(actor) {}
    void reset();
    void tick(double dt, bool fire, bool firePressed);
    void tick(double dt, bool fire, bool firePressed, bool sub, bool subReleased);
    bool tryDodge();
    void cancelDodge();
    bool bodyOverride(double dt);
    bool busy() const;
    double moveSpeed() const;
    CombatPose pose() const;
    WeaponProfile profile() const;
private:
    friend class CombatWorld;
    friend class PlayerSimulation;
    void brolly(bool fire, double dt);
    void fireBrolly();
    void bow(bool fire, double dt);
    void fireBow(double charge);
    void blade(bool fire, bool pressed, double dt);
    void fireBladeDrops();
    void fireBladeWave();
    void bladeMelee();
    void mitts(bool fire, double dt);
    void fireMittsFist(int hand);
    void releaseMittsLeap();
    double damageTaken(double amount, const PlayerSimulation* attacker) const;
    void shooter(double dt, bool fire);
    void fireRound(double spread, const std::string& kind, double power = 1.0, int side = 0);
    void charger(bool fire, double dt);
    void roller(bool fire, bool pressed, double dt, bool brush);
    void bucket(bool fire, double dt);
    void spinner(bool fire, double dt);
    void twins(bool fire, double dt);
    void fireFlick();
    void fireSlosh();
    void fireSwipe();
    glm::dvec3 muzzle(int side = 0) const;
    glm::dvec3 direction(glm::dvec3 from) const;
    glm::dvec3 scatter(glm::dvec3 dir, double degrees);
    glm::dvec3 ballistic(glm::dvec3 from, glm::dvec3 dir, double speed, double straight, double gravity, double drag, double range) const;
    double spread() const;
    bool spend(double ink);
    PlayerSimulation& actor_;
    double cooldown_ = 0.0, bloom_ = 0.0, firingT_ = 0.0;
    double charge_ = 0.0, chargeT_ = 0.0, flick_ = -1.0, flickRecover_ = 0.0;
    double rollT_ = 0.0, burstT_ = 0.0, burstPower_ = 0.0, streamCd_ = 0.0;
    double dodgeT_ = 0.0, rollLock_ = 0.0, sinceRoll_ = 9.0;
    double subCharge_ = 0.0, subChargeBudget_ = 0.0;
    double subAimYaw_ = 0.0, subWagTime_ = 9.0;
    bool charging_ = false, rolling_ = false, turret_ = false, rollQueued_ = false, aimingSub_ = false;
    bool previousSub_ = false, previousJump_ = false;
    int subLookSign_ = 0;
    int rollsUsed_ = 0, twinSide_ = 1, swipeSide_ = 1;
    glm::dvec3 lastRollPos_{0.0}, dodgeDir_{0.0};
    std::unordered_map<PlayerSimulation*, double> rollHits_;
    struct BladeSwingState {
        bool active = false, heavy = false, fired = false, stroke = false;
        double time = 0.0, yaw = 0.0, pitch = 0.0;
        std::vector<PlayerSimulation*> hits;
    } bladeSwing_;
    bool bladeHeld_ = false, bladeCharging_ = false, bladeFull_ = false;
    double bladeHoldTime_ = 0.0, bladeCharge_ = 0.0, bladeCooldown_ = 0.0, bladeQueued_ = 0.0, bladeLunge_ = 0.0;
    glm::dvec3 bladeLungeDirection_{0.0};
    double bladeLungeSpeed_ = 0.0;
    int bladeSide_ = -1;
    bool mittsCharging_ = false, mittsLeaping_ = false;
    double mittsCharge_ = 0.0, mittsChargeTime_ = 0.0, mittsCooldown_ = 0.0, mittsLandTime_ = 99.0;
    int bowRing_ = 0, bowRingEvents_ = 0, mittsLeapEvents_ = 0;
    double bladeStoreTime_ = 0.0;
    double bladeFlashTime_ = 99.0;
    std::array<double,2> sinceHand_{99.0,99.0};
    glm::dvec3 mittsLeapVelocity_{0.0};
    int mittsSide_ = 1;
    BrollyCanopy canopy_;
};
} // namespace restoration
