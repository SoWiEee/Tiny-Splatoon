#include "PlayerProfile.hpp"
#include "ArsenalData.hpp"
#include "../data/Json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace restoration {
namespace {

constexpr std::size_t MaxProfileBytes=1024*1024;
constexpr std::size_t MaxMatchIdBytes=256;
using data::JsonParser;
using data::JsonValue;
using data::detail::ValidUtf8;
using data::detail::DecodeUtf8;

void WriteString(std::ostream& out,std::string_view value){
    static constexpr char Hex[]="0123456789abcdef";out<<'\"';
    for(unsigned char c:value){
        switch(c){case '\"':out<<"\\\"";break;case '\\':out<<"\\\\";break;case '\b':out<<"\\b";break;case '\f':out<<"\\f";break;case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default:if(c<0x20){out<<"\\u00"<<Hex[c>>4]<<Hex[c&15];}else out.put(static_cast<char>(c));}
    }
    out<<'\"';
}

std::string Serialize(const PlayerProfile& p){
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"name\":";WriteString(out,p.name);out<<",\"weapon\":";WriteString(out,p.weapon);
    out<<",\"sub\":";WriteString(out,p.sub);out<<",\"special\":";WriteString(out,p.special);
    out<<",\"level\":"<<p.level<<",\"xp\":"<<p.xp<<",\"wins\":"<<p.wins<<",\"matches\":"<<p.matches<<",\"totalTurf\":"<<p.totalTurf;
    out<<",\"style\":{\"hair\":"<<p.style.hair<<",\"skin\":"<<p.style.skin<<",\"outfit\":"<<p.style.outfit<<",\"eyes\":"<<p.style.eyes<<",\"hat\":"<<p.style.hat<<",\"brows\":"<<p.style.brows<<'}';
    const auto& s=p.settings;out<<",\"settings\":{";
    out<<"\"sensitivity\":"<<s.sensitivity<<",\"padSensitivity\":"<<s.padSensitivity<<",\"invertY\":"<<(s.invertY?"true":"false")
       <<",\"fov\":"<<s.fov<<",\"quality\":";WriteString(out,s.quality);
    out<<",\"qualityChosen\":"<<(s.qualityChosen?"true":"false")<<",\"shadowsChosen\":"<<(s.shadowsChosen?"true":"false")
       <<",\"shadows\":"<<(s.shadows?"true":"false")<<",\"bloom\":"<<(s.bloom?"true":"false")<<",\"cameraShake\":"<<s.cameraShake
       <<",\"showFps\":"<<(s.showFps?"true":"false")<<",\"fpsCap\":"<<s.fpsCap<<",\"master\":"<<s.master<<",\"music\":"<<s.music<<",\"sfx\":"<<s.sfx
       <<",\"colorblind\":"<<(s.colorblind?"true":"false")<<",\"minimap\":"<<(s.minimap?"true":"false")<<",\"matchLength\":"<<s.matchLength;
    out<<",\"bossLength\":"<<s.bossLength;
    out<<",\"lastMode\":";WriteString(out,s.lastMode);out<<",\"difficulty\":";WriteString(out,s.difficulty);
    out<<",\"rumble\":"<<s.rumble<<",\"aimAssist\":"<<s.aimAssist<<",\"aimAssistMouse\":"<<(s.aimAssistMouse?"true":"false")
       <<",\"touchControls\":";WriteString(out,s.touchControls);out<<",\"touchSensitivity\":"<<s.touchSensitivity
       <<",\"touchInvertY\":"<<(s.touchInvertY?"true":"false")<<",\"fullscreen\":"<<(s.fullscreen?"true":"false")
       <<",\"timeOfDay\":";WriteString(out,s.timeOfDay);out<<",\"reducedMotion\":"<<(s.reducedMotion?"true":"false")<<",\"palette\":"<<s.palette;
    out<<",\"graphics\":{\"preset\":";WriteString(out,s.graphics.preset==GraphicsPreset::Low?"low":s.graphics.preset==GraphicsPreset::High?"high":s.graphics.preset==GraphicsPreset::Custom?"custom":"medium");
    out<<",\"renderScale\":"<<s.graphics.renderScale<<",\"shadowMapSize\":"<<s.graphics.shadowMapSize<<",\"shadows\":"<<(s.graphics.shadows?"true":"false")
       <<",\"reflections\":"<<(s.graphics.reflections?"true":"false")<<",\"postEffects\":"<<(s.graphics.postEffects?"true":"false")
       <<",\"antialiasing\":"<<(s.graphics.antialiasing?"true":"false")<<"}}";
    out<<",\"lastSettledMatch\":";WriteString(out,p.lastSettledMatch);out<<'}';
    return out.str();
}

const JsonValue* Field(const JsonValue* object,std::string_view key){return object?object->get(key):nullptr;}
const std::string* StringValue(const JsonValue* value){return value?std::get_if<std::string>(&value->value):nullptr;}
const double* NumberValue(const JsonValue* value){return value?std::get_if<double>(&value->value):nullptr;}
const bool* BoolValue(const JsonValue* value){return value?std::get_if<bool>(&value->value):nullptr;}
std::string ReadString(const JsonValue* object,std::string_view key,std::string fallback){const auto* value=StringValue(Field(object,key));return value?*value:std::move(fallback);}
bool ReadBool(const JsonValue* object,std::string_view key,bool fallback){const auto* value=BoolValue(Field(object,key));return value?*value:fallback;}
double ReadDouble(const JsonValue* object,std::string_view key,double fallback,double min,double max){
    const auto* value=NumberValue(Field(object,key));return value&&std::isfinite(*value)&&*value>=min&&*value<=max?*value:fallback;
}
int ReadInt(const JsonValue* object,std::string_view key,int fallback,int min,int max){
    const auto* value=NumberValue(Field(object,key));
    return value&&std::isfinite(*value)&&std::floor(*value)==*value&&*value>=min&&*value<=max?static_cast<int>(*value):fallback;
}
std::string ReadChoice(const JsonValue* object,std::string_view key,std::string fallback,std::initializer_list<std::string_view> choices){
    const auto* value=StringValue(Field(object,key));if(!value)return fallback;
    return std::find(choices.begin(),choices.end(),*value)!=choices.end()?*value:fallback;
}

void LoadStyle(PlayerProfile& p,const JsonValue* value){
    p.style.hair=ReadInt(value,"hair",0,0,7);p.style.skin=ReadInt(value,"skin",0,0,8);
    p.style.outfit=ReadInt(value,"outfit",0,0,9);p.style.eyes=ReadInt(value,"eyes",0,0,7);
    p.style.hat=ReadInt(value,"hat",0,0,3);p.style.brows=ReadInt(value,"brows",0,0,3);
}

void LoadSettings(PlayerSettings& s,const JsonValue* value){
    s.sensitivity=ReadDouble(value,"sensitivity",1,.2,3);s.padSensitivity=ReadDouble(value,"padSensitivity",1,.2,3);
    s.invertY=ReadBool(value,"invertY",false);s.fov=ReadDouble(value,"fov",82,65,100);
    s.quality=ReadChoice(value,"quality","high",{"low","medium","high","ultra"});
    s.qualityChosen=ReadBool(value,"qualityChosen",false);s.shadowsChosen=ReadBool(value,"shadowsChosen",false);
    s.shadows=ReadBool(value,"shadows",true);s.bloom=ReadBool(value,"bloom",true);s.cameraShake=ReadDouble(value,"cameraShake",1,0,1);
    s.showFps=ReadBool(value,"showFps",false);s.fpsCap=ReadInt(value,"fpsCap",0,0,60);if(s.fpsCap!=0&&s.fpsCap!=30&&s.fpsCap!=60)s.fpsCap=0;
    s.master=ReadDouble(value,"master",.8,0,1);s.music=ReadDouble(value,"music",.6,0,1);s.sfx=ReadDouble(value,"sfx",.85,0,1);
    s.colorblind=ReadBool(value,"colorblind",false);s.minimap=ReadBool(value,"minimap",true);
    s.matchLength=ReadInt(value,"matchLength",180,90,180);if(s.matchLength!=90&&s.matchLength!=180)s.matchLength=180;
    s.bossLength=ReadInt(value,"bossLength",240,180,300);if(s.bossLength!=180&&s.bossLength!=240&&s.bossLength!=300)s.bossLength=240;
    s.lastMode=ReadChoice(value,"lastMode","turf",{"turf","zones","boss"});s.difficulty=ReadChoice(value,"difficulty","normal",{"easy","normal","hard"});
    s.rumble=ReadDouble(value,"rumble",1,0,1);s.aimAssist=ReadDouble(value,"aimAssist",1,0,1);s.aimAssistMouse=ReadBool(value,"aimAssistMouse",false);
    s.touchControls=ReadChoice(value,"touchControls","auto",{"auto","on","off"});s.touchSensitivity=ReadDouble(value,"touchSensitivity",1,.2,3);
    s.touchInvertY=ReadBool(value,"touchInvertY",false);s.fullscreen=ReadBool(value,"fullscreen",false);
    s.timeOfDay=ReadChoice(value,"timeOfDay","day",{"day","dusk"});s.reducedMotion=ReadBool(value,"reducedMotion",false);
    s.palette=ReadInt(value,"palette",0,0,4);
    const auto* graphics=Field(value,"graphics");
    const auto preset=ReadChoice(graphics,"preset","medium",{"low","medium","high","custom"});
    s.graphics=ForPreset(preset=="low"?GraphicsPreset::Low:preset=="high"?GraphicsPreset::High:preset=="custom"?GraphicsPreset::Custom:GraphicsPreset::Medium);
    if(preset=="custom"){
        s.graphics.renderScale=static_cast<float>(ReadDouble(graphics,"renderScale",.85,.25,1));
        int shadowSize=ReadInt(graphics,"shadowMapSize",1024,0,8192);
        if(shadowSize!=0&&(shadowSize<256||(shadowSize&(shadowSize-1))!=0))shadowSize=1024;
        s.graphics.shadowMapSize=shadowSize;s.graphics.shadows=ReadBool(graphics,"shadows",true);
        s.graphics.reflections=ReadBool(graphics,"reflections",false);s.graphics.postEffects=ReadBool(graphics,"postEffects",true);
        s.graphics.antialiasing=ReadBool(graphics,"antialiasing",true);
    }
}

void LoadProfile(PlayerProfile& p,const JsonValue& value){
    if(!value.object())return;
    p.name=ReadString(&value,"name",p.name);p.weapon=ReadString(&value,"weapon",p.weapon);
    p.sub=ReadString(&value,"sub",p.sub);p.special=ReadString(&value,"special",p.special);
    p.level=ReadInt(&value,"level",1,1,(std::numeric_limits<int>::max()-800)/350);
    p.xp=ReadInt(&value,"xp",0,0,std::numeric_limits<int>::max());p.wins=ReadInt(&value,"wins",0,0,std::numeric_limits<int>::max());
    p.matches=ReadInt(&value,"matches",0,0,std::numeric_limits<int>::max());p.totalTurf=ReadDouble(&value,"totalTurf",0,0,1e15);
    LoadStyle(p,Field(&value,"style"));LoadSettings(p.settings,Field(&value,"settings"));
    p.lastSettledMatch=ReadString(&value,"lastSettledMatch",{});
}

bool IsEquipment(EquipmentCategory category,std::string_view id){
    const std::vector<EquipmentData>* data=category==EquipmentCategory::Main?&MainEquipment():category==EquipmentCategory::Sub?&SubEquipment():&SpecialEquipment();
    return std::any_of(data->begin(),data->end(),[&](const EquipmentData& item){return item.id==id;});
}

std::filesystem::path DefaultProfilePath(){
#if defined(_WIN32)
    const DWORD needed=GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if(!needed)throw std::runtime_error("LOCALAPPDATA is unavailable");
    std::wstring local(needed,L'\0');const DWORD written=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),needed);
    if(!written||written>=needed)throw std::runtime_error("Could not read LOCALAPPDATA");
    local.resize(written);return std::filesystem::path(local)/L"Tiny-Splatoon"/L"profile.json";
