#pragma once
#include "RenderPipeline.hpp"
#include "LensInk.hpp"
#include <functional>
#include <string>
#include <vector>

namespace restoration {
struct ScreenEffectsInput {
    bool alive=true,paused=false,onEnemy=false,specialActive=false,charging=false,reducedMotion=false;
    std::string form="kid",phase="playing",jumpPhase;
    double hp=100,horizontalSpeed=0,verticalSpeed=0,charge=0,invulnerability=0,remaining=180,jumpTime=0,jumpDuration=1.2,cameraShake=1;
    int width=1600,height=900;
    glm::dvec3 ownColor{1,.25,.01},enemyColor{.02,.1,1},cameraPosition{0};
    glm::dmat4 viewProjection{1};
    struct Cloud {glm::dvec3 position{0};bool own=false,live=true;};
    glm::dvec3 actorPosition{0};std::vector<Cloud> clouds;
};
class ScreenEffectsController {
public:
    explicit ScreenEffectsController(std::function<double()> random={});
    ScreenEffectsController(const ScreenEffectsController&)=delete;
    ScreenEffectsController& operator=(const ScreenEffectsController&)=delete;
    ScreenEffects Update(double dt,const ScreenEffectsInput& input);
    void Reset();
    void Damage(double amount,const glm::dvec3* attacker=nullptr);
    void Kill();
    void Splat(bool water=false);
    void Respawn();
    void Jump(const std::string& phase);
    void Land();
    void Special(const std::string& id);
    void Blast(glm::dvec3 position,double amount,glm::dvec3 color);
    void Impact(glm::dvec3 position,bool ownTeam,bool roller=false);
    void Countdown(int n);
    void MatchState(const std::string& state);
    LensInk& lens(){return lens_;}
    const ScreenEffects& effects()const{return output_;}
private:
    double Random(),Between(double a,double b);
    void Punch(double value);
    void DamageSplat(double amount);
    glm::dvec2 EdgePoint(double angle,double inset);
    void Flood(double dt);
    struct State {
        double speed=0,stretch=0,punch=0,punchV=0,blast=0,blastTime=9,chroma=0,edge=0,aura=0,auraPulse=0,heart=0,heartPhase=0,hurt=0,urgency=0,urgencyBase=0,swim=0,focus=0,chargePulse=0;
        double shimmer=0,kill=0,flash=0,desaturation=0,saturation=1,saturationPop=0,flood=0,floodDrip=0,hole=0,floodTime=0,jumpCharge=0,stepTime=0,rainTime=0,emergeTime=0,damage=0,damageTime=0;
        double time=0,landTime=-9,lastBlastTime=-9;glm::dvec3 lastBlast{0},attacker{0};
        bool wasJump=false,fullCharge=false,hasAttacker=false;std::string floodMode,lastForm="kid";
    } state_;
    std::function<double()> random_;
    unsigned seed_=0x195016u;
    LensInk lens_;
    ScreenEffectsInput input_;
    ScreenEffects output_;
};
}
