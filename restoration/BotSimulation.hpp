#pragma once

#include "NavGraph.hpp"
#include "PlayerSimulation.hpp"

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace restoration {

class FacePaint;
class CombatWorld;
struct CombatThreat;

enum class BotMode { Paint, Fight, Refill, Retreat };

struct BotDifficulty {
    double reaction = 0.32, aimError = 0.06, fireDiscipline = 0.8;
    double awareness = 21.0, aimOmega = 13.0, aimTurn = 10.0;
};

// Turf War BotBrain port. It writes the same PlayerInput consumed by human actors.
class BotSimulation {
public:
    BotSimulation(PlayerSimulation& actor, const StageData& stage, const StageGeometry& geometry,
                  FacePaint& paint, const NavGraph& nav, CombatWorld& world,
                  std::string_view difficulty = "normal", std::uint32_t seed = 1,
                  std::function<double()> randomSource = {});
    void spawnFacing(double yaw) { aimYaw_ = yaw; aimPitch_ = 0; }
    void setDifficulty(std::string_view difficulty);
    void reset();
    void tick(double dt, bool playing, const std::vector<BotSimulation*>& bots);

    PlayerSimulation& actor() const { return actor_; }
    BotMode mode() const { return mode_; }
    bool hasTarget() const { return target_ != nullptr; }
    int goal() const { return goal_; }
    double stuckTime() const { return noProgress_; }
    const BotDifficulty& difficulty() const { return difficulty_; }

private:
    struct PaintStats { double own = 0, enemy = 0, empty = 0; int count = 0; };
    struct FaceIndex { int id; double minX, maxX, minZ, maxZ; };

    double random();
    PaintStats regionStats(double x, double y, double z, double radius) const;
    void perceive();
    bool pathTo(glm::dvec3 pos, double maxUp = 0.8);
    void pickPaintGoal(const std::vector<BotSimulation*>& bots);
    void pickRefill();
    void pickRetreat();
    glm::dvec3 steer(double dt);
    void wiggle(double duration);
    void unstick(double dt, glm::dvec3& move);
    void backOnNav(double dt, glm::dvec3& move);
    bool fatLos(glm::dvec3 a, glm::dvec3 b) const;
    bool dryLine(glm::dvec3 a, glm::dvec3 b) const;
    bool wet(double x, double z, double y) const;
    bool nearWater(double radius) const;
    double pickEvade(const CombatThreat& threat);
    void avoidWater(glm::dvec3& move);
    void edgeGuard(glm::dvec3& move);
    bool squidWouldDrop(glm::dvec3 move) const;
    double pathRemaining() const;
    void climb(double dt, glm::dvec3& move, PlayerInput& input, double& yaw, double& pitch, double& aimDistance);
    void finish(double dt, glm::dvec3 move, double wantYaw, double wantPitch, double aimDistance, bool fighting);
    void fight(double dt, glm::dvec3& move, double& wantYaw, double& wantPitch, double& aimDistance);
    void paint(double dt, glm::dvec3& move, double& wantYaw, double& wantPitch);
    bool fightSub(double distance);
    bool paintSub();
    bool wantSpecial(bool fighting, double distance, bool visible);
    void specialControl(double dt, PlayerInput& input, glm::dvec3& move, double distance, bool visible);
    void threatControl(double dt, glm::dvec3& move, double& yaw, double& pitch, double& aimDistance);
    void shieldFight(double dt, glm::dvec3& move, double distance);
    bool canopyControl(double dt, glm::dvec3& move, bool free,
                       double& yaw, double& pitch, double& aimDistance);
    bool deviceThreat(const CombatThreat& threat) const;
    double threatScore(const CombatThreat& threat) const;
    void deviceAim(double dt, const CombatThreat& threat, glm::dvec3& move,
                   PlayerInput& input, double& yaw, double& pitch, double& aimDistance);

    PlayerSimulation& actor_;
    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    const NavGraph& nav_;
    CombatWorld& world_;
    BotDifficulty difficulty_;
    std::uint32_t seed_;
    std::function<double()> randomSource_;
    std::vector<FaceIndex> turfFaces_;

    BotMode mode_ = BotMode::Paint;
    PlayerSimulation* target_ = nullptr;
    std::vector<int> path_;
    int pathIndex_ = 0, goal_ = -1;
    double time_ = 0, elapsed_ = 0, think_ = 0, react_ = 0, seeTimer_ = 0, lostTimer_ = 0;
    double repath_ = 0, goalTimer_ = 0, goalCheck_ = 0, refillUntil_ = 0, retreatTime_ = 0;
    double aimYaw_ = 0, aimPitch_ = 0, aimYawVelocity_ = 0, aimPitchVelocity_ = 0;
    double acquisitionTime_ = 9, acquisitionSignYaw_ = 0, acquisitionSignPitch_ = 0, phase1_ = 0, phase2_ = 0;
    double moveYaw_ = 0, moveMagnitude_ = 0, sweep_ = 0, paintYawOffset_ = 0, paintScan_ = 0, paintPause_ = 0;
    double strafe_ = 1, strafeSmooth_ = 0, strafeAmplitude_ = 1, strafeTimer_ = 0, bombCooldown_ = 0;
    double chargeRelease_ = 0.95, fightHold_ = 0;
    double dodgeCooldown_ = 0, jumpCooldown_ = 0, bestDistance_ = 1e30, noProgress_ = 0;
    double displacementTimer_ = 0, moveAccumulation_ = 0, wiggleTimer_ = 0, wiggleYaw_ = 0, airStill_ = 0;
    double strikeTimer_ = 0, refillThreshold_ = 0.85;
    double climbTimer_ = 0, noClimbUntil_ = 0, lookaheadTimer_ = 0, navBackTimer_ = 0;
    double ledgeReplanTime_ = -9;
    bool climbAiming_ = false;
    double threatScanTimer_ = 0, threatEvadeTimer_ = 0, threatEvadeYaw_ = 0, threatNoticeTime_ = 0;
    double flankTimer_ = 0, canopyTimer_ = 0;
    double threatAcquisition_ = 0, threatYawSign_ = 0, threatPitchSign_ = 0, shieldBlockedTime_ = 0;
    double bombReleaseTimer_ = 0, specialTime_ = 0, barrageCooldown_ = 0;
    double lastDamageSeen_ = 99;
    glm::dvec3 lastPosition_{0.0}, displacementPosition_{0.0};
    int lookaheadIndex_ = -1, lookaheadPathIndex_ = -1, ledgeReplans_ = 0, strikes_ = 0, flankSide_ = 0, canopySide_ = 0;
    int threatActs_ = 0, navBackId_ = -1;
    std::uint64_t threatId_ = 0, threatCandidateId_ = 0;
    std::uint64_t shieldId_ = 0, canopyId_ = 0;
    std::vector<CombatShield> shieldSnapshot_;
    std::unordered_map<std::uint64_t, double> threatMemory_;
    bool needJump_ = false, skipped_ = false, wasDead_ = false, firing_ = false, threatFiring_ = false, threatAiming_ = false;
    bool bombAim_ = false, releaseBomb_ = false, threatLos_ = false, threatPulse_ = false;
    bool wasPlanted_ = false, lastThreatGround_ = false, wasShieldBreaking_ = false, canopyBreaking_ = false;
};

} // namespace restoration
