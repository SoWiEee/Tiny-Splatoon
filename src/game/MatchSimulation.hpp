#pragma once
#include "BotSimulation.hpp"
#include "BossSimulation.hpp"
#include "CombatWorld.hpp"
#include "CharacterRenderData.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace restoration {
struct MatchOptions {
    bool practice=false,autopilot=false;
    std::string mode="turf";
    int duration=0;
    std::uint32_t seed=1;
    std::string main="shooter",sub,special,difficulty="normal";
    std::string playerName="You";
    CharacterStyle style;
};
struct MatchRosterEntry { std::string name; CharacterStyle style; int team=0,slot=0; bool local=false; };
enum class MatchPhase { Intro, Playing, Finish, Judge, Results };
struct BossResult { bool win=false; double time=0,hp=0,maxHp=0; int phase=1; };
struct MatchResult { std::array<double,2> coverage; int winner=0; std::optional<BossResult> boss; };

class MatchSimulation {
public:
    MatchSimulation(const StageData& stage,const StageGeometry& geometry,FacePaint& paint,const NavGraph& nav,MatchOptions options={});
    MatchSimulation(MatchSimulation&&)=delete;
    MatchSimulation& operator=(MatchSimulation&&)=delete;
    void restart();
    void tick(double dt);
    void startPlaying();
    void finishJudging();
    // CPU-only animated sockets stay synchronized with fixed simulation ticks.
    void setBossPoseSink(std::function<void(BossSimulation&)> sink);
    PlayerSimulation& local() { return *actors_.front(); }
    const std::vector<std::unique_ptr<PlayerSimulation>>& actors() const { return actors_; }
    const std::vector<std::unique_ptr<BotSimulation>>& bots() const { return bots_; }
    const std::vector<MatchRosterEntry>& roster() const { return roster_; }
    CombatWorld& world() { return world_; }
    BossSimulation* boss() { return boss_.get(); }
    const BossSimulation* boss() const { return boss_.get(); }
    MatchPhase phase() const { return phase_; }
    double remaining() const { return remaining_; }
    const std::optional<MatchResult>& result() const { return result_; }
    bool paused=false;
private:
    double random();
    std::vector<std::string> teamWeapons(const std::string& first);
    CharacterStyle randomStyle();
    void judge();
    std::uint32_t seed_=1; // The shared stream outlives actor cleanup callbacks.
    const StageData& stage_;
    const StageGeometry& geometry_;
    FacePaint& paint_;
    const NavGraph& nav_;
    MatchOptions options_;
    CombatWorld world_;
    std::vector<std::unique_ptr<PlayerSimulation>> actors_;
    std::vector<std::unique_ptr<BotSimulation>> bots_;
    std::vector<BotSimulation*> botPointers_;
    std::vector<PlayerSimulation*> bossSquad_;
    std::unique_ptr<BossSimulation> boss_;
    std::function<void(BossSimulation&)> bossPoseSink_;
    std::vector<MatchRosterEntry> roster_;
    MatchPhase phase_=MatchPhase::Intro;
    double remaining_=180.0,stateTime_=0.0;
    std::optional<MatchResult> result_;
};
}
