#include "GameUi.hpp"
#include "RmlRenderer.hpp"
#include "../game/ArsenalData.hpp"
#include "../game/MatchSimulation.hpp"
#include "../game/PlayerSimulation.hpp"
#include "UiText.hpp"
#include "../world/FacePaint.hpp"
#include "../world/ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace restoration {
namespace {
std::string Escape(const std::string& text) {
    std::string out;
    for(char c:text) switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '\"':out+="&quot;";break;default:out+=c;}
    return out;
}
std::string Integer(double n){return std::to_string(static_cast<int>(std::round(n)));}
std::string Percent(double n){char out[32];std::snprintf(out,sizeof(out),"%.1f%%",n*100);return out;}
std::string Number(double n,int decimals=2){char out[32];std::snprintf(out,sizeof(out),decimals==0?"%.0f":"%.2f",n);return out;}

class UiSystem : public Rml::SystemInterface {
public:
    explicit UiSystem(GLFWwindow* value):window(value),hand(glfwCreateStandardCursor(GLFW_HAND_CURSOR)){}
    ~UiSystem()override{if(hand)glfwDestroyCursor(hand);}
    double GetElapsedTime() override{return glfwGetTime();}
    bool LogMessage(Rml::Log::Type type,const Rml::String& message) override {
        if(type<=Rml::Log::LT_WARNING)std::cerr<<"UI: "<<message<<'\n';
        return true;
    }
    void SetClipboardText(const Rml::String& text)override{glfwSetClipboardString(window,text.c_str());}
    void GetClipboardText(Rml::String& text)override{const auto* value=glfwGetClipboardString(window);text=value?value:"";}
    void SetMouseCursor(const Rml::String& name)override{glfwSetCursor(window,name=="pointer"?hand:nullptr);}
    GLFWwindow* window;
    GLFWcursor* hand;
};

const std::array<const char*,8> HairNames={"Tide","Spike","Twin","Bob","Pony","Crest","Knot","Swoop"};
const std::array<const char*,4> HatNames={"None","Snapback","Beanie","Bucket Hat"};
const std::array<const char*,9> SkinNames={"Rosy","Peach","Tan","Cocoa","Porcelain","Honey","Olive","Chestnut","Ebony"};
const std::array<const char*,8> EyeNames={"Amber","Lagoon","Violet","Lime","Rose","Hazel","Frost","Ember"};
const std::array<const char*,4> BrowNames={"Classic","Bold","Arched","Straight"};
const std::array<const char*,10> OutfitNames={"Basic Ringer","Night Pinstripe","Raglan Runner","Chevron Tee","Breton Stripe","Splatter Tee","Splat Camo","Dip-Dye Tee","Pro Jersey","Track Top"};

template<std::size_t N>
void FillStyleOptions(Rml::ElementDocument* doc,const char* id,const char* kind,const std::array<const char*,N>& names){
    std::string markup;
    for(std::size_t i=0;i<names.size();++i){auto label=StyleLabel(kind,static_cast<int>(i));if(label.empty())label=names[i];markup+="<option value=\""+std::to_string(i)+"\">"+Escape(std::string(label))+"</option>";}
    auto* element=doc->GetElementById(id);if(element)element->SetInnerRML(markup);
}
void SetChecked(Rml::Element* element,bool checked){
    if(!element)return;
    if(checked)element->SetAttribute("checked","");else element->RemoveAttribute("checked");
}
bool IsChecked(Rml::Element* element){
    auto* control=dynamic_cast<Rml::ElementFormControl*>(element);
    return control?control->IsSubmitted():(element&&element->HasAttribute("checked"));
}
}

struct GameUi::Impl : Rml::EventListener {
    struct ControlSync {
        bool& flag;bool previous;
        explicit ControlSync(bool& value):flag(value),previous(std::exchange(value,true)){}
        ~ControlSync(){flag=previous;}
    };
    GLFWwindow* window;
    UiSystem system;
    RmlRenderer renderer;
    Rml::Context* context=nullptr;
    Rml::ElementDocument *menu=nullptr,*hud=nullptr;
    UiSetup selection;
    PlayerProfile player;
    MatchRewardResult reward;
    UiAction action=UiAction::None;
    UiAction leaveAction=UiAction::StageSelect;
    UiScreen screen=UiScreen::Home;
    UiScreen previousScreen=UiScreen::Home;
    std::function<void(const PlayerProfile&)> profileSink;
    std::function<void(const std::string&)> sound;
    std::function<void(float,float,float)> volumes;
    bool menuVisible=true,interactive=true,practiceGear=false,hudOverlay=false;
    bool activePractice=false;
    bool syncingControls=false;
    bool mouse[3]{};
    std::array<bool,12> keys{};
    std::string hovered;
    std::map<std::string,std::string> textCache;
    void* previousUser=nullptr;
    GLFWscrollfun previousScroll=nullptr;
    GLFWcharfun previousChar=nullptr;
    double lastHudTime=0;
    double nextMinimapUpdate=0;
    int hudFps=0;
    std::string minimapCache;
    std::string terrainStage;
    std::string stageOptionMode,durationOptionMode;
    std::vector<unsigned char> terrainCells;

