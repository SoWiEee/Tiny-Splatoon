#pragma once

#include "StageGeometry.hpp"
#include "CombatWorld.hpp"
#include "WeaponRunner.hpp"
#include "CharacterPose.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <memory>
#include <string>

namespace restoration {

class FacePaint;
class WorldSpecials;
struct EquipmentData;

struct PlayerInput {
    glm::dvec3 move{0.0}; // World-space x/z, length at most one.
    bool jump = false, squid = false, fire = false;
    bool sub = false, special = false, cheer = false;
    double aimYaw = 0.0, aimPitch = 0.0;
    glm::dvec3 aimPoint{0.0};
    bool hasAimPoint = false;
    bool specialConfirm = false, hasMapTarget = false;
    glm::dvec3 mapTarget{0.0};
};

struct SuperJumpState {
    bool flight = false, home = false;
    double elapsed = 0.0, duration = 0.0;
    glm::dvec3 from{0.0}, to{0.0}, target{0.0};
};

struct PlayerState {
    glm::dvec3 pos{0.0}, vel{0.0};
    double yaw = 0.0, hp = 100.0, ink = 100.0, specialCharge = 0.0, hurtFlash = 0.0;
    double landT = 99.0, landSpeed = 0.0, smoothY = 0.0;
    double respawnTimer = 0.0, invuln = 0.0;
    bool alive = true, grounded = false, climbing = false, submerged = false, squid = false;
    int groundTeam = 0; // 0 dry, 1 own ink, 2 enemy ink.
    double track = 0.0, poison = 0.0, reveal = 0.0, shield = 0.0;
    int trackTeam = -1, revealTeam = -1;
};

// Fixed-step actor; standalone actors own a world, match actors share one.
class PlayerSimulation {
public:
    PlayerSimulation(const StageData& stage, const StageGeometry& geometry, FacePaint& paint, int team = 0, CombatWorld* world = nullptr);
    ~PlayerSimulation();
    PlayerSimulation(const PlayerSimulation&) = delete;
    PlayerSimulation& operator=(const PlayerSimulation&) = delete;
    void setLoadout(std::string main, std::string sub = {}, std::string special = {});
    const std::string& mainId() const { return mainId_; }
    const std::string& subId() const { return subId_; }
    const std::string& specialId() const { return specialId_; }
    int team() const { return team_; }
    double lastDamage() const { return lastDamage_; }
    WeaponProfile weaponProfile() const { return runner_.profile(); }
    CombatPose combatPose() const;
    SpecialPose specialPose() const;
    double hitRadius() const;
    double hitHeight() const;
    const EquipmentData& weapon() const;
    const EquipmentData& subWeapon() const;
    const EquipmentData& specialWeapon() const;
    CombatWorld& combatWorld() { return *world_; }
    void setCanRespawn(bool value) { canRespawn_ = value; }
    void setRandomSource(std::function<double()> source) { randomSource_ = std::move(source); }
    void setBotControlled(bool value) { botControlled_ = value; }
    bool botControlled() const { return botControlled_; }
    void setLocal(bool value) { local_ = value; }
    bool isLocal() const { return local_; }
    void setPoseTriggerSink(std::function<void(const CharacterTrigger&)> sink) { poseTriggerSink_ = std::move(sink); }
    void setPoseFrameSink(std::function<void(double)> sink) { poseFrameSink_ = std::move(sink); }
    void setDamageSink(std::function<void(double,const glm::dvec3*)> sink) { damageSink_ = std::move(sink); }
    void setSplatSink(std::function<void(bool)> sink) { splatSink_ = std::move(sink); }
    void spawnAt(glm::dvec3 pos, double yaw);
    bool superJump(glm::dvec3 destination, bool instant = false, bool home = false);
    glm::dvec3 jumpAnchor() const;
    const std::optional<SuperJumpState>& superJumpState() const { return superJump_; }
    void tick(double dt);
    bool damage(double amount);
    glm::dvec3 visualPos() const;
    const std::vector<ShooterProjectile>& projectiles() const { return world_->projectiles(); }
    double turfPainted() const { return turfPainted_; }
    double zoneTurf() const { return zoneTurf_; }
    double zoneTurfPainted() const { return zoneTurf_; }
    void addZoneTurf(double area) { if(area>0.0) zoneTurf_+=area; }
    int deaths() const { return deaths_; }
    int splats() const { return splats_; }
    int shotsFired() const { return shotsFired_; }
    glm::dvec3 muzzle(int side=0) const { return runner_.muzzle(side); }
    void setMuzzleSource(std::function<glm::dvec3(int)> source) { muzzleSource_=std::move(source); }

    PlayerState state;
    PlayerInput input;

private:
    friend class WeaponRunner;
    friend class CombatWorld;
    friend class WorldSpecials;
    void horizontal(double dt, bool enemy);
    void climb(double dt);
    void integrate(double dt, bool jumped);
    void resolveMotion(double prevY, bool stick);
    void railFeet(double lo, double hi);
    void roofSlide(const BlockData& block, double dt);
    void surface();
    void splat(bool water=false);
    bool damageFromCombat(double amount, PlayerSimulation* attacker, const std::string& source);
    void finishFrame(double dt);
    void triggerPose(CharacterTrigger event) { if (poseTriggerSink_) poseTriggerSink_(event); }
    void updateSuperJump(double dt);
    double random();
    glm::dvec3 aimDir() const;

    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    int team_;
    std::unique_ptr<CombatWorld> ownWorld_;
    CombatWorld* world_;
    WeaponRunner runner_;
    std::string mainId_ = "shooter", subId_ = "bomb", specialId_ = "storm";
    GroundHit ground_;
    glm::dvec3 wallNormal_{0.0, 0.0, 1.0};
    double lastDamage_ = 99.0, lastFire_ = 99.0, damageFromInk_ = 0.0;
    double coyote_ = 0.0, jumpBuffer_ = 0.0, fireBuffer_ = 0.0, kidT_ = 99.0;
    double firePressT_ = -1.0, squidPressT_ = -1.0, time_ = 0.0;
    double hardLand_ = 0.0, climbSpeed_ = 0.0, climbExit_ = 0.0;
    double smoothYVelocity_ = 0.0, turfPainted_ = 0.0, zoneTurf_ = 0.0;
    double yawVelocity_ = 0.0, faceTarget_ = 0.0;
    double firingT_ = 0.0, fireFacing_ = 0.0, roofT_ = 0.0, roofStall_ = 0.0;
    glm::dvec3 roofDirection_{0.0}, roofPrevious_{0.0};
    bool hasFaceTarget_ = false;
    bool canRespawn_ = true;
    int shotsFired_ = 0;
    int deaths_ = 0, splats_ = 0;
    bool prevJump_ = false, prevFire_ = false, prevSquid_ = false, prevSub_ = false, prevSpecial_ = false;
    std::uint32_t seed_ = 1;
    std::function<double()> randomSource_;
    bool botControlled_ = false, local_ = false;
    std::optional<SuperJumpState> superJump_;
    std::function<void(const CharacterTrigger&)> poseTriggerSink_;
    std::function<void(double)> poseFrameSink_;
    std::function<void(double,const glm::dvec3*)> damageSink_;
    std::function<void(bool)> splatSink_;
    std::function<glm::dvec3(int)> muzzleSource_;
};

} // namespace restoration