#else
    if(const char* local=std::getenv("LOCALAPPDATA");local&&*local)return std::filesystem::u8path(local)/"Tiny-Splatoon"/"profile.json";
    if(const char* home=std::getenv("HOME");home&&*home)return std::filesystem::u8path(home)/".local"/"share"/"Tiny-Splatoon"/"profile.json";
    throw std::runtime_error("No local profile directory is available");
#endif
}

std::filesystem::path TemporaryPath(const std::filesystem::path& target){
    static std::atomic<unsigned long long> sequence{0};
    const auto tick=std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(_WIN32)
    const auto process=GetCurrentProcessId();
#else
    const auto process=::getpid();
#endif
    auto temporary=target;temporary+=std::filesystem::path(".tmp."+std::to_string(process)+"."+std::to_string(tick)+"."+std::to_string(++sequence));return temporary;
}

void ReplaceFile(const std::filesystem::path& temporary,const std::filesystem::path& target){
#if defined(_WIN32)
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Could not replace profile file");
#else
    std::error_code error;std::filesystem::rename(temporary,target,error);
    if(error)throw std::system_error(error,"Could not replace profile file");
#endif
}

void WriteAtomically(const std::filesystem::path& target,std::string_view contents){
    const auto parent=target.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    const auto temporary=TemporaryPath(target);
    try{
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Could not open temporary profile file");
         file.write(contents.data(),static_cast<std::streamsize>(contents.size()));file.flush();if(!file)throw std::runtime_error("Could not write temporary profile file");
         file.close();if(!file)throw std::runtime_error("Could not close temporary profile file");}
        ReplaceFile(temporary,target);
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}

