#include "LobbyEffects.hpp"
#include "../game/LobbyPresentation.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace restoration;

namespace {
void require(bool condition,const std::string& message){
    if(!condition){std::cerr<<"lobby effects: "<<message<<'\n';std::exit(1);}
}

void testTrailAndChoreography(){
    LobbyEffects effects("unused-assets-root");
    LobbyActorState actor;actor.id=42;actor.team=1;actor.form="swim";actor.visible=true;
    actor.speed=10;actor.position={0,0,0};
    effects.Update({actor},{},0.0,0.0);
    actor.position.z=0.141;
    effects.Update({actor},{},1.0/60.0,1.0/60.0);
    require(effects.counts().trailDomes==2,"a 14.1 cm swim segment should place two 7 cm ink domes");

    actor.form="kid";
    effects.Update({actor},{},0.0,1.0/60.0);
    effects.Update({actor},{{"ready",actor.id,actor.position,1.0}},0.0,1.0/60.0);
    for(int i=0;i<8;++i)effects.Update({actor},{},0.05,0.066+0.05*i);
    require(effects.counts().sparkles==14,"ready should produce its source ring of fourteen sparkles after the hop");

    const auto beforeDive=effects.counts().particles;
    effects.Update({actor},{{"dive",actor.id,actor.position,0.7}},0.0,0.5);
    for(int i=0;i<3;++i)effects.Update({actor},{},0.1,0.6+0.1*i);
    require(effects.counts().particles>=beforeDive+9,"dive should emit nine source ink droplets at the hop cue");

    effects.Update({actor},{{"pop",actor.id,actor.position,0.8}},0.0,0.52);
    require(effects.counts().particles>beforeDive+9,"pop should add its source crown splash");
    effects.Update({actor},{{"superjump",actor.id,actor.position,0.4}},0.0,0.53);
    require(effects.counts().particles>beforeDive+9,"launch should add its source crown splash");
    effects.Update({actor},{{"land",actor.id,actor.position,1.0}},0.0,0.54);
    require(effects.counts().splats==1,"landing should add the source glossy splat decal");
}
}

int main(){
    testTrailAndChoreography();
    std::cout<<"lobby effects checks passed\n";
    return 0;
}

