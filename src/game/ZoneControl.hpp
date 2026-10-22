#pragma once

#include <array>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace restoration {

class FacePaint;
class PlayerSimulation;
class StageGeometry;
struct StageData;
namespace network { struct ZoneAuthorityState; }

using ZonePolygon = std::vector<glm::dvec2>; // world x/z

struct ZoneDefinition {
    std::vector<ZonePolygon> polygons;
    double y0 = -2.0, y1 = 6.0;
};

struct ZoneStageDefinition {
    std::string_view stageId;
    std::vector<ZoneDefinition> center;
    ZoneDefinition side; // mirrored across both world axes for team 1
};

const ZoneStageDefinition* FindZoneStageDefinition(std::string_view stageId) noexcept;

struct ZoneFloodState {
    int team = -1; // -1 wipes to bare floor
    double start = 0.0, duration = 0.0, reach = 0.0;
    bool easeOut = false;
};

struct ZoneRegion {
    std::vector<int> cells; // flat FacePaint grid indices
    std::vector<glm::dvec3> cellCenters; // same order as cells, for the CPU flood front
    std::vector<ZonePolygon> polygons;
    double y0 = -2.0, y1 = 6.0;
};

struct ZoneState {
    int id = -1;
    std::string kind; // "center" or "side"
    int home = -1; // -1 center, 0/1 side home
    int owner = -1;
    std::array<double, 2> share{};
    double hold = 0.0, warnAt = -99.0;
    bool warnArmed = true;
    glm::dvec3 center{0.0};
    double reach = 0.0;
    ZoneRegion region;
    std::optional<ZoneFloodState> flood;
};

struct ZoneObjective {
    std::string id; // "center", "sideA", "sideB"
    std::vector<int> zones; // indices in ZoneControl::zones()
    int home = -1;
};

enum class ZoneEventType {
    ZoneOwnerChanged,
    ControlChanged,
    PenaltyAdded,
    ObjectiveChanged,
    Contested,
    FinalStarted,
    OvertimeStarted,
    MatchEnded,
};

struct ZoneEvent {
    ZoneEventType type = ZoneEventType::ControlChanged;
    int zone = -1, owner = -1, previousOwner = -1, team = -1, winner = -1;
    std::string objective, reason;
    std::vector<int> activeZones;
    std::array<double, 2> counts{}, penalties{};
    double time = 0.0, share = 0.0, penalty = 0.0, start = 0.0, end = 0.0;
    int losing = -1;
    bool final = false, moved = false;
};

struct ZonePenaltyRecord {
    double time = 0.0, start = 0.0, end = 0.0, penalty = 0.0, total = 0.0;
    int team = -1;
};

class ZoneControl {
public:
    using RandomSource = std::function<double()>;

    ZoneControl(const StageData& stage, const StageGeometry& geometry, FacePaint& paint,
                double duration, RandomSource random);
    ZoneControl(const ZoneControl&) = delete;
    ZoneControl& operator=(const ZoneControl&) = delete;

    void update(double dt, const std::vector<std::unique_ptr<PlayerSimulation>>& actors);
    bool timeUp(); // true means finish now; false means overtime has started
    network::ZoneAuthorityState captureAuthorityState() const;
    bool applyAuthorityState(const network::ZoneAuthorityState& state);
    bool adoptAuthorityState(const network::ZoneAuthorityState& state);
    void setNetworkAuthority(bool authoritative) { networkAuthority_ = authoritative; }

    const std::vector<ZoneState>& zones() const { return zones_; }
    const std::vector<ZoneObjective>& objectives() const { return objectives_; }
    const std::vector<int>& activeIndices() const { return activeIndices_; }
    const ZoneObjective& objective() const { return objectives_[activeObjective_]; }
    int owner() const { return owner_; }
    const std::array<double, 2>& counts() const { return count_; }
    const std::array<double, 2>& penalty() const { return penalty_; }
    const std::array<double, 2>& holdTime() const { return holdT_; }
    bool overtime() const { return overtime_; }
    bool final() const { return final_; }
    bool placeholder() const { return placeholder_; }
    double nextSwap() const { return nextSwap_; }
    double time() const { return clock_; }
    double overtimeTime() const { return overtimeT_; }
    double neutralTime() const { return neutralT_; }
    bool ended() const { return winner_.has_value(); }
    std::optional<int> winner() const { return winner_; }
    const std::string& reason() const { return reason_; }
    const std::vector<ZoneEvent>& events() const { return events_; }
    const std::vector<ZonePenaltyRecord>& penaltyLog() const { return penaltyLog_; }
    bool contains(const glm::dvec3& point) const;

private:
    void sample();
    void setZoneOwner(ZoneState& zone, int owner);
    void setOwner(int owner);
    void addPenalty(int team);
    void tickCount(int team, double dt);
    void fillSpecials(double dt, const std::vector<std::unique_ptr<PlayerSimulation>>& actors);
    void schedule();
    void swapObjective(int next, bool finalLock);
    void startFinal();
    void updateOvertime(double dt);
    void end(int winner, std::string reason);
    void flood(ZoneState& zone, int team);
    void advanceFloods(double dt);
    void emit(ZoneEvent event);

    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    double duration_ = 300.0, clock_ = 0.0, sampleT_ = 0.0, neutralT_ = 0.0, overtimeT_ = 0.0;
    std::vector<ZoneState> zones_;
    std::vector<ZoneObjective> objectives_;
    std::vector<int> activeIndices_;
    int activeObjective_ = 0, owner_ = -1, lastOwner_ = -1, overtimeLosing_ = -1;
    std::array<double, 2> count_{{100.0, 100.0}}, penalty_{{0.0, 0.0}}, start_{{100.0, 100.0}};
    std::array<std::optional<double>, 2> tieEnd_{};
    std::array<double, 2> holdT_{};
    double nextSwap_ = 0.0;
    bool final_ = false, overtime_ = false, placeholder_ = false;
    bool networkAuthority_ = true;
    std::optional<int> winner_;
    std::string reason_;
    std::vector<ZoneEvent> events_;
    std::vector<ZonePenaltyRecord> penaltyLog_;
    RandomSource random_;
    struct FloodRun { int zone = -1, team = -1; std::vector<double> distance; std::vector<int> cells; std::size_t next = 0; double elapsed = 0.0, duration = 0.0, reach = 0.0; bool easeOut = false; };
    std::vector<FloodRun> floods_;
};

} // namespace restoration