bool SafeId(std::string_view value){return !value.empty()&&value.size()<=MaxMatchIdBytes&&ValidUtf8(value,true);}

bool ValidReward(const MatchRewardInput& input){
    return SafeId(input.matchId)&&(input.mode=="turf"||input.mode=="zones"||input.mode=="boss")&&
        std::isfinite(input.turf)&&input.turf>=0&&input.turf<=10000000&&
        std::isfinite(input.zoneTurf)&&input.zoneTurf>=0&&input.zoneTurf<=10000000&&
        std::isfinite(input.bossDamage)&&input.bossDamage>=0&&input.bossDamage<=10000000&&input.splats>=0&&input.splats<=10000000;
}

} // namespace

int XpForLevel(int level){
    level=std::max(1,level);const auto safeMax=(std::numeric_limits<int>::max()-800)/350;
    if(level>safeMax)return std::numeric_limits<int>::max();
    return 800+level*350;
}

void NormalizeProfile(PlayerProfile& p){
    if(!ValidUtf8(p.name,true)||p.name.empty())p.name="Player";
    std::size_t chars=0;
    for(std::size_t i=0;i<p.name.size();){
        std::size_t bytes=0;std::uint32_t codepoint=0;
        if(!DecodeUtf8(p.name,i,bytes,codepoint)){p.name="Player";break;}
        if(++chars==17){p.name.resize(i);break;}i+=bytes;
    }
    if(p.weapon=="dualies")p.weapon="twins";else if(p.weapon=="splatling")p.weapon="spinner";else if(p.weapon=="slosher")p.weapon="bucket";
    if(!IsEquipment(EquipmentCategory::Main,p.weapon))p.weapon="shooter";
    if(!p.sub.empty()&&!IsEquipment(EquipmentCategory::Sub,p.sub))p.sub.clear();
    if(!p.special.empty()&&!IsEquipment(EquipmentCategory::Special,p.special))p.special.clear();
    constexpr int MaxLevel=(std::numeric_limits<int>::max()-800)/350;
    p.level=std::clamp(p.level,1,MaxLevel);p.xp=std::max(0,p.xp);p.wins=std::max(0,p.wins);p.matches=std::max(0,p.matches);
    if(!std::isfinite(p.totalTurf)||p.totalTurf<0||p.totalTurf>1e15)p.totalTurf=0;
    p.style.hair=std::clamp(p.style.hair,0,7);p.style.skin=std::clamp(p.style.skin,0,8);p.style.outfit=std::clamp(p.style.outfit,0,9);
    p.style.eyes=std::clamp(p.style.eyes,0,7);p.style.hat=std::clamp(p.style.hat,0,3);p.style.brows=std::clamp(p.style.brows,0,3);
    auto& s=p.settings;
    auto finiteClamp=[](double& value,double fallback,double min,double max){if(!std::isfinite(value)||value<min||value>max)value=fallback;};
    finiteClamp(s.sensitivity,1,.2,3);finiteClamp(s.padSensitivity,1,.2,3);finiteClamp(s.fov,82,65,100);
    finiteClamp(s.cameraShake,1,0,1);finiteClamp(s.master,.8,0,1);finiteClamp(s.music,.6,0,1);finiteClamp(s.sfx,.85,0,1);
    finiteClamp(s.rumble,1,0,1);finiteClamp(s.aimAssist,1,0,1);finiteClamp(s.touchSensitivity,1,.2,3);
    if(s.quality!="low"&&s.quality!="medium"&&s.quality!="high"&&s.quality!="ultra")s.quality="high";
    if(s.fpsCap!=0&&s.fpsCap!=30&&s.fpsCap!=60)s.fpsCap=0;
    if(s.matchLength!=90&&s.matchLength!=180)s.matchLength=180;
    if(s.bossLength!=180&&s.bossLength!=240&&s.bossLength!=300)s.bossLength=240;
    if(s.lastMode!="turf"&&s.lastMode!="zones"&&s.lastMode!="boss")s.lastMode="turf";
    if(s.difficulty!="easy"&&s.difficulty!="normal"&&s.difficulty!="hard")s.difficulty="normal";
    if(s.touchControls!="auto"&&s.touchControls!="on"&&s.touchControls!="off")s.touchControls="auto";
    if(s.timeOfDay!="day"&&s.timeOfDay!="dusk")s.timeOfDay="day";
    if(s.palette<0||s.palette>=5)s.palette=0;
    if(p.lastSettledMatch.size()>MaxMatchIdBytes||(!p.lastSettledMatch.empty()&&!ValidUtf8(p.lastSettledMatch,true)))p.lastSettledMatch.clear();
    auto& graphics=s.graphics;
    switch(graphics.preset){
    case GraphicsPreset::Low:graphics=ForPreset(GraphicsPreset::Low);break;
    case GraphicsPreset::Medium:graphics=ForPreset(GraphicsPreset::Medium);break;
    case GraphicsPreset::High:graphics=ForPreset(GraphicsPreset::High);break;
    case GraphicsPreset::Custom:
        if(!std::isfinite(graphics.renderScale)||graphics.renderScale<.25f||graphics.renderScale>1.f)graphics.renderScale=.85f;
        if(graphics.shadowMapSize!=0&&(graphics.shadowMapSize<256||graphics.shadowMapSize>8192||(graphics.shadowMapSize&(graphics.shadowMapSize-1))!=0))graphics.shadowMapSize=1024;
        break;
    default:graphics=ForPreset(GraphicsPreset::Medium);break;
    }
    while(p.xp>=XpForLevel(p.level)&&p.level<MaxLevel){p.xp-=XpForLevel(p.level);++p.level;}
    if(p.level==MaxLevel&&p.xp>=XpForLevel(p.level))p.xp=XpForLevel(p.level)-1;
}

