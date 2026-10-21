#pragma once

namespace restoration {

struct WorldFxActorForm {
    bool squid = false;
    bool swimming = false;
    bool climbing = false;
};

struct WorldFxTransitionConditions {
    bool initialized = false;
    bool alive = false;
    bool fresh = false;
    bool superJump = false;
    bool special = false;
    bool ownInk = false;
    bool recentClimb = false;
    bool nearForm = false;
    bool nearSurface = false;
};

struct WorldFxTransitionEvents {
    bool formPop = false;
    bool dive = false;
    bool emerge = false;
};

// Keep the source actor:form event independent from actor:dive/emerge. In the
// reference, forming into squid while already in own ink skips the form pop,
// then the separate dive event supplies the splash and wake transition.
inline WorldFxTransitionEvents ResolveWorldFxTransitions(
    WorldFxActorForm previous, WorldFxActorForm current,
    WorldFxTransitionConditions canEmit) {
    WorldFxTransitionEvents events;
    if (!canEmit.initialized || !canEmit.alive) return events;
    events.formPop = !canEmit.fresh && !canEmit.superJump && !canEmit.special &&
        !canEmit.ownInk && canEmit.nearForm && previous.squid != current.squid;
    events.dive = !canEmit.fresh && canEmit.nearSurface && current.swimming &&
        !previous.swimming && !previous.climbing;
    events.emerge = !canEmit.fresh && !canEmit.superJump && canEmit.nearSurface &&
        !canEmit.recentClimb && previous.swimming && !current.swimming && !current.climbing;
    return events;
}

} // namespace restoration
