#pragma once

#include "../game/ArsenalData.hpp"
#include <array>
#include <string_view>

namespace restoration {

inline std::string_view EquipmentDescriptionZhTw(EquipmentCategory category, std::string_view id) noexcept {
    struct Entry { std::string_view id, description; };
    static constexpr std::array<Entry, 12> main={{
        {"shooter","連續射出墨滴，射速穩定，適合各種戰況。"},
        {"twins","成對手槍；跳射可翻滾閃避，翻滾後站定可遠距快速連射，移動則回到雙槍模式。"},
        {"brolly","散射墨彈；按住展開傘面，為自己與隊友擋下攻擊，持續按住可推出滾動傘牆。"},
        {"blaster","慢速發射、在空中爆裂；直接命中可立即擊倒對手。"},
        {"spinner","蓄力後連續射墨；蓄力越久，射程越遠、持續射擊時間越長。"},
        {"charger","按住蓄力，放開射出可貫穿的長線墨流；滿蓄力命中可擊倒對手。"},
        {"bow","拉弓填滿兩圈後一次射出三箭；地面水平散射、空中垂直散射，超過第一圈的箭會固定並爆裂。"},
        {"roller","輾出寬幅墨路，揮動滾筒可重擊潑墨。"},
        {"brush","地面疾行留下窄墨路，左右揮擊可甩出墨團。"},
        {"blade","點按快速斬擊，甩出弧形墨；蓄力上斬會放出遠距墨波，滿蓄力可一擊擊倒。"},
        {"mitts","射出墨拳；舉起拳套時可吸收部分正面傷害。按住射擊並跳躍可蓄力長跳，落地潑墨並黏上牆面，攀附會耗墨。"},
        {"bucket","以弧線潑出墨浪；瞄高可投得更遠並越過掩體，任何距離都能造成完整傷害，兩擊可擊倒。"},
    }};
    static constexpr std::array<Entry, 15> sub={{
        {"bomb","落地後彈跳並啟動，近距離爆炸可擊倒對手。"},
        {"sticky","命中地面、牆面或天花板後黏住，稍候以大範圍爆炸。"},
        {"burst","碰撞即爆，消耗較少；兩次直擊或三次近失可擊倒對手。"},
        {"shaker","持續搖晃蓄能，最多連續爆炸三次並向前跳躍。"},
        {"seeker","追向最近的對手並沿途留墨，抵達後爆炸；轉向較寬，晚點閃避即可。"},
        {"waddle","落地後會朝附近感知到的敵人蹣跚前進；若附近無人，就在落點爆炸。"},
        {"torpedo","空中鎖定對手後衝刺追擊，化為墨滴爆裂；同時只能投出一枚。"},
        {"tracer","低角度快速射出，可掠地彈跳並在每次彈跳留下墨跡；命中與墨跡都會標記敵人。"},
        {"boomerang","旋轉懸浮後繞回並爆炸；擊中敵人會貼身爆裂，同時只能投出一枚。"},
        {"scan","爆開形成感知雲，觸及的敵人會被全隊追蹤，不造成傷害。"},
        {"curtain","落下持續墨簾，阻擋敵方玩家與射擊；受攻擊後會更快消退。"},
        {"sprinkler","黏附表面並定時向周圍灑墨，直到被擊破或你被擊倒。"},
        {"mine","埋在腳下的自家墨水中隱藏，敵人靠近會受擊並被標記；最多放置兩枚。"},
        {"beacon","隊友超級跳躍的落點；最多放置三個，每個可供兩次跳躍。"},
        {"mist","釋放毒霧使敵人減速並耗墨；直接命中會使中毒效果持續至霧氣散去。"},
    }};
    static constexpr std::array<Entry, 19> special={{
        {"slam","高高躍起後重擊地面，爆出巨大的墨汁衝擊波。"},
        {"storm","投出雨雲，持續浸染下方地面。"},
        {"barrage","數秒內可快速連投濺墨炸彈，不消耗墨水；仍可使用主要武器。"},
        {"barrage_sticky","數秒內可將黏著蓄爆彈連續貼向眼前牆面，不消耗墨水；仍可使用主要武器。"},
        {"barrage_burst","數秒內可快速連射爆彈丸，不消耗墨水；仍可使用主要武器。"},
        {"barrage_seeker","數秒內可派出一群爬行追蹤炸彈追擊敵人並留下墨跡，不消耗墨水；仍可使用主要武器。"},
        {"barrage_mist","數秒內可用濃霧炸彈覆蓋一片區域，不消耗墨水；仍可使用主要武器。"},
        {"bubbler","力場會把每次命中轉成推力而非傷害；碰觸隊友即可分享護盾。"},
        {"sonar","在畫面與地圖揭露所有敵人；被揭露者移動變慢，墨水消耗加快。"},
        {"strike","在地圖選定位置發射飛彈，落地後形成巨大的旋轉墨汁漩渦。"},
        {"zooka","發射連續的高聳旋風墨流，可遠距擊倒敵人。"},
        {"wail","舉起大型音箱瞄準後按下發射，音波能穿牆並擊倒路徑上的目標。"},
        {"kraken","變成無敵海怪，可在任何墨水中疾行，並以跳躍攻擊擊倒敵人。"},
        {"blower","吹出最多三顆巨大泡泡封鎖區域；你或隊友射擊泡泡即可引發大型墨爆。"},
        {"jetpack","懸浮在場地上方發射強力墨彈，跳躍可加速；結束時會超級跳回起點，位置對所有人可見。"},
        {"stamp","揮舞巨型印章向前衝；正面揮擊可彈開攻擊並擊碎炸彈，但轉身慢且側後方暴露。空中翻擊可打得更遠並攻擊身後；投出印章會結束特殊武器。"},
        {"booyah","舉起墨球蓄力後投出，引發巨大爆炸；隊友加油可加快蓄力，也會補充他們自己的特殊武器。"},
        {"zipcaster","披上神秘光環，將次要武器變成抓鉤，遠距抓牆飛行並保留主要武器；結束時超級跳回起點，位置對所有人可見。"},
        {"crab","駕駛螃蟹坦克：射擊發射機槍、副武器發射迫擊炮、游泳可變成裝甲球。坦克可被擊落，頭頂與背後較脆弱。"},
    }};
    const auto find=[](std::string_view key,const auto& entries)->std::string_view{
        for(const auto& entry:entries)if(entry.id==key)return entry.description;
        return {};
    };
    switch(category){
    case EquipmentCategory::Main:return find(id,main);
    case EquipmentCategory::Sub:return find(id,sub);
    case EquipmentCategory::Special:return find(id,special);
    }
    return {};
}

} // namespace restoration
