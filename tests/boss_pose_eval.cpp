#include "../src/game/BossPose.hpp"
#include "../src/game/BossPoseInput.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

int main() try {
    using namespace restoration;std::cout<<std::setprecision(17);std::unique_ptr<BossPose> pose;std::string line;
    while(std::getline(std::cin,line)){
        if(line=="case"){pose=std::make_unique<BossPose>();continue;}
        if(line=="end")continue;
        if(!pose)return 2;
        BossPoseInput i;std::size_t rings=0,barrels=0;bool sample=false;std::istringstream row(line);
        if(!(row>>i.dt>>i.position.x>>i.position.y>>i.position.z>>i.yaw>>i.speed>>i.turn>>i.hurt>>i.phase>>i.stunned>>i.dead>>i.hasAim>>i.aim.x>>i.aim.y>>i.aim.z>>i.move>>i.movePhase>>i.moveTime>>i.phaseDuration>>i.hasParams>>i.wall>>i.count>>i.flash>>i.weakFlash>>i.trigger>>rings))return 3;
        if(i.move=="-")i.move.clear();if(i.movePhase=="-")i.movePhase.clear();if(i.trigger=="-")i.trigger.clear();
        if(rings>32)return 4;i.rings.resize(rings);for(auto& value:i.rings)if(!(row>>value))return 5;
        if(!(row>>barrels)||barrels>32)return 6;i.barrels.resize(barrels);for(auto& barrel:i.barrels)for(auto& value:barrel)if(!(row>>value))return 7;
        if(!(row>>sample))return 8;
        pose->Update(i);if(!sample)continue;
        std::cout<<"{\"skin\":[";bool comma=false;
        for(const auto& matrix:pose->hullbreaker().skin){if(comma)std::cout<<',';comma=true;std::cout<<'[';const auto* data=glm::value_ptr(matrix);for(int n=0;n<16;n++){if(n)std::cout<<',';std::cout<<data[n];}std::cout<<']';}
        std::cout<<"],\"shapes\":[";comma=false;
        for(const auto& h:pose->hitShapes()){if(comma)std::cout<<',';comma=true;std::cout<<'['<<h.pos.x<<','<<h.pos.y<<','<<h.pos.z<<','<<h.radius<<','<<int(h.weak)<<','<<int(h.active)<<']';}
        std::cout<<"],\"uniforms\":{";comma=false;
        for(const auto& [name,values]:pose->materialUniforms()){if(comma)std::cout<<',';comma=true;std::cout<<'"'<<name<<"\":[";for(std::size_t n=0;n<values.size();n++){if(n)std::cout<<',';std::cout<<values[n];}std::cout<<']';}
        std::cout<<"}}\n";
    }
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
