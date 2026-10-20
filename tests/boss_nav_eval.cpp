#include "BossNav.hpp"
#include "ReferenceData.hpp"
#include <iomanip>
#include <iostream>

int main(){
    using namespace restoration;
    std::cout<<std::fixed<<std::setprecision(6);
    for(const auto& stage:ReferenceStages()){
        if(stage.mode!="turf"||stage.onlineOnly)continue;
        StageGeometry geometry(stage);
        BossNav nav(stage,geometry);
        std::cout<<"STAGE "<<stage.id<<' '<<nav.floorY()<<' '<<nav.planIds().size()<<'\n';
        const auto& ids=nav.planIds();
        glm::dvec2 first{},last{};
        for(const double f:{0.1,0.3,0.5,0.7,0.9}){
            const int i=ids[static_cast<std::size_t>((ids.size()-1)*f)];
            const auto [x,z]=nav.xz(i);
            if(f==0.1)first={x,z};if(f==0.9)last={x,z};
            std::cout<<"PROBE "<<i<<' '<<x<<' '<<z<<' '<<nav.wallClear(x,z)<<' '<<nav.floorClear(x,z)<<' '
                <<nav.floorAt(x,z)<<' '<<nav.poseOk(x,z,0)<<' '<<nav.turnOk(x,z,1.2)<<' '<<nav.cast(x,z,0,8).dist<<'\n';
        }
        const auto path=nav.path(first.x,first.y,last.x,last.y);
        std::cout<<"PATH "<<path.size();for(const auto& p:path)std::cout<<' '<<p.x<<' '<<p.y;std::cout<<'\n';
    }
}