    Impl(GLFWwindow* value,const std::filesystem::path& assets):window(value),system(value){
        Rml::SetSystemInterface(&system);Rml::SetRenderInterface(&renderer);
        if(!Rml::Initialise())throw std::runtime_error("Unable to initialize player UI");
        try{
            if(!Rml::LoadFontFace((assets/"fonts/Inter.ttf").string(),false,Rml::Style::FontWeight::Normal)||
               !Rml::LoadFontFace((assets/"fonts/Inter.ttf").string(),false,Rml::Style::FontWeight::Bold)||
               !Rml::LoadFontFace((assets/"fonts/NotoSansTC.ttf").string(),true,Rml::Style::FontWeight::Normal)||
               !Rml::LoadFontFace((assets/"fonts/NotoSansTC.ttf").string(),true,Rml::Style::FontWeight::Bold))
                throw std::runtime_error("Unable to load Inter / Noto Sans TC fonts");
            context=Rml::CreateContext("game-ui",{1280,720});
            if(!context)throw std::runtime_error("Unable to create player UI context");
            menu=context->LoadDocument((assets/"ui/menu.rml").string());
            hud=context->LoadDocument((assets/"ui/hud.rml").string());
            if(!menu||!hud)throw std::runtime_error("Unable to load player UI documents");
            for(auto* doc:{menu,hud})for(const auto* event:{"click","change","input","mouseover","focus","keydown"})doc->AddEventListener(event,this);
            PopulateOptions();SyncProfileControls();GoTo(UiScreen::Home);menu->Show();hud->Hide();
            previousUser=glfwGetWindowUserPointer(window);glfwSetWindowUserPointer(window,this);
            previousScroll=glfwSetScrollCallback(window,[](GLFWwindow* w,double x,double y){
                auto* self=static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if(!self)return;
                if(self->previousScroll){glfwSetWindowUserPointer(w,self->previousUser);self->previousScroll(w,x,y);glfwSetWindowUserPointer(w,self);}
                if(self->interactive)self->context->ProcessMouseWheel(static_cast<float>(-y),0);
            });
            previousChar=glfwSetCharCallback(window,[](GLFWwindow* w,unsigned int codepoint){
                auto* self=static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if(!self)return;
                if(self->previousChar){glfwSetWindowUserPointer(w,self->previousUser);self->previousChar(w,codepoint);glfwSetWindowUserPointer(w,self);}
                if(self->interactive&&self->context)self->context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
            });
        }catch(...){Rml::Shutdown();Rml::SetSystemInterface(nullptr);Rml::SetRenderInterface(nullptr);throw;}
    }
    ~Impl(){
        glfwSetScrollCallback(window,previousScroll);glfwSetCharCallback(window,previousChar);glfwSetWindowUserPointer(window,previousUser);
        Rml::Shutdown();Rml::SetSystemInterface(nullptr);Rml::SetRenderInterface(nullptr);
    }
    Rml::Element* Find(Rml::ElementDocument* doc,const std::string& id)const{
        auto* element=doc?doc->GetElementById(id):nullptr;if(!element)throw std::runtime_error("Missing UI element: "+id);return element;
    }
    void Hidden(Rml::ElementDocument* doc,const std::string& id,bool hidden){
        auto* element=Find(doc,id);element->SetClass("hidden",hidden);
        if(hidden)element->SetProperty("display","none");else element->RemoveProperty("display");
    }
    void Text(Rml::ElementDocument* doc,const std::string& id,const std::string& value){
        const auto key=doc->GetId()+"/"+id;
        if(textCache[key]!=value){Find(doc,id)->SetInnerRML(Escape(value));textCache[key]=value;}
    }
    Rml::ElementFormControl* Control(const std::string& id){
        auto* control=dynamic_cast<Rml::ElementFormControl*>(Find(menu,id));
        if(!control)throw std::runtime_error("Invalid UI form control: "+id);return control;
    }
    void Value(const std::string& id,const std::string& value){ControlSync sync(syncingControls);Control(id)->SetValue(value);}
    static std::string Bool(bool value){return value?"true":"false";}
    void Play(const std::string& cue){if(sound)sound(cue);}
    void Commit(){
        NormalizeProfile(player);
        SyncProfileControls();
        if(profileSink){try{profileSink(player);}catch(const std::exception& error){SetSaveError(error.what());}catch(...){SetSaveError("Could not save player profile");}}
    }
    void SetSaveError(const std::string& error){
        const bool hidden=error.empty();
        for(const char* id:{"save-error","settings-save-error"}){
            Hidden(menu,id,hidden);if(!hidden)Text(menu,id,"玩家資料讀寫失敗："+error);
        }
    }
    void RebuildStageOptions(){
        ControlSync sync(syncingControls);
        std::string options;std::string first;
        for(const auto& stage:ReferenceStages()){
            if(stage.mode!="turf"||stage.onlineOnly||(selection.mode=="boss"&&stage.noBoss))continue;
            if(first.empty())first=std::string(stage.id);
            std::string_view label=StageLabel(stage.id);if(label.empty())label=stage.name;
            options+="<option value=\""+Escape(std::string(stage.id))+"\">"+Escape(std::string(label))+"</option>";
        }
        auto* stages=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"stage"));
        const std::string optionMode=selection.mode=="practice"?"turf":selection.mode;
        if(stageOptionMode!=optionMode){stages->RemoveAll();stages->SetInnerRML(options);stageOptionMode=optionMode;}
        const bool exists=std::any_of(ReferenceStages().begin(),ReferenceStages().end(),[&](const StageData& stage){return stage.id==selection.stage&&stage.mode=="turf"&&!stage.onlineOnly&&(selection.mode!="boss"||!stage.noBoss);});
        if(!exists)selection.stage=first;
        if(!selection.stage.empty())Value("stage",selection.stage);
        Text(menu,"stage-message",selection.mode=="boss"?"僅顯示可進行協力挑戰的離線場地。":selection.mode=="practice"?"練習場地不計分，也不結算經驗值。":"僅顯示目前可遊玩的離線場地。");
    }
    void RebuildDurationOptions(){
        ControlSync sync(syncingControls);
        const bool boss=selection.mode=="boss",practice=selection.mode=="practice";
        const std::array<int,3> turfValues={90,180,180};const std::array<int,3> bossValues={180,240,300};
        std::string options;
        if(boss){for(const int seconds:bossValues){options+="<option value=\""+std::to_string(seconds)+"\">"+std::to_string(seconds/60)+" 分鐘</option>";}}
        else{for(std::size_t i=0;i<2;++i){const int seconds=turfValues[i];options+="<option value=\""+std::to_string(seconds)+"\">"+(seconds==90?"1.5":"3")+" 分鐘</option>";}}
        auto* durations=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"duration"));
        const std::string optionMode=boss?"boss":"turf";
        if(durationOptionMode!=optionMode){durations->RemoveAll();durations->SetInnerRML(options);durationOptionMode=optionMode;}
        const bool valid=boss?(selection.duration==180||selection.duration==240||selection.duration==300):(selection.duration==90||selection.duration==180);
        if(!valid)selection.duration=180;
        Value("duration",std::to_string(selection.duration));Hidden(menu,"duration-row",practice);Hidden(menu,"difficulty-row",practice);
    }
    void PopulateOptions(){
        ControlSync sync(syncingControls);
        RebuildStageOptions();
        auto fillCategory=[&](const char* id,const std::vector<EquipmentData>& rows,EquipmentCategory category){
            std::string items;for(const auto& row:rows){auto label=EquipmentLabel(category,row.id);if(label.empty())label=row.name;items+="<option value=\""+Escape(std::string(row.id))+"\">"+Escape(std::string(label))+"</option>";}Find(menu,id)->SetInnerRML(items);
        };
        fillCategory("main",MainEquipment(),EquipmentCategory::Main);fillCategory("sub",SubEquipment(),EquipmentCategory::Sub);fillCategory("special",SpecialEquipment(),EquipmentCategory::Special);
        std::string presets="<option value=\"custom\">自訂造型</option>";for(std::size_t i=0;i<CharacterPresets.size();++i){const auto& row=CharacterPresets[i];auto label=StyleLabel("preset",static_cast<int>(i));if(label.empty())label=row.name;presets+="<option value=\""+Escape(row.id)+"\">"+Escape(std::string(label))+"</option>";}
        Find(menu,"style-preset")->SetInnerRML(presets);
        FillStyleOptions(menu,"style-hair","hair",HairNames);FillStyleOptions(menu,"style-hat","hat",HatNames);FillStyleOptions(menu,"style-skin","skin",SkinNames);
        FillStyleOptions(menu,"style-eyes","eyes",EyeNames);FillStyleOptions(menu,"style-brows","brows",BrowNames);FillStyleOptions(menu,"style-outfit","outfit",OutfitNames);
    }
    void SyncGraphicsControls(){
        const bool wasSyncing=syncingControls;syncingControls=true;const auto& graphics=player.settings.graphics;
        const char* preset=graphics.preset==GraphicsPreset::Low?"low":graphics.preset==GraphicsPreset::High?"high":graphics.preset==GraphicsPreset::Custom?"custom":"medium";
        Value("graphics-preset",preset);
        SetChecked(Find(menu,"graphics-shadows"),graphics.shadows);SetChecked(Find(menu,"graphics-reflections"),graphics.reflections);SetChecked(Find(menu,"graphics-post"),graphics.postEffects);
        syncingControls=wasSyncing;
    }
    void SyncProfileControls(){
        syncingControls=true;const auto& s=player.settings;
        Value("profile-name",player.name);Value("main",player.weapon);Value("sub",player.sub.empty()?std::string(GetEquipment(EquipmentCategory::Main,player.weapon).sub):player.sub);
        Value("special",player.special.empty()?std::string(GetEquipment(EquipmentCategory::Main,player.weapon).special):player.special);
        std::string preset="custom";
        for(const auto& item:CharacterPresets)if(item.style.hair==player.style.hair&&item.style.skin==player.style.skin&&item.style.outfit==player.style.outfit&&item.style.eyes==player.style.eyes&&item.style.hat==player.style.hat&&item.style.brows==player.style.brows){preset=item.id;break;}
        Value("style-preset",preset);
        Value("style-hair",std::to_string(player.style.hair));Value("style-hat",std::to_string(player.style.hat));Value("style-skin",std::to_string(player.style.skin));
        Value("style-eyes",std::to_string(player.style.eyes));Value("style-brows",std::to_string(player.style.brows));Value("style-outfit",std::to_string(player.style.outfit));
        for(const auto& item:std::array<std::pair<const char*,double>,14>{{
            {"sensitivity",s.sensitivity},{"pad-sensitivity",s.padSensitivity},{"aim-assist",s.aimAssist},{"touch-sensitivity",s.touchSensitivity},
            {"fov",s.fov},{"master",s.master*100},{"music",s.music*100},{"sfx",s.sfx*100},{"camera-shake",s.cameraShake},{"rumble",s.rumble},
            {"duration",static_cast<double>(selection.duration)},{"match-length",static_cast<double>(s.matchLength)},{"fps-cap",static_cast<double>(s.fpsCap)},{"palette",static_cast<double>(s.palette)}}}){
            const auto value=Number(item.second,item.first==std::string("duration")||item.first==std::string("match-length")||item.first==std::string("fps-cap")||item.first==std::string("palette")?0:2);
            Value(item.first,value);
        }
        Value("touch-controls",s.touchControls);Value("default-mode",s.lastMode=="zones"?"turf":s.lastMode);Value("default-difficulty",s.difficulty);Value("time-of-day",s.timeOfDay);
        SetChecked(Find(menu,"invert-y"),s.invertY);SetChecked(Find(menu,"aim-assist-mouse"),s.aimAssistMouse);SetChecked(Find(menu,"touch-invert-y"),s.touchInvertY);
        SetChecked(Find(menu,"graphics-shadows"),s.graphics.shadows);SetChecked(Find(menu,"bloom"),s.bloom);SetChecked(Find(menu,"show-fps"),s.showFps);SetChecked(Find(menu,"fullscreen"),s.fullscreen);
        SetChecked(Find(menu,"colorblind"),s.colorblind);SetChecked(Find(menu,"minimap"),s.minimap);SetChecked(Find(menu,"reduced-motion"),s.reducedMotion);
        SyncGraphicsControls();
        Text(menu,"sensitivity-value",Number(s.sensitivity,2)+"×");Text(menu,"pad-sensitivity-value",Number(s.padSensitivity,2)+"×");
        Text(menu,"aim-assist-value",Integer(s.aimAssist*100)+"%");Text(menu,"touch-sensitivity-value",Number(s.touchSensitivity,2)+"×");Text(menu,"fov-value",Integer(s.fov)+"°");
        Text(menu,"master-value",Integer(s.master*100)+"%");Text(menu,"music-value",Integer(s.music*100)+"%");Text(menu,"sfx-value",Integer(s.sfx*100)+"%");
        Text(menu,"camera-shake-value",Integer(s.cameraShake*100)+"%");Text(menu,"rumble-value",Integer(s.rumble*100)+"%");
        syncingControls=false;UpdateProfileDisplay();UpdateLoadoutStats();
    }
    void UpdateProfileDisplay(){
        const int need=XpForLevel(player.level);Text(menu,"profile-header",player.name+"　Lv."+std::to_string(player.level));
        Text(menu,"profile-progress","Lv. "+std::to_string(player.level)+"　經驗值（XP） "+std::to_string(player.xp)+" / "+std::to_string(need));
        Text(menu,"profile-stats","勝利  "+std::to_string(player.wins)+"　　對戰  "+std::to_string(player.matches)+"　　總塗地  "+Integer(player.totalTurf));
    }
    void UpdateLoadoutStats(){
        try{
            const auto& main=GetEquipment(EquipmentCategory::Main,player.weapon.empty()?selection.main:player.weapon);
            const std::array<std::pair<const char*,const char*>,5> stats={{{"range","射程"},{"damage","傷害"},{"rate","連射"},{"mobility","機動"},{"paint","塗地"}}};
            for(const auto& [key,label]:stats){const std::string id="stat-"+std::string(key);const double value=std::clamp(main.number(std::string("stats.")+key,.5),0.,1.);Find(menu,id)->SetProperty("width",Integer(value*100)+"%");Text(menu,id+"-value",Integer(value*100));}
        }catch(...){for(const char* id:{"stat-range","stat-damage","stat-rate","stat-mobility","stat-paint"})Find(menu,id)->SetProperty("width","0%");}
    }
    void GoTo(UiScreen destination){
        screen=destination;
        const std::array<std::pair<UiScreen,const char*>,9> panels={{{UiScreen::Title,"title"},{UiScreen::Home,"home"},{UiScreen::Play,"play"},{UiScreen::Online,"online"},{UiScreen::Loadout,"loadout"},{UiScreen::Locker,"locker"},{UiScreen::Settings,"settings"},{UiScreen::HowTo,"howto"},{UiScreen::Credits,"credits"}}};
        for(const auto& [id,name]:panels){Hidden(menu,std::string("panel-")+name,id!=destination);auto* nav=menu->GetElementById(std::string("nav-")+name);if(nav)nav->SetClass("selected",id==destination);}
        Hidden(menu,"navigation",destination==UiScreen::Title);Hidden(menu,"menu-footer",destination==UiScreen::Title);Hidden(menu,"profile-header",destination==UiScreen::Title);
        const char* title=destination==UiScreen::Title?"":"Tiny Splatoon";
        switch(destination){case UiScreen::Home:title="首頁";break;case UiScreen::Play:title="遊玩";break;case UiScreen::Online:title="線上遊玩";break;case UiScreen::Loadout:title="裝備";break;case UiScreen::Locker:title="角色置物櫃";break;case UiScreen::Settings:title="設定";break;case UiScreen::HowTo:title="操作說明";break;case UiScreen::Credits:title="製作名單";break;default:break;}
        Text(menu,"screen-title",title);menu->SetClass("title-screen",destination==UiScreen::Title);
        if(destination==UiScreen::Locker)Find(menu,"preview-surface")->SetClass("locker-preview",true);else Find(menu,"preview-surface")->SetClass("locker-preview",false);
    }
    void LockerTab(const std::string& tab){
        for(const char* name:{"presets","hair","face","outfit"}){const bool active=tab==name;Hidden(menu,std::string("locker-section-")+name,!active);Find(menu,std::string("locker-tab-")+name)->SetClass("selected",active);}
    }
    void Tab(const std::string& tab){
        if(tab=="home")GoTo(UiScreen::Home);else if(tab=="play")GoTo(UiScreen::Play);else if(tab=="online")GoTo(UiScreen::Online);
        else if(tab=="loadout")GoTo(UiScreen::Loadout);else if(tab=="locker")GoTo(UiScreen::Locker);else if(tab=="settings")GoTo(UiScreen::Settings);
        else if(tab=="howto")GoTo(UiScreen::HowTo);else if(tab=="credits")GoTo(UiScreen::Credits);
    }
    void Mode(){
        const bool boss=selection.mode=="boss",practice=selection.mode=="practice";
        selection.practice=practice;RebuildStageOptions();RebuildDurationOptions();
        Find(menu,"mode-turf")->SetClass("selected",!boss&&!practice);Find(menu,"mode-boss")->SetClass("selected",boss);Find(menu,"mode-practice")->SetClass("selected",practice);
        Text(menu,"mode-description",boss?"一名玩家與七名隊友共同挑戰破艦巨獸。":practice?"在選定場地自由移動、射擊與練習，不結算勝負或經驗。":"與七名電腦玩家進行 4 對 4 對戰，塗滿更多地面。");
        Text(menu,"start-label",boss?"開始協力挑戰":practice?"開始自由練習":"開始塗地對戰");
        Text(menu,"start-note",practice?"自由練習 · 不結算資料":"1 位玩家 · 7 名電腦玩家");
    }
    void CommitSettingFromControl(const std::string& id,const std::string& value,Rml::Element* target){
        auto& s=player.settings;
        auto number=[&](){return std::stod(value);};auto check=[&](){return IsChecked(target);};
        if(id=="sensitivity")s.sensitivity=number();else if(id=="pad-sensitivity")s.padSensitivity=number();
        else if(id=="fov")s.fov=number();else if(id=="camera-shake")s.cameraShake=number();else if(id=="rumble")s.rumble=number();
        else if(id=="master")s.master=number()/100.;else if(id=="music")s.music=number()/100.;else if(id=="sfx")s.sfx=number()/100.;
        else if(id=="aim-assist")s.aimAssist=number();else if(id=="touch-sensitivity")s.touchSensitivity=number();
        else if(id=="invert-y")s.invertY=check();else if(id=="shadows")s.shadows=check();else if(id=="bloom")s.bloom=check();
        else if(id=="show-fps")s.showFps=check();else if(id=="fullscreen")s.fullscreen=check();else if(id=="colorblind")s.colorblind=check();
        else if(id=="minimap")s.minimap=check();else if(id=="reduced-motion")s.reducedMotion=check();
        else if(id=="aim-assist-mouse")s.aimAssistMouse=check();else if(id=="touch-invert-y")s.touchInvertY=check();
        else if(id=="touch-controls")s.touchControls=value;else if(id=="quality")s.quality=value;
        else if(id=="fps-cap")s.fpsCap=std::stoi(value);else if(id=="palette")s.palette=std::stoi(value);
        else if(id=="default-mode")s.lastMode=value;else if(id=="default-difficulty")s.difficulty=value;
        else if(id=="match-length")s.matchLength=std::stoi(value);else if(id=="time-of-day")s.timeOfDay=value;
        if(id=="master"||id=="music"||id=="sfx"){
            if(volumes)volumes(static_cast<float>(s.master),static_cast<float>(s.music),static_cast<float>(s.sfx));Play("ui_slider");
        }else if(id=="reduced-motion"||id.find("invert")!=std::string::npos||id=="minimap"||id=="show-fps"||id=="fullscreen"||id=="colorblind"||id=="bloom"||id=="shadows")Play("ui_toggle");
        else Play("ui_slider");
        Commit();
    }
    void ApplyCharacterControl(const std::string& id,const std::string& value){
        auto& style=player.style;
        if(id=="style-preset"){
            const auto found=std::find_if(CharacterPresets.begin(),CharacterPresets.end(),[&](const auto& item){return item.id==value;});
            if(found!=CharacterPresets.end())style=found->style;
        }else{
            const int index=std::stoi(value);
            if(id=="style-hair")style.hair=index;else if(id=="style-hat")style.hat=index;else if(id=="style-skin")style.skin=index;
            else if(id=="style-eyes")style.eyes=index;else if(id=="style-brows")style.brows=index;else if(id=="style-outfit")style.outfit=index;
        }
        Commit();Play("ui_click");
    }
    void ProcessEvent(Rml::Event& event)override{
        if(event.GetType()=="keydown"&&event.GetParameter<int>("key_identifier",0)==Rml::Input::KI_ESCAPE&&hud->IsVisible()&&Find(hud,"leave-confirm")->IsVisible()){
            Find(hud,"leave-stay")->Click();event.StopPropagation();return;
        }
        if(event.GetType()=="keydown"&&event.GetParameter<int>("key_identifier",0)==Rml::Input::KI_ESCAPE&&menuVisible&&screen!=UiScreen::Home&&screen!=UiScreen::Title){
            Find(menu,"menu-back")->Click();event.StopPropagation();return;
        }
        auto* target=event.GetTargetElement();if(!target)return;
        auto* element=target;while(element&&!element->GetId().size())element=element->GetParentNode();if(!element)return;
        const std::string id=element->GetId();const auto type=event.GetType();
        if(type=="mouseover"||type=="focus"){
            if((element->GetTagName()=="button"||element->GetTagName()=="select")&&hovered!=id){hovered=id;Play("ui_hover");}return;
        }
        if(type=="input"||type=="change"){
            if(syncingControls)return;
            auto* control=dynamic_cast<Rml::ElementFormControl*>(target);if(!control)return;
            const auto value=control->GetValue();
            if(id=="profile-name"){
                if(type=="change"){
                    player.name=value;Commit();Play("ui_click");
                }
                return;
            }
            if(type!="change")return;
            if(id=="stage")selection.stage=value;
            else if(id=="theme"){selection.theme=value;player.settings.timeOfDay=value;Commit();}
            else if(id=="difficulty"){selection.difficulty=value;player.settings.difficulty=value;Commit();}
            else if(id=="duration"){
                selection.duration=std::stoi(value);
                if(selection.mode=="boss")player.settings.bossLength=selection.duration;else player.settings.matchLength=selection.duration;
                Commit();
            }
            else if(id=="main"){
                player.weapon=value;const auto& row=GetEquipment(EquipmentCategory::Main,value);player.sub=std::string(row.sub);player.special=std::string(row.special);
                selection.main=value;selection.sub=player.sub;selection.special=player.special;Value("sub",selection.sub);Value("special",selection.special);Commit();Play("ui_click");
            }else if(id=="sub"){player.sub=value;selection.sub=value;Commit();Play("ui_click");}
            else if(id=="special"){player.special=value;selection.special=value;Commit();Play("ui_click");}
            else if(id.rfind("style-",0)==0)ApplyCharacterControl(id,value);
            else if(id=="graphics-preset"){
                if(value=="low")player.settings.graphics=ForPreset(GraphicsPreset::Low);else if(value=="high")player.settings.graphics=ForPreset(GraphicsPreset::High);
                else if(value=="medium")player.settings.graphics=ForPreset(GraphicsPreset::Medium);else player.settings.graphics.preset=GraphicsPreset::Custom;
                player.settings.quality=value=="low"?"low":value=="medium"?"medium":"high";
                Commit();Play("ui_click");
            }else if(id=="graphics-shadows"||id=="graphics-reflections"||id=="graphics-post"){
                auto& graphics=player.settings.graphics;const bool enabled=IsChecked(target);
                if(id=="graphics-shadows"){graphics.shadows=enabled;player.settings.shadows=enabled;if(enabled&&!graphics.shadowMapSize)graphics.shadowMapSize=1024;}
                else if(id=="graphics-reflections")graphics.reflections=enabled;else graphics.postEffects=enabled;
                graphics.preset=GraphicsPreset::Custom;Commit();Play("ui_toggle");
            }else{
                CommitSettingFromControl(id,value,target);
            }
            if(id=="stage"||id=="duration"){Play("ui_click");}
            Mode();return;
        }
        if(type!="click"||element->GetTagName()!="button")return;
        if(id=="title-start"){GoTo(UiScreen::Home);Play("ui_confirm");}
        else if(id.rfind("nav-",0)==0){Tab(id.substr(4));Play("ui_click");}
        else if(id=="home-play"){GoTo(UiScreen::Play);Play("ui_click");}
        else if(id=="online-play"){GoTo(UiScreen::Play);Play("ui_click");}
        else if(id=="menu-back"){
            if(practiceGear){practiceGear=false;menuVisible=false;menu->Hide();hud->Show();hudOverlay=false;action=UiAction::Resume;Play("ui_back");}
            else if(hudOverlay){hudOverlay=false;practiceGear=false;menuVisible=false;menu->Hide();hud->Show();Play("ui_back");}
            else{GoTo(UiScreen::Home);Play("ui_back");}
        }
        else if(id=="mode-turf"||id=="mode-boss"||id=="mode-practice"){
            selection.mode=id.substr(5);selection.practice=selection.mode=="practice";player.settings.lastMode=selection.mode=="practice"?"turf":selection.mode;Mode();Commit();Play("ui_click");
        }
        else if(id=="start"){
            selection.practice=selection.mode=="practice";action=selection.practice?UiAction::Practice:UiAction::Start;Play("ui_confirm");
        }
        else if(id=="nav-locker"||id=="loadout-locker"){GoTo(UiScreen::Locker);LockerTab("presets");Play("ui_click");}
        else if(id.rfind("locker-tab-",0)==0){LockerTab(id.substr(11));Play("ui_click");}
        else if(id=="resume"){if(practiceGear){practiceGear=false;menuVisible=false;menu->Hide();hud->Show();action=UiAction::Resume;}else action=UiAction::Resume;Play("ui_back");}
        else if(id=="results-menu"){action=UiAction::StageSelect;Play("ui_back");}
        else if(id=="results-rematch"){action=UiAction::Rematch;Play("ui_confirm");}
        else if(id=="close"||id=="results-close"){action=UiAction::Close;Play("ui_back");}
        else if(id=="pause-close"||id=="return-menu"){
            leaveAction=id=="pause-close"?UiAction::Close:UiAction::StageSelect;
            Text(hud,"leave-title",id=="pause-close"?"離開遊戲？":"離開這場對戰？");
            Text(hud,"leave-description",id=="pause-close"?"目前對戰不會結算。確定要關閉遊戲嗎？":"目前對戰不會結算。確定要返回選單嗎？");
            Hidden(hud,"leave-confirm",false);Find(hud,"leave-stay")->Focus();Play("ui_click");
        }
        else if(id=="leave-stay"){Hidden(hud,"leave-confirm",true);Play("ui_back");}
        else if(id=="leave-yes"){action=leaveAction;Play("ui_confirm");}
        else if(id=="practice-reset"){action=UiAction::PracticeReset;Play("ui_confirm");}
        else if(id=="practice-fill"){action=UiAction::PracticeFill;Play("ui_confirm");}
        else if(id=="practice-spawn"){action=UiAction::PracticeSpawn;Play("ui_confirm");}
        else if(id=="practice-stage"){action=UiAction::PracticeStage;Play("ui_click");}
        else if(id=="practice-gear"){OpenPracticeGear();Play("ui_click");}
        else if(id=="pause-settings"||id=="pause-howto"){
            previousScreen=screen;hudOverlay=true;menuVisible=true;menu->Show();GoTo(id=="pause-settings"?UiScreen::Settings:UiScreen::HowTo);Play("ui_click");
        }
    }
    void OpenPracticeGear(){
        practiceGear=true;hudOverlay=false;activePractice=true;interactive=true;previousScreen=UiScreen::Locker;menuVisible=true;menu->Show();GoTo(UiScreen::Locker);LockerTab("presets");Find(menu,"menu-back")->Focus();
    }
    UiAction Render(int width,int height,bool renderHud=false){
        menu->SetClass("practice-gear",practiceGear);
        int ww=0,wh=0;glfwGetWindowSize(window,&ww,&wh);
        context->SetDimensions({width,height});const float scale=std::clamp(std::min(width/1440.f,height/900.f),.72f,1.6f);context->SetDensityIndependentPixelRatio(scale);
        if(interactive){
            double x=0,y=0;glfwGetCursorPos(window,&x,&y);
            int modifiers=0;if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS)modifiers|=Rml::Input::KM_SHIFT;
            if(glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS)modifiers|=Rml::Input::KM_CTRL;
            if(glfwGetKey(window,GLFW_KEY_LEFT_ALT)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_ALT)==GLFW_PRESS)modifiers|=Rml::Input::KM_ALT;
            context->ProcessMouseMove(static_cast<int>(x*width/std::max(1,ww)),static_cast<int>(y*height/std::max(1,wh)),modifiers);
            for(int b=0;b<3;++b){const bool held=glfwGetMouseButton(window,b)==GLFW_PRESS;if(held!=mouse[b]){if(held)context->ProcessMouseButtonDown(b,modifiers);else context->ProcessMouseButtonUp(b,modifiers);mouse[b]=held;}}
            const int nativeKeys[]={GLFW_KEY_TAB,GLFW_KEY_ENTER,GLFW_KEY_SPACE,GLFW_KEY_LEFT,GLFW_KEY_RIGHT,GLFW_KEY_UP,GLFW_KEY_DOWN,GLFW_KEY_ESCAPE,GLFW_KEY_BACKSPACE,GLFW_KEY_DELETE,GLFW_KEY_HOME,GLFW_KEY_END};
            const Rml::Input::KeyIdentifier uiKeys[]={Rml::Input::KI_TAB,Rml::Input::KI_RETURN,Rml::Input::KI_SPACE,Rml::Input::KI_LEFT,Rml::Input::KI_RIGHT,Rml::Input::KI_UP,Rml::Input::KI_DOWN,Rml::Input::KI_ESCAPE,Rml::Input::KI_BACK,Rml::Input::KI_DELETE,Rml::Input::KI_HOME,Rml::Input::KI_END};
            for(std::size_t i=0;i<keys.size();++i){const bool held=glfwGetKey(window,nativeKeys[i])==GLFW_PRESS;if(held!=keys[i]){if(held)context->ProcessKeyDown(uiKeys[i],modifiers);else context->ProcessKeyUp(uiKeys[i],modifiers);keys[i]=held;}}
        }
        context->Update();renderer.Begin(width,height);
        if((renderHud&&hud->IsVisible())||(menuVisible&&menu->IsVisible()))context->Render();
        renderer.End();return std::exchange(action,UiAction::None);
    }
    UiPreview Preview()const{
        UiPreview result;result.screen=screen;
        Rml::Element* element=nullptr;
        if((menuVisible||hudOverlay||practiceGear)&&menu&&menu->IsVisible()){
            element=menu->GetElementById("preview-surface");
            result.screen=practiceGear?UiScreen::PracticeGear:screen;
            result.type=practiceGear?UiPreviewType::Locker:screen==UiScreen::Loadout?UiPreviewType::Loadout:screen==UiScreen::Locker?UiPreviewType::Locker:UiPreviewType::Player;
        }else if(hud&&hud->IsVisible()&&hud->GetElementById("results")->IsVisible()){
            element=hud->GetElementById("results-preview");result.screen=UiScreen::Results;result.type=UiPreviewType::Podium;
        }else if(hud&&hud->IsVisible()&&hud->GetElementById("pause")->IsVisible()){
            result.screen=Find(hud,"leave-confirm")->IsVisible()?UiScreen::LeaveConfirm:UiScreen::Pause;result.type=activePractice?UiPreviewType::Practice:UiPreviewType::None;
            if(result.type==UiPreviewType::None)return result;
        }else if(hud&&hud->IsVisible()){
            result.screen=activePractice?UiScreen::Practice:UiScreen::Match;result.type=UiPreviewType::None;return result;
        }else{result.type=UiPreviewType::None;return result;}
        if(!element||!element->IsVisible()){result.type=UiPreviewType::None;return result;}
        const auto offset=element->GetAbsoluteOffset();const float width=static_cast<float>(context->GetDimensions().x),height=static_cast<float>(context->GetDimensions().y);
        if(width<=0||height<=0)return result;
        result.x=offset.x/width;result.y=offset.y/height;result.width=element->GetClientWidth()/width;result.height=element->GetClientHeight()/height;
        if(result.width<=0||result.height<=0){result.type=UiPreviewType::None;result.x=result.y=result.width=result.height=0;}
        return result;
    }
    void UpdateMinimap(const StageData& stage,const FacePaint& paint,MatchSimulation& match){
        if(!player.settings.minimap){Hidden(hud,"minimap",true);return;}Hidden(hud,"minimap",false);
        const auto& state=match.local().state;const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind);
        const double now=glfwGetTime();
        std::string cache=std::string(stage.id)+":"+std::to_string(paint.version())+":"+std::to_string(player.settings.palette)+":"+Bool(player.settings.colorblind);
        for(const auto& actor:match.actors())cache+=":"+std::to_string(static_cast<int>(std::round(actor->state.pos.x*4)))+","+std::to_string(static_cast<int>(std::round(actor->state.pos.z*4)))+","+std::to_string(actor->state.alive);
        if(minimapCache!=cache&&now>=nextMinimapUpdate){
            constexpr int columns=32,rows=24;const double minX=stage.bounds.x,maxX=stage.bounds.y,minZ=stage.bounds.z,maxZ=stage.bounds.w;
            const double spanX=std::max(1e-6,maxX-minX),spanZ=std::max(1e-6,maxZ-minZ);
            if(terrainStage!=stage.id||terrainCells.size()!=columns*rows){
                terrainStage=stage.id;terrainCells.assign(columns*rows,0);
                for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
                    const double wx=minX+(x+.5)*spanX/columns,wz=maxZ-(y+.5)*spanZ/rows;
                    for(const auto& face:stage.faces){
                        if(!face.turf||face.normal.y<.65)continue;
                        const double det=face.u.x*face.v.z-face.u.z*face.v.x;if(std::abs(det)<1e-8)continue;
                        const double dx=wx-face.origin.x,dz=wz-face.origin.z;
                        const double u=(dx*face.v.z-dz*face.v.x)/det,v=(face.u.x*dz-face.u.z*dx)/det;
                        if(u>=-.01&&u<=face.width+.01&&v>=-.01&&v<=face.height+.01){terrainCells[y*columns+x]=1;break;}
                    }
                }
                for(const auto& block:stage.blocks){
                    if(!block.solid||block.hidden)continue;
                    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
                        const double wx=minX+(x+.5)*spanX/columns,wz=maxZ-(y+.5)*spanZ/rows;
                        const double dx=wx-block.center.x,dz=wz-block.center.z;
                        const double alongX=dx*block.axes[0].x+dz*block.axes[0].z;
                        const double alongZ=dx*block.axes[2].x+dz*block.axes[2].z;
                        if(std::abs(alongX)<=block.half.x&&std::abs(alongZ)<=block.half.z)terrainCells[y*columns+x]=block.grate?3:2;
                    }
                }
            }
            std::vector<unsigned char> cells=terrainCells;
            for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
                const double wx=minX+(x+.5)*spanX/columns,wz=maxZ-(y+.5)*spanZ/rows;
                for(std::size_t i=0;i<stage.faces.size();++i){
                    const auto& face=stage.faces[i];if(!face.paintable||!face.turf||face.normal.y<.65)continue;
                    const double det=face.u.x*face.v.z-face.u.z*face.v.x;if(std::abs(det)<1e-8)continue;
                    const double dx=wx-face.origin.x,dz=wz-face.origin.z;
                    const double u=(dx*face.v.z-dz*face.v.x)/det,v=(face.u.x*dz-face.u.z*dx)/det;
                    if(u<0||u>face.width||v<0||v>face.height)continue;
                    if(paint.faceGrid(static_cast<int>(i)).offset<0)continue;
                    const int ink=paint.sample(static_cast<int>(i),u,v);if(ink)cells[y*columns+x]=ink==1?4:5;
                }
            }
            auto classFor=[](unsigned char code)->const char*{switch(code){case 1:return "map-floor";case 2:return "map-obstacle";case 3:return "map-grate";case 4:return "map-team-a";case 5:return "map-team-b";default:return "";}};
            std::string markup;
            for(int y=0;y<rows;++y)for(int x=0;x<columns;){
                const auto code=cells[y*columns+x];int end=x+1;if(code)while(end<columns&&cells[y*columns+end]==code)++end;
                if(code){const double left=100.0*x/columns,top=100.0*y/rows,width=100.0*(end-x)/columns,height=100.0/rows;
                    const std::string color=code==4?std::string(palette[0]):code==5?std::string(palette[1]):std::string();
                    markup+="<div class=\"map-cell "+std::string(classFor(code))+"\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;width:"+Number(width,3)+"%;height:"+Number(height,3)+"%;"+(color.empty()?std::string():"background-color:"+color+";")+"\"></div>";}
                x=end;
            }
            for(std::size_t i=0;i<match.actors().size();++i){
                const auto& actor=match.actors()[i];if(!actor->state.alive)continue;
                const double left=std::clamp((actor->state.pos.x-minX)/spanX*100.,0.,100.);
                const double top=std::clamp((maxZ-actor->state.pos.z)/spanZ*100.,0.,100.);
                const std::string color(actor->team()==0?palette[0]:palette[1]);
                markup+="<div class=\"map-actor "+std::string(i==0?"map-actor-local ":"")+(actor->team()==0?"map-actor-a":"map-actor-b")+"\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;background-color:"+color+";\"></div>";
            }
            const auto key=std::string("hud/minimap-map");if(textCache[key]!=markup){Find(hud,"minimap-map")->SetInnerRML(markup);textCache[key]=markup;}minimapCache=cache;nextMinimapUpdate=now+.1;
        }
        const char* ground=state.groundTeam==1?"自隊墨水":state.groundTeam==2?"敵隊墨水":"乾燥地面";
        const auto coverage=paint.coverage();Text(hud,"terrain-status",ground+std::string("　自隊 ")+Percent(coverage[0]));
        Text(hud,"position-status","位置  "+Number(state.pos.x)+", "+Number(state.pos.z)+"　·　敵隊 "+Percent(coverage[1]));
    }
    void UpdateResults(MatchSimulation& match,const std::array<double,2>& coverage){
        if(!match.result())return;const auto& result=*match.result();const auto& local=match.local();const bool win=result.boss?result.boss->win:result.winner==local.team();
        Text(hud,"result-title",win?"勝利":"再接再厲");Text(hud,"result-detail",result.boss?(result.boss->win?"已擊破破艦巨獸":"協力挑戰結束"):"塗地對戰結束");
        Text(hud,"result-score",result.boss?("破艦巨獸剩餘生命值  "+Integer(result.boss->hp)):Percent(coverage[0])+"  /  "+Percent(coverage[1]));
        std::string xp="本場經驗值（XP） +"+std::to_string(reward.gained);
        if(reward.applied&&reward.levelAfter>reward.levelBefore)xp+="　升級 Lv."+std::to_string(reward.levelBefore)+" → Lv."+std::to_string(reward.levelAfter);
        else xp+="　Lv."+std::to_string(player.level)+"　"+std::to_string(player.xp)+" / "+std::to_string(XpForLevel(player.level));
        Text(hud,"result-xp",xp);
        std::string rows="<tr><th>玩家</th><th>塗地</th><th>倒下</th>";if(match.boss())rows+="<th>破艦巨獸傷害</th><th>弱點命中</th><th>小螃蟹擊破</th>";rows+="</tr>";
        for(std::size_t i=0;i<match.actors().size();++i){const auto& actor=*match.actors()[i];rows+="<tr><td>"+Escape(match.roster()[i].name)+"</td><td>"+Integer(actor.turfPainted())+"</td><td>"+std::to_string(actor.deaths())+"</td>";
            if(const auto* boss=match.boss())rows+="<td>"+Integer(boss->squadDamage()[i])+"</td><td>"+std::to_string(boss->squadWeakHits()[i])+"</td><td>"+std::to_string(boss->squadSplats()[i])+"</td>";rows+="</tr>";}
        const auto key="hud/results-table";if(textCache[key]!=rows){Find(hud,"results-table")->SetInnerRML(rows);textCache[key]=rows;}
    }
    void RenderHudData(const StageData& stage,const FacePaint& paint,MatchSimulation& match,bool paused,bool nativeApp,bool practice){
        const bool results=match.phase()==MatchPhase::Results;
        activePractice=practice;
        interactive=paused||results||practiceGear||hudOverlay;
        Hidden(hud,"pause",!paused||results);Hidden(hud,"results",!results);Hidden(hud,"return-menu",!nativeApp);Hidden(hud,"results-menu",!nativeApp);
        Hidden(hud,"practice-tools",!practice);Hidden(hud,"reticle",paused||results);
        Text(hud,"pause-title",practice?"練習暫停":"已暫停");Text(hud,"pause-description",practice?"練習不會計時或結算。調整角色、補充資源，或更換場地。":"準備好後，繼續這場對戰。");
        const auto stageLabel=StageLabel(stage.id);Text(hud,"stage-name",stageLabel.empty()?std::string(stage.name):std::string(stageLabel));
        const int seconds=static_cast<int>(std::ceil(std::max(0.,match.remaining())));char clock[16];std::snprintf(clock,sizeof(clock),"%02d:%02d",seconds/60,seconds%60);Text(hud,"clock",clock);
        const auto coverage=paint.coverage();Text(hud,"score-a",Percent(coverage[0]));Text(hud,"score-b",Percent(coverage[1]));
        const auto& local=match.local();const auto& state=local.state;const double cost=local.weapon().number("specialCost",190);
        Text(hud,"hp",Integer(state.hp));Text(hud,"ink",Integer(state.ink));Text(hud,"special-status",state.specialCharge>=cost?"特殊技能就緒  F / Q":Integer(state.specialCharge)+" / "+Integer(cost));
        Find(hud,"special-fill")->SetProperty("width",Integer(std::clamp(state.specialCharge/cost,0.,1.)*100)+"%");Find(hud,"special-meter")->SetClass("ready",state.specialCharge>=cost);
        Text(hud,"player-status",state.alive?(local.specialPose().mapTarget?"瞄準目標後按左鍵確認":""):"等待復活  "+Integer(std::ceil(state.respawnTimer))+" 秒");
        Hidden(hud,"boss",!match.boss());Hidden(hud,"team-score",match.boss()!=nullptr);
        if(const auto* boss=match.boss()){
            const auto& state=boss->state();Text(hud,"boss-phase","階段 "+std::to_string(state.phase));Find(hud,"boss-fill")->SetProperty("width",Integer(state.maxHp>0?std::clamp(state.hp/state.maxHp,0.,1.)*100:0)+"%");Text(hud,"boss-hp",Integer(state.hp)+" / "+Integer(state.maxHp));
            std::string move;if(boss->move()){const char* names[]={"重鉗衝擊","貨櫃彈幕","墨砲掃射","船殼衝撞","小蟹群","狂亂攻勢"};move=names[static_cast<int>(boss->move()->id)];switch(boss->move()->phase(state.clock)){case BossMovePhase::Telegraph:move+="  ·  注意預兆";break;case BossMovePhase::Active:move+="  ·  攻擊中";break;case BossMovePhase::Recovery:move+="  ·  恢復中";break;default:break;}}Text(hud,"boss-move",move);
        }
        UpdateMinimap(stage,paint,match);
        const double now=glfwGetTime();if(lastHudTime>0&&now>lastHudTime)hudFps=static_cast<int>(std::round(1./(now-lastHudTime)));lastHudTime=now;
        Hidden(hud,"fps",!player.settings.showFps);if(player.settings.showFps)Text(hud,"fps-value",std::to_string(hudFps));
        if(results)UpdateResults(match,coverage);
    }
};

