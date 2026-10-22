#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace restoration {

class BotSimulation;
class FacePaint;
class NavGraph;
class PlayerSimulation;
class StageGeometry;
class ZoneControl;
struct StageData;

enum class ZoneBotRole { Paint, Guard, Watch, Push, Stage };
enum class ZoneWaveState { Free, Stage, Push };

struct ZoneBotAssignment {
    ZoneBotRole role = ZoneBotRole::Paint;
    int zone = -1;
};

// Match-owned shared objective plan. ZoneControl is updated before update(), then the
// same plan is queried by each BotSimulation during that fixed step.
class ZonePlan {
public:
    using RandomSource = std::function<double()>;

    ZonePlan(const ZoneControl& zones, const StageData& stage, const StageGeometry& geometry,
             const FacePaint& paint, const NavGraph& nav, std::uint32_t seed = 1,
             RandomSource random = {});
    void update(double dt, const std::vector<BotSimulation*>& bots);
    // Drop pointer-keyed and timed planning decisions when bots are rebound after host migration.
    void resetAfterAuthorityChange();

    ZoneBotAssignment assignment(const BotSimulation& bot) const;
    ZoneWaveState waveState(int team) const;

private:
    friend class BotSimulation;
    struct StageSpot {
        bool valid = false;
        glm::dvec3 point{0.0};
        std::vector<int> nodes;
    };
    struct ZoneInfo {
        int id = -1;
        double radius = 2.0;
        std::size_t reachable = 0;
        std::vector<int> nodes, ring, far;
        std::vector<std::pair<int, int>> cellNode;
        std::vector<std::vector<int>> neighbors;
        std::array<std::vector<float>, 2> need;
        std::array<std::vector<unsigned short>, 2> ink;
        std::array<double, 2> needSum{{0.0, 0.0}};
        std::array<StageSpot, 2> stage;
        double y0 = -2.0, y1 = 6.0;
    };
    struct Wave {
        ZoneWaveState state = ZoneWaveState::Free;
        double started = 0.0;
        int zone = -1, pushes = 0;
    };
    struct BombAim {
        bool active = false, released = false;
        double yaw = 0.0, pitch = 0.0, remaining = 0.0, scan = 0.0;
    };
    struct BotState {
        ZoneBotAssignment assignment;
        bool hasAssignment = false;
        std::uint64_t objectiveSignature = 0;
        double holdUntil = 0.0, holdDuration = 0.0, jumpAt = 0.0;
        double faceUntil = 0.0, aimTimer = 0.0;
        int aimNode = -1, failures = 0;
        BombAim bomb;
    };

    double random();
    void assignTeam(int team, const std::vector<BotSimulation*>& bots);
    bool onZone(int zoneId, const glm::dvec3& point) const;
    void syncBot(BotSimulation& bot);
    bool planGoal(BotSimulation& bot, const std::vector<BotSimulation*>& bots);
    bool shouldEngage(const BotSimulation& bot, const PlayerSimulation* target) const;
    bool holdGround(const BotSimulation& bot, const PlayerSimulation* target) const;
    bool shouldUseSpecial(BotSimulation& bot, bool fighting);
    bool paintAim(BotSimulation& bot, double dt, glm::dvec3 move, bool moving,
                  double& wantYaw, double& wantPitch);
    void bombAim(BotSimulation& bot, double dt, double& wantYaw, double& wantPitch);
    void onRespawn(BotSimulation& bot, const std::vector<BotSimulation*>& bots);
    void maybeRotationJump(BotSimulation& bot, const std::vector<BotSimulation*>& bots);

    const ZoneControl& zones_;
    const StageData& stage_;
    const StageGeometry& geometry_;
    const FacePaint& paint_;
    const NavGraph& nav_;
    std::uint32_t seed_ = 1;
    RandomSource randomSource_;
    std::vector<ZoneInfo> info_;
    std::unordered_map<const BotSimulation*, ZoneBotAssignment> assignments_;
    std::unordered_map<const BotSimulation*, BotState> botState_;
    std::array<int, 2> focus_{{-1, -1}};
    std::array<double, 2> nextSpecial_{{0.0, 0.0}};
    double time_ = 0.0, scanTimer_ = 0.0, roleTimer_ = -1.0;
    std::uint64_t activeSignature_ = 0, activeSetSignature_ = 0;
    bool initialized_ = false;
    std::array<Wave, 2> waves_;
};

} // namespace restoration
