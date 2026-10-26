#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include "NavGraph.hpp"
#include "BossSimulation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace restoration {

double CombatWorld::paint(PlayerSimulation& owner,glm::dvec3 at,double radius,double seed,const SplatOptions& options,bool special) {
    struct ActorContext {
        FacePaint& paint;std::uint32_t previous;
        ~ActorContext(){paint.setNetworkActor(previous);}
    } context{paint_,paint_.networkActor()};
    paint_.setNetworkActor(owner.networkNid());
    const double area=paint_.splat(at,radius,owner.team(),seed,options);
    owner.turfPainted_+=area;
    if(!special && !owner.combatPose().special) owner.state.specialCharge=std::min(owner.weapon().number("specialCost",190),owner.state.specialCharge+area*GetEquipment(EquipmentCategory::Special,owner.specialId()).number("chargeRate",0.8));
    return area;
}

void CombatWorld::paintDevice(CombatDevice& device,glm::dvec3 at,double radius) {
    if(!device.owner || radius<=0) return;
    const auto& sub=GetEquipment(EquipmentCategory::Sub,device.kind);
    if(device.kind=="torpedo" && !device.locked) radius=sub.number("fallbackPaint");
    paint(*device.owner,at,radius,device.owner->random(),{},device.special);
    int satellites=4;
    double minRadius=0.55,maxRadius=1.0,minSize=0.5,maxSize=0.9;
    if(device.kind=="torpedo") {
        satellites=device.locked?5:0;
        minSize=0.55;maxSize=1.0;
    }
    for(int i=0;i<satellites;++i) {
        const double angle=device.owner->random()*2*3.14159265358979323846;
        const double r=radius*(minRadius+(maxRadius-minRadius)*device.owner->random());
        const double size=minSize+(maxSize-minSize)*device.owner->random();
        paint(*device.owner,at+glm::dvec3(std::cos(angle)*r,0.3,std::sin(angle)*r),size,device.owner->random(),{},device.special);
    }
}

} // namespace restoration
