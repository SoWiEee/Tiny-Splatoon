#include "match_setup_fixtures.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
bool sameStyle(const restoration::CharacterStyle& a,const restoration::CharacterStyle& b) {
    return a.hair==b.hair&&a.skin==b.skin&&a.outfit==b.outfit&&a.eyes==b.eyes&&a.hat==b.hat&&a.brows==b.brows;
}
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
}
int main() {
    using namespace restoration;
    try {
        const StageData* stage=nullptr;
        for(const auto& s:ReferenceStages())if(s.id=="halyard"&&s.mode=="turf")stage=&s;
        check(stage!=nullptr,"Missing Halyard");
        StageGeometry geometry(*stage);FacePaint paint(*stage,geometry);NavGraph nav(*stage,geometry);
        std::optional<glm::dvec3> paintTarget;
        for(std::size_t i=0;i<stage->faces.size();++i) {
            const auto& f=stage->faces[i];const auto& g=paint.faceGrid(static_cast<int>(i));
            if(f.turf&&g.offset>=0&&g.columns>8&&g.rows>8&&!paint.dead()[g.offset+(g.rows/2)*g.columns+g.columns/2]) {
                paintTarget=f.origin+f.u*(f.width/2)+f.v*(f.height/2)+f.normal*0.02;break;
            }
        }
        check(paintTarget.has_value(),"Missing live Turf test target");
        for(const auto& source:test::matchSetupFixtures) {
            MatchOptions options;options.seed=source.seed;options.practice=source.practice;options.autopilot=source.autopilot;
            options.main=source.main;options.sub=source.sub;options.special=source.special;options.playerName=source.name;options.style=source.style;
            MatchSimulation match(*stage,geometry,paint,nav,options);
            for(int attempt=0;attempt<2;++attempt) {
                if(attempt)match.restart();
                const auto label="seed="+std::to_string(source.seed)+" practice="+std::to_string(source.practice)+" autopilot="+std::to_string(source.autopilot);
                check(match.actors().size()==source.actors.size()&&match.roster().size()==source.actors.size(),label+" actor count");
                check(match.bots().size()==source.bots,label+" bot count");
                check(match.phase()==(source.practice?MatchPhase::Playing:MatchPhase::Intro),label+" starting phase");
                for(std::size_t i=0;i<source.actors.size();++i) {
                    const auto& e=source.actors[i];const auto& a=*match.actors()[i];const auto& r=match.roster()[i];
                    const auto actorLabel=label+" actor="+std::to_string(i);
                    check(r.name==e.name&&r.team==e.team&&r.slot==e.slot&&r.local==e.local,actorLabel+" identity");
                    check(a.botControlled()==e.bot,actorLabel+" source bot-controller ownership");
                    check(sameStyle(r.style,e.style),actorLabel+" style / shared RNG ordering");
                    check(a.mainId()==e.main&&a.subId()==e.sub&&a.specialId()==e.special,actorLabel+" loadout / shared RNG ordering");
                    check(glm::length(a.state.pos-e.pos)<1e-9&&std::abs(a.state.yaw-e.yaw)<1e-12&&a.state.invuln==e.invuln,actorLabel+" placement");
                }
                if(source.main=="shooter") {
                    match.local().input.fire=true;match.local().tick(1.0/60);
                    const auto& shots=match.world().projectiles();
                    check(!shots.empty(),label+" first shot");
                    check(std::abs(shots.front().seed-source.nextRandom[2])<1e-15,label+" actor must continue the same setup/brain RNG stream");
                }
            }
        }
        for(const auto& source:test::matchLifecycleFixtures) {
            MatchOptions options;options.seed=source.seed;
            MatchSimulation match(*stage,geometry,paint,nav,options);
            for(const auto& actor:match.actors()) { actor->state.alive=false;actor->state.respawnTimer=1000; }
            if(source.paintTeam>=0)paint.splat(*paintTarget,2.0,source.paintTeam,0.3);
            const auto expectedCoverage=paint.coverage();
            if(source.paintTeam>=0)check(expectedCoverage[source.paintTeam]>0&&expectedCoverage[1-source.paintTeam]==0,"Missing winner test paint");
            bool rejected=false;
            try { match.finishJudging(); } catch(const std::logic_error&) { rejected=true; }
            check(rejected,"Results must wait for the judge phase");
            int frame=0;
            for(const auto& sample:source.samples) {
                while(frame<sample.frame) {
                    ++frame;match.paused=frame>=100&&frame<110;match.tick(0.1);
                }
                if(sample.phase==MatchPhase::Results)match.finishJudging();
                check(match.phase()==sample.phase&&std::abs(match.remaining()-sample.time)<1e-9,
                      "Source clock/phase first difference seed="+std::to_string(source.seed)+" frame="+std::to_string(frame));
            }
            check(match.result().has_value()&&match.result()->coverage==expectedCoverage&&match.result()->winner==source.winner,
                  "Source judging winner / shared RNG tie-break differs");
            const auto frozen=match.result()->coverage;
            paint.splat(*paintTarget,2.0,0,0.3);
            check(match.result()->coverage==frozen,"Judge result must preserve the coverage snapshot");
            match.restart();check(!match.result().has_value(),"Restart retained prior result");
        }
        std::cout<<test::matchSetupFixtures.size()<<" actual-source roster/setup RNG cases, deterministic restart, and "
                 <<test::matchLifecycleFixtures.size()<<" clock/pause/judge traces passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