MatchRewardResult ApplyMatchReward(PlayerProfile& p,const MatchRewardInput& input){
    NormalizeProfile(p);MatchRewardResult result;
    result.levelBefore=result.levelAfter=p.level;result.xpBefore=result.xpAfter=p.xp;
    result.xpToNextBefore=result.xpToNextAfter=XpForLevel(p.level);
    // shortcut: only the newest match ID is retained; use a persisted ID ledger if older results can replay after another match.
    if(!ValidReward(input)||input.matchId==p.lastSettledMatch||p.matches==std::numeric_limits<int>::max()||(input.won&&p.wins==std::numeric_limits<int>::max()))return result;
    const auto turf=static_cast<long long>(std::llround(input.turf));
    const auto zoneTurf=static_cast<long long>(std::llround(input.zoneTurf));
    std::int64_t gained=0;
    if(input.mode=="zones"){
        gained=(input.won?1200:500)+static_cast<std::int64_t>(std::llround(turf*.6))+zoneTurf+
            static_cast<std::int64_t>(input.splats)*40+(input.won&&input.knockout?300:0);
    }else if(input.mode=="boss"){
        const auto bossDamage=static_cast<long long>(std::llround(input.bossDamage));
        gained=std::llround((input.won?1200:500)+turf+static_cast<std::int64_t>(input.splats)*40+bossDamage*.04);
    }else gained=(input.won?1200:500)+turf+static_cast<std::int64_t>(input.splats)*40;
    if(gained<0||gained>std::numeric_limits<int>::max()||p.totalTurf+turf>1e15)return result;
    result.applied=true;result.gained=static_cast<int>(gained);
    auto remaining=static_cast<std::int64_t>(p.xp)+gained;
    while(remaining>=XpForLevel(p.level)&&p.level<(std::numeric_limits<int>::max()-800)/350){remaining-=XpForLevel(p.level);++p.level;}
    p.xp=static_cast<int>(std::min<std::int64_t>(remaining,std::numeric_limits<int>::max()));p.matches++;if(input.won)p.wins++;
    p.totalTurf+=turf;p.lastSettledMatch=input.matchId;
    NormalizeProfile(p);
    result.levelAfter=p.level;result.xpAfter=p.xp;result.xpToNextAfter=XpForLevel(p.level);
    return result;
}

