#pragma once

#include "../game/CombatWorld.hpp"
#include <vector>

namespace restoration {
// The session drains once per fixed tick; frame consumers retain every tick in order.
class MatchEventBatch {
public:
    void Collect(CombatWorld& world) {
        tick_=world.takeVisualEvents();
        frame_.insert(frame_.end(),tick_.begin(),tick_.end());
    }
    const std::vector<WorldVisualEvent>& tick() const { return tick_; }
    std::vector<WorldVisualEvent> TakeFrame() {
        std::vector<WorldVisualEvent> events;
        events.swap(frame_);
        return events;
    }
private:
    std::vector<WorldVisualEvent> tick_,frame_;
};
} // namespace restoration
