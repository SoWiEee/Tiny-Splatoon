#include "UiText.hpp"

#include <array>
#include <cassert>
#include <string_view>

using namespace restoration;

namespace {
bool ContainsHan(std::string_view text) {
    for (const unsigned char ch : text) if (ch >= 0xe3) return true;
    return false;
}

void CheckEquipment(const std::vector<EquipmentData>& equipment, EquipmentCategory category) {
    for (const auto& item : equipment) {
        const auto label = EquipmentLabel(category, item.id);
        assert(!label.empty());
        assert(ContainsHan(label));
    }
}
}

int main() {
    CheckEquipment(MainEquipment(), EquipmentCategory::Main);
    CheckEquipment(SubEquipment(), EquipmentCategory::Sub);
    CheckEquipment(SpecialEquipment(), EquipmentCategory::Special);
    assert(MainEquipment().size() + SubEquipment().size() + SpecialEquipment().size() == 46);
    assert(EquipmentLabel(EquipmentCategory::Main, "bomb").empty());
    assert(EquipmentLabel(EquipmentCategory::Sub, "shooter").empty());
    assert(EquipmentLabel(EquipmentCategory::Special, "unknown").empty());

    constexpr std::array<std::string_view, 8> stages{{"tidewater", "kelpline", "halyard", "saltpan", "crossmarket", "lockgate", "terraces", "cargo"}};
    for (const auto id : stages) {
        const auto label = StageLabel(id);
        assert(!label.empty() && ContainsHan(label));
    }
    assert(StageLabel("unknown").empty());

    struct StyleRange { std::string_view kind; int count; };
    constexpr std::array<StyleRange, 7> styles{{{"hair", 8}, {"skin", 9}, {"outfit", 10}, {"eyes", 8}, {"hat", 4}, {"brows", 4}, {"preset", 10}}};
    for (const auto style : styles) {
        for (int i = 0; i < style.count; ++i) {
            const auto label = StyleLabel(style.kind, i);
            assert(!label.empty() && ContainsHan(label));
        }
        assert(StyleLabel(style.kind, -1).empty());
        assert(StyleLabel(style.kind, style.count).empty());
    }
    assert(StyleLabel("unknown", 0).empty());
    assert(StyleLabel("hair", 8).empty());
    return 0;
}
