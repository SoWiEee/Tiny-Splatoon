#include "../src/render/WorldFxTransitions.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace restoration;
    try {
        const WorldFxTransitionConditions visible{true, true, false, false, false, false, false, true, true};
        const auto kid=WorldFxActorForm{};
        const WorldFxActorForm squid{true, false, false};
        auto events=ResolveWorldFxTransitions(kid,squid,visible);
        check(events.formPop&&!events.dive&&!events.emerge,"Dry kid-to-squid transition should only pop the form");

        auto ownInk=visible;ownInk.ownInk=true;
        events=ResolveWorldFxTransitions(kid,{true,true,false},ownInk);
        check(!events.formPop&&events.dive&&!events.emerge,"Own-ink squid entry should dive without a form pop");

        events=ResolveWorldFxTransitions({true,true,false},{true,false,false},visible);
        check(!events.formPop&&!events.dive&&events.emerge,"Leaving swim form should emerge separately");

        events=ResolveWorldFxTransitions({true,true,false},{true,false,true},visible);
        check(!events.emerge,"Changing from swim to climb remains submerged");
        events=ResolveWorldFxTransitions({true,false,true},kid,visible);
        check(!events.emerge,"Leaving a climb uses the climb-pop path, not an emerge splash");

        auto fresh=visible;fresh.fresh=true;
        events=ResolveWorldFxTransitions(kid,{true,true,false},fresh);
        check(!events.formPop&&!events.dive,"Fresh spawn transition noise should be suppressed");
        auto jumping=visible;jumping.superJump=true;
        events=ResolveWorldFxTransitions({true,true,false},kid,jumping);
        check(!events.emerge,"Super-jump arrival should not emit an emerge splash");
        auto recentClimb=visible;recentClimb.recentClimb=true;
        events=ResolveWorldFxTransitions({true,true,false},kid,recentClimb);
        check(!events.emerge,"Recent climb exit should use climb-pop rather than emerge splash");
        std::cout<<"WORLD_FX_TRANSITIONS formPop=separate dive=own-ink emerge=swim-only gates=pass\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
    return 0;
}
