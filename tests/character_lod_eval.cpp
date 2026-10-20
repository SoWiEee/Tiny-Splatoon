#include "../src/render/CharacterLod.hpp"
#include <cmath>
#include <iostream>

int main() {
    using namespace restoration;
    const auto projected=CharacterLod::screenPixels(glm::dmat4(1.0),{0,0.66,4},2.0,900);
    if(std::abs(projected-297.0)>1e-9)return 1;
    CharacterLod lod;
    int quality=0,local=0;
    double pixels=0,dt=0;
    while(std::cin>>quality>>local>>pixels>>dt) {
        if(quality<0) { lod=CharacterLod{}; continue; }
        const auto frame=lod.update(pixels,local!=0,dt,static_cast<CharacterQuality>(quality));
        std::cout<<static_cast<int>(frame.tier)<<' '<<static_cast<int>(frame.to)<<' '
                 <<frame.fade<<' '<<frame.transitioning<<'\n';
    }
}