std::array<std::string_view,2> ProfilePaletteColors(int palette,bool colorblind,bool boss){
    if(colorblind)return {"#ffd21a","#2a52ff"};
    static constexpr std::array<std::array<std::string_view,2>,5> palettes={{{"#ff8a14","#2f5bff"},{"#ff3f9e","#18d48c"},{"#f2e312","#8a3cff"},{"#10d2e6","#ff4150"},{"#a6f01a","#e02cd8"}}};
    auto colors=palettes[palette>=0&&palette<static_cast<int>(palettes.size())?static_cast<std::size_t>(palette):0];
    if(boss)colors[1]="#2f5bff";
    return colors;
}

ProfileStore::ProfileStore(std::filesystem::path path):path_(path.empty()?DefaultProfilePath():std::move(path)){}

PlayerProfile ProfileStore::load()const{
    std::error_code error;const bool exists=std::filesystem::exists(path_,error);
    if(error)throw std::system_error(error,"Could not inspect profile file");
    if(!exists)return {};
    const auto size=std::filesystem::file_size(path_,error);
    if(error)throw std::system_error(error,"Could not inspect profile file size");
    if(size>MaxProfileBytes)return {};
    std::ifstream file(path_,std::ios::binary);if(!file)throw std::runtime_error("Could not open profile file");
    std::string contents(static_cast<std::size_t>(size),'\0');
    if(size&&!file.read(contents.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Could not read profile file");
    PlayerProfile result;
    try{LoadProfile(result,JsonParser(contents).parse());}catch(const std::exception&){return {};}
    NormalizeProfile(result);return result;
}

void ProfileStore::save(const PlayerProfile& profile)const{
    PlayerProfile normalized=profile;NormalizeProfile(normalized);WriteAtomically(path_,Serialize(normalized));
}

} // namespace restoration
