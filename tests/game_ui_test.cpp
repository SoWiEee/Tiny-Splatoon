#include "GameUi.hpp"
#include "ArsenalData.hpp"
#include "MatchSimulation.hpp"
#include "FacePaint.hpp"
#include "ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void Capture(const std::filesystem::path& file,int width,int height){
    std::filesystem::create_directories(file.parent_path());
    std::vector<unsigned char> pixels(width*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    stbi_flip_vertically_on_write(1);
    Check(stbi_write_png(file.string().c_str(),width,height,3,pixels.data(),width*3)!=0,"UI capture failed");
}
}
int main(int argc,char** argv){
    if(argc!=3||!glfwInit())return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1600,900,"Player UI interaction test",nullptr,nullptr);
    if(!window){glfwTerminate();return 1;}
    glfwMakeContextCurrent(window);if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))return 1;
    int result=0;
    try{
        using namespace restoration;
        GameUi ui(window,std::filesystem::path(argv[1])/"assets");
        std::vector<std::string> sounds;float master=0;
        ui.SetSoundSink([&](const auto& name){sounds.push_back(name);});
        ui.SetVolumeSink([&](float a,float,float){master=a;});
        ui.ShowMenu("tidewater");
        auto* context=Rml::GetContext("game-ui");Check(context!=nullptr,"Missing game UI context");
        auto* menu=context->GetDocument("menu");Check(menu!=nullptr,"Missing menu document");
        auto element=[&](const char* id){auto* e=menu->GetElementById(id);Check(e!=nullptr,"Missing menu control");return e;};
        auto render=[&]{glViewport(0,0,1600,900);glClearColor(.96f,.97f,.95f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderMenu(1600,900);};
        render();Capture(std::filesystem::path(argv[2])/"menu.png",1600,900);
        Check(!element("duration-row")->IsVisible(),"Turf must not expose Boss duration");
        Check(element("nav-play")->Focus(),"Menu navigation must accept keyboard focus");
        context->ProcessKeyDown(Rml::Input::KI_TAB,0);context->ProcessKeyUp(Rml::Input::KI_TAB,0);
        Check(context->GetFocusElement()!=element("nav-play"),"Tab must move keyboard focus");
        element("mode-boss")->Click();render();
        Check(ui.setup().mode=="boss"&&element("duration-row")->IsVisible(),"Boss mode must expose duration and update match setup");
        element("nav-loadout")->Click();render();
        auto* main=dynamic_cast<Rml::ElementFormControl*>(element("main"));Check(main!=nullptr,"Main loadout is not a form control");
        main->SetValue("charger");main->DispatchEvent("change",{});
        Check(ui.setup().main=="charger"&&ui.setup().sub==GetEquipment(EquipmentCategory::Main,"charger").sub,"Main weapon must apply source default kit");
        render();Capture(std::filesystem::path(argv[2])/"loadout.png",1600,900);
        element("nav-settings")->Click();render();
        auto* volume=dynamic_cast<Rml::ElementFormControl*>(element("master"));Check(volume!=nullptr,"Missing volume control");
        volume->SetValue("25");volume->DispatchEvent("change",{});Check(master==.25f,"Volume control did not reach the audio bus sink");
        render();Capture(std::filesystem::path(argv[2])/"settings.png",1600,900);
        element("nav-play")->Click();render();Check(element("start")->Focus(),"Start must accept keyboard focus");
        context->ProcessKeyDown(Rml::Input::KI_RETURN,0);context->ProcessKeyUp(Rml::Input::KI_RETURN,0);
        Check(render()==UiAction::Start,"Enter must activate the selected start action");
        Check(!sounds.empty()&&sounds.back()=="ui_confirm","Start must emit the source confirm cue");
        const StageData* stage=nullptr;
        for(const auto& row:ReferenceStages())if(row.id=="tidewater"&&row.mode=="turf"&&!row.onlineOnly)stage=&row;
        Check(stage!=nullptr,"Missing UI test stage");
        StageGeometry geometry(*stage);FacePaint paint(*stage,geometry);NavGraph nav(*stage,geometry);
        MatchSimulation match(*stage,geometry,paint,nav);
        auto hudRender=[&](bool paused){glViewport(0,0,1600,900);glClearColor(.25f,.3f,.35f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);return ui.RenderHud(*stage,paint,match,1600,900,paused,true);};
        hudRender(true);
        auto* hud=context->GetDocument("hud");Check(hud!=nullptr,"Missing match HUD document");
        Check(hud->GetElementById("pause")->IsVisible(),"Pause overlay did not appear");
        Capture(std::filesystem::path(argv[2])/"pause.png",1600,900);
        hud->GetElementById("resume")->Click();Check(hudRender(false)==UiAction::Resume,"Resume did not reach the match loop");
        Check(!hud->GetElementById("pause")->IsVisible(),"Resume did not hide the pause overlay");
        match.startPlaying();
        for(int i=0;i<1850&&match.phase()!=MatchPhase::Judge;++i)match.tick(.1);
        Check(match.phase()==MatchPhase::Judge,"UI lifecycle test did not reach judging");match.finishJudging();
        hudRender(false);Check(hud->GetElementById("results")->IsVisible(),"Results overlay did not appear");
        Check(hud->GetElementById("results-table")->GetNumChildren()>=9,"Results lost squad rows");
        Capture(std::filesystem::path(argv[2])/"results.png",1600,900);
        hud->GetElementById("results-menu")->Click();Check(hudRender(false)==UiAction::StageSelect,"Return action did not reach the match loop");
        ui.ShowMenu("tidewater");Check(render()==UiAction::None&&menu->IsVisible(),"Returning from results did not restore the menu");
        Check(glGetError()==GL_NO_ERROR,"UI interaction draw produced GL errors");
        std::cout<<"PLAYER_UI keyboard, modes, source loadout and volume sink passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
