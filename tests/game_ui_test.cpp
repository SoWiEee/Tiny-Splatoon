#include "GameUi.hpp"
#include "ArsenalData.hpp"
#include "MatchSimulation.hpp"
#include "PlayerController.hpp"
#include "ZoneControl.hpp"
#include "FacePaint.hpp"
#include "ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
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
        GameUi ui(window,std::filesystem::path(argv[1])/"assets");
        std::vector<std::string> sounds;float master=0;
        ui.SetSoundSink([&](const auto& name){sounds.push_back(name);});
        ui.SetVolumeSink([&](float a,float,float){master=a;});
        ui.ShowTitle("tidewater");
        auto* context=Rml::GetContext("game-ui");Check(context!=nullptr,"Missing game UI context");
        auto* menu=context->GetDocument("menu");Check(menu!=nullptr,"Missing menu document");
        auto element=[&](const char* id){auto* e=menu->GetElementById(id);Check(e!=nullptr,"Missing menu control");return e;};
        auto render=[&]{glViewport(0,0,1600,900);glClearColor(.96f,.97f,.95f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderMenu(1600,900);};
        render();Capture(std::filesystem::path(argv[2])/"menu.png",1600,900);
        Check(element("brand-wordmark")->GetInnerRML().find("Tiny Splatoon")!=Rml::String::npos,"Menu wordmark should use the original game name");
        Check(element("panel-title")->IsVisible()&&!element("panel-home")->IsVisible()&&!element("navigation")->IsVisible(),"Startup must show a focused title screen before the home panel");
        Check(element("title-start")->Focus(),"Title start action must accept keyboard focus");context->ProcessKeyDown(Rml::Input::KI_RETURN,0);context->ProcessKeyUp(Rml::Input::KI_RETURN,0);
        render();Check(element("panel-home")->IsVisible()&&!element("panel-title")->IsVisible(),"Starting from the title screen must enter Home");
        Check(ui.preview().type==UiPreviewType::Player&&ui.preview().x>.6f&&ui.preview().width>.25f,"Main menu should expose its right-side character viewport");
        int profileWrites=0;PlayerProfile loaded;loaded.name="Mint";loaded.weapon="shooter";loaded.xp=120;loaded.style.hair=2;loaded.settings.showFps=true;
        ui.SetProfile(loaded);ui.SetProfileSink([&](const PlayerProfile&){++profileWrites;});
        Check(profileWrites==0&&ui.profile().name=="Mint","Loading a profile must not write it back immediately");
        auto* name=dynamic_cast<Rml::ElementFormControl*>(element("profile-name"));Check(name!=nullptr,"Profile name must be editable");
        name->SetValue("Mint Ice");name->DispatchEvent("change",{});
        Check(ui.profile().name=="Mint Ice"&&profileWrites==1,"Editing the profile name must persist through the profile sink");
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
                    Check(marker&&marker->GetInnerRML()==std::to_string(target.key%10),"Map marker must visibly print its target digit: "+markerId);
                    if(!marker)continue;
                    const auto markerOffset=marker->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto markerSize=marker->GetBox().GetSize(Rml::BoxArea::Border);
                    const double left=std::clamp((target.x-bounds.x)/spanX*100.,3.,97.);
                    const double top=std::clamp((bounds.w-target.z)/spanZ*100.,5.,95.);
                    const double centerX=markerOffset.x+markerSize.x*.5,centerY=markerOffset.y+markerSize.y*.5;
                    const double expectedX=mapOffset.x+mapBounds.x*left/100.,expectedY=mapOffset.y+mapBounds.y*top/100.;
                    Check(markerOffset.x>=mapOffset.x-.5&&markerOffset.y>=mapOffset.y-.5&&
                          markerOffset.x+markerSize.x<=mapOffset.x+mapBounds.x+.5&&
                          markerOffset.y+markerSize.y<=mapOffset.y+mapBounds.y+.5,
                          "Map target marker bounding rect must remain inside the minimap: "+markerId);
                    Check(std::abs(centerX-expectedX)<3.0&&std::abs(centerY-expectedY)<3.0,
                          "Map target marker must align with its projected target position: "+markerId);
                }
                const auto pixels=Capture(std::filesystem::path(argv[2])/filename,width,height);
                const auto pixel=[&](int x,int y){
                    x=std::clamp(x,0,width-1);y=std::clamp(y,0,height-1);
                    const auto index=(static_cast<std::size_t>(height-1-y)*width+static_cast<std::size_t>(x))*3;
                    return std::array<unsigned char,3>{pixels[index],pixels[index+1],pixels[index+2]};
                };
                int originGreenPixels=0;
                for(int y=1;y<std::min(height,36);++y)for(int x=1;x<std::min(width,36);++x){
                    const auto color=pixel(x,y);
                    if(color[1]>65&&color[1]>color[0]*1.45&&color[1]>color[2]*1.08)++originGreenPixels;
                }
                Check(originGreenPixels<5,"Framebuffer must not draw a displaced green map marker at the window origin (pixels="+std::to_string(originGreenPixels)+")");
                for(const auto& target:targets){
                    const auto markerId="jump-map-marker-"+std::to_string(target.key);
                    auto* marker=hud->GetElementById(markerId.c_str());
                    if(!marker)continue;
                    const auto offset=marker->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto size=marker->GetBox().GetSize(Rml::BoxArea::Border);
                    const int left=static_cast<int>(std::floor(offset.x+size.x*.2));
                    const int right=static_cast<int>(std::ceil(offset.x+size.x*.8));
                    const int top=static_cast<int>(std::floor(offset.y+size.y*.2));
                    const int bottom=static_cast<int>(std::ceil(offset.y+size.y*.8));
                    const int digitLeft=static_cast<int>(std::floor(offset.x+size.x*.35));
                    const int digitRight=static_cast<int>(std::ceil(offset.x+size.x*.65));
                    const int digitTop=static_cast<int>(std::floor(offset.y+size.y*.25));
                    const int digitBottom=static_cast<int>(std::ceil(offset.y+size.y*.75));
                    int greenPixels=0,glyphWhitePixels=0;
                    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
                        const auto color=pixel(x,y);
                        if(color[1]>65&&color[1]>color[0]*1.45&&color[1]>color[2]*1.08)++greenPixels;
                    }
                    for(int y=digitTop;y<digitBottom;++y)for(int x=digitLeft;x<digitRight;++x){
                        const auto color=pixel(x,y);
                        if(color[0]>244&&color[1]>244&&color[2]>244)++glyphWhitePixels;
                    }
                    Check(greenPixels>=std::max(6,int(size.x*size.y*.05)),
                          "Framebuffer must contain the green target face inside "+markerId+" (pixels="+std::to_string(greenPixels)+")");
                    Check(glyphWhitePixels>=std::max(3,int(size.x*.15)),
                          "Framebuffer must contain the white target digit inside "+markerId+" (pixels="+std::to_string(glyphWhitePixels)+")");
                }
            };
            auto clickTarget=[&](int key){
                const std::string id="jump-target-"+std::to_string(key);
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
            auto* minimapHeading=hud->GetElementById("minimap-heading");
            auto* minimapTitle=minimapHeading->GetFirstChild();
            auto* terrainStatus=hud->GetElementById("terrain-status");
            auto* minimapMap=hud->GetElementById("minimap-map");
            auto* positionCoverage=hud->GetElementById("position-team-coverage");
            checkVerticalOrder(minimapTitle,terrainStatus,"Minimap title and terrain status");
            checkVerticalOrder(terrainStatus,minimapMap,"Minimap terrain status and map");
            checkVerticalOrder(minimapMap,positionCoverage,"Minimap map and footer");
            checkReadableFont(hud->GetElementById("terrain-status"),"Minimap heading metadata");
            checkReadableFont(positionCoverage,"Minimap team coverage metadata");
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
        const auto mapBefore=minimap->GetInnerRML();Check(mapBefore.find("map-cell")!=Rml::String::npos&&mapBefore.find("map-actor-local")!=Rml::String::npos,"Minimap must render spatial terrain and the local actor marker");
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
        Check(hud->GetElementById("results-table")->GetNumChildren()>=9,"Results lost squad rows");
        auto* xpLabel=hud->GetElementById("result-xp-label");
        Check(xpLabel->GetInnerRML().find("+0")!=Rml::String::npos,"Results should begin the XP reveal at zero");
        const double xpMidTime=glfwGetTime()+.2;glfwSetTime(xpMidTime);hudRender(false);
        Check(xpLabel->GetInnerRML().find("+640")==Rml::String::npos&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"XP should count up with one tick while the award is in flight");
        const auto xpMidReveal=xpLabel->GetInnerRML();ui.SetReward(reward);glfwSetTime(xpMidTime);hudRender(false);
        Check(xpLabel->GetInnerRML()==xpMidReveal&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"Repeated reward assignments must not restart the XP reveal or duplicate a tick");
        glfwSetTime(glfwGetTime()+2.0);hudRender(false);
        Check(xpLabel->GetInnerRML().find("+640")!=Rml::String::npos,"Results must finish by displaying the saved XP reward");
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
        glfwSetTime(glfwGetTime()+.2);ui.RenderHud(*stage,paint,match,1600,900,false,true);
        Check(xpLabel->GetInnerRML().find("+640")==Rml::String::npos&&std::count(sounds.begin(),sounds.end(),"xp_tick")==1,"The same-match rematch's identical award should animate and tick once");
        hud->GetElementById("results-rematch")->Click();Check(hudRender(false)==UiAction::Rematch,"Results rematch action did not reach the match loop");
        hud->GetElementById("results-menu")->Click();Check(hudRender(false)==UiAction::StageSelect,"Return action did not reach the match loop");
        ui.ShowMenu("tidewater");Check(render()==UiAction::None&&menu->IsVisible(),"Returning from results did not restore the menu");
        Check(glGetError()==GL_NO_ERROR,"UI interaction draw produced GL errors");
        std::cout<<"PLAYER_UI keyboard, modes, source loadout and volume sink passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