GameUi::GameUi(GLFWwindow* window,const std::filesystem::path& assets):impl_(std::make_unique<Impl>(window,assets)){}
GameUi::~GameUi()=default;
const UiSetup& GameUi::setup()const{return impl_->selection;}
const PlayerProfile& GameUi::profile()const{return impl_->player;}
const GraphicsSettings& GameUi::graphicsSettings()const{return impl_->player.settings.graphics;}
UiPreview GameUi::preview()const{return impl_->Preview();}
void GameUi::SetProfile(PlayerProfile profile){
    auto& p=*impl_;NormalizeProfile(profile);p.player=std::move(profile);
    p.selection.main=p.player.weapon;p.selection.sub=p.player.sub.empty()?std::string(GetEquipment(EquipmentCategory::Main,p.player.weapon).sub):p.player.sub;
    p.selection.special=p.player.special.empty()?std::string(GetEquipment(EquipmentCategory::Main,p.player.weapon).special):p.player.special;
    p.selection.mode=p.player.settings.lastMode=="boss"?"boss":"turf";p.selection.practice=false;p.selection.difficulty=p.player.settings.difficulty;
    p.selection.duration=p.selection.mode=="boss"?p.player.settings.bossLength:p.player.settings.matchLength;p.selection.theme=p.player.settings.timeOfDay;
    p.SyncProfileControls();p.Mode();
}
void GameUi::SetProfileSink(std::function<void(const PlayerProfile&)> sink){impl_->profileSink=std::move(sink);}
void GameUi::SetSaveError(std::string error){impl_->SetSaveError(error);}
void GameUi::SetReward(MatchRewardResult reward){impl_->reward=reward;}
void GameUi::SetSoundSink(std::function<void(const std::string&)> value){impl_->sound=std::move(value);}
void GameUi::SetVolumeSink(std::function<void(float,float,float)> value){impl_->volumes=std::move(value);if(impl_->volumes)impl_->volumes(static_cast<float>(impl_->player.settings.master),static_cast<float>(impl_->player.settings.music),static_cast<float>(impl_->player.settings.sfx));}
void GameUi::ShowMenu(const std::string& initialStage){
    auto& p=*impl_;p.selection.stage=initialStage;p.selection.mode=p.player.settings.lastMode=="boss"?"boss":"turf";p.selection.practice=false;
    p.RebuildStageOptions();p.selection.difficulty=p.player.settings.difficulty;p.selection.theme=p.player.settings.timeOfDay;
    p.selection.duration=p.selection.mode=="boss"?p.player.settings.bossLength:p.player.settings.matchLength;
    p.Value("difficulty",p.selection.difficulty);p.Value("theme",p.selection.theme);p.Value("duration",std::to_string(p.selection.duration));
    p.Tab("home");p.LockerTab("presets");p.menu->Show();p.hud->Hide();p.menuVisible=true;p.practiceGear=false;p.hudOverlay=false;p.interactive=true;p.action=UiAction::None;p.Mode();
}
void GameUi::ShowTitle(const std::string& initialStage){ShowMenu(initialStage);auto& p=*impl_;p.GoTo(UiScreen::Title);p.Find(p.menu,"title-start")->Focus();}
void GameUi::ShowPracticeGear(){impl_->OpenPracticeGear();}
UiAction GameUi::RenderMenu(int width,int height){return impl_->Render(width,height,false);}
void GameUi::ShowPracticeStage(const std::string& initialStage){ShowMenu(initialStage);auto& p=*impl_;p.selection.mode="practice";p.selection.practice=true;p.Mode();p.GoTo(UiScreen::Play);}
UiAction GameUi::RenderHud(const StageData& stage,const FacePaint& paint,MatchSimulation& match,int width,int height,bool paused,bool nativeApp,bool practice){
    auto& p=*impl_;
    if(p.practiceGear||p.hudOverlay){p.menu->Show();p.hud->Show();p.menuVisible=true;}
    else{p.menu->Hide();p.hud->Show();p.menuVisible=false;}
    p.RenderHudData(stage,paint,match,paused,nativeApp,practice);
    return p.Render(width,height,true);
}
} // namespace restoration
