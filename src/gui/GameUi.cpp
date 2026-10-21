#include "GameUi.hpp"
#include "RmlRenderer.hpp"
#include "ReferenceData.hpp"
#include "ArsenalData.hpp"
#include "MatchSimulation.hpp"
#include "FacePaint.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>

namespace restoration {
namespace {
std::string Escape(const std::string& text) {
    std::string out;
    for(char c:text) switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;default:out+=c;}
    return out;
}
std::string Integer(double n){return std::to_string(static_cast<int>(std::round(n)));}
std::string Percent(double n){char out[32];std::snprintf(out,sizeof(out),"%.1f%%",n*100);return out;}
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
}

struct GameUi::Impl : Rml::EventListener {
    GLFWwindow* window;
    UiSystem system;
    RmlRenderer renderer;
    Rml::Context* context=nullptr;
    Rml::ElementDocument *menu=nullptr,*hud=nullptr;
    UiSetup selection;
    GraphicsSettings graphics;
    UiAction action=UiAction::None;
    std::function<void(const std::string&)> sound;
    std::function<void(float,float,float)> volumes;
    float master=.8f,music=.6f,sfx=.85f;
    bool menuVisible=true,interactive=true;
    bool syncingGraphicsControls=false;
    bool mouse[3]{};
    std::array<bool,8> keys{};
    std::string hovered;
    std::map<std::string,std::string> textCache;
    void* previousUser=nullptr;
    GLFWscrollfun previousScroll=nullptr;

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
            for(auto* doc:{menu,hud})for(const auto* event:{"click","change","mouseover","focus"})doc->AddEventListener(event,this);
            Populate();menu->Show();hud->Hide();
            previousUser=glfwGetWindowUserPointer(window);glfwSetWindowUserPointer(window,this);
            previousScroll=glfwSetScrollCallback(window,[](GLFWwindow* w,double x,double y){
                auto* self=static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if(self->previousScroll)self->previousScroll(w,x,y);
                if(self->interactive)self->context->ProcessMouseWheel(static_cast<float>(-y),0);
            });
        }catch(...){Rml::Shutdown();Rml::SetSystemInterface(nullptr);Rml::SetRenderInterface(nullptr);throw;}
    }
    ~Impl(){
        glfwSetScrollCallback(window,previousScroll);glfwSetWindowUserPointer(window,previousUser);
        Rml::Shutdown();Rml::SetSystemInterface(nullptr);Rml::SetRenderInterface(nullptr);
    }
    Rml::Element* Find(Rml::ElementDocument* doc,const std::string& id){
        auto* e=doc->GetElementById(id);if(!e)throw std::runtime_error("Missing UI element: "+id);return e;
    }
    void Hidden(Rml::ElementDocument* doc,const std::string& id,bool hidden){
        auto* element=Find(doc,id);
        if(hidden)element->SetProperty("display","none");else element->RemoveProperty("display");
    }
    void Text(Rml::ElementDocument* doc,const std::string& id,const std::string& value){
        const auto key=doc->GetId()+"/"+id;
        if(textCache[key]!=value){Find(doc,id)->SetInnerRML(Escape(value));textCache[key]=value;}
    }
    void Value(const std::string& id,const std::string& value){
        auto* control=dynamic_cast<Rml::ElementFormControl*>(Find(menu,id));
        if(!control)throw std::runtime_error("Invalid UI form control: "+id);
        control->SetValue(value);
    }
    void SyncGraphicsControls(){
        syncingGraphicsControls=true;
        const char* preset=graphics.preset==GraphicsPreset::Low?"low":graphics.preset==GraphicsPreset::High?"high":graphics.preset==GraphicsPreset::Custom?"custom":"medium";
        Value("graphics-preset",preset);
        for(const auto& option:std::array<std::pair<const char*,bool>,3>{{{"graphics-shadows",graphics.shadows},{"graphics-reflections",graphics.reflections},{"graphics-post",graphics.postEffects}}}){
            auto* control=Find(menu,option.first);
            if(control->HasAttribute("checked")!=option.second){
                if(option.second)control->SetAttribute("checked","");else control->RemoveAttribute("checked");
            }
        }
        syncingGraphicsControls=false;
    }
    void Populate(){
        std::string options;
        for(const auto& stage:ReferenceStages())if(stage.mode=="turf"&&!stage.onlineOnly)
            options+="<option value=\""+Escape(std::string(stage.id))+"\">"+Escape(std::string(stage.name))+"</option>";
        Find(menu,"stage")->SetInnerRML(options);
        const auto equipment=[&](const char* id,const auto& rows){
            std::string items;
            for(const auto& row:rows)items+="<option value=\""+Escape(std::string(row.id))+"\">"+Escape(std::string(row.name))+"</option>";
            Find(menu,id)->SetInnerRML(items);
        };
        equipment("main",MainEquipment());equipment("sub",SubEquipment());equipment("special",SpecialEquipment());
        const auto& weapon=GetEquipment(EquipmentCategory::Main,selection.main);
        selection.sub=std::string(weapon.sub);selection.special=std::string(weapon.special);
        Value("stage",selection.stage);Value("main",selection.main);Value("sub",selection.sub);Value("special",selection.special);
        Value("difficulty",selection.difficulty);Value("duration",std::to_string(selection.duration));
        Mode();
    }
    void Mode(){
        const bool boss=selection.mode=="boss";
        Find(menu,"mode-turf")->SetClass("selected",!boss);Find(menu,"mode-boss")->SetClass("selected",boss);
        Hidden(menu,"duration-row",!boss);
        Text(menu,"mode-description",boss?"一名玩家與七名隊友共同挑戰 HULLBREAKER。":"與七名 Bot 進行 4 對 4 對戰，塗滿更多地面。每場 3 分鐘。");
        Text(menu,"start-label",boss?"開始協力挑戰":"開始塗地對戰");
    }
    void Tab(const std::string& name){
        for(const char* tab:{"play","loadout","settings","controls"}){
            const bool selected=name==tab;Hidden(menu,std::string("panel-")+tab,!selected);
            Find(menu,std::string("nav-")+tab)->SetClass("selected",selected);
        }
    }
    void Play(const std::string& cue){if(sound)sound(cue);}
    void ProcessEvent(Rml::Event& event)override{
        auto* target=event.GetTargetElement();
        if(!target)return;
        auto* element=target;
        while(element&&element->GetId().empty())element=element->GetParentNode();
        if(!element)return;
        const std::string id=element->GetId();
        if(event.GetType()=="mouseover"||event.GetType()=="focus"){
            if((element->GetTagName()=="button"||element->GetTagName()=="select")&&hovered!=id){hovered=id;Play("ui_hover");}
            return;
        }
        if(event.GetType()=="change"){
            if(syncingGraphicsControls)return;
            auto* control=dynamic_cast<Rml::ElementFormControl*>(target);if(!control)return;
            const auto value=control->GetValue();
            if(id=="stage")selection.stage=value;
            else if(id=="difficulty")selection.difficulty=value;
            else if(id=="main"){
                selection.main=value;const auto& row=GetEquipment(EquipmentCategory::Main,value);
                selection.sub=std::string(row.sub);selection.special=std::string(row.special);
                Value("sub",selection.sub);Value("special",selection.special);
            }else if(id=="sub")selection.sub=value;
            else if(id=="special")selection.special=value;
            else if(id=="duration")selection.duration=std::stoi(value);
            else if(id=="reduced-motion"){selection.reducedMotion=target->HasAttribute("checked");Play("ui_toggle");}
            else if(id=="graphics-preset"){
                if(value=="low")graphics=ForPreset(GraphicsPreset::Low);
                else if(value=="high")graphics=ForPreset(GraphicsPreset::High);
                else if(value=="medium")graphics=ForPreset(GraphicsPreset::Medium);
                else graphics.preset=GraphicsPreset::Custom;
                SyncGraphicsControls();Play("ui_click");
            }else if(id=="graphics-shadows"||id=="graphics-reflections"||id=="graphics-post"){
                const bool enabled=control->IsSubmitted();
                if(id=="graphics-shadows"){
                    graphics.shadows=enabled;
                    if(enabled&&!graphics.shadowMapSize)graphics.shadowMapSize=1024;
                }else if(id=="graphics-reflections")graphics.reflections=enabled;
                else graphics.postEffects=enabled;
                graphics.preset=GraphicsPreset::Custom;Value("graphics-preset","custom");Play("ui_toggle");
            }else if(id=="master"||id=="music"||id=="sfx"){
                const float gain=std::clamp(std::stof(value)/100.f,0.f,1.f);
                if(id=="master")master=gain;else if(id=="music")music=gain;else sfx=gain;
                Text(menu,id+"-value",Integer(gain*100)+"%");if(volumes)volumes(master,music,sfx);
                Play("ui_slider");
            }
            return;
        }
        if(event.GetType()!="click"||element->GetTagName()!="button")return;
        if(id.rfind("nav-",0)==0){Tab(id.substr(4));Play("ui_click");}
        else if(id=="mode-turf"||id=="mode-boss"){selection.mode=id.substr(5);Mode();Play("ui_click");}
        else if(id=="start"){action=UiAction::Start;Play("ui_confirm");}
        else if(id=="resume"){action=UiAction::Resume;Play("ui_back");}
        else if(id=="return-menu"||id=="results-menu"){action=UiAction::StageSelect;Play("ui_back");}
        else if(id=="close"||id=="pause-close"||id=="results-close"){action=UiAction::Close;Play("ui_back");}
    }
    UiAction Render(int width,int height){
        int ww=0,wh=0;glfwGetWindowSize(window,&ww,&wh);
        context->SetDimensions({width,height});
        const float scale=std::clamp(std::min(width/1440.f,height/900.f),.72f,1.6f);
        context->SetDensityIndependentPixelRatio(scale);
        if(interactive){
            double x=0,y=0;glfwGetCursorPos(window,&x,&y);
            int modifiers=0;if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS)modifiers|=Rml::Input::KM_SHIFT;
            context->ProcessMouseMove(static_cast<int>(x*width/std::max(1,ww)),static_cast<int>(y*height/std::max(1,wh)),modifiers);
            for(int b=0;b<3;++b){const bool held=glfwGetMouseButton(window,b)==GLFW_PRESS;if(held!=mouse[b]){if(held)context->ProcessMouseButtonDown(b,modifiers);else context->ProcessMouseButtonUp(b,modifiers);mouse[b]=held;}}
            const int nativeKeys[]={GLFW_KEY_TAB,GLFW_KEY_ENTER,GLFW_KEY_SPACE,GLFW_KEY_LEFT,GLFW_KEY_RIGHT,GLFW_KEY_UP,GLFW_KEY_DOWN,GLFW_KEY_ESCAPE};
            const Rml::Input::KeyIdentifier uiKeys[]={Rml::Input::KI_TAB,Rml::Input::KI_RETURN,Rml::Input::KI_SPACE,Rml::Input::KI_LEFT,Rml::Input::KI_RIGHT,Rml::Input::KI_UP,Rml::Input::KI_DOWN,Rml::Input::KI_ESCAPE};
            for(std::size_t i=0;i<keys.size();++i){const bool held=glfwGetKey(window,nativeKeys[i])==GLFW_PRESS;if(held!=keys[i]){if(held)context->ProcessKeyDown(uiKeys[i],modifiers);else context->ProcessKeyUp(uiKeys[i],modifiers);keys[i]=held;}}
        }
        context->Update();renderer.Begin(width,height);context->Render();renderer.End();
        return std::exchange(action,UiAction::None);
    }
};

