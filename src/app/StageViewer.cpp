#include "StageViewer.hpp"
#include "LaunchOptions.hpp"
#include "MatchSession.hpp"
#include "AppFrame.hpp"
#include "ReferenceData.hpp"
#include "ArsenalData.hpp"
#include "RenderPipeline.hpp"
#include "EnvironmentRenderer.hpp"
#include "GameUi.hpp"
#include "PlayerProfile.hpp"
#include "PlayerShowcase.hpp"
#include "LobbyRenderer.hpp"
#include "LobbyEffects.hpp"
#include "LobbyView.hpp"
#include "LobbyPresentation.hpp"
#include "TeamPalette.hpp"
#include "AudioEngine.hpp"
#include "GameAudio.hpp"
#include "OnlinePlay.hpp"
#include "../network/MatchSync.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <utility>

namespace restoration {
namespace {
struct WindowMode {
    int x=80,y=80,width=1600,height=900;
    void apply(GLFWwindow* window,bool fullscreen){
        if((glfwGetWindowMonitor(window)!=nullptr)==fullscreen)return;
        if(fullscreen){
            auto* monitor=glfwGetPrimaryMonitor();const auto* mode=monitor?glfwGetVideoMode(monitor):nullptr;
            if(!mode)return;
            glfwGetWindowPos(window,&x,&y);glfwGetWindowSize(window,&width,&height);
            glfwSetWindowMonitor(window,monitor,0,0,mode->width,mode->height,mode->refreshRate);
        }else glfwSetWindowMonitor(window,nullptr,x,y,width,height,GLFW_DONT_CARE);
    }
};

LaunchOptions ParseLaunchOptions(int argc, char** argv) {
    LaunchOptions result;
    result.executable=std::filesystem::absolute(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference-practice") {
            result.match=true;result.practice=true;
            if(i+1<argc&&argv[i+1][0]!='-')result.stage=argv[++i];
            continue;
        }
        if (arg == "--reference-match") {
            result.match = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') result.stage = argv[++i];
            continue;
        }
        if (arg == "--reference-boss") {
            result.match = true;result.mode="boss";
            if (i + 1 < argc && argv[i + 1][0] != '-') result.stage = argv[++i];
            continue;
        }
        if (arg == "--reference-zones") {
            result.match=true;result.mode="zones";
            if(i+1<argc&&argv[i+1][0]!='-')result.stage=argv[++i];
            continue;
        }
        if (arg == "--native") { result.native = true; continue; }
        if (arg == "--reference-game" || arg == "--reference-stage" || arg == "--paint-probe" || arg == "--play-probe" || arg == "--capture")
            throw std::runtime_error("Retired diagnostic option: " + arg + "; use the native menu or --reference-practice/--match-capture");
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
        const std::string value = argv[++i];
        if (arg == "--reference-mode") result.mode = value;
        else if (arg == "--duration") {
            std::size_t end=0;const int seconds=std::stoi(value,&end);
            if(end!=value.size()||(seconds!=180&&seconds!=240&&seconds!=300))throw std::runtime_error("Boss duration must be 180, 240, or 300 seconds");
            result.duration=seconds;
        }
        else if (arg == "--match-capture") result.matchCapture = value;
        else if (arg == "--ui-capture") result.uiCapture = value;
        else if (arg == "--benchmark-report") { result.benchmarkReport = value; result.benchmarkReportSpecified = true; }
        else if(arg=="--graphics-quality"){
            if(value!="low"&&value!="medium"&&value!="high")throw std::runtime_error("--graphics-quality must be low, medium or high");
            result.graphicsQuality=value;
        }
        else if (arg == "--benchmark-camera") {
            result.benchmarkTuningSpecified=true;
            if(value!="player"&&value!="overview")throw std::runtime_error("--benchmark-camera must be player or overview");
            result.benchmarkPlayer=value=="player";
        }
        else if (arg == "--benchmark-seed") {
            result.benchmarkTuningSpecified = true;
            std::size_t end = 0;
            const auto seed = std::stoull(value, &end);
            if (end != value.size() || seed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid unsigned seed for " + arg);
            result.benchmarkSeed = static_cast<std::uint32_t>(seed);
        }
        else if (arg == "--benchmark-warmup" || arg == "--benchmark-frames") {
            result.benchmarkTuningSpecified = true;
            std::size_t end = 0;
            const int number = std::stoi(value, &end);
            if (end != value.size() || number < (arg == "--benchmark-warmup" ? 0 : 1) || number > 100000)
                throw std::runtime_error("Invalid frame count for " + arg);
            if (arg == "--benchmark-warmup") result.benchmarkWarmup = number;
            else result.benchmarkFrames = number;
        }

        else if (arg == "--frames" || arg == "--width" || arg == "--height") {
            std::size_t end = 0;
            const int number = std::stoi(value, &end);
            if (end != value.size() || number <= 0 || number > 16384)
                throw std::runtime_error("Invalid positive integer for " + arg);
            if (arg == "--frames") result.frames = number;
            else if (arg == "--width") { result.width = number; result.widthSpecified = true; }
            else { result.height = number; result.heightSpecified = true; }
        } else throw std::runtime_error("Unknown game option: " + arg);
    }
    if (!result.match) result.native = true;
    if (result.match) {
        if (!result.widthSpecified) result.width = 1920;
        if (!result.heightSpecified) result.height = 1080;
    }
    if (!result.matchCapture.empty() && !result.frames) result.frames = 3;
    if (!result.uiCapture.empty() && !result.frames) result.frames = 3;
    if (!result.uiCapture.empty() && !result.native) throw std::runtime_error("--ui-capture requires the native menu");
    if (result.native && (result.match ||
                          !result.matchCapture.empty() || result.benchmarkReportSpecified || result.benchmarkTuningSpecified || !result.graphicsQuality.empty()))
        throw std::runtime_error("--native cannot be combined with reference matches, practice probes, captures, or benchmark options");
    if (!result.benchmarkReport.empty() && !result.match)
        throw std::runtime_error("--benchmark-report requires --reference-match");
    if(!result.graphicsQuality.empty()&&!result.match)throw std::runtime_error("--graphics-quality requires --reference-match");
    if(result.benchmarkPlayer&&result.benchmarkReport.empty())throw std::runtime_error("--benchmark-camera player requires --benchmark-report");
    if (result.duration&&( !result.match||result.mode!="boss"))throw std::runtime_error("--duration requires a Boss match");
    if (!result.matchCapture.empty() && !result.match)
        throw std::runtime_error("--match-capture requires --reference-match");
    if (!result.benchmarkReport.empty() && result.frames)
        throw std::runtime_error("--frames cannot shorten a fixed-size benchmark capture");
    return result;
}






void NativeMenu(GLFWwindow* window,const LaunchOptions& options,const StageData& initial,
                const std::filesystem::path& assetRoot,GameUi& ui,AudioEngine& audio,GameAudio& gameAudio) {
    const auto models=assetRoot/"models",world=assetRoot/"world",effects=assetRoot/"effects";
    ProfileStore store;
    WindowMode windowMode;
    OnlinePlay online(options.executable);
    std::string handledMatch;
    try{ui.SetProfile(store.load());}
    catch(const std::exception& error){ui.SetProfile({});ui.SetSaveError(error.what());}
    audio.SetVolumes(float(ui.profile().settings.master),float(ui.profile().settings.music),float(ui.profile().settings.sfx));
    windowMode.apply(window,ui.profile().settings.fullscreen);
    SetInkPalette(ProfilePaletteColors(ui.profile().settings.palette,ui.profile().settings.colorblind));
    ui.SetProfileSink([&](const PlayerProfile& profile){
        windowMode.apply(window,profile.settings.fullscreen);
        try{store.save(profile);ui.SetSaveError({});}
        catch(const std::exception& error){ui.SetSaveError(error.what());}
        online.ProfileChanged(profile);
    });
    const StageData* selected=&initial;
    ui.ShowTitle(std::string(initial.id));
    int loadingWidth=0,loadingHeight=0;glfwGetFramebufferSize(window,&loadingWidth,&loadingHeight);
    if(loadingWidth>0&&loadingHeight>0){
        glViewport(0,0,loadingWidth,loadingHeight);glClearColor(.96f,.97f,.95f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderMenu(loadingWidth,loadingHeight);glfwSwapBuffers(window);
    }
    PlayerShowcase showcase(models/"characters");
    gameAudio.Menu(true);
    std::unique_ptr<LobbyRenderer> lobbyScene;
    std::unique_ptr<LobbyPresentation> lobby;
    std::unique_ptr<LobbyEffects> lobbyEffects;
    std::unique_ptr<RenderPipeline> lobbyPipeline;
    bool lobbyLoadFailed=false;
    LobbyCameraMotion lobbyCamera;
    double previousFrame=glfwGetTime();
    int frame=0;
    while(!glfwWindowShouldClose(window)) {
        const double frameStart=glfwGetTime();
        const double menuDt=std::clamp(frameStart-previousFrame,0.0,.1);previousFrame=frameStart;
        SetInkPalette(ProfilePaletteColors(ui.profile().settings.palette,ui.profile().settings.colorblind));
        glfwPollEvents();
        audio.Update();
        online.Update(frameStart);ui.SetOnlineState(online.view());
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if(width<=0||height<=0) {glfwWaitEventsTimeout(0.05);continue;}
        glViewport(0,0,width,height);
        glClearColor(.94f,.91f,.84f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        const auto& roomView=online.view();
        const bool starting=online.session()&&(roomView.phase==network::RoomPhase::Starting||roomView.phase==network::RoomPhase::Match);
        const auto currentMatch=roomView.code+"-"+std::to_string(roomView.matchEpoch)+"-"+std::to_string(roomView.seed);
        if(starting&&currentMatch!=handledMatch)ui.ShowOnline();
        auto preview=ui.preview();
        if(preview.screen==UiScreen::Online&&!lobbyScene&&!lobbyLoadFailed){
            try{
                lobbyScene=std::make_unique<LobbyRenderer>(world/"lobby");
                lobby=std::make_unique<LobbyPresentation>(lobbyScene->Layout());
                lobbyEffects=std::make_unique<LobbyEffects>(effects/"particles");
                lobbyPipeline=std::make_unique<RenderPipeline>(effects/"post",assetRoot/"shaders");
            }catch(const std::exception& error){
                lobby.reset();lobbyEffects.reset();lobbyScene.reset();lobbyPipeline.reset();lobbyLoadFailed=true;
                online.Fail("無法載入大廳場景："+std::string(error.what()));ui.SetOnlineState(online.view());
            }
        }
        std::vector<LobbyPresentationEvent> lobbyEvents;
        if(lobby){lobby->Update(online.view(),ui.profile(),menuDt);lobbyEvents=lobby->takeEvents();}
        if(lobby&&!lobby->hubMode())SetInkPalette(ProfilePaletteColors(static_cast<int>(roomView.settings.palette),ui.profile().settings.colorblind));
        if(lobbyEffects)lobbyEffects->Update(ui.profile().settings.reducedMotion?std::vector<LobbyActorState>{}:lobby->actors(),
            ui.profile().settings.reducedMotion?std::vector<LobbyPresentationEvent>{}:lobbyEvents,menuDt,frameStart);
        const bool drawLobby=lobby&&preview.screen==UiScreen::Online;
        std::vector<UiLobbyPlate> lobbyPlates;
        if(drawLobby){
            const auto graphics=ui.graphicsSettings();
            const int rw=std::max(1,int(std::round(width*graphics.renderScale))),rh=std::max(1,int(std::round(height*graphics.renderScale)));
            const auto camera=lobbyCamera.Update(lobbyScene->Layout(),lobby->hubMode(),width,height,menuDt,frameStart,
                lobby->launching(),lobby->countdown(),ui.profile().settings.reducedMotion);
            const auto& shot=lobby->camera();
            lobbyPipeline->Configure(graphics.renderScale>.99f?4:0,graphics.postEffects,graphics.postEffects&&ui.profile().settings.bloom,graphics.antialiasing);
            lobbyPipeline->Begin(rw,rh,glm::mat4(camera.projection),glm::mat4(glm::inverse(camera.view)),float(shot.nearPlane),float(shot.farPlane));
            glClearColor(.025f,.035f,.06f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            showcase.updateLobby(lobby->actors(),frameStart,camera.eye,ui.profile().settings.reducedMotion,lobbyEvents);
            lobbyScene->SetReflectionsEnabled(graphics.reflections);
            lobbyScene->SetKeyShadowEnabled(graphics.shadows);
            lobbyScene->Draw(camera.view,camera.projection,camera.eye,frameStart,InkColors[0],InkColors[1],
                [&](const glm::dmat4& reflectedView,const glm::dmat4& reflectedProjection,glm::dvec3 reflectedEye){
                    GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
                    const auto shadow=lobbyScene->KeyShadowState();
                    showcase.drawLobby(lobby->actors(),{viewport[0],viewport[1],viewport[2],viewport[3]},reflectedView,reflectedProjection,reflectedEye,lobbyScene->Lights(),[&](glm::dvec3 p){return lobbyScene->LightingAt(p);},graphics.shadows?&shadow:nullptr);
                    if(!ui.profile().settings.reducedMotion)lobbyEffects->Draw(reflectedView,reflectedProjection,reflectedEye,lobbyScene->Lights(),frameStart);
                },[&](const glm::dmat4& lightView,const glm::dmat4& lightProjection,glm::dvec3 lightEye,const CharacterShadowState& shadow){
                    GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
                    showcase.drawLobby(lobby->actors(),{viewport[0],viewport[1],viewport[2],viewport[3]},lightView,lightProjection,lightEye,lobbyScene->Lights(),{},&shadow);
                });
            const auto lobbyShadow=lobbyScene->KeyShadowState();
            showcase.drawLobby(lobby->actors(),{0,0,rw,rh},camera.view,camera.projection,camera.eye,lobbyScene->Lights(),[&](glm::dvec3 p){return lobbyScene->LightingAt(p);},graphics.shadows?&lobbyShadow:nullptr);
            if(!ui.profile().settings.reducedMotion)lobbyEffects->Draw(camera.view,camera.projection,camera.eye,lobbyScene->Lights(),frameStart);
            lobbyPipeline->End(GradeSettings{},ScreenEffects{},width,height);
            for(const auto& actor:lobby->actors()){
                const auto clip=camera.projection*camera.view*glm::dvec4(actor.position+glm::dvec3(0,actor.form=="kid"?1.8:.65,0),1);
                if(clip.w>0)lobbyPlates.push_back({actor.id,float((clip.x/clip.w+1)*.5),float((1-clip.y/clip.w)*.5),actor.visible});
            }
        }
        if(lobby){
            for(const auto& event:lobbyEvents){
                if(event.kind=="countdown_start"){audio.Duck(.6,4);audio.Play("ui_confirm");}
                else if(event.kind=="countdown_3"||event.kind=="countdown_2"||event.kind=="countdown_1")audio.Play("countdown_tick");
                else if(event.kind=="go")audio.Play("go_horn");
                else if(event.kind=="superjump")audio.Play("super_jump",.4);
                else if(event.kind=="land")audio.Play("splat_small",.35);
                else if(event.kind=="pop")audio.Play("squid_out",.5);
                else if(event.kind=="dive"||event.kind=="switch"||event.kind=="leave")audio.Play("squid_in",.35);
                if(event.kind=="land"||event.kind=="pop"||event.kind=="dive"||event.kind=="superjump")lobbyScene->AddRipple(event.position,frameStart,event.strength);
            }
        }
        ui.SetLobbyPresentation(drawLobby,lobby?lobby->countdown():-1,drawLobby&&lobby->launching(),lobbyPlates);
        if(!drawLobby&&preview.type!=UiPreviewType::None&&preview.type!=UiPreviewType::Podium&&preview.width>0&&preview.height>0)
            showcase.drawPlayer(ui.profile(),ui.setup().main,ui.setup().sub,
                {int(preview.x*width),int((1-preview.y-preview.height)*height),int(preview.width*width),int(preview.height*height)},frameStart,ui.profile().settings.reducedMotion);
        const auto action=ui.RenderMenu(width,height);
        for(const auto& request:ui.TakeOnlineRequests())online.Handle(request,ui.profile());
        const auto& selection=ui.setup();
        for(const auto& candidate:ReferenceStages())if(candidate.mode==(selection.mode=="zones"?"zones":"turf")&&!candidate.onlineOnly&&candidate.id==selection.stage)selected=&candidate;
        if(action==UiAction::Close) {glfwSetWindowShouldClose(window,1);break;}
        if(options.frames&&++frame>=options.frames) {
            if(!options.uiCapture.empty())Capture(options.uiCapture,width,height);
            std::cout<<"NATIVE_MENU state=stage-select stage="<<selected->id<<" frames="<<frame<<'\n';
            break;
        }
        glfwSwapBuffers(window);
        const auto roomMatch=online.view().code+"-"+std::to_string(online.view().matchEpoch)+"-"+std::to_string(online.view().seed);
        if(online.session()&&lobby&&lobby->launchComplete()&&(online.view().phase==network::RoomPhase::Starting||online.view().phase==network::RoomPhase::Match)&&roomMatch!=handledMatch){
            const auto& room=online.view();handledMatch=roomMatch;
            const StageData* onlineStage=nullptr;
            for(const auto& candidate:ReferenceStages())if(candidate.id==room.settings.map&&candidate.mode==(room.settings.mode=="zones"?"zones":"turf")){onlineStage=&candidate;break;}
            if(!onlineStage)online.Fail("找不到房間選擇的場地，請重新加入房間。");
            else{
                LaunchOptions game=options;game.match=true;game.native=false;game.practice=false;game.stage=room.settings.map;game.mode=room.settings.mode;game.duration=static_cast<int>(room.settings.duration);game.difficulty=room.settings.difficulty;
                StageData gameStage=*onlineStage;gameStage.theme=room.settings.time=="dusk"?"sunset":"day";
                SetInkPalette(ProfilePaletteColors(static_cast<int>(room.settings.palette),ui.profile().settings.colorblind));gameAudio.Menu(false);
                try{
                    const auto exit=RunMatchSession(window,game,gameStage,assetRoot,ui,audio,gameAudio,true,&store,&showcase,&online);
                    if(exit==MatchExit::Close)break;
                }catch(const std::exception& error){online.Fail("無法進入線上對戰："+std::string(error.what()));}
                gameAudio.Menu(true);
            }
            ui.ShowMenu(std::string(selected->id));ui.ShowOnline();ui.SetOnlineState(online.view());frame=0;previousFrame=glfwGetTime();
        }
        if(action==UiAction::Start||action==UiAction::Practice) {
            online.Leave();ui.SetOnlineState(online.view());
            LaunchOptions game=options;
            game.stage=std::string(selected->id);
            game.practice=action==UiAction::Practice||selection.mode=="practice";
            game.mode=game.practice?"turf":selection.mode;game.main=selection.main;game.sub=selection.sub;game.special=selection.special;game.difficulty=selection.difficulty;
            game.duration=game.practice?0:selection.duration;
            game.match=true;
            game.native=false;
            gameAudio.Menu(false);
            StageData gameStage=*selected;gameStage.theme=selection.theme=="dusk"?"sunset":"day";
            MatchExit exit;
            do{exit=RunMatchSession(window,game,gameStage,assetRoot,ui,audio,gameAudio,true,&store,&showcase);}while(exit==MatchExit::Rematch&&!glfwWindowShouldClose(window));
            if(exit==MatchExit::Close) break;
            if(exit==MatchExit::PracticeStage)ui.ShowPracticeStage(std::string(selected->id));else ui.ShowMenu(std::string(selected->id));
            gameAudio.Menu(true);
            frame=0;
        }
        if(!options.frames)LimitFrame(frameStart,ui.profile().settings.fpsCap);
    }
    ui.SetProfileSink({});
}

}

int RunStageViewer(int argc, char** argv) {
    GLFWwindow* window=nullptr;
    bool glfw=false;
    try {
        const auto options=ParseLaunchOptions(argc,argv);
        const auto& stages=ReferenceStages();
        const auto found=std::find_if(stages.begin(),stages.end(),[&](const StageData& stage){
            return stage.id==options.stage && stage.mode==(options.mode=="boss"?"turf":options.mode);
        });
        if (found==stages.end()) throw std::runtime_error("Unknown or unsupported stage/mode: "+options.stage+"/"+options.mode);
        if((options.match || options.native) && (found->onlineOnly || (found->mode!="turf"&&!(options.match&&options.mode=="zones"))))
            throw std::runtime_error("Native gameplay requires an offline stage for its selected mode");
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfw=true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        const bool hidden=!options.matchCapture.empty()||!options.benchmarkReport.empty();
        glfwWindowHint(GLFW_VISIBLE,hidden?GLFW_FALSE:GLFW_TRUE);
        if(!options.benchmarkReport.empty()) glfwWindowHint(GLFW_DECORATED,GLFW_FALSE);
        window=glfwCreateWindow(options.width,options.height,"Tiny Splatoon",nullptr,nullptr);
        if (!window) throw std::runtime_error("OpenGL 4.5 window creation failed");
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) throw std::runtime_error("GLAD initialization failed");
        std::cout<<"GPU "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<'\n';
        std::cout<<"STAGE "<<found->id<<" MODE "<<found->mode<<" BLOCKS "<<found->blocks.size()<<" FACES "<<found->faces.size()
                 <<" PROP_COLLIDERS "<<found->propColliderCount<<'\n';
        glfwSwapInterval(hidden?0:1);
        auto assetRoot=std::filesystem::absolute(argv[0]).parent_path()/"assets";
        if(!std::filesystem::exists(assetRoot/"models/characters"))assetRoot=std::filesystem::current_path()/"assets";
        if(options.native||options.match) {
            AudioEngine audio(assetRoot/"audio");
            GameAudio gameAudio(audio);
            if(!audio.Initialize())std::cerr<<"AUDIO unavailable: "<<audio.lastError()<<'\n';
            GameUi ui(window,assetRoot);
            ui.SetDetailedSoundSink([&](const std::string& name,double volume,double pitch){AudioPlayOptions request;request.volume=volume;request.pitch=pitch;audio.Play(name,request);});
            ui.SetVolumeSink([&](float master,float music,float sfx){audio.SetVolumes(master,music,sfx);});
            if(options.native)NativeMenu(window,options,*found,assetRoot,ui,audio,gameAudio);
            else RunMatchSession(window,options,*found,assetRoot,ui,audio,gameAudio);
        }
        glfwDestroyWindow(window);window=nullptr;
        glfwTerminate();glfw=false;
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Tiny Splatoon: "<<error.what()<<'\n';
        if (window) glfwDestroyWindow(window);
        if (glfw) glfwTerminate();
        return 1;
    }
}
}
