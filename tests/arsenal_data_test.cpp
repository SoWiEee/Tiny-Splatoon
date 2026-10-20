#include "../src/game/ArsenalData.hpp"
#include <cmath>
#include <iostream>
#include <set>

using namespace restoration;

int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
    check(MainEquipment().size()==12 && SubEquipment().size()==15 && SpecialEquipment().size()==19,"source roster size");
    for(auto category:{EquipmentCategory::Main,EquipmentCategory::Sub,EquipmentCategory::Special}) {
        const auto& list=category==EquipmentCategory::Main?MainEquipment():category==EquipmentCategory::Sub?SubEquipment():SpecialEquipment();
        std::set<std::string_view> ids;
        for(const auto& entry:list) {
            check(ids.insert(entry.id).second && !entry.name.empty(),"unique named equipment");
            check(&GetEquipment(category,entry.id)==&entry,"stable source lookup");
            for(std::size_t i=0;i<entry.fieldCount;++i)
                check(std::isfinite(entry.fields[i].value),"finite nested tuning");
            if(category==EquipmentCategory::Main) {
                check(!entry.kind.empty(),"main kind");
                check(!GetEquipment(EquipmentCategory::Sub,entry.sub).id.empty(),"valid default sub");
                check(!GetEquipment(EquipmentCategory::Special,entry.special).id.empty(),"valid default special");
                check(entry.number("stats.range")>=0 && entry.number("stats.range")<=1,"nested stats retained");
            }
        }
    }
    const auto& shooter=GetEquipment(EquipmentCategory::Main,"shooter");
    check(shooter.number("damage")==36 && shooter.number("fireInterval")==0.1 && shooter.special=="zooka","Spritzer source calibration");
    check(shooter.number("notPresent",7)==7 && shooter.text("notPresent","fallback")=="fallback","optional field fallback");
    check(PlayerTuning().number("swimSpeed")==11.8 && DifficultyTuning("hard").number("reaction")==0.17,"shared player and difficulty source");
    const auto durations=MatchTuning().array("durations");
    check(durations.size()==2 && durations[0]==90 && durations[1]==180 && !BotNames().empty(),"source arrays and roster names");
    bool rejected=false;
    try{GetEquipment(EquipmentCategory::Main,"missing");}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"unknown ID rejected");
    rejected=false;
    try{shooter.number("name",7);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"wrong field type rejected rather than fallback");
    if(!failures) std::cout<<"46 source equipment definitions and nested tuning passed\n";
    return failures?1:0;
}