GameUi::GameUi(GLFWwindow* window,const std::filesystem::path& assets):impl_(std::make_unique<Impl>(window,assets)){}
GameUi::~GameUi()=default;
const UiSetup& GameUi::setup()const{return impl_->selection;}
const GraphicsSettings& GameUi::graphicsSettings()const{return impl_->graphics;}
void GameUi::SetSoundSink(std::function<void(const std::string&)> value){impl_->sound=std::move(value);}
void GameUi::SetVolumeSink(std::function<void(float,float,float)> value){impl_->volumes=std::move(value);if(impl_->volumes)impl_->volumes(impl_->master,impl_->music,impl_->sfx);}
void GameUi::ShowMenu(const std::string& initialStage){
    auto& p=*impl_;p.selection.stage=initialStage;p.Value("stage",initialStage);p.Tab("play");
    p.menu->Show();p.hud->Hide();p.menuVisible=true;p.interactive=true;p.action=UiAction::None;
}
UiAction GameUi::RenderMenu(int width,int height){return impl_->Render(width,height);}
UiAction GameUi::RenderHud(const StageData& stage,const FacePaint& paint,MatchSimulation& match,int width,int height,bool paused,bool nativeApp){
    auto& p=*impl_;
    if(p.menuVisible){p.menu->Hide();p.hud->Show();p.menuVisible=false;}
    const bool results=match.phase()==MatchPhase::Results;
    p.interactive=paused||results;
    p.Hidden(p.hud,"pause",!paused||results);
    p.Hidden(p.hud,"results",!results);
    p.Hidden(p.hud,"return-menu",!nativeApp);
    p.Hidden(p.hud,"results-menu",!nativeApp);
    p.Hidden(p.hud,"reticle",paused||results);
    p.Text(p.hud,"stage-name",std::string(stage.name));
    const int seconds=static_cast<int>(std::ceil(std::max(0.,match.remaining())));
    char clock[16];std::snprintf(clock,sizeof(clock),"%02d:%02d",seconds/60,seconds%60);p.Text(p.hud,"clock",clock);
    const auto coverage=paint.coverage();p.Text(p.hud,"score-a",Percent(coverage[0]));p.Text(p.hud,"score-b",Percent(coverage[1]));
    const auto& local=match.local();const auto& player=local.state;
    const double cost=local.weapon().number("specialCost",190);
    p.Text(p.hud,"hp",Integer(player.hp));p.Text(p.hud,"ink",Integer(player.ink));
    p.Text(p.hud,"special-status",player.specialCharge>=cost?"特殊技能就緒  F / Q":Integer(player.specialCharge)+" / "+Integer(cost));
    p.Find(p.hud,"special-fill")->SetProperty("width",Integer(std::clamp(player.specialCharge/cost,0.,1.)*100)+"%");
    p.Find(p.hud,"special-meter")->SetClass("ready",player.specialCharge>=cost);
    p.Text(p.hud,"player-status",player.alive?(local.specialPose().mapTarget?"瞄準目標後按左鍵確認":""):"等待復活  "+Integer(std::ceil(player.respawnTimer))+" 秒");
    p.Hidden(p.hud,"boss",!match.boss());
    p.Hidden(p.hud,"team-score",match.boss()!=nullptr);
    if(const auto* boss=match.boss()){
        const auto& state=boss->state();p.Text(p.hud,"boss-phase","階段 "+std::to_string(state.phase));
        p.Find(p.hud,"boss-fill")->SetProperty("width",Integer(state.maxHp>0?std::clamp(state.hp/state.maxHp,0.,1.)*100:0)+"%");
        p.Text(p.hud,"boss-hp",Integer(state.hp)+" / "+Integer(state.maxHp));
        std::string move;
        if(boss->move()){
            const char* names[]={"重鉗衝擊","貨櫃彈幕","墨砲掃射","船殼衝撞","小蟹群","狂亂攻勢"};
            move=names[static_cast<int>(boss->move()->id)];
            switch(boss->move()->phase(state.clock)){case BossMovePhase::Telegraph:move+="  ·  注意預兆";break;case BossMovePhase::Active:move+="  ·  攻擊中";break;case BossMovePhase::Recovery:move+="  ·  恢復中";break;default:break;}
        }
        p.Text(p.hud,"boss-move",move);
    }
    if(results&&match.result()){
        const auto& result=*match.result();const bool win=result.boss?result.boss->win:result.winner==local.team();
        p.Text(p.hud,"result-title",win?"勝利":"再接再厲");
        p.Text(p.hud,"result-detail",result.boss?(result.boss->win?"HULLBREAKER 已擊破":"協力挑戰結束"):"塗地對戰結束");
        p.Text(p.hud,"result-score",result.boss?("Boss 剩餘 HP  "+Integer(result.boss->hp)):Percent(coverage[0])+"  /  "+Percent(coverage[1]));
        std::string rows="<tr><th>玩家</th><th>塗地</th><th>倒下</th>";
        if(match.boss())rows+="<th>Boss 傷害</th><th>弱點</th><th>小蟹</th>";rows+="</tr>";
        for(std::size_t i=0;i<match.actors().size();++i){
            const auto& actor=*match.actors()[i];rows+="<tr><td>"+Escape(match.roster()[i].name)+"</td><td>"+Integer(actor.turfPainted())+"</td><td>"+std::to_string(actor.deaths())+"</td>";
            if(const auto* boss=match.boss())rows+="<td>"+Integer(boss->squadDamage()[i])+"</td><td>"+std::to_string(boss->squadWeakHits()[i])+"</td><td>"+std::to_string(boss->squadSplats()[i])+"</td>";
            rows+="</tr>";
        }
        const auto key="results-table";if(p.textCache[key]!=rows){p.Find(p.hud,key)->SetInnerRML(rows);p.textCache[key]=rows;}
    }
    return p.Render(width,height);
}
} // namespace restoration
