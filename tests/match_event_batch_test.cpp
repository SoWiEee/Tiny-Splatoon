#include "../src/app/MatchEventBatch.hpp"
#include "ReferenceData.hpp"
#include <stdexcept>
#include <iostream>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    using namespace restoration;
    try{
        const auto& stage=ReferenceStages().front();
        StageGeometry geometry(stage);FacePaint paint(stage,geometry);CombatWorld world(stage,geometry,paint);
        MatchEventBatch batch;
        WorldVisualEvent first;first.id=17;first.kind="explosion";first.seed=.3;
        world.emitVisual(first);batch.Collect(world);
        check(batch.tick().size()==1&&batch.tick()[0].id==17,"Session did not collect the fixed-tick event");
        check(world.takeVisualEvents().empty(),"Session did not drain the world exactly once");
        const auto retained=batch.tick();
        WorldVisualEvent second;second.id=18;second.kind="bulletImpact";world.emitVisual(second);batch.Collect(world);
        check(batch.tick().size()==1&&batch.tick()[0].id==18&&retained[0].id==17,"Tick consumers received stale or aliased events");
        batch.Collect(world);
        check(batch.tick().empty(),"Empty tick replayed the preceding event");
        const auto frame=batch.TakeFrame();
        check(frame.size()==2&&frame[0].id==17&&frame[1].id==18&&frame[0].seed==.3,"Catch-up ticks lost event order or payload");
        check(batch.TakeFrame().empty(),"Zero-tick frame replayed presentation events");
        std::cout<<"Single drain, ordered multi-tick fan-out, and zero-tick frame passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
