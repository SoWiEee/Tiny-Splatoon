#pragma once
#include <array>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

namespace restoration {
// BossModel.update's animation state, independent of simulation and GL lifetime.
struct BossPoseInput {
    double dt=1./60;
    glm::dvec3 position{0},aim{0};
    double yaw=0,speed=0,turn=0,hurt=0;
    int phase=1;
    bool stunned=false,dead=false,hasAim=false;
    std::string move,movePhase;
    double moveTime=0,phaseDuration=0;
    bool hasParams=false,wall=true;
    int count=3;
    std::vector<double> rings;
    std::vector<std::array<double,4>> barrels;
    double flash=0;
    bool weakFlash=false;
    double weakFlashAmount=0;
    std::string trigger;
};
}
