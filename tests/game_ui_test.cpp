#include "GameUi.hpp"
#include "StageMapView.hpp"
#include "StageRenderer.hpp"
#include "EnvironmentRenderer.hpp"
#include "RenderPipeline.hpp"
#include "PlayerShowcase.hpp"
#include "TeamPalette.hpp"
#include "ArsenalData.hpp"
#include "../src/gui/UiText.hpp"
#include "WeaponReticleElement.hpp"
#include "MatchSimulation.hpp"
#include "../src/network/AuthorityState.hpp"
#include "PlayerController.hpp"
#include "ZoneControl.hpp"
#include "FacePaint.hpp"
#include "ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::vector<unsigned char> Capture(const std::filesystem::path& file,int width,int height){
    std::filesystem::create_directories(file.parent_path());
    std::vector<unsigned char> pixels(width*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    stbi_flip_vertically_on_write(1);
    Check(stbi_write_png(file.string().c_str(),width,height,3,pixels.data(),width*3)!=0,"UI capture failed");
    return pixels;
}
}
int main(int argc,char** argv){
    if(argc!=3||!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_DECORATED,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1920,1080,"Player UI interaction test",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;
    int result=0;
    try{
        int framebufferWidth=0,framebufferHeight=0;glfwGetFramebufferSize(window,&framebufferWidth,&framebufferHeight);
        Check(framebufferWidth>=1920&&framebufferHeight>=1080,"Frameless UI test framebuffer must support 1920x1080 captures (actual="+std::to_string(framebufferWidth)+"x"+std::to_string(framebufferHeight)+")");
        using namespace restoration;
        for(const auto& weapon:MainEquipment())Check(std::filesystem::exists(std::filesystem::path(argv[1])/"assets/ui/weapons"/(std::string(weapon.kind)+".png")),"Every selectable main weapon must have source HUD artwork: "+std::string(weapon.id));
        GameUi ui(window,std::filesystem::path(argv[1])/"assets");
        std::vector<std::string> sounds;float master=0;
        ui.SetSoundSink([&](const auto& name){sounds.push_back(name);});
        ui.SetVolumeSink([&](float a,float,float){master=a;});
        ui.ShowTitle("tidewater");
        auto* context=Rml::GetContext("game-ui");Check(context!=nullptr,"Missing game UI context");
        // Wheel geometry must not depend on wall-clock animation progress.
        context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant,1.f);
        auto* menu=context->GetDocument("menu");Check(menu!=nullptr,"Missing menu document");
        auto element=[&](const char* id){auto* e=menu->GetElementById(id);Check(e!=nullptr,"Missing menu control");return e;};
        auto render=[&]{glViewport(0,0,1600,900);glClearColor(.96f,.97f,.95f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderMenu(1600,900);};
        render();Capture(std::filesystem::path(argv[2])/"menu.png",1600,900);
        Check(element("brand-wordmark")->GetInnerRML().find("Tiny Splatoon")!=Rml::String::npos,"Menu wordmark should use the original game name");
        Check(element("panel-title")->IsVisible()&&!element("panel-home")->IsVisible()&&!element("navigation")->IsVisible(),"Startup must show a focused title screen before the home panel");
        Check(element("title-start")->Focus(),"Title start action must accept keyboard focus");context->ProcessKeyDown(Rml::Input::KI_RETURN,0);context->ProcessKeyUp(Rml::Input::KI_RETURN,0);
        render();Check(element("panel-home")->IsVisible()&&!element("panel-title")->IsVisible(),"Starting from the title screen must enter Home");
        Check(ui.preview().type==UiPreviewType::None,"Home must use the full page without an unnecessary character viewport");
        int profileWrites=0;PlayerProfile loaded;loaded.name="Mint";loaded.weapon="shooter";loaded.xp=120;loaded.style.hair=2;loaded.settings.showFps=true;
        ui.SetProfile(loaded);ui.SetProfileSink([&](const PlayerProfile&){++profileWrites;});
        Check(profileWrites==0&&ui.profile().name=="Mint","Loading a profile must not write it back immediately");
        auto* name=dynamic_cast<Rml::ElementFormControl*>(element("profile-name"));Check(name!=nullptr,"Profile name must be editable");
        name->SetValue("Mint Ice");name->DispatchEvent("change",{});
        Check(ui.profile().name=="Mint Ice"&&profileWrites==1,"Editing the profile name must persist through the profile sink");
        {
            network::RoomView room;ui.SetOnlineState(room);ui.ShowOnline();render();
            Check(element("online-connect")->IsVisible()&&!element("online-room")->IsVisible(),"Offline room UI must offer create and join");
            element("online-create")->Click();auto requests=ui.TakeOnlineRequests();
            Check(requests.size()==1&&requests[0].kind==UiOnlineRequest::Kind::Create&&requests[0].address=="127.0.0.1:27020","Create must preserve the selected room-service address");
            room.phase=network::RoomPhase::Connecting;room.status="正在連線…";ui.SetOnlineState(room);render();
            Check(element("online-create")->HasAttribute("disabled")&&element("online-join")->HasAttribute("disabled"),"Connecting must disable duplicate create/join requests");
            Check(element("online-cancel")->IsVisible(),"Connecting must always offer cancellation");
            element("online-cancel")->Click();requests=ui.TakeOnlineRequests();Check(requests.size()==1&&requests[0].kind==UiOnlineRequest::Kind::Leave,"Cancel must release the pending session");
            room.phase=network::RoomPhase::Error;room.error="找不到房間，請確認房碼。";ui.SetOnlineState(room);render();
            Check(element("online-error")->IsVisible(),"A room error must be visible with a recovery message");
            room.phase=network::RoomPhase::Lobby;room.error.clear();room.code="BCE23";room.selfId=1;room.hostId=1;
            for(unsigned id=1;id<=8;++id){network::Member member;member.id=id;member.name="玩家 "+std::to_string(id);member.team=id%2;member.main="shooter";member.ping=12+id;room.members.push_back(member);}
            ui.SetOnlineState(room);render();
            Check(element("online-roster")->GetNumChildren()==8&&element("online-start")->HasAttribute("disabled"),"An eight-player lobby must show every member and wait for ready");
            for(auto& member:room.members)member.ready=true;
            room.members[0].ready=false;ui.SetOnlineState(room);render();
            Check(!element("online-start")->HasAttribute("disabled"),"Host start must accept its own implicit readiness");
            room.members[0].ready=true;
            ui.SetOnlineState(room);render();
            Check(!element("online-start")->HasAttribute("disabled"),"A fully ready lobby must allow its host to start");
            element("online-ready")->Click();requests=ui.TakeOnlineRequests();
            Check(requests.size()==1&&requests[0].kind==UiOnlineRequest::Kind::Ready&&!requests[0].ready,"Ready must support cancelling the current ready state");
            element("online-copy")->Click();
            if(element("online-status")->GetInnerRML().find("無法存取剪貼簿")!=Rml::String::npos){
                Check(element("online-status")->GetInnerRML().find(room.code)!=Rml::String::npos,"A denied clipboard must show the room code for manual sharing");
                std::cout<<"CLIPBOARD OS denied access; manual sharing fallback verified\n";
            }else{
                const char* copiedCode=glfwGetClipboardString(window);
                Check(copiedCode&&std::string(copiedCode)==room.code,"Copy must share the current five-character code");
            }
            for(const auto size:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
                glViewport(0,0,size[0],size[1]);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderMenu(size[0],size[1]);
                Capture(std::filesystem::path(argv[2])/("online-room-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png"),size[0],size[1]);
                const auto available=element("online-room")->GetClientWidth();
                Check(available>250&&element("online-roster")->GetClientWidth()<=available+1,"Room roster must fit the available menu width");
                Check(element("online-ready")->GetAbsoluteOffset().y+element("online-ready")->GetOffsetHeight()<size[1],"Ready must stay in the viewport with eight members");
            }
            room.hostId=2;ui.SetOnlineState(room);render();
            Check(!element("online-start")->IsVisible()&&!element("online-settings-toggle")->IsVisible(),"Host migration must update room controls without leaving the room");
            room.members[0].name="<button id='spoof'>";ui.SetOnlineState(room);render();
            Check(menu->GetElementById("spoof")==nullptr,"Member names must render as escaped text");
            element("online-leave")->Click();requests=ui.TakeOnlineRequests();
            Check(requests.size()==1&&requests[0].kind==UiOnlineRequest::Kind::Leave,"Leave must reach the session owner");
            ui.SetOnlineState({});ui.ShowMenu("tidewater");render();
        }
        element("nav-locker")->Click();render();
        auto* hair=dynamic_cast<Rml::ElementFormControl*>(element("style-hair"));Check(hair!=nullptr,"Locker must expose source hair choices");
        const int writesBeforeHair=profileWrites;
        hair->SetValue("4");hair->DispatchEvent("change",{});
        Check(ui.profile().style.hair==4&&profileWrites>writesBeforeHair&&ui.preview().type==UiPreviewType::Locker,"Locker changes must persist and expose a character preview");
        element("nav-home")->Click();render();
        Check(!element("save-error")->IsVisible(),"Save errors should start hidden");
        ui.SetSaveError("Disk is read-only");render();Check(element("save-error")->IsVisible(),"Profile write errors must be visible at home");
        element("nav-settings")->Click();render();Check(element("settings-save-error")->IsVisible(),"Profile write errors must be visible in settings");
        element("nav-home")->Click();render();
        ui.SetSaveError("");
        element("nav-settings")->Click();render();
        for(const auto size:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
            auto settingsRender=[&]{glViewport(0,0,size[0],size[1]);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderMenu(size[0],size[1]);};
            for(const auto& tab:std::array<const char*,4>{"controls","video","audio","gameplay"}){
                const std::string button="settings-tab-"+std::string(tab),panel="settings-"+std::string(tab);
                element(button.c_str())->Click();settingsRender();
                Check(element(panel.c_str())->IsVisible(),"Each settings category must be reachable");
                Check(element(button.c_str())->GetAttribute<Rml::String>("aria-selected","")=="true","Settings tabs must expose their selected state");
                auto* scroll=element("settings-scroll");scroll->SetScrollTop(0);settingsRender();
                const auto offset=scroll->GetAbsoluteOffset();
                if(scroll->GetScrollHeight()>scroll->GetClientHeight()+4){
                    context->ProcessMouseMove(int(offset.x+scroll->GetClientWidth()/2),int(offset.y+40),0);
                    context->ProcessMouseWheel(Rml::Vector2f(0,40),0);settingsRender();
                    Check(scroll->GetScrollTop()>0,"Overflowing settings must respond to the mouse wheel");
                    scroll->SetScrollTop(scroll->GetScrollHeight());settingsRender();
                }
                Check(element("menu-back")->GetAbsoluteOffset().y+element("menu-back")->GetOffsetHeight()<=size[1]+1,"Settings return action must stay in the viewport");
                Check(ui.preview().type==UiPreviewType::None,"Settings must use the full page without character preview");
                Capture(std::filesystem::path(argv[2])/("settings-"+std::string(tab)+"-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png"),size[0],size[1]);
                scroll->SetScrollTop(0);settingsRender();Check(scroll->GetScrollTop()<1,"Settings must return to the first option");
            }
        }
        element("nav-play")->Click();render();
        if(!element("duration-row")->IsVisible()||dynamic_cast<Rml::ElementFormControlSelect*>(element("duration"))->GetNumOptions()!=2)
            throw std::runtime_error("Turf duration: visible="+std::to_string(element("duration-row")->IsVisible())+" options="+std::to_string(dynamic_cast<Rml::ElementFormControlSelect*>(element("duration"))->GetNumOptions())+" mode="+ui.setup().mode);
        Check(dynamic_cast<Rml::ElementFormControl*>(element("stage"))->GetValue()=="tidewater","Initial stage selection must remain an internal source ID");
        Check(element("nav-play")->Focus(),"Menu navigation must accept keyboard focus");
        context->ProcessKeyDown(Rml::Input::KI_TAB,0);context->ProcessKeyUp(Rml::Input::KI_TAB,0);
        Check(context->GetFocusElement()!=element("nav-play"),"Tab must move keyboard focus");
        element("mode-boss")->Click();render();
        Check(ui.setup().mode=="boss"&&element("duration-row")->IsVisible()&&dynamic_cast<Rml::ElementFormControlSelect*>(element("duration"))->GetNumOptions()==3,"Boss mode must expose its supported duration choices");
        auto* duration=dynamic_cast<Rml::ElementFormControl*>(element("duration"));Check(duration!=nullptr,"Mode duration selector is missing");
        duration->SetValue("240");duration->DispatchEvent("change",{});
        Check(ui.setup().duration==240&&ui.profile().settings.bossLength==240,"Boss duration should update setup and persist independently");
        element("mode-zones")->Click();render();
        Check(ui.setup().mode=="zones"&&ui.setup().duration==300,"Zone Control should select its own mode with a fixed five-minute match");
        Check(!element("duration-row")->IsVisible()&&dynamic_cast<Rml::ElementFormControlSelect*>(element("duration"))->GetNumOptions()==1,"Zone Control should show the fixed five-minute duration without a duration selector");
        Check(ui.profile().settings.lastMode=="zones"&&dynamic_cast<Rml::ElementFormControl*>(element("default-mode"))->GetValue()=="zones","Zone Control should persist as the default mode");
        auto* zoneStage=dynamic_cast<Rml::ElementFormControl*>(element("stage"));Check(zoneStage!=nullptr,"Zone stage selector is missing");
        zoneStage->SetValue("lockgate");zoneStage->DispatchEvent("change",{});
        Check(ui.setup().mode=="zones"&&ui.setup().stage=="lockgate","Zone Control must keep the selected internal map ID for the stage variant");
        element("mode-turf")->Click();render();Check(ui.setup().mode=="turf"&&ui.profile().settings.lastMode=="turf","Returning to Turf must restore its saved mode preference");
        element("nav-loadout")->Click();render();
        Check(element("weapon-cards")->GetNumChildren()==static_cast<int>(MainEquipment().size()),"Loadout cards must expose every current main weapon");
        for(const auto& weapon:MainEquipment()){
            const auto id="weapon-card-"+std::string(weapon.id);element(id.c_str())->Click();render();
            Check(ui.profile().weapon==weapon.id&&element(id.c_str())->GetAttribute<Rml::String>("aria-pressed","")=="true","Weapon cards must equip and expose selection through the real profile");
        }
        element("weapon-card-shooter")->Click();render();
        const auto subBefore=ui.profile().sub;element("sub-next")->Click();render();Check(ui.profile().sub!=subBefore,"Sub weapon card control must equip the next real sub");
        const auto specialBefore=ui.profile().special;element("special-next")->Click();render();Check(ui.profile().special!=specialBefore,"Special weapon card control must equip the next real special");
        for(const auto size:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
            glViewport(0,0,size[0],size[1]);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderMenu(size[0],size[1]);
            Capture(std::filesystem::path(argv[2])/("loadout-cards-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png"),size[0],size[1]);
            Check(ui.preview().type==UiPreviewType::Loadout,"Loadout must retain the necessary character preview");
        }

        auto* main=dynamic_cast<Rml::ElementFormControl*>(element("main"));Check(main!=nullptr,"Main loadout is not a form control");
        main->SetValue("charger");main->DispatchEvent("change",{});
        Check(ui.setup().main=="charger"&&ui.setup().sub==GetEquipment(EquipmentCategory::Main,"charger").sub,"Main weapon must apply source default kit");
        render();Capture(std::filesystem::path(argv[2])/"loadout.png",1600,900);
        element("nav-settings")->Click();render();
        auto* sensitivity=dynamic_cast<Rml::ElementFormControl*>(element("sensitivity"));Check(sensitivity!=nullptr,"Mouse sensitivity must be editable");
        const int writesBeforeSensitivity=profileWrites;
        sensitivity->SetValue("1.5");sensitivity->DispatchEvent("change",{});
        Check(ui.profile().settings.sensitivity==1.5&&profileWrites>writesBeforeSensitivity,"Changing a supported control setting must update and save the profile");
        auto* palette=dynamic_cast<Rml::ElementFormControl*>(element("palette"));Check(palette!=nullptr,"Team palette selector is missing");
        const int writesBeforePalette=profileWrites;
        palette->SetValue("3");palette->DispatchEvent("change",{});
        Check(ui.profile().settings.palette==3&&profileWrites>writesBeforePalette,"Team palette changes must persist");
        Check(ui.graphicsSettings().preset==GraphicsPreset::Medium&&ui.graphicsSettings().renderScale==.85f,"Graphics settings must start at the balanced preset");
        auto* preset=dynamic_cast<Rml::ElementFormControl*>(element("graphics-preset"));Check(preset!=nullptr,"Missing graphics preset control");
        preset->SetValue("low");preset->DispatchEvent("change",{});
        Check(ui.graphicsSettings().preset==GraphicsPreset::Low&&ui.graphicsSettings().renderScale==.65f&&!ui.graphicsSettings().shadows&&!ui.graphicsSettings().reflections&&!ui.graphicsSettings().postEffects,"Low preset did not apply its performance settings");
        element("graphics-shadows")->Click();
        Check(ui.graphicsSettings().preset==GraphicsPreset::Custom&&ui.graphicsSettings().shadows&&ui.graphicsSettings().shadowMapSize==1024,"Changing a graphics effect must preserve custom settings");
        element("graphics-reflections")->Click();element("graphics-post")->Click();
        Check(ui.graphicsSettings().reflections&&ui.graphicsSettings().postEffects,"Graphics effect toggles did not update custom settings");
        preset->SetValue("high");preset->DispatchEvent("change",{});
        Check(ui.graphicsSettings().preset==GraphicsPreset::High&&ui.graphicsSettings().renderScale==1.f&&ui.graphicsSettings().shadowMapSize==2048&&ui.graphicsSettings().shadows&&ui.graphicsSettings().reflections&&ui.graphicsSettings().postEffects,"High preset did not apply its quality settings");
        auto* volume=dynamic_cast<Rml::ElementFormControl*>(element("master"));Check(volume!=nullptr,"Missing volume control");
        volume->SetValue("25");volume->DispatchEvent("change",{});Check(master==.25f&&ui.profile().settings.master==.25,"Volume control did not reach the audio bus and player profile");
        render();Capture(std::filesystem::path(argv[2])/"settings.png",1600,900);
        element("nav-play")->Click();render();Check(element("start")->Focus(),"Start must accept keyboard focus");
        sounds.clear();
        context->ProcessKeyDown(Rml::Input::KI_RETURN,0);context->ProcessKeyUp(Rml::Input::KI_RETURN,0);
        Check(render()==UiAction::Start,"Enter must activate the selected start action");
        Check(std::count(sounds.begin(),sounds.end(),"ui_confirm")==1,"Start must emit exactly one source confirm cue");
        const StageData* stage=nullptr;
        for(const auto& row:ReferenceStages())if(row.id=="tidewater"&&row.mode=="turf"&&!row.onlineOnly)stage=&row;
        Check(stage!=nullptr,"Missing UI test stage");
        const StageData* zonesStage=nullptr;
        for(const auto& row:ReferenceStages())if(row.id=="tidewater"&&row.mode=="zones"&&!row.onlineOnly)zonesStage=&row;
        Check(zonesStage!=nullptr,"Missing Zone Control stage variant");
        StageGeometry geometry(*stage);FacePaint paint(*stage,geometry);NavGraph nav(*stage,geometry);
        MatchOptions matchOptions;matchOptions.duration=90;
        MatchSimulation match(*stage,geometry,paint,nav,matchOptions);
        auto hudRender=[&](bool paused,bool practice=false){glViewport(0,0,1600,900);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderHud(*stage,paint,match,1600,900,paused,true,practice);};
        hudRender(true);
        auto* hud=context->GetDocument("hud");Check(hud!=nullptr,"Missing match HUD document");
        Check(!menu->IsVisible()&&hud->IsVisible(),"Starting a match must hide the menu and reveal the HUD");
        Check(hud->GetElementById("pause")->IsVisible(),"Pause overlay did not appear");
        Check(hud->GetElementById("minimap")->IsVisible()&&hud->GetElementById("fps")->IsVisible(),"Saved minimap and FPS preferences must apply to the HUD");
        {
            const auto saved=match.local().state;auto* gauge=hud->GetElementById("ink-gauge");
            Check(gauge&&hud->GetElementById("ink-tank"),"HUD must provide the reference's reticle-side ink tube");
            match.local().state.ink=50;
            for(int frame=0;frame<12;++frame){glfwSetTime(glfwGetTime()+.05);hudRender(false);}
            Check(gauge->IsVisible()&&!gauge->IsClassSet("idle")&&gauge->IsClassSet("nosub"),"Half ink must show the tank and mark an unaffordable default bomb");
            Check(gauge->GetAttribute<Rml::String>("aria-valuenow","")=="50","The ink meter must expose actual ink, independent of its smoothed liquid");
            for(const auto size:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
                glViewport(0,0,size[0],size[1]);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                ui.RenderHud(*stage,paint,match,size[0],size[1],false,true);
                const auto position=gauge->GetAbsoluteOffset(Rml::BoxArea::Border);const auto bounds=gauge->GetBox().GetSize(Rml::BoxArea::Border);
                Check(position.x>size[0]*.5&&position.x<size[0]*.6&&std::abs(position.y+bounds.y*.5-size[1]*.5)<2,"The ink tube must sit immediately right of the reticle and stay vertically centered");
                const auto pixels=Capture(std::filesystem::path(argv[2])/("ink-tank-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png"),size[0],size[1]);
                int inkPixels=0;
                for(int y=int(position.y);y<int(position.y+bounds.y);++y)for(int x=int(position.x);x<int(position.x+bounds.x);++x){
                    const auto pixel=(std::size_t(size[1]-1-y)*size[0]+x)*3;
                    const auto high=std::max({pixels[pixel],pixels[pixel+1],pixels[pixel+2]});const auto low=std::min({pixels[pixel],pixels[pixel+1],pixels[pixel+2]});
                    if(high>120&&high-low>60)++inkPixels;
                }
                Check(inkPixels>20,"The native framebuffer must contain colored liquid inside the ink tube");
            }
            match.local().state.ink=10;hudRender(false);
            Check(gauge->IsClassSet("low")&&hud->GetElementById("ink-low")->IsVisible(),"Below 18 ink must show a visible low-ink warning");
            match.local().state.ink=100;
            for(int frame=0;frame<22;++frame){glfwSetTime(glfwGetTime()+.1);hudRender(false);}
            Check(gauge->IsClassSet("idle"),"A full idle ink tube must fade after the source's 1.4-second delay");
            Check(hud->GetElementById("ink-tank")->GetComputedValues().opacity()<.01f,"The custom ink geometry must inherit the completed idle opacity");
            match.local().state.ink=90;hudRender(false);Check(!gauge->IsClassSet("idle"),"Spending ink must reveal the tube immediately");
            hudRender(true);Check(!gauge->IsVisible(),"The reticle ink tube must hide while the pause menu is open");
            match.local().state=saved;
        }
        {
            auto& actor=match.local();const auto saved=actor.state;const auto main=actor.mainId(),sub=actor.subId(),special=actor.specialId();
            actor.state.squid=false;actor.state.alive=true;
            auto* aim=dynamic_cast<WeaponReticleElement*>(hud->GetElementById("reticle"));Check(aim,"A native custom reticle must replace the generic dot");
            std::vector<std::vector<unsigned char>> familyPixels;
            for(const char* kind:{"shooter","blaster","roller","brush","charger","twins","bucket","spinner","bow","mitts","brolly","blade"}){
                actor.setLoadout(kind);hudRender(false);Check(aim->IsVisible()&&aim->GetAttribute<Rml::String>("data-weapon","")==kind,"The active weapon must reach the reticle renderer");
                auto pixels=Capture(std::filesystem::path(argv[2])/(std::string("reticle-")+kind+".png"),1600,900);
                std::vector<unsigned char> crop;int visible=0;
                for(int y=380;y<520;++y)for(int x=680;x<920;++x){const auto at=(std::size_t(y)*1600+x)*3;for(int c=0;c<3;++c)crop.push_back(pixels[at+c]);if(pixels[at]>180&&pixels[at+1]>180&&pixels[at+2]>180)++visible;}
                Check(visible>20,"Every weapon family must draw visible native reticle geometry");
                if(std::string(kind)!="brush"){for(const auto& old:familyPixels)Check(old!=crop,"Distinct source reticle families must not collapse to a generic dot");familyPixels.push_back(std::move(crop));}
            }
            const auto input=actor.input;actor.setLoadout("charger");hudRender(false);
            std::vector<unsigned char> empty(100*100*3),full(empty.size());glReadPixels(750,400,100,100,GL_RGB,GL_UNSIGNED_BYTE,empty.data());
            actor.input.fire=true;for(int frame=0;frame<120;++frame)actor.tick(1./60);hudRender(false);
            glReadPixels(750,400,100,100,GL_RGB,GL_UNSIGNED_BYTE,full.data());
            Check(actor.combatPose().charge>.99&&empty!=full,"Actual weapon charging must update native charge-arc pixels");actor.input=input;
            actor.setLoadout(main,sub,special);actor.state=saved;actor.state.squid=true;hudRender(false);Check(!aim->IsVisible(),"Submerged form must hide the weapon reticle");actor.state=saved;
        }
        {
            std::vector<PlayerState> saved;for(const auto& actor:match.actors())saved.push_back(actor->state);
            const auto anchor=match.local().state.pos;
            const glm::vec3 eye=glm::vec3(anchor)+glm::vec3(0,3,12);
            ui.SetWorldView(glm::perspective(glm::radians(60.f),1600.f/900,.1f,200.f)*glm::lookAt(eye,glm::vec3(anchor)+glm::vec3(0,1,0),glm::vec3(0,1,0)),eye);
            std::size_t ally=0,enemy=0;
            for(std::size_t i=0;i<match.actors().size();++i){auto& actor=*match.actors()[i];actor.state.pos=anchor;actor.state.smoothY=anchor.y;actor.state.alive=true;if(!actor.isLocal())(actor.team()==match.local().team()?ally:enemy)=i;}
            hudRender(false);
            auto* markers=hud->GetElementById("ally-markers");
            auto marker=[&](std::size_t i){return hud->GetElementById("ally-marker-"+std::to_string(i));};
            Check(markers->IsVisible()&&markers->GetNumChildren()==3&&marker(ally),"Every living teammate must have an overhead position tag");
            Check(!marker(enemy),"An untracked enemy must never receive an ally position marker");
            Check(!marker(ally)->IsClassSet("offscreen"),"A visible teammate must use an overhead tag");
            match.actors()[ally]->state.pos.x+=100;hudRender(false);
            Check(marker(ally)->IsClassSet("offscreen"),"A teammate outside the view must have a screen-edge direction marker");
            const auto offset=marker(ally)->GetAbsoluteOffset();
            Check(offset.x>=0&&offset.x+marker(ally)->GetOffsetWidth()<=1601,"An edge marker must fit inside the viewport");
            match.actors()[enemy]->state.track=3;match.actors()[enemy]->state.trackTeam=match.local().team();hudRender(false);
            Check(marker(enemy)&&marker(enemy)->IsClassSet("tracked"),"Only an enemy tracked by the local team may show a marked enemy tag");
            match.actors()[ally]->state.alive=false;hudRender(false);Check(!marker(ally),"A splatted teammate must lose its world tag");
            Capture(std::filesystem::path(argv[2])/"ally-markers-1600x900.png",1600,900);
            hudRender(true);Check(!markers->IsVisible(),"Pause must hide world position markers");
            for(std::size_t i=0;i<saved.size();++i)match.actors()[i]->state=saved[i];hudRender(false);
        }
        {
            auto* own=hud->GetElementById("squad-a");auto* enemy=hud->GetElementById("squad-b");
            Check(own->GetNumChildren()==4&&enemy->GetNumChildren()==4,"Combat roster must display both four-player teams");
            Check(hud->GetElementById("squad-a-count")->GetInnerRML().find("4 / 4")!=Rml::String::npos,"Squad count must report actual living actors");
            auto& actor=match.local();const auto saved=actor.state;
            actor.state.alive=false;actor.state.respawnTimer=2.2;hudRender(false);
            auto* badge=hud->GetElementById("squad-player-0");
            Check(badge&&badge->IsClassSet("self")&&badge->IsClassSet("dead")&&badge->GetInnerRML().find(">3<")!=Rml::String::npos,"A splatted local actor must retain its badge, cross and rounded respawn countdown");
            Check(hud->GetElementById("squad-a-count")->GetInnerRML().find("3 / 4")!=Rml::String::npos,"Death must lower the living count without removing the roster slot");
            Capture(std::filesystem::path(argv[2])/"battle-death-1600x900.png",1600,900);
            actor.state=saved;actor.state.specialCharge=actor.weapon().number("specialCost",190);hudRender(false);
            badge=hud->GetElementById("squad-player-0");
            Check(badge->IsClassSet("ready")&&!badge->IsClassSet("dead"),"Respawn and special-ready must refresh the existing player's HUD state");
            for(const auto size:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
                glViewport(0,0,size[0],size[1]);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                ui.RenderHud(*stage,paint,match,size[0],size[1],false,true);
                const auto clockOffset=hud->GetElementById("clock")->GetAbsoluteOffset();
                const auto left=own->GetAbsoluteOffset();const auto right=enemy->GetAbsoluteOffset();
                Check(left.x+own->GetOffsetWidth()<=clockOffset.x+1&&right.x>=clockOffset.x+hud->GetElementById("clock")->GetOffsetWidth()-1,"Squads must flank the clock without overlap");
                Capture(std::filesystem::path(argv[2])/("battle-roster-"+std::to_string(size[0])+"x"+std::to_string(size[1])+".png"),size[0],size[1]);
            }
            actor.state=saved;hudRender(false,true);
            Check(!own->IsVisible()&&!enemy->IsVisible(),"Practice must hide competitive team slots");hudRender(true);
            FacePaint onlinePaint(*stage,geometry);MatchOptions onlineOptions;onlineOptions.network=true;onlineOptions.duration=180;onlineOptions.localPeer=2;onlineOptions.authorityPeer=1;
            for(unsigned i=0;i<3;++i){network::RosterEntry entry;entry.nid=i;entry.owner=i==1?2:1;entry.bot=i==2;entry.team=i==0?0:1;entry.slot=i==2?1:0;entry.name=i==1?"<self>":"Peer";entry.main="shooter";onlineOptions.networkRoster.push_back(entry);}
            MatchSimulation onlineMatch(*stage,geometry,onlinePaint,nav,onlineOptions);
            ui.RenderHud(*stage,onlinePaint,onlineMatch,1600,900,false,true);
            Check(own->GetNumChildren()==2&&enemy->GetNumChildren()==1&&hud->GetElementById("squad-player-1")->IsClassSet("self"),"A guest must see its actual team on the left even when its actor is not index zero");
            onlineMatch.setNetworkActorActive(2,false);ui.RenderHud(*stage,onlinePaint,onlineMatch,1600,900,false,true);
            Check(own->GetNumChildren()==1&&hud->GetElementById("squad-player-2")==nullptr,"An inactive departed actor must leave both the roster and its living count");
            FacePaint bossPaint(*stage,geometry);MatchOptions bossOptions;bossOptions.mode="boss";
            MatchSimulation bossMatch(*stage,geometry,bossPaint,nav,bossOptions);
            glViewport(0,0,1600,900);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderHud(*stage,bossPaint,bossMatch,1600,900,false,true);
            Check(own->GetNumChildren()==4&&enemy->GetNumChildren()==4&&hud->GetElementById("squad-a-count")->GetInnerRML().find("8 / 8")!=Rml::String::npos,"Boss HUD must split the eight-person squad around the timer and count the full squad");
            Capture(std::filesystem::path(argv[2])/"boss-roster-1600x900.png",1600,900);
            for(const auto& actor:bossMatch.actors())Check(actor->team()==0,"Every Boss squad actor must use the same ink team");
            bossMatch.startPlaying();for(int i=0;i<40;++i)bossMatch.tick(.1);
            auto* boss=bossMatch.boss();
            for(const int phase:{2,3}){
                auto wire=boss->captureAuthorityState();wire.state.phase=phase;wire.state.hp=wire.state.maxHp*(phase==2?.6:.3);wire.state.anim=2;wire.state.animT0=wire.state.clock;
                Check(boss->adoptAuthorityState(wire),"Boss phase presentation fixture must adopt a valid state");
                ui.RenderHud(*stage,bossPaint,bossMatch,1600,900,false,true);
                Check(hud->GetElementById("boss-phase")->GetInnerRML().find(phase==2?"◆◆◇":"◆◆◆")!=Rml::String::npos,"Boss phase pips must follow live simulation");
                Check(hud->GetElementById("boss-state")->GetInnerRML().find("怒吼")!=Rml::String::npos,"Phase transformation must display the live roar state");
                Check(hud->GetElementById("boss-hp")->GetInnerRML()==(phase==2?"60%":"30%"),"Boss HP must show the real percentage");
                if(phase==3)Check(hud->GetElementById("boss-emblem")->GetAttribute<Rml::String>("src","")=="markers/boss-cracked.png","Final phase must display the cracked source emblem");
                Capture(std::filesystem::path(argv[2])/("boss-phase-"+std::to_string(phase)+".png"),1600,900);
            }
            for(bool won:{false,true}){
                if(won){bossMatch.restart();bossMatch.startPlaying();for(int i=0;i<40;++i)bossMatch.tick(.1);BossSegmentHit hit;hit.weak=true;bossMatch.boss()->hit(bossMatch.local(),1000000,hit,"shooter");}
                for(int i=0;i<2600&&bossMatch.phase()!=MatchPhase::Judge;++i)bossMatch.tick(.1);
                Check(bossMatch.phase()==MatchPhase::Judge,"Boss fixture must complete the real victory/timeout flow");bossMatch.finishJudging();
                ui.RenderHud(*stage,bossPaint,bossMatch,1600,900,false,true);
                hud->GetElementById("result-skip")->Click();ui.RenderHud(*stage,bossPaint,bossMatch,1600,900,false,true);
                Check(hud->GetElementById("result-boss")->IsVisible()&&!hud->GetElementById("result-score")->IsVisible(),"Boss results must use a dedicated HP/phase/time panel");
                Check(!hud->GetElementById("result-boss-time")->GetInnerRML().empty()&&hud->GetElementById("result-boss-stamp")->GetInnerRML()==(won?"擊破！":"逃脫"),"Boss result must show the actual clear/timeout result");
                const auto rows=hud->GetElementById("results-table")->GetInnerRML();Check(rows.find("5–8")!=Rml::String::npos&&rows.find("弱點")!=Rml::String::npos&&rows.find("塗地")!=Rml::String::npos,"Boss results require both squad columns, weak hits and turf");
                glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderHud(*stage,bossPaint,bossMatch,1600,900,false,true);
                Capture(std::filesystem::path(argv[2])/(won?"boss-results-win.png":"boss-results-timeout.png"),1600,900);
            }
            hudRender(true);
        }
        {
            FacePaint jumpPaint(*stage,geometry);
            MatchOptions jumpOptions; jumpOptions.duration=90; jumpOptions.seed=73;
            MatchSimulation jumpMatch(*stage,geometry,jumpPaint,nav,jumpOptions);
            jumpMatch.startPlaying();
            PlayerController jumpController;
            const auto& bounds=stage->bounds;
            const auto mapPoint=[&](double x,double z){return glm::dvec3(bounds.x+x*(bounds.y-bounds.x),0.0,bounds.z+z*(bounds.w-bounds.z));};
            auto& jumpLocal=jumpMatch.local();
            jumpLocal.setLoadout("shooter","beacon","slam");
            jumpLocal.spawnAt(mapPoint(.18,.78),0.0);
            jumpLocal.combatWorld().deploy(jumpLocal,"beacon");
            jumpLocal.state.pos=mapPoint(.5,.7);
            const std::array<glm::dvec2,3> teammateMapPoints{{{.18,.2},{.82,.23},{.78,.78}}};
            std::size_t teammatePoint=0;
            for(std::size_t index=1;index<jumpMatch.actors().size()&&teammatePoint<teammateMapPoints.size();++index){
                auto* actor=jumpMatch.actors()[index].get();
                if(actor->team()!=jumpLocal.team())continue;
                const auto& point=teammateMapPoints[teammatePoint++];
                actor->spawnAt(mapPoint(point.x,point.y),0.0);
            }
            Check(teammatePoint==teammateMapPoints.size(),"Map capture fixture needs three allied target positions");
            auto controllerTargets=[&]{return jumpController.SuperJumpMapTargets(jumpMatch);};
            auto uiTargets=[&]{
                std::vector<UiJumpMapTarget> targets;
                for(const auto& target:controllerTargets())targets.push_back({target.key,target.x,target.z,target.label,target.available});
                return targets;
            };
            auto renderJumpMap=[&](int width,int height,const char* filename){
                const auto targets=uiTargets();
                Check(targets.size()>=5&&targets[4].key==5,"Map captures should include the deployed beacon as target 5");
                const auto mapCamera=FitStageMapCamera(stage->bounds,double(width)/height,jumpLocal.team()==1);
                ui.SetWorldView(glm::mat4(mapCamera.projection*mapCamera.view),mapCamera.eye,float(mapCamera.projection[1][1]));
                ui.SetJumpMap(true,targets);glfwSetTime(glfwGetTime()+.2);
                glViewport(0,0,width,height);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                ui.RenderHud(*stage,jumpPaint,jumpMatch,width,height,false,true);
                auto* panel=hud->GetElementById("minimap");
                Check(panel->IsVisible()&&panel->IsClassSet("jump-map-open"),"Opening the Super Jump map must expand and show the minimap panel");
                const auto mapSize=panel->GetBox().GetSize(Rml::BoxArea::Border);
                const auto detailSize=hud->GetElementById("jump-map-help")->GetBox().GetSize(Rml::BoxArea::Border);
                Check(mapSize.x>450.f&&detailSize.y>0.f,"Expanded Super Jump map should expose its map panel and instructions");
                Check(hud->GetElementById("jump-map-legend")->GetInnerRML().find("jump-target-1")!=Rml::String::npos,"Expanded Super Jump map should list teammate targets");
                auto* map=hud->GetElementById("minimap-map");
                const auto mapOffset=map->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto mapBounds=map->GetBox().GetSize(Rml::BoxArea::Border);
                const double spanX=std::max(1e-6,bounds.y-bounds.x),spanZ=std::max(1e-6,bounds.w-bounds.z);
                for(const auto& target:targets){
                    const auto markerId="jump-map-marker-"+std::to_string(target.key);
                    auto* marker=hud->GetElementById(markerId.c_str());
                    Check(marker&&marker->GetInnerRML().find("<strong>"+std::to_string(target.key%10)+"</strong>")!=Rml::String::npos,"Map marker must visibly print its target digit: "+markerId);
                    if(!marker)continue;
                    const auto markerOffset=marker->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto markerSize=marker->GetBox().GetSize(Rml::BoxArea::Border);
                    const auto clip=mapCamera.projection*mapCamera.view*glm::dvec4(target.x,target.y,target.z,1);
                    const double left=(clip.x/clip.w*.5+.5)*100,top=(.5-clip.y/clip.w*.5)*100;
                    auto* anchor=marker->GetParentNode();
                    Check(std::abs(anchor->GetProperty("left")->Get<float>()-left)<.02&&std::abs(anchor->GetProperty("top")->Get<float>()-top)<.02,
                          "Map pin anchor must align with its actual 3D projection: "+markerId);
                    Check(markerOffset.x>=mapOffset.x-1&&markerOffset.y>=mapOffset.y-1&&markerOffset.x+markerSize.x<=width+1&&markerOffset.y+markerSize.y<=height+1,
                          "Projected jump badge must stay inside the map viewport");
                }
                const auto pixels=Capture(std::filesystem::path(argv[2])/filename,width,height);
                const auto pixel=[&](int x,int y){
                    x=std::clamp(x,0,width-1);y=std::clamp(y,0,height-1);
                    const auto index=(static_cast<std::size_t>(height-1-y)*width+static_cast<std::size_t>(x))*3;
                    return std::array<unsigned char,3>{pixels[index],pixels[index+1],pixels[index+2]};
                };
                for(const auto& target:targets){
                    auto* marker=hud->GetElementById(("jump-map-marker-"+std::to_string(target.key)).c_str());
                    Check(marker,"Every fitted target must have a rendered pin");
                    const auto offset=marker->GetAbsoluteOffset();const auto size=marker->GetBox().GetSize(Rml::BoxArea::Border);
                    int visible=0;
                    for(int y=int(offset.y);y<int(offset.y+size.y);++y)for(int x=int(offset.x);x<int(offset.x+size.x);++x){const auto color=pixel(x,y);if(color[0]>100||color[1]>100||color[2]>100)++visible;}
                    Check(visible>20,"Each jump badge must draw real framebuffer pixels");
                }
            };
            auto clickTarget=[&](int key){
                const std::string id="jump-map-marker-"+std::to_string(key);
                auto* target=hud->GetElementById(id.c_str());
                Check(target!=nullptr,"Super Jump target button is missing: "+id);
                target->Click();
                return ui.TakeJumpMapSelection();
            };
            auto setMapTargets=[&]{ui.SetJumpMap(true,uiTargets());glfwSetTime(glfwGetTime()+.2);};
            auto* minimapPanel=hud->GetElementById("minimap");
            renderJumpMap(1280,720,"jump-map-1280x720.png");
            renderJumpMap(1920,1080,"jump-map-1920x1080.png");
            Check(clickTarget(1)==1&&jumpController.SelectSuperJumpMapTarget(jumpMatch,1),"Clicking a live teammate target must route into the controller and start the selected jump");
            Check(jumpMatch.local().superJumpState()&&jumpMatch.local().superJumpState()->actorTarget==controllerTargets()[0].actor,"Teammate click must preserve the selected actor target");
            jumpMatch.local().spawnAt(stage->spawnPads[jumpMatch.local().team()],0.0);
            setMapTargets();
            Check(clickTarget(4)==4&&jumpController.SelectSuperJumpMapTarget(jumpMatch,4),"Clicking the base target must route into the controller and start the base jump");
            Check(jumpMatch.local().superJumpState()&&glm::distance(jumpMatch.local().superJumpState()->target,stage->spawnPads[jumpMatch.local().team()])<.01,"Base click must use the selected team's spawn pad");
            jumpMatch.local().spawnAt(stage->spawnPads[jumpMatch.local().team()],0.0);

            auto* ally=controllerTargets()[0].actor;
            Check(ally!=nullptr,"Super Jump map fixture needs a teammate target");
            ally->state.invuln=0;
            Check(ally->damage(200)&&!ally->state.alive,"Disabled-target fixture should splat the selected teammate");
            setMapTargets();
            ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(!controllerTargets()[0].available&&hud->GetElementById("jump-map-legend")->GetInnerRML().find("jump-target-1")!=Rml::String::npos&&hud->GetElementById("jump-map-legend")->GetInnerRML().find("unavailable")!=Rml::String::npos&&hud->GetElementById("jump-target-1")->HasAttribute("disabled"),"Dead teammates must remain listed with a visibly disabled target state");
            const auto confirmsBeforeDeadClick=std::count(sounds.begin(),sounds.end(),"ui_confirm");
            Check(clickTarget(1)==-1&&!jumpController.SelectSuperJumpMapTarget(jumpMatch,1)&&!jumpMatch.local().superJumpState()&&std::count(sounds.begin(),sounds.end(),"ui_confirm")==confirmsBeforeDeadClick,"Clicking an unavailable teammate must not start a jump or emit a confirm cue");
            ally->spawnAt(stage->spawnPads[ally->team()]+glm::dvec3(2,0,0),0.0);

            jumpMatch.local().setCanRespawn(false);
            setMapTargets();ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(!controllerTargets()[3].available&&hud->GetElementById("jump-target-4")->HasAttribute("disabled"),"Respawn gating must disable the base target");
            Check(clickTarget(4)==-1&&!jumpController.SelectSuperJumpMapTarget(jumpMatch,4)&&!jumpMatch.local().superJumpState(),"Clicking the gated base target must not start a jump");
            jumpMatch.local().setCanRespawn(true);

            jumpMatch.local().setLoadout("shooter","beacon","slam");
            jumpMatch.local().combatWorld().deploy(jumpMatch.local(),"beacon");
            setMapTargets();ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            const auto targetsWithBeacon=controllerTargets();
            const auto beaconTarget=std::find_if(targetsWithBeacon.begin(),targetsWithBeacon.end(),[](const auto& target){return target.kind==SuperJumpMapTargetKind::Beacon;});
            Check(beaconTarget!=targetsWithBeacon.end()&&beaconTarget->key==5,"The oldest live team beacon should be listed at key 5");
            jumpMatch.local().setCanRespawn(false);
            setMapTargets();ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(!controllerTargets()[4].available&&hud->GetElementById("jump-target-5")->HasAttribute("disabled"),"Respawn gating must disable the beacon target");
            Check(clickTarget(5)==-1&&!jumpController.SelectSuperJumpMapTarget(jumpMatch,5),"Clicking a gated beacon must not start a jump");
            jumpMatch.local().setCanRespawn(true);
            setMapTargets();ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(clickTarget(5)==5&&jumpController.SelectSuperJumpMapTarget(jumpMatch,5),"Clicking the beacon target must route into the controller and start the beacon jump");
            Check(jumpMatch.local().superJumpState()&&jumpMatch.local().superJumpState()->beaconId==beaconTarget->beaconId,"Beacon click must preserve the selected device id");

            jumpMatch.local().spawnAt(stage->spawnPads[jumpMatch.local().team()],0.0);
            setMapTargets();
            jumpMatch.local().state.invuln=0;
            Check(jumpMatch.local().damage(200)&&!jumpMatch.local().state.alive,"Dead-map fixture should splat the local player before choosing a queued target");
            setMapTargets();ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(controllerTargets()[0].available&&!hud->GetElementById("jump-target-1")->HasAttribute("disabled"),"A living teammate remains available as a queued target while the local player is dead");
            Check(clickTarget(1)==1&&jumpController.SelectSuperJumpMapTarget(jumpMatch,1)&&jumpController.HasQueuedSuperJump()&&jumpController.QueuedSuperJumpKey()==1,"Clicking an available teammate while dead should queue that teammate's respawn jump");
            ui.SetJumpMap(true,uiTargets(),1,"隊友");glfwSetTime(glfwGetTime()+.2);ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(hud->GetElementById("jump-map-marker-1")->IsClassSet("queued"),"The queued landing pin must show its selection ring on the map");
            jumpController.ClearQueuedSuperJump();
            jumpMatch.local().spawnAt(stage->spawnPads[jumpMatch.local().team()],0.0);
            setMapTargets();
            hud->GetElementById("jump-target-1")->Click();
            ui.SetJumpMap(false,{});
            glfwSetTime(glfwGetTime()+.2);ui.RenderHud(*stage,jumpPaint,jumpMatch,1600,900,false,true);
            Check(!minimapPanel->IsClassSet("jump-map-open")&&ui.TakeJumpMapSelection()==-1,"Closing the map must collapse it and discard any pending target click");
        }
        StageGeometry zonesGeometry(*zonesStage);FacePaint zonesPaint(*zonesStage,zonesGeometry);NavGraph zonesNav(*zonesStage,zonesGeometry);
        MatchOptions zonesOptions;zonesOptions.mode="zones";zonesOptions.duration=300;zonesOptions.seed=41;
        MatchSimulation zonesMatch(*zonesStage,zonesGeometry,zonesPaint,zonesNav,zonesOptions);
        Check(zonesMatch.zones()!=nullptr,"Zone Control match should expose its live objective state");
        auto zonesHudRender=[&]{glViewport(0,0,1600,900);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderHud(*zonesStage,zonesPaint,zonesMatch,1600,900,true,true);};
        zonesHudRender();
        Check(hud->GetElementById("zones-hud")->IsVisible()&&!hud->GetElementById("team-score")->IsVisible(),"Zone Control should replace the turf coverage panel with its own score HUD");
        Check(hud->GetElementById("zone-count-a")->GetInnerRML().find("100")!=Rml::String::npos&&hud->GetElementById("zone-penalty-a")->GetInnerRML().find("0")!=Rml::String::npos,"Zone Control must display score count and separate penalty values");
        Check(hud->GetElementById("zone-objective")->GetInnerRML().find("區")!=Rml::String::npos&&hud->GetElementById("zones-active")->GetInnerRML().find("zone-share-labels")!=Rml::String::npos,"Zone HUD must locate the active center/side area and show both teams’ control share");
        auto captureZonesHud=[&](int width,int height,const char* filename){
            glViewport(0,0,width,height);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            ui.RenderHud(*zonesStage,zonesPaint,zonesMatch,width,height,false,true);
            auto* panel=hud->GetElementById("zones-hud");
            Check(panel->GetBox().GetSize(Rml::BoxArea::Border).y<height*.25f,"Zone HUD should stay compact at 720p and 1080p");
            auto* phaseDetail=hud->GetElementById("zone-phase-detail");
            const float phaseHeight=phaseDetail->GetBox().GetSize(Rml::BoxArea::Border).y;
            const float dpRatio=std::clamp(std::min(width/1440.f,height/900.f),.72f,1.6f);
            Check(phaseHeight>=14.f,"Zone phase detail should be at least 14 screen px high (box="+std::to_string(phaseHeight)+"px, font="+std::to_string(phaseDetail->GetComputedValues().font_size())+"px, DPR="+std::to_string(dpRatio)+")");
            auto checkReadableFont=[&](Rml::Element* element,const char* label){
                const float fontSize=element->GetComputedValues().font_size();
                Check(fontSize>=14.f,std::string(label)+" should be at least 14 screen px at 720p and 1080p (font="+std::to_string(fontSize)+"px, DPR="+std::to_string(dpRatio)+")");
            };
            auto bounds=[](Rml::Element* element){
                const auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto size=element->GetBox().GetSize(Rml::BoxArea::Border);
                return std::array<float,4>{offset.x,offset.y,offset.x+size.x,offset.y+size.y};
            };
            auto checkVerticalOrder=[&](Rml::Element* above,Rml::Element* below,const char* label){
                const auto a=bounds(above),b=bounds(below);
                Check(a[3]<=b[1]+1.f,std::string(label)+" elements must not overlap vertically");
            };
            const auto phone=bounds(hud->GetElementById("minimap"));
            Check(phone[0]>=0&&phone[0]<width*.2f&&phone[3]<=height+1,"The compact phone map must stay at bottom left inside the viewport");
            Check(!hud->GetElementById("terrain-status")->IsVisible()&&!hud->GetElementById("position-team-coverage")->IsVisible(),"The phone map must not retain the old terrain/footer panel");
            checkReadableFont(hud->GetElementById("zone-owner"),"Zone owner metadata");
            checkReadableFont(hud->GetElementById("zone-team-a-name"),"Zone team label");
            auto* firstZoneCard=hud->GetElementById("zones-active")->GetFirstChild();
            Check(firstZoneCard!=nullptr,"Zone HUD should have at least one active area card");
            auto* shareLabels=firstZoneCard->GetLastChild();
            Check(shareLabels!=nullptr,"Zone HUD area card should include share percentages");
            checkReadableFont(shareLabels,"Zone share percentages");
            Capture(std::filesystem::path(argv[2])/filename,width,height);
        };
        captureZonesHud(1280,720,"zones-1280x720.png");captureZonesHud(1920,1080,"zones-1920x1080.png");
        Check(hud->GetElementById("minimap-map")->GetInnerRML().find("map-zone-edge")!=Rml::String::npos,"The minimap must locate the active zone outlines");
        auto* liveZones=const_cast<ZoneControl*>(zonesMatch.zones());
        Check(!liveZones->timeUp()&&liveZones->overtime(),"A tied Zone Control round should transition into overtime");
        zonesHudRender();
        Check(hud->GetElementById("zone-phase")->GetInnerRML().find("延長賽")!=Rml::String::npos&&hud->GetElementById("zone-callout")->IsVisible(),"Overtime must update the live phase and show a callout");
        hudRender(true);
        auto* minimap=dynamic_cast<Rml::Element*>(hud->GetElementById("minimap-map"));Check(minimap!=nullptr,"Missing minimap element");
        const auto mapBefore=minimap->GetInnerRML();Check(hud->GetElementById("minimap-render")->IsVisible()&&mapBefore.find("map-actor-local")!=Rml::String::npos,"Minimap must render spatial terrain and the local actor marker");
        match.local().state.pos.x+=2.0;glfwSetTime(glfwGetTime()+.12);hudRender(true);
        const auto mapAfter=minimap->GetInnerRML();Check(mapAfter.find("map-actor-local")!=Rml::String::npos&&mapAfter!=mapBefore,"Moving the player must move its visible minimap marker");
        Capture(std::filesystem::path(argv[2])/"pause.png",1600,900);
        hud->GetElementById("return-menu")->Click();Check(hudRender(true)==UiAction::None&&ui.preview().screen==UiScreen::LeaveConfirm,"Leaving an unfinished match must ask for confirmation");
        context->ProcessKeyDown(Rml::Input::KI_ESCAPE,0);context->ProcessKeyUp(Rml::Input::KI_ESCAPE,0);
        Check(hudRender(true)==UiAction::None&&!hud->GetElementById("leave-confirm")->IsVisible(),"Escape must cancel leaving without resuming the match");
        hud->GetElementById("resume")->Click();Check(hudRender(false)==UiAction::Resume,"Resume did not reach the match loop");
        Check(!hud->GetElementById("pause")->IsVisible(),"Resume did not hide the pause overlay");
        ui.ShowPracticeGear();hudRender(true,true);
        Check(ui.preview().type==UiPreviewType::Locker&&ui.preview().screen==UiScreen::PracticeGear&&menu->IsVisible()&&hud->IsVisible(),"Practice gear should expose its screen and open over the HUD");
        context->ProcessKeyDown(Rml::Input::KI_ESCAPE,0);context->ProcessKeyUp(Rml::Input::KI_ESCAPE,0);
        Check(hudRender(true,true)==UiAction::Resume&&!menu->IsVisible(),"Escape must close practice gear and resume the practice session");
        hud->GetElementById("practice-reset")->Click();Check(hudRender(true,true)==UiAction::PracticeReset,"Practice reset action did not reach the match loop");
        hud->GetElementById("practice-fill")->Click();Check(hudRender(true,true)==UiAction::PracticeFill,"Practice refill action did not reach the match loop");
        hud->GetElementById("practice-spawn")->Click();Check(hudRender(true,true)==UiAction::PracticeSpawn,"Practice spawn action did not reach the match loop");
        hud->GetElementById("practice-stage")->Click();Check(hudRender(true,true)==UiAction::PracticeStage,"Practice stage selector action did not reach the match loop");
        ui.ShowPracticeStage("halyard");render();Check(ui.setup().mode=="practice"&&ui.setup().stage=="halyard"&&element("panel-play")->IsVisible(),"Practice map selection must preserve the practice context");
        ui.ShowMenu("tidewater");render();element("nav-play")->Click();render();element("mode-practice")->Click();render();
        Check(!element("duration-row")->IsVisible()&&!element("difficulty-row")->IsVisible(),"Practice should hide match-only duration and Bot difficulty controls");
        Check(ui.setup().practice&&ui.setup().mode=="practice","Practice mode should be represented in match setup");
        element("start")->Click();Check(render()==UiAction::Practice,"Starting the practice mode must produce a practice action");
        match.startPlaying();
        for(int i=0;i<1850&&match.phase()!=MatchPhase::Judge;++i)match.tick(.1);
        Check(match.phase()==MatchPhase::Judge,"UI lifecycle test did not reach judging");match.finishJudging();
        MatchRewardResult reward;reward.applied=true;reward.gained=640;reward.levelBefore=1;reward.levelAfter=2;reward.xpBefore=100;reward.xpAfter=40;ui.SetReward(reward);
        sounds.clear();
        hudRender(false);Check(hud->GetElementById("results")->IsVisible(),"Results overlay did not appear");
        Check(hud->GetElementById("results-table")->GetNumChildren()==2,"Results must show two squad columns");
        Check(hud->GetElementById("results")->IsClassSet("results-intro")&&ui.preview().type==UiPreviewType::None,"Results must begin with the source map reveal before the podium");
        Capture(std::filesystem::path(argv[2])/"results-intro.png",1600,900);
        Check(hud->GetElementById("result-medals")->GetNumChildren()==0,"An inactive local player must not receive an unearned medal");
        auto* xpLabel=hud->GetElementById("result-xp-label");
        Check(xpLabel->GetInnerRML().find("+0")!=Rml::String::npos,"Results should begin the XP reveal at zero");
        const double xpMidTime=glfwGetTime()+2.3+3.6+.2;glfwSetTime(xpMidTime);hudRender(false);
        Check(xpLabel->GetInnerRML().find("+640")==Rml::String::npos&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"XP should count up with one tick while the award is in flight");
        const auto shownXp=[&]{const auto text=xpLabel->GetInnerRML();return std::stoi(text.substr(text.find('+')+1));};
        const int xpMidReveal=shownXp();const double firstRenderDrift=glfwGetTime()-xpMidTime;
        ui.SetReward(reward);glfwSetTime(xpMidTime);hudRender(false);
        const double clockDrift=std::max(firstRenderDrift,glfwGetTime()-xpMidTime);
        Check(std::abs(shownXp()-xpMidReveal)<=std::ceil(clockDrift*reward.gained*3/1.7)+1&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"Repeated reward assignments must not restart the XP reveal or duplicate a tick");
        glfwSetTime(glfwGetTime()+2.0);hudRender(false);
        Check(xpLabel->GetInnerRML().find("+640")!=Rml::String::npos,"Results must finish by displaying the saved XP reward");
        match.local().applyNetworkStats(1500,0,0,7,0);hudRender(false);
        Check(hud->GetElementById("result-medals")->GetInnerRML().find("塗地之王")!=Rml::String::npos&&hud->GetElementById("result-medals")->GetInnerRML().find("擊倒之星")!=Rml::String::npos,"Earned Turf King and Top Splatter medals must appear from final stats");
        Check(hud->GetElementById("results-table")->GetInnerRML().find("BOT")!=Rml::String::npos&&hud->GetElementById("results-table")->GetInnerRML().find("result-weapon")!=Rml::String::npos,"Results must identify live bots and equipped weapon artwork");
        Capture(std::filesystem::path(argv[2])/"results.png",1600,900);
        auto captureResults=[&](int width,int height,const char* filename){
            glViewport(0,0,width,height);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            ui.RenderHud(*stage,paint,match,width,height,false,true);
            Capture(std::filesystem::path(argv[2])/filename,width,height);
        };
        captureResults(1280,720,"results-1280x720.png");captureResults(1920,1080,"results-1920x1080.png");
        match.restart();
        glViewport(0,0,1600,900);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        ui.RenderHud(*stage,paint,match,1600,900,false,true); // Intro frame clears the prior result identity.
        match.startPlaying();
        for(int i=0;i<1000&&match.phase()!=MatchPhase::Judge;++i)match.tick(.1);
        Check(match.phase()==MatchPhase::Judge,"Rematch UI lifecycle did not reach judging");match.finishJudging();
        sounds.clear();ui.SetReward(reward);
        glViewport(0,0,1600,900);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        ui.RenderHud(*stage,paint,match,1600,900,false,true);
        Check(xpLabel->GetInnerRML().find("+0")!=Rml::String::npos,"The same-match rematch should start a fresh XP reveal for an identical award");
        glfwSetTime(glfwGetTime()+2.3+3.6+.2);ui.RenderHud(*stage,paint,match,1600,900,false,true);
        Check(xpLabel->GetInnerRML().find("+640")==Rml::String::npos&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"The same-match rematch's identical award should animate and tick once");
        hud->GetElementById("result-skip")->Click();hudRender(false);
        Check(ui.preview().type==UiPreviewType::Podium,"Skipping the reveal must expose the complete podium without exiting the match");
        hud->GetElementById("results-rematch")->Click();Check(hudRender(false)==UiAction::Rematch,"Results rematch action did not reach the match loop");
        hud->GetElementById("results-menu")->Click();Check(hudRender(false)==UiAction::StageSelect,"Return action did not reach the match loop");
        ui.ShowMenu("tidewater");Check(render()==UiAction::None&&menu->IsVisible(),"Returning from results did not restore the menu");
        {
            for(const auto& equipment:SubEquipment())Check(std::filesystem::exists(std::filesystem::path(argv[1])/"assets/ui/equipment"/("sub-"+std::string(equipment.id)+".png")),"Every sub must have source HUD artwork");
            for(const auto& equipment:SpecialEquipment())Check(std::filesystem::exists(std::filesystem::path(argv[1])/"assets/ui/equipment"/("special-"+std::string(equipment.id)+".png")),"Every special must have source HUD artwork");
            FacePaint combatPaint(*stage,geometry);MatchOptions options;options.duration=90;
            MatchSimulation combatMatch(*stage,geometry,combatPaint,nav,options);combatMatch.startPlaying();
            auto draw=[&](int w=1600,int h=900,bool paused=false){glViewport(0,0,w,h);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderHud(*stage,combatPaint,combatMatch,w,h,paused,true);};
            auto& local=combatMatch.local();local.state.invuln=0;std::vector<PlayerSimulation*> foes;PlayerSimulation* ally=nullptr;
            for(const auto& actor:combatMatch.actors()){actor->state.invuln=0;if(actor->team()!=local.team())foes.push_back(actor.get());else if(actor.get()!=&local)ally=actor.get();}
            Check(foes.size()==4&&ally,"Combat HUD fixture needs two complete teams");draw();
            Check(hud->GetElementById("special-display")->IsVisible(),"Combat HUD must show the special orb");
            local.state.ink=0;local.input.sub=true;local.tick(1./60);local.input.sub=false;local.tick(1./60);draw();Check(hud->GetElementById("equipment-denied")->IsVisible(),"An unaffordable sub must display denial feedback");local.input.sub=false;local.state.ink=100;
            local.state.specialCharge=local.weapon().number("specialCost",190);draw();
            Check(hud->GetElementById("special-percent")->GetInnerRML().find("就緒")!=Rml::String::npos,"A full special must show its ready state");
            auto before=Capture(std::filesystem::path(argv[2])/"battle-hud-ready.png",1600,900);
            for(int y=300;y<600;++y)for(int x=1458;x<1470;++x){const auto i=(y*1600+x)*3;Check(std::abs(int(before[i])-64)<3&&std::abs(int(before[i+1])-76)<3&&std::abs(int(before[i+2])-89)<3,"Special liquid mesh must remain inside its orb, without full-height edge artifacts");}

            combatMatch.world().applyHit(local,*foes[0],36,"shooter");draw();
            Check(hud->GetElementById("battle-feedback")->GetAttribute<Rml::String>("data-hit","")=="true","Applied damage must trigger a hit marker");
            auto hit=Capture(std::filesystem::path(argv[2])/"battle-hud-hit.png",1600,900);int changed=0;
            for(int y=380;y<520;++y)for(int x=730;x<870;++x){const auto i=(y*1600+x)*3;if(before[i]!=hit[i]||before[i+1]!=hit[i+1]||before[i+2]!=hit[i+2])++changed;}
            Check(changed>20,"Hit feedback must change real crosshair-area pixels");
            combatMatch.world().applyHit(local,*foes[0],100,"shooter");draw();
            Check(hud->GetElementById("kill-cards")->GetNumChildren()==1&&hud->GetElementById("battle-callout")->GetInnerRML().find("首次")!=Rml::String::npos,"A first splat must show its victim card and first-splat callout");
            Check(hud->GetElementById("minimap-map")->GetInnerRML().find("map-death-mark")==Rml::String::npos,"An untracked enemy death must not reveal a map marker");
            const auto cards=hud->GetElementById("kill-cards")->GetInnerRML();draw();Check(cards==hud->GetElementById("kill-cards")->GetInnerRML(),"Rendering the same journal must not replay or duplicate kill cards");
            combatMatch.world().applyHit(local,*foes[1],15,"shooter");combatMatch.world().applyHit(*ally,*foes[1],100,"shooter");draw();
            Check(hud->GetElementById("kill-cards")->GetInnerRML().find("助攻")!=Rml::String::npos,"Recent damage followed by an allied splat must show an assist card");
            combatMatch.world().applyHit(local,*foes[2],100,"shooter");draw();
            Check(hud->GetElementById("battle-callout")->GetInnerRML().find("雙重")!=Rml::String::npos,"Two local splats within 4.2 seconds must show the source multi-splat callout");
            combatMatch.world().applyHit(local,*foes[3],100,"shooter");draw();
            Check(hud->GetElementById("battle-callout")->GetInnerRML().find("全隊")!=Rml::String::npos,"Wipeout must take priority over multi-splat callouts");
            Capture(std::filesystem::path(argv[2])/"battle-hud-wipeout.png",1600,900);
            foes[0]->spawnAt(foes[0]->state.pos,0);foes[0]->state.invuln=0;combatMatch.world().applyHit(*foes[0],local,200,"bomb");draw();
            Check(hud->GetElementById("death-card")->IsVisible()&&hud->GetElementById("death-cause")->GetInnerRML().find(std::string(EquipmentLabel(EquipmentCategory::Sub,"bomb")))!=Rml::String::npos,"Death feedback must preserve the actual subweapon cause");
            Capture(std::filesystem::path(argv[2])/"battle-hud-death.png",1600,900);
            draw(1600,900,true);Check(!hud->GetElementById("battle-hud-layer")->IsVisible(),"Paused HUD must hide transient combat overlays");
            combatMatch.restart();combatMatch.startPlaying();draw();
            Check(hud->GetElementById("kill-cards")->GetNumChildren()==0&&!hud->GetElementById("death-card")->IsVisible(),"Restart must clear old cards, deaths and journal cursors");
            Check(hud->GetElementById("minimap-map")->GetInnerRML().find("map-death-mark")==Rml::String::npos,"Restart must clear old death positions");
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){draw(dims.first,dims.second);auto* orb=hud->GetElementById("special-display");const auto pos=orb->GetAbsoluteOffset();Check(pos.x>=0&&pos.x+orb->GetOffsetWidth()<=dims.first&&pos.y+orb->GetOffsetHeight()<=dims.second,"Special orb must fit at both supported HUD resolutions");Capture(std::filesystem::path(argv[2])/("battle-hud-"+std::to_string(dims.second)+".png"),dims.first,dims.second);}
        }
        {
            FacePaint markerPaint(*stage,geometry);MatchOptions options;options.duration=90;
            MatchSimulation markerMatch(*stage,geometry,markerPaint,nav,options);markerMatch.startPlaying();
            auto& local=markerMatch.local();PlayerSimulation *ally=nullptr,*enemy=nullptr;
            for(const auto& actor:markerMatch.actors()){
                actor->state.invuln=0;
                if(actor.get()!=&local&&actor->team()==local.team())ally=actor.get();
                if(actor->team()!=local.team())enemy=actor.get();
            }
            Check(ally&&enemy,"Death marker fixture needs an ally and an enemy");
            const auto deathPosition=ally->state.pos;
            markerMatch.world().applyHit(*enemy,*ally,200,"shooter");
            ally->spawnAt(deathPosition+glm::dvec3(5,0,0),0);
            const auto view=glm::scale(glm::mat4(1),glm::vec3(.05f))*glm::translate(glm::mat4(1),-glm::vec3(deathPosition));
            ui.SetWorldView(view,deathPosition+glm::dvec3(0,4,0),1.0f);
            auto draw=[&](int width=1600,int height=900){glViewport(0,0,width,height);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderHud(*stage,markerPaint,markerMatch,width,height,false,true);};
            draw();
            Check(hud->GetElementById("death-markers")->GetNumChildren()==1&&hud->GetElementById("minimap-map")->GetInnerRML().find("map-death-mark")!=Rml::String::npos,"A death before the first HUD frame must retain its original world and map positions after respawn");
            draw(1280,720);Capture(std::filesystem::path(argv[2])/"death-markers-1280x720.png",1280,720);
            draw(1920,1080);Capture(std::filesystem::path(argv[2])/"death-markers-1920x1080.png",1920,1080);
            for(int i=0;i<228;++i)markerMatch.world().tick(1./60);
            draw();
            auto* fadingMark=hud->GetElementById("death-markers")->GetFirstChild();
            Check(hud->GetElementById("death-markers")->GetNumChildren()==1&&fadingMark&&fadingMark->GetComputedValues().opacity()<.99f&&fadingMark->GetComputedValues().opacity()>.8f,"The final 1.3 seconds of a death mark must fade");
            for(int i=0;i<73;++i)markerMatch.world().tick(1./60);
            draw();
            Check(hud->GetElementById("death-markers")->GetNumChildren()==0&&hud->GetElementById("minimap-map")->GetInnerRML().find("map-death-mark")==Rml::String::npos,"Death marks must expire after five simulation seconds");
        }
        {
            FacePaint scenePaint(*stage,geometry);MatchOptions options;options.duration=90;
            MatchSimulation sceneMatch(*stage,geometry,scenePaint,nav,options);sceneMatch.startPlaying();
            for(int i=0;i<60;++i)sceneMatch.tick(.1);
            SetInkPalette(ProfilePaletteColors(ui.profile().settings.palette,ui.profile().settings.colorblind));
            const auto references=std::filesystem::path(argv[1])/"assets/reference";
            StageRenderer sceneRenderer(references/"stages",true);sceneRenderer.Load(*stage,0,scenePaint);
            sceneRenderer.UploadInk(*stage,scenePaint);
            EnvironmentRenderer environment(references/"environment");environment.SetReflectionsEnabled(false);environment.Load(*stage);
            StageMapView smallMap(stage->bounds,sceneMatch.local().team()==1);
            const double frameTime=glfwGetTime();
            Check(smallMap.Render(sceneRenderer,environment,scenePaint.version(),frameTime),"Native minimap must render an initial stage frame");
            Check(!smallMap.Render(sceneRenderer,environment,scenePaint.version()+1,frameTime+.01),"Painting must not bypass the native minimap render cadence");
            ui.SetNativeMap(smallMap.texture(),glm::mat4(smallMap.viewProjection()));
            RenderPipeline pipeline(references/"post");pipeline.Configure(0,false,false);
            auto background=[&](int w,int h){
                const auto camera=FitStageMapCamera(stage->bounds,double(w)/h,sceneMatch.local().team()==1);
                pipeline.Begin(w,h,glm::mat4(camera.projection),glm::inverse(glm::mat4(camera.view)),1,6000);
                glClearColor(.2f,.35f,.55f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                sceneRenderer.Draw(camera.view,camera.projection,camera.eye,environment.Lights());
                environment.DrawSea(glm::mat4(camera.view),glm::mat4(camera.projection),glm::vec3(camera.eye));
                environment.DrawScenery(glm::mat4(camera.view),glm::mat4(camera.projection),glm::vec3(camera.eye));
                environment.DrawSky(glm::mat4(camera.view),glm::mat4(camera.projection),glm::vec3(camera.eye));
                pipeline.End(GradeSettings::Theme(std::string(stage->theme)),ScreenEffects{},w,h);
                ui.SetWorldView(glm::mat4(camera.projection*camera.view),camera.eye,float(camera.projection[1][1]));
            };
            std::vector<UiJumpMapTarget> targets;int key=1;
            for(std::size_t i=0;i<sceneMatch.actors().size();++i){const auto& actor=*sceneMatch.actors()[i];if(actor.team()==sceneMatch.local().team()&&!actor.isLocal()&&key<4){targets.push_back({key++,actor.state.pos.x,actor.state.pos.z,sceneMatch.roster()[i].name,actor.state.alive,actor.state.pos.y,std::string(actor.weapon().kind)});}}
            const auto spawn=stage->spawnPads[sceneMatch.local().team()];targets.push_back({4,spawn.x,spawn.z,"生點",true});
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){
                background(dims.first,dims.second);ui.SetJumpMap(true,targets);
                ui.RenderHud(*stage,scenePaint,sceneMatch,dims.first,dims.second,false,true);
                Check(hud->GetElementById("jump-map-marker-1")->IsVisible(),"The actual map camera must project a friendly jump pin");
                Capture(std::filesystem::path(argv[2])/("source-native-tab-"+std::to_string(dims.second)+".png"),dims.first,dims.second);
                background(dims.first,dims.second);ui.SetJumpMap(false,{});ui.RenderHud(*stage,scenePaint,sceneMatch,dims.first,dims.second,false,true);
                Check(hud->GetElementById("minimap-render")->IsVisible(),"The compact map must show the native stage texture");
                Check(hud->GetElementById("jump-map-marker-1")==nullptr,"Closing Tab must immediately remove large jump pins from the compact map");
                Capture(std::filesystem::path(argv[2])/("source-native-mini-"+std::to_string(dims.second)+".png"),dims.first,dims.second);
            }
            for(int i=0;i<1000&&sceneMatch.phase()!=MatchPhase::Judge;++i)sceneMatch.tick(.1);
            Check(sceneMatch.phase()==MatchPhase::Judge,"Native results composition fixture must finish the match");sceneMatch.finishJudging();sceneRenderer.UploadInk(*stage,scenePaint);
            ui.SetReward({});background(1600,900);ui.RenderHud(*stage,scenePaint,sceneMatch,1600,900,false,true);
            Capture(std::filesystem::path(argv[2])/"source-native-results-intro.png",1600,900);
            hud->GetElementById("result-skip")->Click();ui.RenderHud(*stage,scenePaint,sceneMatch,1600,900,false,true);
            PlayerShowcase showcase(references/"characters");std::vector<PlayerShowcase::PodiumPlayer> squad;
            for(std::size_t i=0;i<sceneMatch.actors().size();++i){const auto& actor=*sceneMatch.actors()[i];if(actor.team()==sceneMatch.local().team())squad.push_back({sceneMatch.roster()[i].name,sceneMatch.roster()[i].style,actor.mainId(),actor.subId()});}
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){
                background(dims.first,dims.second);ui.RenderHud(*stage,scenePaint,sceneMatch,dims.first,dims.second,false,true);const auto preview=ui.preview();background(dims.first,dims.second);
                showcase.drawPodium(squad,sceneMatch.result()->winner==sceneMatch.local().team(),{int(preview.x*dims.first),int((1-preview.y-preview.height)*dims.second),int(preview.width*dims.first),int(preview.height*dims.second)},glfwGetTime(),true);
                ui.RenderHud(*stage,scenePaint,sceneMatch,dims.first,dims.second,false,true);
                Capture(std::filesystem::path(argv[2])/("source-native-results-full-"+std::to_string(dims.second)+".png"),dims.first,dims.second);
            }
            FacePaint bossScenePaint(*stage,geometry);MatchOptions bossOptions;bossOptions.mode="boss";
            MatchSimulation bossScene(*stage,geometry,bossScenePaint,nav,bossOptions);bossScene.startPlaying();
            for(int i=0;i<40;++i)bossScene.tick(.1);
            BossSegmentHit weakHit;weakHit.weak=true;bossScene.boss()->hit(bossScene.local(),1000000,weakHit,"shooter");
            for(int i=0;i<70&&bossScene.phase()!=MatchPhase::Judge;++i)bossScene.tick(.1);
            Check(bossScene.phase()==MatchPhase::Judge,"Boss scene composition must complete the victory flow");bossScene.finishJudging();
            ui.RenderHud(*stage,bossScenePaint,bossScene,1600,900,false,true);hud->GetElementById("result-skip")->Click();
            std::vector<PlayerShowcase::PodiumPlayer> bossSquad;for(std::size_t i=0;i<4;++i){const auto& actor=*bossScene.actors()[i];bossSquad.push_back({bossScene.roster()[i].name,bossScene.roster()[i].style,actor.mainId(),actor.subId()});}
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){
                ui.RenderHud(*stage,bossScenePaint,bossScene,dims.first,dims.second,false,true);const auto preview=ui.preview();background(dims.first,dims.second);
                showcase.drawPodium(bossSquad,true,{int(preview.x*dims.first),int((1-preview.y-preview.height)*dims.second),int(preview.width*dims.first),int(preview.height*dims.second)},glfwGetTime(),true);
                ui.RenderHud(*stage,bossScenePaint,bossScene,dims.first,dims.second,false,true);
                Check(!hud->GetElementById("boss")->IsVisible(),"Results must hide the combat Boss plate");
                Check(hud->GetElementById("results-table")->GetFirstChild()->GetOffsetWidth()>dims.first*.4,"Boss rankings must occupy two readable columns");
                Capture(std::filesystem::path(argv[2])/("source-native-boss-results-"+std::to_string(dims.second)+".png"),dims.first,dims.second);
            }
            ui.ShowMenu("tidewater");element("nav-loadout")->Click();ui.RenderMenu(1600,900);
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){
                ui.RenderMenu(dims.first,dims.second);const auto preview=ui.preview();glClearColor(.94f,.91f,.84f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                showcase.drawPlayer(ui.profile(),ui.setup().main,ui.setup().sub,{int(preview.x*dims.first),int((1-preview.y-preview.height)*dims.second),int(preview.width*dims.first),int(preview.height*dims.second)},glfwGetTime(),true);
                ui.RenderMenu(dims.first,dims.second);Capture(std::filesystem::path(argv[2])/("source-native-loadout-"+std::to_string(dims.second)+".png"),dims.first,dims.second);
            }
            element("nav-settings")->Click();element("settings-tab-controls")->Click();
            for(const auto dims:{std::pair<int,int>{1280,720},{1920,1080}}){glViewport(0,0,dims.first,dims.second);glClearColor(.94f,.91f,.84f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderMenu(dims.first,dims.second);Capture(std::filesystem::path(argv[2])/("source-native-settings-"+std::to_string(dims.second)+".png"),dims.first,dims.second);}
            ui.SetNativeMap(0,glm::mat4(1));
        }
        const auto error=glGetError();if(error)std::cerr<<"GL error: "<<error<<"\n";Check(error==GL_NO_ERROR,"UI interaction draw produced GL errors");
        std::cout<<"PLAYER_UI keyboard, modes, source loadout and volume sink passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
