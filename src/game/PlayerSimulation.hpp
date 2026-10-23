#pragma once

#include "StageGeometry.hpp"
#include "CombatWorld.hpp"
#include "WeaponRunner.hpp"
#include "CharacterPose.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <memory>
#include <string>

namespace restoration {

class FacePaint;
class WorldSpecials;
class PlayerSimulation;
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
    PlayerSimulation* actorTarget = nullptr;
    std::uint64_t beaconId = 0;
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
    double crosshairSpreadDegrees() const { return runner_.spread(); }
    double streamDuration() const;
    SpecialPose specialPose() const;
    double hitRadius() const;
    double hitHeight() const;
    const EquipmentData& weapon() const;
    const EquipmentData& subWeapon() const;
    const EquipmentData& specialWeapon() const;
    CombatWorld& combatWorld() { return *world_; }
    void setCanRespawn(bool value) { canRespawn_ = value; }
    bool canRespawn() const { return canRespawn_; }
    void setRandomSource(std::function<double()> source) { randomSource_ = std::move(source); }
    std::function<double()> randomSource() const { return randomSource_; }
    void setBotControlled(bool value) { botControlled_ = value; }
    bool botControlled() const { return botControlled_; }
    void setLocal(bool value) { local_ = value; }
    bool isLocal() const { return local_; }
    void setNetworkIdentity(std::uint32_t nid,std::uint32_t owner) { networkNid_=nid;networkOwner_=owner; }
    std::uint32_t networkNid() const { return networkNid_; }
    std::uint32_t networkOwner() const { return networkOwner_; }
    void prepareNetworkAdoption();
    void setNetworkGhostPose(CombatPose combat,SpecialPose special) { networkCombatPose_=std::move(combat);networkSpecialPose_=std::move(special); }
    bool damageFromNetwork(double amount,PlayerSimulation* attacker,const std::string& source) { return damageFromCombat(amount,attacker,source); }
    void updatePresentation(double dt) { if(poseFrameSink_)poseFrameSink_(dt); }
    void setPoseTriggerSink(std::function<void(const CharacterTrigger&)> sink) { poseTriggerSink_ = std::move(sink); }
    void setNetworkPoseTriggerSink(std::function<void(const CharacterTrigger&)> sink) { networkPoseTriggerSink_ = std::move(sink); }
    void applyNetworkPoseTrigger(const CharacterTrigger& event) { if(poseTriggerSink_)poseTriggerSink_(event); }
    void setPoseFrameSink(std::function<void(double)> sink) { poseFrameSink_ = std::move(sink); }
    void setDamageSink(std::function<void(double,const glm::dvec3*)> sink) { damageSink_ = std::move(sink); }
    void setSplatSink(std::function<void(bool)> sink) { splatSink_ = std::move(sink); }
    void spawnAt(glm::dvec3 pos, double yaw);
    bool superJump(glm::dvec3 destination, bool instant = false, bool home = false);
    bool superJumpToActor(PlayerSimulation& target,bool allowJumpingTarget=false);
    bool superJumpToBeacon(std::uint64_t beaconId);
    glm::dvec3 jumpAnchor() const;
    const std::optional<SuperJumpState>& superJumpState() const { return superJump_; }
    void setNetworkSuperJump(std::optional<SuperJumpState> state) { superJump_ = std::move(state); }
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
    void applyNetworkStats(double turf,double zone,int deaths,int splats,int shots) {
        if(std::isfinite(turf)&&turf>=0.0)turfPainted_=turf;
        if(std::isfinite(zone)&&zone>=0.0)zoneTurf_=zone;
        deaths_=std::max(0,deaths);splats_=std::max(0,splats);shotsFired_=std::max(0,shots);
    }
    glm::dvec3 muzzle(int side=0) const { return runner_.muzzle(side); }
    glm::dvec3 aimDirection() const { return aimDir(); }
    glm::dmat4 tankWorld() const { return tankSource_?tankSource_():glm::translate(glm::dmat4(1),state.pos+glm::dvec3(0,1.25,0)); }
    void setTankSource(std::function<glm::dmat4()> source) { tankSource_=std::move(source); }
    glm::dvec3 subNozzle() const { return subNozzleSource_?subNozzleSource_().first:state.pos+glm::dvec3(0,1.2,0); }
    glm::dvec3 subNozzleDirection() const { return subNozzleSource_?subNozzleSource_().second:glm::dvec3(0,1,0); }
    void setSubNozzleSource(std::function<std::pair<glm::dvec3,glm::dvec3>()> source) { subNozzleSource_=std::move(source); }
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
    void splat(bool water=false, PlayerSimulation* attacker=nullptr, const std::string& source="environment");
    bool damageFromCombat(double amount, PlayerSimulation* attacker, const std::string& source);
    void finishFrame(double dt);
    void triggerPose(CharacterTrigger event) { if (poseTriggerSink_) poseTriggerSink_(event); if(networkPoseTriggerSink_)networkPoseTriggerSink_(event); }
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
    std::uint32_t networkNid_=0,networkOwner_=0;
    std::optional<CombatPose> networkCombatPose_;
    std::optional<SpecialPose> networkSpecialPose_;
    std::optional<SuperJumpState> superJump_;
    std::function<void(const CharacterTrigger&)> poseTriggerSink_;
    std::function<void(const CharacterTrigger&)> networkPoseTriggerSink_;
    std::function<void(double)> poseFrameSink_;
    std::function<void(double,const glm::dvec3*)> damageSink_;
    std::function<void(bool)> splatSink_;
    std::function<glm::dvec3(int)> muzzleSource_;
    std::function<glm::dmat4()> tankSource_;
    std::function<std::pair<glm::dvec3,glm::dvec3>()> subNozzleSource_;
};

} // namespace restoration
