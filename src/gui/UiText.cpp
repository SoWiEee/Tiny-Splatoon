#include "UiText.hpp"

#include <array>

namespace restoration {
namespace {
struct LabelEntry { std::string_view id, label; };

constexpr std::array<LabelEntry, 12> MainLabels{{
    {"shooter", "潑墨射擊槍"}, {"twins", "雙焰手槍"}, {"brolly", "天篷防空傘"},
    {"blaster", "爆點爆破槍"}, {"spinner", "暴風旋轉槍"}, {"charger", "閃光蓄力槍"},
    {"bow", "潮線弓"}, {"roller", "湧浪滾筒"}, {"brush", "刷刷毛筆"},
    {"blade", "鹽潮彎刀"}, {"mitts", "海綿拳套"}, {"bucket", "艙底潑桶"},
}};
constexpr std::array<LabelEntry, 15> SubLabels{{
    {"bomb", "濺墨炸彈"}, {"sticky", "黏著蓄爆彈"}, {"burst", "爆彈丸"},
    {"shaker", "搖晃炸彈"}, {"seeker", "爬行追蹤炸彈"}, {"waddle", "蹣跚炸彈"},
    {"torpedo", "潮汐魚雷"}, {"tracer", "追跡彈"}, {"boomerang", "旋返迴力鏢"},
    {"scan", "回聲偵測球"}, {"curtain", "滴落墨簾"}, {"sprinkler", "旋轉灑墨器"},
    {"mine", "潛伏地雷"}, {"beacon", "跳躍信標"}, {"mist", "濃霧炸彈"},
}};
constexpr std::array<LabelEntry, 19> SpecialLabels{{
    {"slam", "潮汐重擊"}, {"storm", "墨汁風暴"}, {"barrage", "濺墨炸彈連發"},
    {"barrage_sticky", "黏著蓄爆彈連發"}, {"barrage_burst", "爆彈丸連發"},
    {"barrage_seeker", "爬行炸彈連發"}, {"barrage_mist", "濃霧炸彈連發"},
    {"bubbler", "泡泡護盾"}, {"sonar", "深海聲納"}, {"strike", "漩渦打擊"},
    {"zooka", "旋風火箭炮"}, {"wail", "嚎叫音箱"}, {"kraken", "海怪"},
    {"blower", "泡泡吹風器"}, {"jetpack", "墨汁噴射器"}, {"stamp", "巨型印章"},
    {"booyah", "歡呼能量球"}, {"zipcaster", "觸手飛索"}, {"crab", "螃蟹機甲"},
}};
constexpr std::array<LabelEntry, 8> StageLabels{{
    {"tidewater", "潮汐廣場"}, {"kelpline", "海帶線貨運站"}, {"halyard", "升帆遊艇港"},
    {"saltpan", "鹽田盆地"}, {"crossmarket", "十字路口市場"}, {"lockgate", "閘門運河"},
    {"terraces", "台地高地"}, {"cargo", "貨運碼頭"},
}};

constexpr std::array<std::string_view, 8> HairLabels{{"潮汐", "尖刺", "雙辮", "波波頭", "馬尾", "冠羽", "髮髻", "側梳"}};
constexpr std::array<std::string_view, 9> SkinLabels{{"玫瑰調", "蜜桃", "古銅", "可可", "瓷白", "蜂蜜", "橄欖", "栗棕", "烏木"}};
constexpr std::array<std::string_view, 10> OutfitLabels{{"滾邊T恤", "夜色細條紋", "插肩運動衫", "V字條紋T恤", "海魂條紋", "飛濺T恤", "飛濺迷彩", "漸層染色T恤", "專業球衣", "運動外套"}};
constexpr std::array<std::string_view, 8> EyeLabels{{"琥珀", "潟湖", "紫羅蘭", "萊姆", "玫瑰", "榛果", "冰霜", "餘燼"}};
constexpr std::array<std::string_view, 4> HatLabels{{"無", "後扣棒球帽", "毛帽", "漁夫帽"}};
constexpr std::array<std::string_view, 4> BrowLabels{{"經典", "濃眉", "彎眉", "平眉"}};
constexpr std::array<std::string_view, 10> PresetLabels{{"新手", "疾速", "皮普", "珊瑚", "馬洛", "激流", "諾麗", "蘇琪", "海帶", "船長"}};

template<std::size_t N>
std::string_view Find(std::string_view id, const std::array<LabelEntry, N>& labels) noexcept {
    for (const auto& entry : labels) if (entry.id == id) return entry.label;
    return {};
}

template<std::size_t N>
std::string_view At(int index, const std::array<std::string_view, N>& labels) noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < N ? labels[static_cast<std::size_t>(index)] : std::string_view{};
}
} // namespace

std::string_view EquipmentLabel(EquipmentCategory category, std::string_view id) noexcept {
    switch (category) {
    case EquipmentCategory::Main: return Find(id, MainLabels);
    case EquipmentCategory::Sub: return Find(id, SubLabels);
    case EquipmentCategory::Special: return Find(id, SpecialLabels);
    }
    return {};
}

std::string_view StageLabel(std::string_view id) noexcept {
    return Find(id, StageLabels);
}

std::string_view StyleLabel(std::string_view kind, int index) noexcept {
    if (kind == "hair") return At(index, HairLabels);
    if (kind == "skin") return At(index, SkinLabels);
    if (kind == "outfit") return At(index, OutfitLabels);
    if (kind == "eyes") return At(index, EyeLabels);
    if (kind == "hat") return At(index, HatLabels);
    if (kind == "brows") return At(index, BrowLabels);
    if (kind == "preset") return At(index, PresetLabels);
    return {};
}
} // namespace restoration
