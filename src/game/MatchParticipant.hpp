#pragma once

#include "CharacterStyle.hpp"
#include <cstdint>
#include <string>

namespace restoration {
using PeerId = std::uint32_t;

struct MatchParticipant {
    std::uint32_t nid = 0;
    PeerId owner = 0;
    bool bot = false;
    int team = 0, slot = 0;
    std::string name, main = "shooter", sub, special;
    CharacterStyle style{};
};
} // namespace restoration
