#include "PlayerProfile.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

using namespace restoration;

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,std::string_view contents){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(contents.data(),static_cast<std::streamsize>(contents.size()));if(!file)throw std::runtime_error("test fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}

int main(){
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("tiny-splatoon-profile-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        std::filesystem::create_directories(root);const auto path=root/"profile.json";ProfileStore store(path);
        auto profile=store.load();Check(profile.name=="Player"&&profile.level==1&&profile.xp==0&&profile.weapon=="shooter","missing profile defaults changed");
        Check(profile.settings.graphics.preset==GraphicsPreset::Medium&&profile.settings.palette==0,"native settings defaults changed");

        profile.name=u8"甲乙丙丁戊己庚辛壬癸一二三四五六七";profile.weapon="dualies";profile.sub="sticky";profile.special="jetpack";
        profile.style={7,8,9,7,3,2};profile.settings.quality="ultra";profile.settings.invertY=true;profile.settings.fov=91;
        profile.settings.palette=4;profile.settings.reducedMotion=true;profile.settings.graphics.preset=GraphicsPreset::Custom;
        profile.settings.bossLength=300;profile.settings.lastMode="boss";
        profile.settings.graphics.renderScale=.72f;profile.settings.graphics.shadowMapSize=4096;profile.settings.graphics.reflections=true;
        store.save(profile);profile=store.load();
        Check(profile.name==u8"甲乙丙丁戊己庚辛壬癸一二三四五六","UTF-8 profile name was not limited to 16 characters");
        Check(profile.weapon=="twins"&&profile.sub=="sticky"&&profile.special=="jetpack","weapon migration or loadout persistence failed");
        Check(profile.style.hair==7&&profile.style.skin==8&&profile.style.outfit==9&&profile.style.eyes==7&&profile.style.hat==3&&profile.style.brows==2,"character style persistence failed");
        Check(profile.settings.invertY&&profile.settings.fov==91&&profile.settings.palette==4&&profile.settings.reducedMotion,"player settings persistence failed");
        Check(profile.settings.bossLength==300&&profile.settings.lastMode=="boss","Boss preferences did not survive reload");
        Check(profile.settings.graphics.preset==GraphicsPreset::Custom&&profile.settings.graphics.renderScale==.72f&&profile.settings.graphics.shadowMapSize==4096&&profile.settings.graphics.reflections,"graphics settings persistence failed");
        Check(ProfilePaletteColors(4)[0]=="#a6f01a"&&ProfilePaletteColors(2,true)[1]=="#2a52ff","source team palette colors changed");

        Write(path,R"({"name":"","weapon":"splatling","level":-1,"settings":{"fov":200,"fpsCap":144,"quality":"potato"}})");
        profile=store.load();Check(profile.name=="Player"&&profile.weapon=="spinner"&&profile.level==1,"invalid values or legacy weapon were not normalized");
        Check(profile.settings.fov==82&&profile.settings.fpsCap==0&&profile.settings.quality=="high","invalid settings did not use defaults");
        Write(path,R"({"name":"Tide","settings":{"invertY":true}})");profile=store.load();
        Check(profile.name=="Tide"&&profile.level==1&&profile.weapon=="shooter"&&profile.settings.invertY&&profile.settings.fov==82,"missing fields did not keep defaults");
        Write(path,"not json");profile=store.load();Check(profile.name=="Player"&&profile.level==1,"malformed profile did not use defaults");

        PlayerProfile turf;auto reward=ApplyMatchReward(turf,{"match-turf","turf",true,false,100.2,0,0,3});
        Check(reward.applied&&reward.gained==1420&&reward.levelBefore==1&&reward.levelAfter==2&&reward.xpAfter==270,"Turf XP calculation changed");
        Check(turf.wins==1&&turf.matches==1&&turf.totalTurf==100,"Turf reward counters changed");
        reward=ApplyMatchReward(turf,{"match-turf","turf",true,false,100.2,0,0,3});
        Check(!reward.applied&&turf.wins==1&&turf.matches==1&&turf.xp==270,"duplicate match reward was applied");

        PlayerProfile zones;reward=ApplyMatchReward(zones,{"match-zones","zones",true,true,101,25.5,0,2});
        Check(reward.applied&&reward.gained==1667&&zones.level==2&&zones.xp==517,"Zone XP components changed");
        store.save(zones);zones=store.load();reward=ApplyMatchReward(zones,{"match-zones","zones",true,true,101,25.5,0,2});
        Check(!reward.applied&&zones.wins==1&&zones.matches==1&&zones.xp==517,"saved duplicate match reward was applied");

        PlayerProfile boss;reward=ApplyMatchReward(boss,{"match-boss","boss",false,false,100.6,0,250.4,3});
        Check(reward.applied&&reward.gained==731&&boss.matches==1&&boss.wins==0,"Boss XP calculation changed");
        PlayerProfile multi;reward=ApplyMatchReward(multi,{"match-multi","turf",false,false,5000,0,0,0});
        Check(reward.applied&&reward.gained==5500&&multi.level==4&&multi.xp==1000,"multi-level progression changed");
        Check(!ApplyMatchReward(multi,{"","turf",true,false,1,0,0,0}).applied,"empty match id was accepted");

#if defined(_WIN32)
        store.save(turf);const auto original=Read(path);
        const auto lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(lock!=INVALID_HANDLE_VALUE,"could not lock profile for atomic-write test");
        auto changed=turf;changed.name="replacement";bool failed=false;try{store.save(changed);}catch(const std::exception&){failed=true;}
        CloseHandle(lock);Check(failed&&Read(path)==original,"failed atomic replacement damaged the old profile");
#endif

        std::filesystem::remove_all(root);std::cout<<"Player profile persistence and rewards passed\n";return 0;
    }catch(const std::exception& error){std::error_code ignored;std::filesystem::remove_all(root,ignored);std::cerr<<error.what()<<'\n';return 1;}
}
