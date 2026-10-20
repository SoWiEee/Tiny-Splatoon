#pragma once

#include "BossNav.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace restoration {
class FacePaint;
class PlayerSimulation;

enum class BossMoveId { Slam, Barrage, Sweep, Charge, Crablets, Frenzy };
enum class BossMovePhase { None, Telegraph, Active, Recovery };
struct BossBarrel { double x=0,y=0,z=0,throwAt=0; };
struct BossMove {
    BossMoveId id=BossMoveId::Slam;
    double t0=0,tele=0,act=0,rec=0;
    std::uint32_t seed=0;
    glm::dvec3 point{0},origin{0};
    double yaw=0,length=0,speed=0,a0=0,a1=0,rot0=0;
    bool wall=false,spin=false,stun=false;
    int count=0;
    std::vector<double> rings;
    std::vector<BossBarrel> barrels;
    double end() const {return tele+act+rec;}
    BossMovePhase phase(double time) const;
};
struct BossCrablet {
    int id=0,target=-1;
    glm::dvec3 pos{0};
    double yaw=0,hp=30,age=0,retarget=0,speed=0;
    bool dead=false;
};
struct BossHitShape {
    std::string socket;
    glm::dvec3 pos{0};
    double radius=0;
    bool weak=false,active=true;
};
struct BossSegmentHit {
    glm::dvec3 point{0};
    double t=0,distance=0;
    int shape=-1,crablet=-1;
    bool weak=false;
};
struct BossThreat {
    double level=0,ax=0,az=0,ringIn=-1;
    bool cover=false,beam=false,lane=false;
};
struct BossPoseHitPulse { double flash=0,weakFlash=0,hurt=0; };
struct BossState {
    glm::dvec3 pos{0},vel{0};
    double yaw=0,turn=0,clock=0,hp=0,maxHp=0;
    int phase=1,target=-1,anim=0,killer=-1;
    double animT0=0;
    bool visible=false,dead=false,invulnerable=false,stunned=false;
};

// Offline HULLBREAKER owns the source move timeline and victim-side geometry.
class BossSimulation {
public:
    BossSimulation(const StageData& stage,const StageGeometry& geometry,FacePaint& paint,
        const std::vector<PlayerSimulation*>& squad,const std::string& difficulty,std::uint32_t seed=1);
    void reset();
    void tick(double dt,bool playing);
    void finish();
    const BossState& state() const {return state_;}
    std::optional<glm::dvec3> poseAim() const;
    BossPoseHitPulse takePoseHitPulse();
    const BossNav& nav() const {return nav_;}
    const std::optional<BossMove>& move() const {return move_;}
    const std::vector<BossMove>& moves() const {return moves_;}
    const std::vector<BossCrablet>& crablets() const {return crabs_;}
    const std::array<double,8>& squadDamage() const {return totalDamageBy_;}
    const std::array<int,8>& squadWeakHits() const {return weakHitsBy_;}
    const std::array<int,8>& squadSplats() const {return splatsBy_;}
    const std::array<BossHitShape,10>& hitShapes() const {return hitShapes_;}
    void setHitShapes(const std::array<BossHitShape,10>& shapes);
    std::optional<BossSegmentHit> segmentHit(glm::dvec3 from,glm::dvec3 to,double pad=0) const;
    double hit(PlayerSimulation& attacker,double damage,const BossSegmentHit& hit,const std::string& weapon);
    double splash(PlayerSimulation& attacker,glm::dvec3 center,double radius,double maxDamage,double minDamage,const std::string& weapon);
    BossThreat threat(double x,double y,double z,double horizon=1.2) const;
    void forceMove(BossMoveId id); // deterministic source move audits
    static double chargeDistance(const BossMove& move,double time);
    static double chargeTime(double length,double speed);
    static double beamAngle(const BossMove& move,double time);
private:
    double random();
    double pace() const;
    void chooseTarget();
    void chooseGoal(PlayerSimulation* target);
    bool turnToward(double want,double dt);
    void walk(double dt,PlayerSimulation* target);
    void chooseMove(PlayerSimulation* target,double dt);
    std::optional<std::tuple<double,double,bool>> chargeLane(double yaw) const;
    void startMove(BossMoveId id,PlayerSimulation* target);
    void runMove(double dt);
    void updateCrablets(double dt);
    void spawnCrablets(int count);
    void popCrablet(BossCrablet& crab,bool killed);
    void advanceHazards(double from,double to,double dt,bool live);
    void hurtSquad(const BossMove& move,double from,double to,double dt);
    void paintHazards(const BossMove& move,double from,double to);
    double reachAt(const BossMove& move,double x,double z,double radius) const;
    double beamLength(const BossMove& move,double angle) const;
    void paintAt(double x,double y,double z,double radius);
    void damage(PlayerSimulation& actor,double amount);
    double applyDamage(PlayerSimulation& attacker,double amount,bool weak);
    void updateDefaultHitShapes();
    PlayerSimulation* targetActor() const;
    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    const std::vector<PlayerSimulation*>& squad_;
    BossNav nav_;
    std::string difficulty_;
    std::uint32_t seed_=1,initialSeed_=1;
    BossState state_;
    std::optional<BossMove> move_;
    std::vector<BossMove> moves_;
    std::vector<BossCrablet> crabs_;
    std::array<BossHitShape,10> hitShapes_{};
    bool externalHitShapes_=false,spawnedCrabs_=false,finished_=false;
    int pendingPhase_=0,crabSeq_=1;
    double damageMultiplier_=1,cool_=2.4,retarget_=0,goalTime_=0,stuckTime_=0,
        speed_=0,crabletCooldown_=0,frenzyCooldown_=0;
    std::optional<BossMoveId> lastMove_,forcedMove_;
    glm::dvec2 home_{0},goal_{0};
    bool hasGoal_=false;
    std::vector<glm::dvec2> path_;
    std::size_t pathIndex_=0;
    std::array<double,8> attention_{},damageBy_{},totalDamageBy_{};
    std::array<int,8> weakHitsBy_{},splatsBy_{};
    std::vector<std::string> oneShotHits_;
    BossPoseHitPulse poseHitPulse_;
};
} // namespace restoration
