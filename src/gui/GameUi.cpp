#include "GameUi.hpp"
#include "InkTankElement.hpp"
#include "WeaponReticleElement.hpp"
#include "SpecialGaugeElement.hpp"
#include "BattleFeedbackElement.hpp"
#include "RmlRenderer.hpp"
#include "../game/ArsenalData.hpp"
#include "../game/MatchSimulation.hpp"
#include "../game/PlayerSimulation.hpp"
#include "../game/ZoneControl.hpp"
#include "UiText.hpp"
#include "EquipmentDescriptionZhTw.hpp"
#include "../world/FacePaint.hpp"
#include "../world/ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
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
std::string ZoneCount(double n){return std::to_string(static_cast<int>(std::ceil(std::max(0.0,n)-1e-9)));}
std::string Percent(double n){char out[32];std::snprintf(out,sizeof(out),"%.1f%%",n*100);return out;}
std::string Number(double n,int decimals=2){char out[32];std::snprintf(out,sizeof(out),decimals==0?"%.0f":"%.2f",n);return out;}
std::string DeathMarkIcon(const PlayerProfile& profile,int team){
    const int palette=profile.settings.colorblind?5:(profile.settings.palette>=0&&profile.settings.palette<5?profile.settings.palette:0);
    return "markers/death-"+std::to_string(palette)+"-"+std::to_string(team==0?0:1)+".png";
}
std::string ZoneReason(std::string_view reason){
    if(reason=="knockout")return "擊倒勝";
    if(reason=="time")return "時間結束";
    if(reason=="comeback")return "延長賽反超";
    if(reason=="retake")return "延長賽奪回區域";
    if(reason=="neutralised")return "延長賽守住勝勢";
    if(reason=="overtime-cap")return "延長賽時間上限";
    return "區域爭奪結束";
}

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
    auto* element=dynamic_cast<Rml::ElementFormControlSelect*>(doc->GetElementById(id));if(element){element->RemoveAll();element->SetInnerRML(markup);}
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
    Rml::ElementInstancerGeneric<InkTankElement> inkTankInstancer;
    Rml::ElementInstancerGeneric<WeaponReticleElement> reticleInstancer;
    Rml::ElementInstancerGeneric<SpecialGaugeElement> specialGaugeInstancer;
    Rml::ElementInstancerGeneric<BattleFeedbackElement> feedbackInstancer;
    Rml::ElementInstancerGeneric<HudRingElement> ringInstancer;
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
    bool jumpMapOpen=false;
    int jumpMapSelection=-1,jumpMapQueuedKey=-1;
    std::string jumpMapQueuedLabel;
    std::vector<UiJumpMapTarget> jumpMapTargets;
    network::RoomView online;
    std::vector<UiOnlineRequest> onlineRequests;
    bool onlineServerSettings=false,onlineRoomSettings=false;
    std::string onlineStagesMode,onlineRosterMarkup;
    double onlineNoticeUntil=0;
    bool syncingControls=false;
    bool mouse[3]{};
    std::array<bool,12> keys{};
    std::string hovered;
    std::map<std::string,std::string> textCache;
    void* previousUser=nullptr;
    GLFWscrollfun previousScroll=nullptr;
    GLFWcharfun previousChar=nullptr;
    double lastHudTime=0;
    double inkFullIdle=0;
    const MatchSimulation* inkMatch=nullptr;
    const PlayerSimulation* inkActor=nullptr;
    glm::mat4 worldView{1.f};
    glm::mat4 nativeMapView{1.f};
    float projectionY=1.78f;
    glm::dvec3 worldEye{0};
    bool haveWorldView=false,haveNativeMapView=false;
    double nextMinimapUpdate=0;
    int hudFps=0;
    std::string minimapCache;
    std::string stageOptionMode,durationOptionMode;
    const ZoneControl* displayedZones=nullptr;
    std::size_t zoneEventCursor=0;
    double zoneCalloutUntil=0.0;
    const MatchSimulation* resultXpMatch=nullptr;
    double resultXpStartedAt=0.0,resultXpNextTick=0.0;
    bool resultIntro=false,resultSkipped=false,resultDone=false,resultFocused=false;
    struct DeathMark { glm::dvec3 pos; std::string name; int team=0; double born=0; std::uint64_t serial=0; };
    const MatchSimulation* deathMatch=nullptr;
    std::uint64_t deathEpoch=0,deathSeenSerial=0;
    std::vector<DeathMark> deathMarks;
    double deathClock=0.0;
    std::uint64_t deathSerial=0;

    Impl(GLFWwindow* value,const std::filesystem::path& assets):window(value),system(value){
        Rml::SetSystemInterface(&system);Rml::SetRenderInterface(&renderer);
        if(!Rml::Initialise())throw std::runtime_error("Unable to initialize player UI");
        try{
            Rml::Factory::RegisterElementInstancer("ink-tank",&inkTankInstancer);
            Rml::Factory::RegisterElementInstancer("weapon-reticle",&reticleInstancer);
            Rml::Factory::RegisterElementInstancer("special-gauge",&specialGaugeInstancer);
            Rml::Factory::RegisterElementInstancer("battle-feedback",&feedbackInstancer);
            Rml::Factory::RegisterElementInstancer("hud-ring",&ringInstancer);
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
    void Disable(const std::string& id,bool disabled){
        auto* element=Find(menu,id);
        if(auto* control=dynamic_cast<Rml::ElementFormControl*>(element)){control->SetDisabled(disabled);return;}
        if(disabled)element->SetAttribute("disabled","disabled");else element->RemoveAttribute("disabled");
        element->SetPseudoClass("disabled",disabled);
        if(disabled)element->SetProperty("tab-index","none");else element->RemoveProperty("tab-index");
    }
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
            if(stage.mode!=(selection.mode=="zones"?"zones":"turf")||stage.onlineOnly||(selection.mode=="boss"&&stage.noBoss)||(selection.mode=="zones"&&!FindZoneStageDefinition(stage.id)))continue;
            if(first.empty())first=std::string(stage.id);
            std::string_view label=StageLabel(stage.id);if(label.empty())label=stage.name;
            options+="<option value=\""+Escape(std::string(stage.id))+"\">"+Escape(std::string(label))+"</option>";
        }
        auto* stages=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"stage"));
        const std::string optionMode=selection.mode=="practice"?"turf":selection.mode;
        if(stageOptionMode!=optionMode){stages->RemoveAll();stages->SetInnerRML(options);stageOptionMode=optionMode;}
        const bool exists=std::any_of(ReferenceStages().begin(),ReferenceStages().end(),[&](const StageData& stage){return stage.id==selection.stage&&stage.mode==(selection.mode=="zones"?"zones":"turf")&&!stage.onlineOnly&&(selection.mode!="boss"||!stage.noBoss)&&(selection.mode!="zones"||FindZoneStageDefinition(stage.id));});
        if(!exists)selection.stage=first;
        if(!selection.stage.empty())Value("stage",selection.stage);
        Text(menu,"stage-message",selection.mode=="boss"?"僅顯示可進行協力挑戰的離線場地。":selection.mode=="zones"?"區域爭奪會載入該場地的區域配置。":selection.mode=="practice"?"練習場地不計分，也不結算經驗值。":"僅顯示目前可遊玩的離線場地。");
    }
    void RebuildDurationOptions(){
        ControlSync sync(syncingControls);
        const bool boss=selection.mode=="boss",zones=selection.mode=="zones",practice=selection.mode=="practice";
        const std::array<int,3> turfValues={90,180,180};const std::array<int,3> bossValues={180,240,300};
        std::string options;
        if(zones)options="<option value=\"300\">5 分鐘（固定）</option>";
        else if(boss){for(const int seconds:bossValues){options+="<option value=\""+std::to_string(seconds)+"\">"+std::to_string(seconds/60)+" 分鐘</option>";}}
        else{for(std::size_t i=0;i<2;++i){const int seconds=turfValues[i];options+="<option value=\""+std::to_string(seconds)+"\">"+(seconds==90?"1.5":"3")+" 分鐘</option>";}}
        auto* durations=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"duration"));
        const std::string optionMode=zones?"zones":boss?"boss":"turf";
        if(durationOptionMode!=optionMode){durations->RemoveAll();durations->SetInnerRML(options);durationOptionMode=optionMode;}
        const bool valid=zones?selection.duration==300:boss?(selection.duration==180||selection.duration==240||selection.duration==300):(selection.duration==90||selection.duration==180);
        if(!valid)selection.duration=zones?300:180;
        Value("duration",std::to_string(selection.duration));Hidden(menu,"duration-row",practice||zones);Hidden(menu,"difficulty-row",practice);
    }
    void PopulateOptions(){
        ControlSync sync(syncingControls);
        RebuildStageOptions();
        auto fillCategory=[&](const char* id,const std::vector<EquipmentData>& rows,EquipmentCategory category){
            std::string items;for(const auto& row:rows){auto label=EquipmentLabel(category,row.id);if(label.empty())label="未翻譯裝備";items+="<option value=\""+Escape(std::string(row.id))+"\">"+Escape(std::string(label))+"</option>";}auto* select=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,id));select->RemoveAll();select->SetInnerRML(items);
        };
        fillCategory("main",MainEquipment(),EquipmentCategory::Main);fillCategory("sub",SubEquipment(),EquipmentCategory::Sub);fillCategory("special",SpecialEquipment(),EquipmentCategory::Special);
        std::string weaponCards;
        for(const auto& row:MainEquipment()){
            const auto label=EquipmentLabel(EquipmentCategory::Main,row.id);
            const std::string name=label.empty()?"未翻譯武器":std::string(label);
            weaponCards+="<button id=\"weapon-card-"+Escape(std::string(row.id))+"\" class=\"weapon-card\" aria-label=\""+Escape(name)+"\" aria-pressed=\"false\"><img src=\"weapons/"+Escape(std::string(row.kind))+".png\" alt=\"\"/><span>"+Escape(name)+"</span></button>";
        }
        Find(menu,"weapon-cards")->SetInnerRML(weaponCards);
        std::string presets="<option value=\"custom\">自訂造型</option>";for(std::size_t i=0;i<CharacterPresets.size();++i){const auto& row=CharacterPresets[i];auto label=StyleLabel("preset",static_cast<int>(i));if(label.empty())label=row.name;presets+="<option value=\""+Escape(row.id)+"\">"+Escape(std::string(label))+"</option>";}
        auto* stylePreset=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"style-preset"));stylePreset->RemoveAll();stylePreset->SetInnerRML(presets);
        FillStyleOptions(menu,"style-hair","hair",HairNames);FillStyleOptions(menu,"style-hat","hat",HatNames);FillStyleOptions(menu,"style-skin","skin",SkinNames);
        FillStyleOptions(menu,"style-eyes","eyes",EyeNames);FillStyleOptions(menu,"style-brows","brows",BrowNames);FillStyleOptions(menu,"style-outfit","outfit",OutfitNames);
    }
    void SyncGraphicsControls(){
        ControlSync sync(syncingControls);const auto& graphics=player.settings.graphics;
        const char* preset=graphics.preset==GraphicsPreset::Low?"low":graphics.preset==GraphicsPreset::High?"high":graphics.preset==GraphicsPreset::Custom?"custom":"medium";
        Value("graphics-preset",preset);
        SetChecked(Find(menu,"graphics-shadows"),graphics.shadows);SetChecked(Find(menu,"graphics-reflections"),graphics.reflections);SetChecked(Find(menu,"graphics-post"),graphics.postEffects);
    }
    void SyncProfileControls(){
        ControlSync sync(syncingControls);const auto& s=player.settings;
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
        Value("touch-controls",s.touchControls);Value("default-mode",s.lastMode);Value("default-difficulty",s.difficulty);Value("time-of-day",s.timeOfDay);
        SetChecked(Find(menu,"invert-y"),s.invertY);SetChecked(Find(menu,"aim-assist-mouse"),s.aimAssistMouse);SetChecked(Find(menu,"touch-invert-y"),s.touchInvertY);
        SetChecked(Find(menu,"graphics-shadows"),s.graphics.shadows);SetChecked(Find(menu,"bloom"),s.bloom);SetChecked(Find(menu,"show-fps"),s.showFps);SetChecked(Find(menu,"fullscreen"),s.fullscreen);
        SetChecked(Find(menu,"colorblind"),s.colorblind);SetChecked(Find(menu,"minimap"),s.minimap);SetChecked(Find(menu,"reduced-motion"),s.reducedMotion);
        SyncGraphicsControls();
        Text(menu,"sensitivity-value",Number(s.sensitivity,2)+"×");Text(menu,"pad-sensitivity-value",Number(s.padSensitivity,2)+"×");
        Text(menu,"aim-assist-value",Integer(s.aimAssist*100)+"%");Text(menu,"touch-sensitivity-value",Number(s.touchSensitivity,2)+"×");Text(menu,"fov-value",Integer(s.fov)+"°");
        Text(menu,"master-value",Integer(s.master*100)+"%");Text(menu,"music-value",Integer(s.music*100)+"%");Text(menu,"sfx-value",Integer(s.sfx*100)+"%");
        Text(menu,"camera-shake-value",Integer(s.cameraShake*100)+"%");Text(menu,"rumble-value",Integer(s.rumble*100)+"%");
        UpdateProfileDisplay();UpdateLoadoutStats();
    }
    void UpdateProfileDisplay(){
        const int need=XpForLevel(player.level);Text(menu,"profile-header",player.name+"　Lv."+std::to_string(player.level));
        Text(menu,"profile-progress","Lv. "+std::to_string(player.level)+"　經驗值（XP） "+std::to_string(player.xp)+" / "+std::to_string(need));
        Text(menu,"profile-stats","勝利  "+std::to_string(player.wins)+"　　對戰  "+std::to_string(player.matches)+"　　總塗地  "+Integer(player.totalTurf));
    }
    void UpdateLoadoutStats(){
        try{
            const auto& main=GetEquipment(EquipmentCategory::Main,player.weapon.empty()?selection.main:player.weapon);
            std::string mainInkCost;
            if(main.kind=="roller")mainInkCost="甩墨 "+Number(main.number("flickInk"),2)+"% · 滾動每公尺 "+Number(main.number("rollInkPerMeter"),2)+"%";
            else if(main.kind=="brush")mainInkCost="每次揮筆 "+Number(main.number("swipeInk"),2)+"%";
            else if(main.kind=="blade")mainInkCost="輕斬 "+Number(main.number("tapInk"),2)+"% · 重斬 "+Number(main.number("heavyInk"),2)+"%";
            else if(main.kind=="mitts")mainInkCost="每拳 "+Number(main.number("inkPerPunch"),2)+"%";
            else if(main.number("inkFull",0)>0)mainInkCost="滿蓄力 "+Number(main.number("inkFull"),2)+"%（依蓄力比例）";
            else mainInkCost="每發 "+Number(main.number("inkPerShot"),2)+"%";
            Text(menu,"main-ink-cost","耗墨："+mainInkCost);
            const auto& mains=MainEquipment();
            const auto selected=std::find_if(mains.begin(),mains.end(),[&](const auto& item){return item.id==main.id;});
            const std::size_t selectedIndex=selected==mains.end()?0:static_cast<std::size_t>(selected-mains.begin());
            const auto label=[](EquipmentCategory category,const EquipmentData& item){const auto translated=EquipmentLabel(category,item.id);return translated.empty()?std::string("未翻譯裝備"):std::string(translated);};
            Text(menu,"weapon-count",std::to_string(selectedIndex+1)+" / "+std::to_string(mains.size()));
            Text(menu,"weapon-name",label(EquipmentCategory::Main,main));
            Text(menu,"weapon-blurb",std::string(EquipmentDescriptionZhTw(EquipmentCategory::Main,main.id)));
            for(const auto& item:mains)if(auto* card=menu->GetElementById("weapon-card-"+std::string(item.id))){const bool equipped=item.id==main.id;card->SetClass("selected",equipped);card->SetAttribute("aria-pressed",equipped?"true":"false");}
            const auto& sub=GetEquipment(EquipmentCategory::Sub,player.sub.empty()?main.sub:player.sub);
            const auto& special=GetEquipment(EquipmentCategory::Special,player.special.empty()?main.special:player.special);
            Find(menu,"sub-icon")->SetAttribute("src","equipment/sub-"+std::string(sub.id)+".png");
            Find(menu,"special-icon")->SetAttribute("src","equipment/special-"+std::string(special.id)+".png");
            Text(menu,"sub-name",label(EquipmentCategory::Sub,sub));Text(menu,"sub-blurb",std::string(EquipmentDescriptionZhTw(EquipmentCategory::Sub,sub.id)));
            const double inkCost=sub.number("inkCost",0.0);Text(menu,"sub-ink-cost",inkCost>0?"消耗 "+Integer(inkCost<=1?inkCost*100:inkCost)+"% 墨水":"");
            Text(menu,"special-name",label(EquipmentCategory::Special,special));Text(menu,"special-blurb",std::string(EquipmentDescriptionZhTw(EquipmentCategory::Special,special.id)));
            const double specialCost=main.number("specialCost",0.0);Text(menu,"special-cost",specialCost>0?Integer(specialCost)+"p":"");
            const std::array<std::pair<const char*,const char*>,5> stats={{{"range","射程"},{"damage","傷害"},{"rate","連射"},{"mobility","機動"},{"paint","塗地"}}};
            for(const auto& [key,label]:stats){const std::string id="stat-"+std::string(key);const double value=std::clamp(main.number(std::string("stats.")+key,.5),0.,1.);Find(menu,id)->SetProperty("width",Integer(value*100)+"%");Text(menu,id+"-value",Integer(value*100));}
        }catch(...){for(const char* id:{"stat-range","stat-damage","stat-rate","stat-mobility","stat-paint"})Find(menu,id)->SetProperty("width","0%");}
    }
    void SelectMainWeapon(const std::string& id){
        player.weapon=id;const auto& row=GetEquipment(EquipmentCategory::Main,id);player.sub=std::string(row.sub);player.special=std::string(row.special);
        selection.main=id;selection.sub=player.sub;selection.special=player.special;Value("main",id);Value("sub",player.sub);Value("special",player.special);Commit();Play("ui_click");
    }
    void CycleLoadoutPart(EquipmentCategory category,int direction){
        const auto& items=category==EquipmentCategory::Sub?SubEquipment():SpecialEquipment();
        const std::string current=category==EquipmentCategory::Sub?player.sub:player.special;
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.id==current;});
        const int count=static_cast<int>(items.size());
        const int index=found==items.end()?(direction>0?-1:0):static_cast<int>(found-items.begin());
        const auto& item=items[static_cast<std::size_t>((index+direction+count)%count)];
        const std::string id(item.id);
        if(category==EquipmentCategory::Sub){player.sub=id;selection.sub=id;Value("sub",id);}else{player.special=id;selection.special=id;Value("special",id);}
        Commit();Play("ui_click");
    }
    void SettingsTab(const std::string& tab){
        if(tab!="controls"&&tab!="video"&&tab!="audio"&&tab!="gameplay")return;
        for(const char* name:{"controls","video","audio","gameplay"}){
            const bool selected=tab==name;Hidden(menu,std::string("settings-")+name,!selected);
            Find(menu,std::string("settings-")+name)->SetAttribute("aria-hidden",selected?"false":"true");
            auto* button=Find(menu,std::string("settings-tab-")+name);button->SetClass("selected",selected);
            button->SetAttribute("aria-selected",selected?"true":"false");button->SetAttribute("tabindex",selected?"0":"-1");
        }
    }
    void GoTo(UiScreen destination){
        screen=destination;
        menu->SetClass("with-preview",destination==UiScreen::Loadout||destination==UiScreen::Locker||practiceGear);
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
        ControlSync sync(syncingControls);
        const bool boss=selection.mode=="boss",zones=selection.mode=="zones",practice=selection.mode=="practice";
        selection.practice=practice;RebuildStageOptions();RebuildDurationOptions();
        Find(menu,"mode-turf")->SetClass("selected",!boss&&!zones&&!practice);Find(menu,"mode-zones")->SetClass("selected",zones);Find(menu,"mode-boss")->SetClass("selected",boss);Find(menu,"mode-practice")->SetClass("selected",practice);
        Text(menu,"mode-description",boss?"一名玩家與七名隊友共同挑戰破艦巨獸。":zones?"與七名電腦玩家進行區域爭奪。倒數 5 分鐘，最後 30 秒鎖定中央區，時間到仍平手則進入延長賽。":practice?"在選定場地自由移動、射擊與練習，不結算勝負或經驗。":"與七名電腦玩家進行 4 對 4 對戰，塗滿更多地面。");
        Text(menu,"start-label",boss?"開始協力挑戰":zones?"開始區域爭奪":practice?"開始自由練習":"開始塗地對戰");
        Text(menu,"start-note",practice?"自由練習 · 不結算資料":zones?"1 位玩家 · 7 名電腦玩家 · 固定 5 分鐘＋延長賽":"1 位玩家 · 7 名電腦玩家");
    }
    void OnlineRequest(UiOnlineRequest::Kind kind){
        if(onlineRequests.size()>=16)return;
        UiOnlineRequest request;request.kind=kind;request.address=Control("online-address")->GetValue();request.code=Control("online-code")->GetValue();
        request.settings=online.settings;
        if(kind==UiOnlineRequest::Kind::Settings){
            request.settings.mode=Control("online-mode")->GetValue();request.settings.map=Control("online-stage")->GetValue();
            const auto stageMode=request.settings.mode=="zones"?"zones":"turf";
            const auto validStage=[&](const StageData& stage){return stage.mode==stageMode&&(request.settings.mode!="boss"||!stage.noBoss);};
            const bool validMap=std::any_of(ReferenceStages().begin(),ReferenceStages().end(),[&](const StageData& stage){return validStage(stage)&&stage.id==request.settings.map;});
            if(!validMap)for(const auto& stage:ReferenceStages())if(validStage(stage)){request.settings.map=std::string(stage.id);break;}
            request.settings.time=Control("online-time")->GetValue();request.settings.difficulty=Control("online-difficulty")->GetValue();
            request.settings.duration=static_cast<std::uint32_t>(std::stoul(Control("online-duration")->GetValue()));
            request.settings.palette=static_cast<std::uint32_t>(std::stoul(Control("online-palette")->GetValue()));
            request.settings.bots=IsChecked(Find(menu,"online-bots"));
        }
        request.team=std::stoi(Control("online-team")->GetValue());
        const auto self=std::find_if(online.members.begin(),online.members.end(),[&](const auto& member){return member.id==online.selfId;});
        request.ready=self==online.members.end()||!self->ready;request.emote="wave";
        onlineRequests.push_back(std::move(request));
    }
    void OnlineState(const network::RoomView& view){
        ControlSync sync(syncingControls);online=view;
        using network::RoomPhase;
        const bool inRoom=view.phase==RoomPhase::Lobby||view.phase==RoomPhase::Starting||view.phase==RoomPhase::Match;
        const bool lobby=view.phase==RoomPhase::Lobby,host=view.selfId&&view.selfId==view.hostId,busy=view.phase==RoomPhase::Connecting;
        Hidden(menu,"online-connect",inRoom);Hidden(menu,"online-room",!inRoom);
        Hidden(menu,"online-error",view.error.empty());Text(menu,"online-error",view.error);
        Text(menu,"online-status",glfwGetTime()<onlineNoticeUntil?"房碼已複製，可以分享給朋友。":view.status.empty()?(inRoom?"選好裝備與隊伍，準備一起出發。":"建立房間，或輸入朋友分享的房碼。"):view.status);
        for(const char* id:{"online-create","online-join","online-local-service"})Disable(id,busy);
        Disable("online-join",busy||Control("online-code")->GetValue().size()<4);Hidden(menu,"online-cancel",!busy);
        Text(menu,"online-room-code",view.code);
        const auto& settings=view.settings;
        if(onlineStagesMode!=settings.mode){
            std::string markup;
            for(const auto& stage:ReferenceStages()){
                if(stage.mode!=(settings.mode=="zones"?"zones":"turf")||(settings.mode=="boss"&&stage.noBoss))continue;
                markup+="<option value=\""+Escape(std::string(stage.id))+"\">"+Escape(std::string(StageLabel(stage.id)))+"</option>";
            }
            auto* stages=dynamic_cast<Rml::ElementFormControlSelect*>(Find(menu,"online-stage"));stages->RemoveAll();stages->SetInnerRML(markup);onlineStagesMode=settings.mode;
        }
        Value("online-mode",settings.mode);Value("online-stage",settings.map);Value("online-time",settings.time);
        Value("online-duration",std::to_string(settings.duration));Value("online-difficulty",settings.difficulty);Value("online-palette",std::to_string(settings.palette));SetChecked(Find(menu,"online-bots"),settings.bots);
        for(const char* id:{"online-mode","online-stage","online-time","online-duration","online-difficulty","online-palette","online-bots"})Control(id)->SetDisabled(!host||!lobby||(std::string(id)=="online-duration"&&settings.mode=="zones"));
        Hidden(menu,"online-settings-toggle",!host);Hidden(menu,"online-room-settings",!host||!onlineRoomSettings);
        Hidden(menu,"online-server-settings",!onlineServerSettings);
        const auto self=std::find_if(view.members.begin(),view.members.end(),[&](const auto& member){return member.id==view.selfId;});
        const bool ready=self!=view.members.end()&&self->ready;
        Value("online-team",self==view.members.end()||self->autoTeam?"-1":std::to_string(self->team));
        Control("online-team")->SetDisabled(!lobby||settings.mode=="boss");Disable("online-ready",!lobby);Disable("online-emote",!lobby);
        Text(menu,"online-ready",ready?"取消準備":"準備");
        const bool allReady=!view.members.empty()&&std::all_of(view.members.begin(),view.members.end(),[&](const auto& member){return member.ready||member.id==view.hostId;});
        const StageData* chosen=nullptr;for(const auto& stage:ReferenceStages())if(stage.id==settings.map&&stage.mode==(settings.mode=="zones"?"zones":"turf")){chosen=&stage;break;}
        const bool bothTeams=std::any_of(view.members.begin(),view.members.end(),[](const auto& member){return member.team==0;})&&std::any_of(view.members.begin(),view.members.end(),[](const auto& member){return member.team==1;});
        const bool humansOnly=chosen&&chosen->noBots;
        const bool enoughPlayers=!humansOnly||(view.members.size()>=2&&bothTeams);
        Disable("online-start",!host||!lobby||!allReady||!enoughPlayers);Hidden(menu,"online-start",!host);
        Text(menu,"online-start-note",!lobby?(view.phase==RoomPhase::Starting?"正在準備進入對戰…":"等待房主返回房間。") :!host?"準備完成後，等待房主開始對戰。":!enoughPlayers?"此場地需至少兩位玩家，且雙方各有一人。":!allReady?"等待所有玩家準備。":"所有玩家已準備，可以開始對戰。");
        const std::string mode=settings.mode=="boss"?"破艦巨獸協力":settings.mode=="zones"?"區域爭奪":"塗地對戰";
        Text(menu,"online-summary",mode+" · "+std::string(StageLabel(settings.map))+" · "+std::to_string(settings.duration)+" 秒 · "+(settings.bots?"電腦玩家補位":"僅真人玩家"));
        std::string markup;
        for(const auto& member:view.members){
            std::string name=member.name+(member.id==view.selfId?"（你）":"")+(member.id==view.hostId?" · 房主":"");
            const std::string state=(settings.mode=="boss"?"協力隊":member.team==0?"橘隊":"藍隊")+std::string(" · ")+(member.ready?"已準備":"未準備");
            const auto kit=EquipmentLabel(EquipmentCategory::Main,member.main);
            markup+="<div class=\"online-member "+std::string(member.id==view.selfId?"self ":"")+(member.team==0?"team-orange":"team-blue")+"\"><span class=\"online-member-name\">"+Escape(name)+"</span><span class=\"online-member-kit\">"+Escape(std::string(kit))+"</span><span class=\"online-member-state\">"+Escape(member.emote.empty()?state:state+" · 招呼")+"<br/>"+std::to_string(member.ping)+" ms</span></div>";
        }
        if(markup!=onlineRosterMarkup){Find(menu,"online-roster")->SetInnerRML(markup);onlineRosterMarkup=std::move(markup);}
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
        auto* element=target;
        if(event.GetType()=="click")for(auto* ancestor=target;ancestor;ancestor=ancestor->GetParentNode())if(ancestor->GetTagName()=="button"){element=ancestor;break;}
        while(element&&!element->GetId().size())element=element->GetParentNode();if(!element)return;
        const std::string id=element->GetId();const auto type=event.GetType();
        if(type=="keydown"&&id.rfind("settings-tab-",0)==0){
            const int key=event.GetParameter<int>("key_identifier",0);
            if(key==Rml::Input::KI_LEFT||key==Rml::Input::KI_RIGHT){
                const std::array<const char*,4> tabs={"controls","video","audio","gameplay"};
                auto current=std::find_if(tabs.begin(),tabs.end(),[&](const char* tab){return id==std::string("settings-tab-")+tab;});
                if(current!=tabs.end()){const int index=static_cast<int>(current-tabs.begin());const int next=(index+(key==Rml::Input::KI_RIGHT?1:3))%4;const std::string selected=tabs[static_cast<std::size_t>(next)];SettingsTab(selected);Find(menu,"settings-tab-"+selected)->Focus();event.StopPropagation();return;}
            }
        }
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
            if(id.rfind("online-",0)==0){
                if(id=="online-team")OnlineRequest(UiOnlineRequest::Kind::Team);
                else if(id!="online-address"&&id!="online-code")OnlineRequest(UiOnlineRequest::Kind::Settings);
                return;
            }
            if(id=="stage")selection.stage=value;
            else if(id=="theme"){selection.theme=value;player.settings.timeOfDay=value;Commit();}
            else if(id=="difficulty"){selection.difficulty=value;player.settings.difficulty=value;Commit();}
            else if(id=="duration"){
                selection.duration=std::stoi(value);
                if(selection.mode=="boss")player.settings.bossLength=selection.duration;
                else if(selection.mode!="zones")player.settings.matchLength=selection.duration;
                Commit();
            }
            else if(id=="main")SelectMainWeapon(value);
            else if(id=="sub"){player.sub=value;selection.sub=value;Commit();Play("ui_click");}
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
        if(id.rfind("online-",0)==0){
            if(element->HasAttribute("disabled"))return;
            if(id=="online-server-toggle"){onlineServerSettings=!onlineServerSettings;Hidden(menu,"online-server-settings",!onlineServerSettings);}
            else if(id=="online-settings-toggle"){onlineRoomSettings=!onlineRoomSettings;Hidden(menu,"online-room-settings",!onlineRoomSettings);}
            else if(id=="online-copy"){
                glfwGetError(nullptr);glfwSetClipboardString(window,online.code.c_str());
                const bool copied=glfwGetError(nullptr)==GLFW_NO_ERROR;onlineNoticeUntil=glfwGetTime()+2;
                Text(menu,"online-status",copied?"房碼已複製，可以分享給朋友。":"無法存取剪貼簿，請手動記下房碼："+online.code);
            }
            else if(id=="online-create")OnlineRequest(UiOnlineRequest::Kind::Create);
            else if(id=="online-join")OnlineRequest(UiOnlineRequest::Kind::Join);
            else if(id=="online-leave"||id=="online-cancel")OnlineRequest(UiOnlineRequest::Kind::Leave);
            else if(id=="online-ready")OnlineRequest(UiOnlineRequest::Kind::Ready);
            else if(id=="online-start")OnlineRequest(UiOnlineRequest::Kind::Start);
            else if(id=="online-emote")OnlineRequest(UiOnlineRequest::Kind::Emote);
            else if(id=="online-local-service")OnlineRequest(UiOnlineRequest::Kind::LocalService);
            Play("ui_click");return;
        }
        if(id.rfind("jump-target-",0)==0||id.rfind("jump-map-marker-",0)==0){
            const auto dash=id.find_last_of('-');
            if(jumpMapOpen&&dash!=std::string::npos){
                try{
                    const int key=std::stoi(id.substr(dash+1));
                    const auto target=std::find_if(jumpMapTargets.begin(),jumpMapTargets.end(),[&](const auto& item){return item.key==key;});
                    if(target!=jumpMapTargets.end()&&target->available){jumpMapSelection=key;Play("ui_confirm");}
                }catch(const std::exception&){}
            }
            return;
        }
        if(id.rfind("weapon-card-",0)==0)SelectMainWeapon(id.substr(12));
        else if(id=="sub-prev")CycleLoadoutPart(EquipmentCategory::Sub,-1);
        else if(id=="sub-next")CycleLoadoutPart(EquipmentCategory::Sub,1);
        else if(id=="special-prev")CycleLoadoutPart(EquipmentCategory::Special,-1);
        else if(id=="special-next")CycleLoadoutPart(EquipmentCategory::Special,1);
        else if(id.rfind("settings-tab-",0)==0){SettingsTab(id.substr(13));Play("ui_click");}
        else if(id=="title-start"){GoTo(UiScreen::Home);Play("ui_confirm");}
        else if(id.rfind("nav-",0)==0){Tab(id.substr(4));Play("ui_click");}
        else if(id=="home-play"){GoTo(UiScreen::Play);Play("ui_click");}
        else if(id=="online-play"){GoTo(UiScreen::Play);Play("ui_click");}
        else if(id=="menu-back"){
            if(practiceGear){practiceGear=false;menuVisible=false;menu->Hide();hud->Show();hudOverlay=false;action=UiAction::Resume;Play("ui_back");}
            else if(hudOverlay){hudOverlay=false;practiceGear=false;menuVisible=false;menu->Hide();hud->Show();Play("ui_back");}
            else{GoTo(UiScreen::Home);Play("ui_back");}
        }
        else if(id=="mode-turf"||id=="mode-zones"||id=="mode-boss"||id=="mode-practice"){
            selection.mode=id.substr(5);selection.practice=selection.mode=="practice";if(selection.mode!="practice")player.settings.lastMode=selection.mode;Mode();Commit();Play("ui_click");
        }
        else if(id=="start"){
            selection.practice=selection.mode=="practice";action=selection.practice?UiAction::Practice:UiAction::Start;Play("ui_confirm");
        }
        else if(id=="nav-locker"||id=="loadout-locker"){GoTo(UiScreen::Locker);LockerTab("presets");Play("ui_click");}
        else if(id.rfind("locker-tab-",0)==0){LockerTab(id.substr(11));Play("ui_click");}
        else if(id=="resume"){if(practiceGear){practiceGear=false;menuVisible=false;menu->Hide();hud->Show();action=UiAction::Resume;}else action=UiAction::Resume;Play("ui_back");}
        else if(id=="result-skip"){resultSkipped=true;resultDone=true;Play("ui_click");}
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
            for(std::size_t i=0;i<keys.size();++i){
                if(jumpMapOpen&&nativeKeys[i]==GLFW_KEY_TAB)continue;
                const bool held=glfwGetKey(window,nativeKeys[i])==GLFW_PRESS;
                if(held!=keys[i]){
                    if(held&&hud->IsVisible()&&Find(hud,"results")->IsVisible()){
                        if(nativeKeys[i]==GLFW_KEY_ENTER&&!resultDone){resultSkipped=true;resultDone=true;Play("ui_click");keys[i]=held;continue;}
                        if(nativeKeys[i]==GLFW_KEY_LEFT||nativeKeys[i]==GLFW_KEY_RIGHT){
                            auto* target=Find(hud,nativeKeys[i]==GLFW_KEY_LEFT?"results-rematch":"results-menu");
                            if(!target->IsVisible())target=Find(hud,"results-menu");
                            target->Focus();keys[i]=held;continue;
                        }
                    }
                    if(held)context->ProcessKeyDown(uiKeys[i],modifiers);else context->ProcessKeyUp(uiKeys[i],modifiers);
                    keys[i]=held;
                }
            }
        }
        context->Update();renderer.Begin(width,height);
        if((renderHud&&hud->IsVisible())||(menuVisible&&menu->IsVisible()))context->Render();
        renderer.End();return std::exchange(action,UiAction::None);
    }
    UiPreview Preview()const{
        UiPreview result;result.screen=screen;
        Rml::Element* element=nullptr;
        if((menuVisible||hudOverlay||practiceGear)&&menu&&menu->IsVisible()){
            result.screen=practiceGear?UiScreen::PracticeGear:screen;
            if(!practiceGear&&screen!=UiScreen::Loadout&&screen!=UiScreen::Locker){result.type=UiPreviewType::None;return result;}
            element=menu->GetElementById("preview-surface");
            result.type=practiceGear?UiPreviewType::Locker:screen==UiScreen::Loadout?UiPreviewType::Loadout:UiPreviewType::Locker;
        }else if(hud&&hud->IsVisible()&&hud->GetElementById("results")->IsVisible()){
            result.screen=UiScreen::Results;
            if(resultIntro){result.type=UiPreviewType::None;return result;}
            element=hud->GetElementById("results-preview");result.type=UiPreviewType::Podium;
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
        if(!player.settings.minimap&&!jumpMapOpen){Hidden(hud,"minimap",true);return;}Hidden(hud,"minimap",false);
        Find(hud,"minimap")->SetClass("jump-map-open",jumpMapOpen);
        const auto& state=match.local().state;const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
        const double now=glfwGetTime();
        const std::string stageKey=std::string(stage.id)+":"+std::string(stage.mode);
        std::string cache=stageKey+":"+std::to_string(paint.version())+":"+std::to_string(player.settings.palette)+":"+Bool(player.settings.colorblind)+":"+std::to_string(context->GetDimensions().x);
        cache+=jumpMapOpen?":jump-map-open":":jump-map-closed";
        cache+=":"+std::to_string(jumpMapQueuedKey);
        for(const auto& target:jumpMapTargets)cache+=":"+std::to_string(target.key)+","+std::to_string(static_cast<int>(std::round(target.x*4)))+","+std::to_string(static_cast<int>(std::round(target.y*4)))+","+std::to_string(static_cast<int>(std::round(target.z*4)))+","+Bool(target.available)+","+target.label+","+target.weapon;
        if(const auto* zones=match.zones()){cache+=":"+zones->objective().id;for(const int id:zones->activeIndices())cache+=":"+std::to_string(zones->zones()[id].owner);}
        for(const auto& actor:match.actors())cache+=":"+std::to_string(static_cast<int>(std::round(actor->state.pos.x*4)))+","+std::to_string(static_cast<int>(std::round(actor->state.pos.y*4)))+","+std::to_string(static_cast<int>(std::round(actor->state.pos.z*4)))+","+std::to_string(actor->state.alive)+","+std::to_string(actor->state.track>0&&actor->state.trackTeam==match.local().team());
        cache+=":"+std::to_string(deathMarks.size())+","+std::to_string(deathSerial)+","+std::to_string(static_cast<int>(deathClock*10));
        const glm::mat4& mapView=jumpMapOpen||!haveNativeMapView?worldView:nativeMapView;
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            cache+=":"+std::to_string(static_cast<int>(std::round(mapView[column][row]*1000.0f)));
        const auto project=[&](glm::dvec3 point,double& left,double& top){
            const glm::vec4 clip=mapView*glm::vec4(glm::vec3(point),1.0f);
            if(clip.w<=1e-4f)return false;
            const double x=clip.x/clip.w,y=clip.y/clip.w;
            if(std::abs(x)>1.04||std::abs(y)>1.04)return false;
            left=(x*.5+.5)*100.0;top=(.5-y*.5)*100.0;return true;
        };
        if(minimapCache!=cache&&now>=nextMinimapUpdate){
            auto* map=Find(hud,"minimap-map");const double mapWidth=map->GetClientWidth(),mapHeight=map->GetClientHeight();
            std::string markup,targetLegend;
            if(const auto* zones=match.zones()){
                for(const int id:zones->activeIndices()){
                    const auto& zone=zones->zones()[id];const std::string color=zone.owner>=0?std::string(palette[zone.owner]):"#f4cb62";
                    for(const auto& polygon:zone.region.polygons)for(std::size_t i=0;i<polygon.size();++i){
                        const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];double x0=0,y0=0,x1=0,y1=0;
                        if(!project({a.x,.15,a.y},x0,y0)||!project({b.x,.15,b.y},x1,y1))continue;
                        const double dx=(x1-x0)*mapWidth/100.0,dy=(y1-y0)*mapHeight/100.0;
                        markup+="<div class=\"map-zone-edge\" style=\"left:"+Number(x0,3)+"%;top:"+Number(y0,3)+"%;width:"+Number(std::hypot(dx,dy),3)+"px;background-color:"+color+";transform:rotate("+Number(std::atan2(dy,dx)*180/3.141592653589793,3)+"deg);\"></div>";
                    }
                }
            }
            for(std::size_t i=0;i<match.actors().size();++i){
                const auto& actor=match.actors()[i];if(!match.roster()[i].active||!actor->state.alive)continue;
                const bool local=actor->isLocal(),ally=actor->team()==match.local().team();
                const bool tracked=!ally&&actor->state.track>0&&actor->state.trackTeam==match.local().team();
                if(!local&&!ally&&!tracked)continue;
                if(jumpMapOpen&&!local)continue;
                const auto position=actor->visualPos();double left=0,top=0;if(!project(position,left,top))continue;
                const std::string color(actor->team()==0?palette[0]:palette[1]);
                std::string contents;
                if(local){
                    double facingX=0,facingY=0,rotation=0;
                    const auto facing=position+glm::dvec3(std::sin(actor->state.yaw),0,std::cos(actor->state.yaw))*2.0;
                    if(mapWidth>0&&mapHeight>0&&project(facing,facingX,facingY)){
                        const double dx=(facingX-left)*mapWidth/100.0,dy=(facingY-top)*mapHeight/100.0;
                        if(dx*dx+dy*dy>1e-8)rotation=std::atan2(dx,-dy)*180.0/3.141592653589793;
                    }
                    contents="<img class=\"map-self-arrow\" src=\"markers/arrow-"+std::to_string(player.settings.colorblind?5:std::clamp(player.settings.palette,0,4))+"-"+std::to_string(actor->team())+".png\" style=\"transform:rotate("+Number(rotation,2)+"deg);\"/>";
                    if(jumpMapOpen)contents+="<span class=\"map-you-label\">自己</span>";
                }
                const std::string actorClass=local?"map-actor-local map-you ":ally?"map-actor-ally ":"map-actor-tracked ";
                markup+="<div class=\"map-actor "+actorClass+"map-actor-"+(actor->team()==0?"a":"b")+"\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;background-color:"+(local&&!jumpMapOpen?"transparent":color)+";\">"+contents+"</div>";
            }
            for(const auto& mark:deathMarks){
                double left=0,top=0;if(!project(mark.pos,left,top))continue;
                const double alpha=std::clamp((mark.born+5.0-deathClock)/1.3,0.0,1.0);
                markup+="<div class=\"map-death-mark\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;opacity:"+Number(alpha)+";\"><img src=\""+DeathMarkIcon(player,mark.team)+"\"/></div>";
            }
            for(const auto& target:jumpMapTargets){
                const std::string stateClass=target.available?"available":"unavailable";
                const std::string disabled=target.available&&jumpMapOpen?"":" disabled=\"disabled\"";
                const std::string icon=!target.weapon.empty()?"<img src=\"weapons/"+Escape(target.weapon)+".png\"/>":target.key==4?"<span class=\"map-base-icon\"><i></i><b></b><em><u></u></em></span>":(target.key==0||target.key>4)?"<img src=\"equipment/sub-beacon.png\"/>":std::string();
                targetLegend+="<button id=\"jump-target-"+std::to_string(target.key)+"\" class=\"jump-target "+stateClass+"\""+disabled+">"+icon+"<strong>"+std::to_string(target.key%10)+"</strong><span>"+Escape(target.label)+"</span><small>"+(target.available?"可用":"暫不可用")+"</small></button>";
                if(!jumpMapOpen)continue;
                double left=0,top=0;if(!project({target.x,target.y,target.z},left,top))continue;
                std::string displayLabel=target.label;
                std::string mapIcon=icon;
                if(target.key==4){displayLabel="BASE";mapIcon="<span class=\"map-base-icon\"><i></i><b></b><em><u></u></em></span>";}
                else if(target.key==0||target.key>4){displayLabel="訊標 "+std::to_string(target.key==0?6:target.key-4);mapIcon="<img src=\"equipment/sub-beacon.png\"/>";}
                else if(displayLabel.rfind("隊友 ",0)==0)displayLabel.erase(0,std::string("隊友 ").size());
                const std::string teamColor(palette[static_cast<std::size_t>(match.local().team())]);
                const std::string style=" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;\"";
                markup+="<div class=\"map-marker\""+style+"><div class=\"map-pin-stem\"></div><div class=\"map-pin-ground\" style=\"background-color:"+teamColor+";\"></div><button id=\"jump-map-marker-"+std::to_string(target.key)+"\" class=\"map-target "+stateClass+(jumpMapQueuedKey==target.key?" queued":"")+"\" style=\"background-color:"+teamColor+";\""+disabled+" title=\""+Escape(target.label)+"\">"+mapIcon+"<strong>"+std::to_string(target.key%10)+"</strong></button><span class=\"map-target-label\">"+Escape(displayLabel)+"</span></div>";
            }
            const auto legendKey=std::string("hud/jump-map-legend");if(textCache[legendKey]!=targetLegend){Find(hud,"jump-map-legend")->SetInnerRML(targetLegend);textCache[legendKey]=targetLegend;}
            const auto key=std::string("hud/minimap-map");if(textCache[key]!=markup){Find(hud,"minimap-map")->SetInnerRML(markup);textCache[key]=markup;}
            minimapCache=cache;nextMinimapUpdate=now+.1;
        }
        Text(hud,"jump-map-help",jumpMapOpen?"點擊地圖上的隊友，或按 1–0 選擇跳躍目標　·　Tab / M 關閉":"");
        Text(hud,"jump-map-queue",jumpMapQueuedKey>=0?std::to_string(jumpMapQueuedKey%10)+"　"+jumpMapQueuedLabel:"");
        const char* ground=state.groundTeam==1?"自隊墨水":state.groundTeam==2?"敵隊墨水":"乾燥地面";
        const auto coverage=paint.coverage();Text(hud,"terrain-status",ground+std::string("　自隊 ")+Percent(coverage[0]));
        Text(hud,"position-team-coverage","敵隊 "+Percent(coverage[1]));
    }
    void UpdateAllyMarkers(MatchSimulation& match,bool hidden){
        Hidden(hud,"ally-markers",hidden||!haveWorldView);
        if(hidden||!haveWorldView)return;
        const auto size=context->GetDimensions();if(size.x<=0||size.y<=0)return;
        const double marginX=std::max(85.,80.*context->GetDensityIndependentPixelRatio()+5.);
        const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
        struct Placement {std::string id;double x,y,angle,opacity;};
        std::vector<Placement> placements;
        std::string markup;
        for(std::size_t i=0;i<match.actors().size();++i){
            const auto& actor=*match.actors()[i];const auto& entry=match.roster()[i];
            const bool tracked=actor.team()!=match.local().team()&&actor.state.track>0&&actor.state.trackTeam==match.local().team();
            if(!entry.active||!actor.state.alive||actor.isLocal()||(actor.team()!=match.local().team()&&!tracked))continue;
            const auto point=actor.visualPos()+glm::dvec3(0,actor.state.squid?1.:1.9,0);
            const auto clip=worldView*glm::vec4(glm::vec3(point),1);
            const double denominator=std::abs(clip.w)>1e-6?clip.w:1e-6;
            double x=clip.x/denominator*.5*size.x,y=-clip.y/denominator*.5*size.y;
            const bool onScreen=clip.w>0&&clip.z/denominator<=1&&std::abs(x)<size.x*.5-20&&std::abs(y)<size.y*.5-20;
            if(!onScreen){if(clip.w<=0){x=-x;y=-y;}const double k=std::min((size.x*.5-marginX)/std::max(1e-3,std::abs(x)),(size.y*.5-65)/std::max(1e-3,std::abs(y)));x*=k;y*=k;}
            const double angle=std::atan2(y,x)*180/3.141592653589793;
            x=std::clamp(x+size.x*.5,marginX,size.x-marginX);y=std::clamp(y+size.y*.5,65.,size.y-45.);
            const bool ready=actor.state.specialCharge>=actor.weapon().number("specialCost",190);
            const double opacity=1-.35*std::clamp((glm::distance(point,worldEye)-14)/20,0.,1.);
            const auto id="ally-marker-"+std::to_string(i);
            placements.push_back({id,x*100/size.x,y*100/size.y,onScreen?90:angle,opacity});
            markup+="<div id=\""+id+"\" class=\"ally-marker"+std::string(onScreen?"":" offscreen")+(ready?" ready":"")+(tracked?" tracked":"")+"\"><div class=\"ally-tag\" style=\"background-color:"+std::string(palette[actor.team()])+";\"><img src=\"weapons/"+std::string(actor.weapon().kind)+".png\"/><span>"+Escape(entry.name)+(ready?" ★":"")+(tracked?" · 已標記":"")+"</span></div><i class=\"ally-direction\"><i class=\"ally-direction-fill\" style=\"border-left-color:"+std::string(palette[actor.team()])+";\"></i></i></div>";
        }
        const auto key="hud/ally-markers";if(textCache[key]!=markup){Find(hud,"ally-markers")->SetInnerRML(markup);textCache[key]=markup;}
        for(const auto& p:placements){auto* tag=Find(hud,p.id);tag->SetProperty("left",Number(p.x)+"%");tag->SetProperty("top",Number(p.y)+"%");tag->SetProperty("opacity",Number(p.opacity));tag->GetLastChild()->SetProperty("transform","rotate("+Number(p.angle)+"deg)");}
    }
    void UpdateDeathMarks(MatchSimulation& match,bool paused,bool hidden){
        (void)paused;
        auto& world=match.world();deathClock=world.time();
        if(deathMatch!=&match||deathEpoch!=world.hudEventEpoch()){
            deathMatch=&match;deathEpoch=world.hudEventEpoch();deathSeenSerial=0;deathMarks.clear();nextMinimapUpdate=0;
        }
        for(const auto& event:world.hudEvents()){
            if(event.serial<=deathSeenSerial)continue;
            deathSeenSerial=event.serial;
            if(event.kind!=CombatHudEvent::Kind::Splat||deathClock-event.time>=5.0||match.phase()==MatchPhase::Results)continue;
            if(event.victimTeam!=match.local().team()&&event.victimTrackedTeam!=match.local().team())continue;
            std::string name;
            for(std::size_t i=0;i<match.actors().size();++i){
                if(match.actors()[i].get()==event.victim){name=match.roster()[i].name;break;}
            }
            if(deathMarks.size()==12)deathMarks.erase(deathMarks.begin());
            deathMarks.push_back({event.victimPos,std::move(name),event.victimTeam,event.time,++deathSerial});nextMinimapUpdate=0;
        }
        if(match.phase()==MatchPhase::Results&&!deathMarks.empty()){deathMarks.clear();nextMinimapUpdate=0;}
        const auto beforeExpiry=deathMarks.size();
        deathMarks.erase(std::remove_if(deathMarks.begin(),deathMarks.end(),[&](const DeathMark& mark){return deathClock-mark.born>=5.0;}),deathMarks.end());
        if(beforeExpiry!=deathMarks.size())nextMinimapUpdate=0;
        Hidden(hud,"death-markers",hidden||!haveWorldView);
        if(hidden||!haveWorldView)return;
        const auto size=context->GetDimensions();if(size.x<=0||size.y<=0)return;
        std::string markup;
        for(const auto& mark:deathMarks){
            const auto clip=worldView*glm::vec4(glm::vec3(mark.pos+glm::dvec3(0,0.95,0)),1);
            if(clip.w<=0||clip.z/clip.w>1)continue;
            const double x=(clip.x/clip.w*.5+.5)*size.x,y=(-clip.y/clip.w*.5+.5)*size.y;
            if(x<0||x>=size.x||y<0||y>=size.y)continue;
            const double alpha=std::clamp((mark.born+5.0-deathClock)/1.3,0.0,1.0);
            const double scale=std::clamp(1.12-glm::distance(mark.pos,worldEye)/60.0,0.56,1.0);
            markup+="<div class=\"death-marker\" style=\"left:"+Number(x*100/size.x)+"%;top:"+Number(y*100/size.y)+"%;opacity:"+Number(alpha)+";transform:scale("+Number(scale)+");\"><img src=\""+DeathMarkIcon(player,mark.team)+"\"/>";
            if(mark.team==match.local().team()&&!mark.name.empty())markup+="<span>"+Escape(mark.name)+"</span>";
            markup+="</div>";
        }
        const auto key="hud/death-markers";if(textCache[key]!=markup){Find(hud,"death-markers")->SetInnerRML(markup);textCache[key]=markup;}
    }
    void UpdateInkGauge(MatchSimulation& match,bool paused,bool results){
        auto& actor=match.local();const auto& state=actor.state;
        auto* gauge=Find(hud,"ink-gauge");
        auto* tank=dynamic_cast<InkTankElement*>(Find(hud,"ink-tank"));
        if(!tank)throw std::runtime_error("Ink tank element was not registered");
        if(inkMatch!=&match||inkActor!=&actor){inkMatch=&match;inkActor=&actor;inkFullIdle=0;tank->Reset();}
        const double now=glfwGetTime();
        const double dt=!paused&&!results&&lastHudTime>0?std::clamp(now-lastHudTime,0.0,.1):0;
        const auto combat=actor.combatPose();const auto special=actor.specialPose();
        const double subCost=special.active&&special.phase=="barrage"?0:actor.subWeapon().number("inkCost",70);
        const bool low=state.ink<18;
        inkFullIdle=state.ink>=99.5&&!low&&!combat.subAim?inkFullIdle+dt:0;
        gauge->SetClass("idle",inkFullIdle>1.4);
        gauge->SetClass("low",low);gauge->SetClass("nosub",state.ink<subCost);
        const auto kind=actor.weapon().kind;
        gauge->SetClass("roller",kind=="roller"||kind=="brush");gauge->SetClass("charger",kind=="charger");
        gauge->SetAttribute("aria-valuenow",Integer(std::clamp(state.ink,0.,100.)));
        Hidden(hud,"ink-gauge",paused||results||!state.alive||jumpMapOpen||hudOverlay||practiceGear);
        Hidden(hud,"ink-low",!low);
        const auto hex=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr)[actor.team()];
        const auto rgb=std::stoul(std::string(hex.substr(1)),nullptr,16);
        InkTankFrame frame;frame.ink=state.ink;frame.subCost=subCost;frame.low=low;frame.aiming=combat.subAim;
        frame.reducedMotion=player.settings.reducedMotion;frame.yaw=actor.input.aimYaw;frame.vx=state.vel.x;frame.vz=state.vel.z;frame.dt=dt;
        frame.color=Rml::Colourb((rgb>>16)&255,(rgb>>8)&255,rgb&255,255);if(match.world().time()<battle.emptyUntil)frame.color=Rml::Colourb(255,70,88,255);tank->Update(frame);
        auto* reticle=dynamic_cast<WeaponReticleElement*>(Find(hud,"reticle"));
        if(!reticle)throw std::runtime_error("Weapon reticle element was not registered");
        WeaponReticleFrame aim;aim.weapon=kind;aim.pose=combat;aim.dt=dt;aim.shotsFired=actor.shotsFired();
        aim.grounded=state.grounded;aim.clinging=state.climbing;aim.burstDuration=actor.streamDuration();aim.inkColor=frame.color;
        const double pixels=std::tan(actor.crosshairSpreadDegrees()*3.141592653589793/180)*projectionY*context->GetDimensions().y*.5;
        aim.spreadPx=std::min(90.,pixels);
        if(kind=="roller"||kind=="brush")aim.spreadPx=kind=="roller"?28:22;
        reticle->SetAttribute("data-weapon",std::string(kind));reticle->Update(aim);
    }
    void UpdateSquads(MatchSimulation& match,bool practice){
        Hidden(hud,"squad-a",practice);Hidden(hud,"squad-b",practice);
        Hidden(hud,"squad-a-count",practice);Hidden(hud,"squad-b-count",practice);
        Find(hud,"battle-top")->SetClass("boss-squad",match.boss()!=nullptr);
        const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
        for(int side=0;side<2;++side){
            const int team=side==0?match.local().team():1-match.local().team();
            std::string markup;int alive=0,total=0;
            for(std::size_t i=0;i<match.actors().size();++i){
                const auto& actor=*match.actors()[i];const auto& entry=match.roster()[i];
                if(!entry.active||actor.team()!=(match.boss()?match.local().team():team))continue;
                ++total;if(actor.state.alive)++alive;
                if(match.boss()&&((total-1)/4)!=side)continue;
                const bool ready=actor.state.alive&&actor.state.specialCharge>=actor.weapon().number("specialCost",190);
                const std::string status=actor.state.alive?(ready?"特殊就緒":"存活"):actor.canRespawn()?"復活 "+Integer(std::ceil(actor.state.respawnTimer))+" 秒":"已退場";
                const auto label=EquipmentLabel(EquipmentCategory::Main,actor.mainId());
                markup+="<div id=\"squad-player-"+std::to_string(i)+"\" class=\"squad-player"+std::string(actor.state.alive?"":" dead")+(ready?" ready":"")+(actor.isLocal()?" self":"")+"\" title=\""+Escape(entry.name+" · "+std::string(label)+" · "+status)+"\" style=\"background-color:"+std::string(actor.state.alive?palette[actor.team()]:"#403c4a")+";\">";
                markup+="<img class=\"squad-weapon\" src=\"weapons/"+std::string(actor.weapon().kind)+".png\" /><i class=\"squad-cross-a\"></i><i class=\"squad-cross-b\"></i>";
                markup+="<hud-ring id=\"squad-ring-"+std::to_string(i)+"\" class=\"squad-ring\"></hud-ring><span class=\"squad-state\">"+(actor.state.alive?(ready?std::string("就緒"):std::string()):actor.canRespawn()?Integer(std::ceil(actor.state.respawnTimer)):std::string("退場"))+"</span><span class=\"squad-self\">你</span></div>";
            }
            const char* id=side==0?"squad-a":"squad-b";
            const auto key=std::string("hud/")+id;
            if(textCache[key]!=markup){Find(hud,id)->SetInnerRML(markup);textCache[key]=markup;}
            Text(hud,side==0?"squad-a-count":"squad-b-count",std::string(match.boss()?"協力小隊":side==0?"我方":"對方")+"  "+std::to_string(alive)+" / "+std::to_string(total));
        }
    }
    void UpdateZonesHud(MatchSimulation& match){
        const auto* zones=match.zones();
        Hidden(hud,"zones-hud",zones==nullptr);
        if(!zones){displayedZones=nullptr;zoneEventCursor=0;Hidden(hud,"zone-callout",true);return;}

        const int localTeam=match.local().team(),enemyTeam=1-localTeam;
        const auto teamName=[&](int team){return team==localTeam?std::string("我方"):team==enemyTeam?std::string("對方"):std::string("無隊伍");};
        const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
        const auto& counts=zones->counts();const auto& penalties=zones->penalty();
        Text(hud,"zone-team-a-name",teamName(localTeam));Text(hud,"zone-team-b-name",teamName(enemyTeam));
        Text(hud,"zone-count-a",ZoneCount(counts[localTeam]));Text(hud,"zone-count-b",ZoneCount(counts[enemyTeam]));
        Text(hud,"zone-penalty-a",ZoneCount(penalties[localTeam]));Text(hud,"zone-penalty-b",ZoneCount(penalties[enemyTeam]));

        const auto& objective=zones->objective();
        const std::string objectiveLabel=objective.id=="center"?"中央區":objective.home==localTeam?"己方側區":"敵方側區";
        Text(hud,"zone-objective",objectiveLabel);
        Text(hud,"zone-owner",zones->owner()<0?"尚未控制":teamName(zones->owner())+"控制中");
        const auto& active=zones->activeIndices();const auto& all=zones->zones();
        std::string markup;
        for(const int index:active){
            if(index<0||static_cast<std::size_t>(index)>=all.size())continue;
            const auto& zone=all[static_cast<std::size_t>(index)];
            const std::string label=zone.kind=="center"?"中央區":zone.home==localTeam?"己方側區":"敵方側區";
            const double ownShare=std::clamp(zone.share[localTeam],0.0,1.0),enemyShare=std::clamp(zone.share[enemyTeam],0.0,1.0);
            const std::string owner=zone.owner<0?"尚未控制":teamName(zone.owner)+"掌控";
            markup+="<div class=\"zone-card\"><div class=\"zone-card-heading\"><strong>"+label+"</strong><span>"+owner+"</span></div>";
            markup+="<div class=\"zone-share-track\"><div class=\"zone-share-a\" style=\"width:"+Number(ownShare*100,2)+"%;background-color:"+std::string(palette[0])+";\"></div><div class=\"zone-share-b\" style=\"width:"+Number(enemyShare*100,2)+"%;background-color:"+std::string(palette[1])+";\"></div><i class=\"zone-control-threshold\"></i></div>";
            markup+="<div class=\"zone-share-labels\"><span>我方 "+ZoneCount(ownShare*100)+"%</span><span>對方 "+ZoneCount(enemyShare*100)+"%</span></div></div>";
        }
        if(markup.empty())markup="<div class=\"zone-card\"><div class=\"zone-card-heading\"><strong>區域資訊</strong><span>等待場地載入</span></div></div>";
        const auto key=std::string("hud/zones-active");if(textCache[key]!=markup){Find(hud,"zones-active")->SetInnerRML(markup);textCache[key]=markup;}

        const double secondsToSwap=std::max(0.0,zones->nextSwap());
        if(zones->overtime()){
            Text(hud,"zone-phase","延長賽");
            Text(hud,"zone-phase-detail","離開有效區 "+ZoneCount(zones->neutralTime())+" / 10 秒");
        }else if(zones->final()){
            Text(hud,"zone-phase","最後 30 秒");Text(hud,"zone-phase-detail","僅中央區計分");
        }else if(secondsToSwap<=5.0){
            Text(hud,"zone-phase","即將輪替");Text(hud,"zone-phase-detail","區域於 "+ZoneCount(secondsToSwap)+" 秒後輪替");
        }else{
            const int swap=static_cast<int>(std::ceil(secondsToSwap));
            Text(hud,"zone-phase","區域輪替");Text(hud,"zone-phase-detail","下次輪替 "+std::to_string(swap/60)+":"+(swap%60<10?"0":"")+std::to_string(swap%60));
        }

        if(displayedZones!=zones||zones->events().size()<zoneEventCursor){displayedZones=zones;zoneEventCursor=zones->events().size();zoneCalloutUntil=0;}
        const auto& events=zones->events();const double now=glfwGetTime();
        for(;zoneEventCursor<events.size();++zoneEventCursor){
            const auto& event=events[zoneEventCursor];std::string callout;
            switch(event.type){
                case ZoneEventType::ZoneOwnerChanged:callout=std::string("區域由")+teamName(event.owner)+"掌控";break;
                case ZoneEventType::ControlChanged:callout=event.owner<0?"區域控制權爭奪中":std::string(teamName(event.owner))+"取得區域控制";break;
                case ZoneEventType::PenaltyAdded:callout=std::string(teamName(event.team))+"罰則 +"+ZoneCount(event.penalty);break;
                case ZoneEventType::ObjectiveChanged:callout=event.final?"最後 30 秒，僅中央區":event.moved?"區域輪替至"+std::string(event.objective=="center"?"中央區":"側區"):std::string();break;
                case ZoneEventType::FinalStarted:callout="最後 30 秒，僅中央區";break;
                case ZoneEventType::OvertimeStarted:callout="進入延長賽";break;
                case ZoneEventType::Contested:callout="區域爭奪中";break;
                case ZoneEventType::MatchEnded:callout=event.reason=="knockout"?"擊倒勝！":ZoneReason(event.reason);break;
            }
            if(!callout.empty()){Text(hud,"zone-callout",callout);zoneCalloutUntil=now+4.0;}
        }
        Hidden(hud,"zone-callout",now>=zoneCalloutUntil);
    }
    std::string ResultMedals(MatchSimulation& match,const MatchResult& result){
        std::vector<std::size_t> players;
        std::size_t self=match.actors().size();
        for(std::size_t i=0;i<match.actors().size();++i)if(match.roster()[i].active){
            players.push_back(i);if(match.actors()[i].get()==&match.local())self=i;
        }
        if(self==match.actors().size())return {};
        const auto& local=*match.actors()[self];
        std::vector<std::string> awards;
        auto give=[&](const char* label){if(awards.size()<4)awards.emplace_back(label);};
        auto turf=[&](std::size_t i){return match.actors()[i]->turfPainted();};
        auto splats=[&](std::size_t i){return match.actors()[i]->splats();};
        auto deaths=[&](std::size_t i){return match.actors()[i]->deaths();};
        double maxTurf=0;int maxSplats=0,maxDeaths=0;
        for(const auto i:players){maxTurf=std::max(maxTurf,turf(i));maxSplats=std::max(maxSplats,splats(i));maxDeaths=std::max(maxDeaths,deaths(i));}
        if(const auto* boss=match.boss()){
            auto damage=[&](std::size_t i){return boss->squadDamage()[i];};
            auto weak=[&](std::size_t i){return boss->squadWeakHits()[i];};
            auto brood=[&](std::size_t i){return boss->squadSplats()[i];};
            double maxDamage=0;int maxWeak=0,maxBrood=0;
            for(const auto i:players){maxDamage=std::max(maxDamage,damage(i));maxWeak=std::max(maxWeak,weak(i));maxBrood=std::max(maxBrood,brood(i));}
            auto score=[&](std::size_t i){return damage(i)/std::max(1.0,maxDamage)+.35*weak(i)/std::max(1,maxWeak)+.2*brood(i)/std::max(1,maxBrood)+.15*turf(i)/std::max(1.0,maxTurf)-.25*deaths(i)/std::max(1,maxDeaths);};
            std::size_t mvp=match.actors().size();
            for(const auto i:players)if(damage(i)>0||turf(i)>0)if(mvp==match.actors().size()||score(i)>score(mvp)+1e-9)mvp=i;
            if(mvp==self)give("MVP");
            if(maxDamage>0&&damage(self)==maxDamage)give("重擊手");
            if(maxWeak>=3&&weak(self)==maxWeak)give("破殼高手");
            if(maxBrood>=2&&brood(self)==maxBrood)give("小蟹清道夫");
            std::vector<std::size_t> active;for(const auto i:players)if(damage(i)>0||turf(i)>=30)active.push_back(i);
            int zero=0,minDeaths=std::numeric_limits<int>::max(),minCount=0;
            for(const auto i:active){if(deaths(i)==0)++zero;if(deaths(i)<minDeaths){minDeaths=deaths(i);minCount=1;}else if(deaths(i)==minDeaths)++minCount;}
            if(zero>0&&zero<=3&&deaths(self)==0&&std::find(active.begin(),active.end(),self)!=active.end())give("不沉之身");
            else if(zero==0&&minCount==1&&deaths(self)==minDeaths&&std::find(active.begin(),active.end(),self)!=active.end())give("生存者");
            if(maxTurf>0&&turf(self)==maxTurf)give("清理隊員");
        }else{
            auto score=[&](std::size_t i){return turf(i)/std::max(1.0,maxTurf)+.55*splats(i)/std::max(1,maxSplats)-.3*deaths(i)/std::max(1,maxDeaths);};
            std::size_t mvp=match.actors().size();
            for(const auto i:players)if(match.roster()[i].team==result.winner&&(turf(i)>0||splats(i)>0))
                if(mvp==match.actors().size()||score(i)>score(mvp)+1e-9||(std::abs(score(i)-score(mvp))<1e-9&&turf(i)>turf(mvp)))mvp=i;
            if(mvp==self)give("MVP");
            const bool king=maxTurf>0&&turf(self)==maxTurf;
            if(king)give("塗地之王");
            if(maxSplats>0&&splats(self)==maxSplats)give("擊倒之星");
            bool teamKing=false;double teamTurf=0;
            for(const auto i:players)if(match.roster()[i].team==local.team()){teamKing|=maxTurf>0&&turf(i)==maxTurf;teamTurf=std::max(teamTurf,turf(i));}
            if(!teamKing&&teamTurf>0&&turf(self)==teamTurf)give("隊伍塗地高手");
            std::vector<std::size_t> active;for(const auto i:players)if(turf(i)>=30||splats(i)>0)active.push_back(i);
            int zero=0,minDeaths=std::numeric_limits<int>::max(),minCount=0;
            for(const auto i:active){if(deaths(i)==0)++zero;if(deaths(i)<minDeaths){minDeaths=deaths(i);minCount=1;}else if(deaths(i)==minDeaths)++minCount;}
            if(zero>0&&zero<=3&&deaths(self)==0&&std::find(active.begin(),active.end(),self)!=active.end())give("毫髮無傷");
            else if(zero==0&&minCount==1&&deaths(self)==minDeaths&&std::find(active.begin(),active.end(),self)!=active.end())give("生存者");
            std::stable_sort(players.begin(),players.end(),[&](std::size_t a,std::size_t b){return turf(a)>turf(b);});
            if(splats(self)==0&&turf(self)>0&&std::find(players.begin(),players.begin()+std::min<std::size_t>(3,players.size()),self)!=players.begin()+std::min<std::size_t>(3,players.size()))give("純粹塗地者");
        }
        std::string markup;for(const auto& award:awards)markup+="<span class=\"result-medal\">★ "+award+"</span>";
        return markup;
    }
    #include "SourceResults.inc"
    #include "BattleHud.inc"
    void RenderHudData(const StageData& stage,const FacePaint& paint,MatchSimulation& match,bool paused,bool nativeApp,bool practice){
        const bool results=match.phase()==MatchPhase::Results;
        const bool onlineMatch=online.phase==network::RoomPhase::Match||online.phase==network::RoomPhase::Starting;
        if(!results){resultXpMatch=nullptr;resultIntro=false;resultSkipped=false;resultDone=false;resultFocused=false;}
        hud->SetClass("results-active",results);
        activePractice=practice;
        interactive=paused||results||practiceGear||hudOverlay;
        interactive=interactive||jumpMapOpen;
        Hidden(hud,"pause",!paused||results);Hidden(hud,"results",!results);Hidden(hud,"return-menu",!nativeApp);Hidden(hud,"results-menu",!nativeApp);
        Hidden(hud,"practice-tools",!practice);Hidden(hud,"reticle",paused||results||!match.local().state.alive||match.local().state.squid||jumpMapOpen||hudOverlay||practiceGear);
        Text(hud,"pause-title",onlineMatch?"對戰選單":practice?"練習暫停":"已暫停");Text(hud,"pause-description",onlineMatch?"線上對戰會持續進行。關閉選單即可繼續操作。":practice?"練習不會計時或結算。調整角色、補充資源，或更換場地。":"準備好後，繼續這場對戰。");
        Text(hud,"pause-hint",onlineMatch?"Esc 選單":"Esc 暫停");Text(hud,"results-menu",onlineMatch?"返回房間":"返回選單");Hidden(hud,"results-rematch",onlineMatch);
        const auto stageLabel=StageLabel(stage.id);Text(hud,"stage-name",stageLabel.empty()?std::string(stage.name):std::string(stageLabel));
        const int seconds=static_cast<int>(std::ceil(std::max(0.,match.remaining())));char clock[16];std::snprintf(clock,sizeof(clock),"%02d:%02d",seconds/60,seconds%60);Text(hud,"clock",clock);
        Find(hud,"clock")->SetClass("last-minute",seconds<=60&&!practice);Find(hud,"clock")->SetClass("final-seconds",seconds<=10&&!practice);
        UpdateSquads(match,practice);
        const auto coverage=paint.coverage();Text(hud,"score-a",Percent(coverage[0]));Text(hud,"score-b",Percent(coverage[1]));
        const auto& local=match.local();const auto& state=local.state;const double cost=local.weapon().number("specialCost",190);
        Text(hud,"hp",Integer(state.hp));Text(hud,"ink",Integer(state.ink));Text(hud,"special-status",state.specialCharge>=cost?"特殊技能就緒  F / Q":Integer(state.specialCharge)+" / "+Integer(cost));
        Find(hud,"special-fill")->SetProperty("width",Integer(std::clamp(state.specialCharge/cost,0.,1.)*100)+"%");Find(hud,"special-meter")->SetClass("ready",state.specialCharge>=cost);
        UpdateInkGauge(match,paused,results);
        UpdateBattleHud(match,paused,results,practice);
        UpdateAllyMarkers(match,paused||results||jumpMapOpen||hudOverlay||practiceGear);
        UpdateDeathMarks(match,paused,paused||results||jumpMapOpen||hudOverlay||practiceGear);
        Text(hud,"player-status",state.alive?(local.specialPose().mapTarget?"瞄準目標後按左鍵確認":""):"等待復活  "+Integer(std::ceil(state.respawnTimer))+" 秒");
        Text(hud,"jump-map-help",state.alive?"按 1–3 選隊友　4 回生點　5–0 選隊伍訊標":"復活規劃：按數字選擇隊友或隊伍訊標，4 清除規劃");
        Text(hud,"jump-map-queue",jumpMapQueuedKey>=0?"復活彈射："+std::to_string(jumpMapQueuedKey%10)+"　"+jumpMapQueuedLabel:"尚未安排復活彈射");
        Find(hud,"jump-map-queue")->SetClass("queued",jumpMapQueuedKey>=0);
        Hidden(hud,"boss",!match.boss());Hidden(hud,"team-score",match.boss()!=nullptr||match.zones()!=nullptr);
        if(const auto* boss=match.boss()){
            const auto& state=boss->state();
            const double hp=state.maxHp>0?std::clamp(state.hp/state.maxHp,0.,1.)*100:0;
            Text(hud,"boss-phase","階段 "+std::string(state.phase>=1?"◆":"◇")+(state.phase>=2?"◆":"◇")+(state.phase>=3?"◆":"◇"));
            Find(hud,"boss-fill")->SetProperty("width",Integer(hp)+"%");Text(hud,"boss-hp",Integer(hp)+"%");
            Find(hud,"boss-emblem")->SetAttribute("src",state.phase>=3?"markers/boss-cracked.png":"markers/boss.png");
            Find(hud,"boss")->SetClass("enraged",state.phase>=3);Find(hud,"boss")->SetClass("open",state.stunned);
            Text(hud,"boss-state",state.dead?"已擊破":state.stunned?"弱點開啟！":state.anim==2?"換階段 · 怒吼":state.phase>=3?"外殼破裂 · 攻擊腹部！":"");
            std::string move;if(boss->move()){const char* names[]={"重鉗衝擊","貨櫃彈幕","墨砲掃射","船殼衝撞","小蟹群","狂亂攻勢"};move=names[static_cast<int>(boss->move()->id)];switch(boss->move()->phase(state.clock)){case BossMovePhase::Telegraph:move+="  ·  注意預兆";break;case BossMovePhase::Active:move+="  ·  攻擊中";break;case BossMovePhase::Recovery:move+="  ·  恢復中";break;default:break;}}Text(hud,"boss-move",move);
        }
        UpdateZonesHud(match);
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
    p.selection.mode=p.player.settings.lastMode=="zones"?"zones":p.player.settings.lastMode=="boss"?"boss":"turf";p.selection.practice=false;p.selection.difficulty=p.player.settings.difficulty;
    p.selection.duration=p.selection.mode=="zones"?300:p.selection.mode=="boss"?p.player.settings.bossLength:p.player.settings.matchLength;p.selection.theme=p.player.settings.timeOfDay;
    p.SyncProfileControls();p.Mode();
}
void GameUi::SetProfileSink(std::function<void(const PlayerProfile&)> sink){impl_->profileSink=std::move(sink);}
void GameUi::SetSaveError(std::string error){impl_->SetSaveError(error);}
void GameUi::SetReward(MatchRewardResult reward){impl_->reward=reward;}
void GameUi::SetSoundSink(std::function<void(const std::string&)> value){impl_->sound=std::move(value);}
void GameUi::SetWorldView(const glm::mat4& viewProjection,const glm::dvec3& eye,float projectionY){impl_->projectionY=projectionY;impl_->worldView=viewProjection;impl_->worldEye=eye;impl_->haveWorldView=true;}
void GameUi::SetNativeMap(unsigned int texture,const glm::mat4& viewProjection){auto& p=*impl_;p.renderer.SetNativeMapTexture(static_cast<GLuint>(texture));p.nativeMapView=viewProjection;p.haveNativeMapView=true;}
void GameUi::SetVolumeSink(std::function<void(float,float,float)> value){impl_->volumes=std::move(value);if(impl_->volumes)impl_->volumes(static_cast<float>(impl_->player.settings.master),static_cast<float>(impl_->player.settings.music),static_cast<float>(impl_->player.settings.sfx));}
void GameUi::SetJumpMap(bool open,std::vector<UiJumpMapTarget> targets,int queuedKey,std::string queuedLabel){
    auto& p=*impl_;
    if(p.jumpMapOpen!=open){p.minimapCache.clear();p.nextMinimapUpdate=0;}
    p.jumpMapOpen=open;p.jumpMapTargets=std::move(targets);p.jumpMapQueuedKey=queuedKey;p.jumpMapQueuedLabel=std::move(queuedLabel);
    if(!open)p.jumpMapSelection=-1;
}
int GameUi::TakeJumpMapSelection(){return std::exchange(impl_->jumpMapSelection,-1);}
void GameUi::ShowMenu(const std::string& initialStage){
    auto& p=*impl_;p.resultXpMatch=nullptr;p.resultIntro=false;p.resultSkipped=false;p.resultDone=false;p.resultFocused=false;p.inkMatch=nullptr;p.inkActor=nullptr;p.inkFullIdle=0;p.lastHudTime=0;p.selection.stage=initialStage;p.selection.mode=p.player.settings.lastMode=="zones"?"zones":p.player.settings.lastMode=="boss"?"boss":"turf";p.selection.practice=false;
    p.RebuildStageOptions();p.selection.difficulty=p.player.settings.difficulty;p.selection.theme=p.player.settings.timeOfDay;
    p.selection.duration=p.selection.mode=="zones"?300:p.selection.mode=="boss"?p.player.settings.bossLength:p.player.settings.matchLength;
    p.Value("difficulty",p.selection.difficulty);p.Value("theme",p.selection.theme);p.Value("duration",std::to_string(p.selection.duration));
    p.Tab("home");p.LockerTab("presets");p.menu->Show();p.hud->Hide();p.menuVisible=true;p.practiceGear=false;p.hudOverlay=false;p.interactive=true;p.action=UiAction::None;p.Mode();
}
void GameUi::ShowTitle(const std::string& initialStage){ShowMenu(initialStage);auto& p=*impl_;p.GoTo(UiScreen::Title);p.Find(p.menu,"title-start")->Focus();}
void GameUi::ShowPracticeGear(){impl_->OpenPracticeGear();}
void GameUi::ShowOnline(){auto& p=*impl_;p.menu->Show();p.hud->Hide();p.menuVisible=true;p.practiceGear=false;p.hudOverlay=false;p.GoTo(UiScreen::Online);}
void GameUi::SetOnlineState(const network::RoomView& view){impl_->OnlineState(view);}
std::vector<UiOnlineRequest> GameUi::TakeOnlineRequests(){return std::exchange(impl_->onlineRequests,{});}
UiAction GameUi::RenderMenu(int width,int height){return impl_->Render(width,height,false);}
void GameUi::ShowPracticeStage(const std::string& initialStage){ShowMenu(initialStage);auto& p=*impl_;p.selection.mode="practice";p.selection.practice=true;p.Mode();p.GoTo(UiScreen::Play);}
UiAction GameUi::RenderHud(const StageData& stage,const FacePaint& paint,MatchSimulation& match,int width,int height,bool paused,bool nativeApp,bool practice){
    auto& p=*impl_;
    if(p.practiceGear||p.hudOverlay){p.menu->Show();p.hud->Show();p.menuVisible=true;}
    else{p.menu->Hide();p.hud->Show();p.menuVisible=false;}
    p.context->SetDimensions({width,height});
    p.context->SetDensityIndependentPixelRatio(std::clamp(std::min(width/1440.f,height/900.f),.72f,1.6f));
    p.RenderHudData(stage,paint,match,paused,nativeApp,practice);
    return p.Render(width,height,true);
}
} // namespace restoration
