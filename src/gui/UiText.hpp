#pragma once

#include "ArsenalData.hpp"
#include <string_view>

namespace restoration {

// Traditional Chinese display labels; stable source IDs and style indices are unchanged.
std::string_view EquipmentLabel(EquipmentCategory category, std::string_view id) noexcept;
std::string_view StageLabel(std::string_view id) noexcept;
std::string_view StyleLabel(std::string_view kind, int index) noexcept;

} // namespace restoration
