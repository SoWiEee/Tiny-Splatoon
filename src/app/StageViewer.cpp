#include "StageViewer.hpp"
#include "ReferenceData.hpp"
#include "StageGeometry.hpp"
#include "FacePaint.hpp"
#include "PlayerSimulation.hpp"
#include "MatchSimulation.hpp"
#include "NavGraph.hpp"
#include "CharacterArt.hpp"
#include "CharacterLod.hpp"
#include "ArsenalData.hpp"
#include "PerformanceCapture.hpp"
#include "StageRenderer.hpp"
#include "RenderPipeline.hpp"
#include "DirectionalShadow.hpp"
#include "ScreenEffectsController.hpp"
#include "LensField.hpp"
#include "WorldEffects.hpp"
#include "ZoneRenderer.hpp"
#include "ProjectileRenderer.hpp"
#include "EnvironmentRenderer.hpp"
#include "DeviceRenderer.hpp"
#include "BossRenderer.hpp"
#include "GameUi.hpp"
#include "PlayerProfile.hpp"
#include "PlayerController.hpp"
#include "PlayerShowcase.hpp"
#include "TeamPalette.hpp"
#include "AudioEngine.hpp"
#include "GameAudio.hpp"
#include "FollowCamera.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
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
struct Options {
    std::string stage = "tidewater", mode = "turf", matchCapture,uiCapture;
    std::string benchmarkReport,graphicsQuality;
    std::string main="shooter",sub,special,difficulty="normal";
    int duration = 0;
    int frames = 0, width = 1600, height = 900;
    int benchmarkWarmup = 120, benchmarkFrames = 600;
    std::uint32_t benchmarkSeed = 1;
    bool match = false, native = false, practice = false;
    bool benchmarkReportSpecified = false;
    bool benchmarkTuningSpecified = false, benchmarkPlayer = false;
    bool widthSpecified = false, heightSpecified = false;
};

void LimitFrame(double started,int cap){
    if(cap<=0)return;
    const double remaining=1.0/cap-(glfwGetTime()-started);
    if(remaining>0)std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
}

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

Options ParseOptions(int argc, char** argv) {
    Options result;

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


void Capture(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Cannot write capture: " + path);
    std::cout << "CAPTURE " << path << " " << width << "x" << height << '\n';
}

CameraTarget CameraActor(const PlayerSimulation& player) {
    CameraTarget target;
    const auto& s=player.state;
    target.position=s.pos; target.visualPosition=player.visualPos(); target.velocity=s.vel;
    target.grounded=s.grounded; target.climbing=s.climbing; target.squid=s.squid;
    target.swimming=s.submerged; target.landT=s.landT; target.landSpeed=s.landSpeed;
    return target;
}

enum class MatchExit { Close, StageSelect, Rematch, PracticeStage };

MatchExit MatchLoop(GLFWwindow* window,const Options& options,const StageData& stage,
                    const std::filesystem::path& characterAssets,GameUi& ui,AudioEngine& audio,GameAudio& gameAudio,bool nativeApp=false,ProfileStore* store=nullptr,PlayerShowcase* showcase=nullptr) {
    audio.Clear();
    const auto graphics=options.graphicsQuality.empty()?ui.graphicsSettings():ForPreset(options.graphicsQuality=="low"?GraphicsPreset::Low:options.graphicsQuality=="high"?GraphicsPreset::High:GraphicsPreset::Medium);
    StageGeometry geometry(stage);
    FacePaint paint(stage,geometry);
    NavGraph nav(stage,geometry);
    FollowCamera camera;
    MatchOptions setup;
    setup.practice=options.practice;
    setup.seed=options.benchmarkSeed;
    setup.autopilot=false;
    setup.mode=options.mode;
    setup.duration=options.duration;
    setup.main=options.main;setup.sub=options.sub;setup.special=options.special;setup.difficulty=options.difficulty;
    if(nativeApp){setup.playerName=ui.profile().name;setup.style=ui.profile().style;}
    if(nativeApp){camera.horizontalFovDegrees=ui.profile().settings.fov;camera.shake=ui.profile().settings.reducedMotion?0:ui.profile().settings.cameraShake;}
    MatchSimulation match(stage,geometry,paint,nav,setup);
    if(match.actors().size()!=(options.practice?1:8)) throw std::runtime_error("Incorrect offline actor roster");
    if(match.bots().size()!=(options.practice?0:7)) throw std::runtime_error("Incorrect offline bot roster");
    if(options.mode!="boss"&&!nativeApp)match.startPlaying();
    gameAudio.BeginMatch(match);
    camera.yaw=match.local().state.yaw;
    camera.pitch=-0.1;
    camera.reset(CameraActor(match.local()));

    StageRenderer renderer(characterAssets.parent_path()/"stages",true);
    EnvironmentRenderer environment(characterAssets.parent_path()/"environment");
    environment.SetReflectionsEnabled(graphics.reflections);
    environment.Load(stage);
    renderer.Load(stage,0,paint);
    renderer.UploadInk(stage,paint);
    RenderPipeline pipeline;
    pipeline.Configure(graphics.renderScale>.99f?4:0,graphics.postEffects,graphics.postEffects&&(!nativeApp||ui.profile().settings.bloom));
    CharacterArt art(characterAssets,true,true);
    const std::string matchId=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    WorldEffects worldEffects(art.data());
    worldEffects.SetPaintSurface(&paint);
    DeviceRenderer deviceRenderer(characterAssets.parent_path()/"devices");
    worldEffects.SetDeviceRenderer(&deviceRenderer);
    ProjectileRenderer projectileRenderer(characterAssets.parent_path()/"projectiles",true);
    std::unique_ptr<ZoneRenderer> zoneRenderer;
    if(match.zones())zoneRenderer=std::make_unique<ZoneRenderer>(characterAssets.parent_path()/"zones",std::string(stage.id));
    ScreenEffectsController screenController;
    LensField lensField;
    DirectionalShadow directionalShadow;
    std::unique_ptr<BossRenderer> bossRenderer;
    if(match.boss()){
        bossRenderer=std::make_unique<BossRenderer>(characterAssets.parent_path()/"boss",true);
        match.setBossPoseSink([&](BossSimulation& boss){bossRenderer->Update(boss);});
    }
    match.local().setDamageSink([&screenController](double amount,const glm::dvec3* attacker){screenController.Damage(amount,attacker);});
    match.local().setSplatSink([&screenController](bool water){screenController.Splat(water);});
    std::vector<WorldVisualEvent> pendingVisualEvents;
    screenController.MatchState("intro");
    struct LiveCharacter {
        std::unique_ptr<CharacterPose> pose;
        CharacterStyle style;
        CharacterSceneInput scene;
        CharacterLod lod;
    };
    std::vector<std::unique_ptr<LiveCharacter>> characters;
    characters.reserve(match.actors().size());
    const auto teamColors=InkColors;
    const double dt=1.0/60.0;
    double accumulator=0.0,previous=glfwGetTime();
    int frame=0;
    const glm::dvec3 target((stage.bounds.x+stage.bounds.y)*0.5,
        (stage.spawnPads[0].y+stage.spawnPads[1].y)*0.5,
        (stage.bounds.z+stage.bounds.w)*0.5);
    const double span=std::max(stage.bounds.y-stage.bounds.x,stage.bounds.w-stage.bounds.z);
    const glm::dvec3 overviewEye=target+glm::dvec3(span*0.82,span*1.15,span*0.82);
    const bool benchmark=!options.benchmarkReport.empty();
    bool paused=false,escapeWas=false,gearWas=false,cheerWas=false,leftWas=false,fireBlocked=true,resultReported=false;
    MatchExit matchExit=MatchExit::Close;
    bool pauseSubWasHeld=false,pauseSpecialWasHeld=false,subResumeSync=false,specialResumeBlock=false;
    double judgeElapsed=0.0;
    double mouseX=0.0,mouseY=0.0;
    double pendingMouseX=0,pendingMouseY=0;
    PlayerController controller;
    GLFWgamepadstate pad{};
    bool hasPad=false,usingPad=false;
    bool jumpMapOpen=false;
    std::array<bool,10> jumpTargetWasHeld{};
    auto captureMouse=[&](bool capture) {
        fireBlocked=true;
        pendingMouseX=pendingMouseY=0;
        glfwSetInputMode(window,GLFW_CURSOR,capture?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
        if(glfwRawMouseMotionSupported()) glfwSetInputMode(window,GLFW_RAW_MOUSE_MOTION,capture?GLFW_TRUE:GLFW_FALSE);
        glfwGetCursorPos(window,&mouseX,&mouseY);
    };
    auto pauseMatch=[&] {
        const auto& input=match.local().input;
        pauseSubWasHeld=input.sub || glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS ||
            glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS || (hasPad&&pad.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER]);
        pauseSpecialWasHeld=input.special || glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS ||
            glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS || (hasPad&&(pad.buttons[GLFW_GAMEPAD_BUTTON_Y]||pad.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB]));
        paused=true;match.paused=true;accumulator=0.0;captureMouse(false);gameAudio.Pause();
    };
    auto resumeMatch=[&] {
        paused=false;match.paused=false;accumulator=0.0;
        subResumeSync=pauseSubWasHeld;specialResumeBlock=pauseSpecialWasHeld;
        captureMouse(true);
    };
    std::uint32_t simulationStep=0;
    MatchPhase previousPhase=match.phase();
    bool previousAlive=match.local().state.alive,previousSpecial=false,previousJump=false;
    int previousCountdown=std::numeric_limits<int>::max();
    auto tick=[&] {
        auto& local=match.local();
        auto& input=local.input;
        controller.ValidateSuperJumpQueue(match);
        input={};
        if(benchmark) {
            const double phase=(static_cast<double>(simulationStep)+options.benchmarkSeed*31.0)/90.0;
            const glm::dvec3 waypoint=target+glm::dvec3(std::sin(phase)*span*0.12,0.0,
                std::cos(phase*0.73)*span*0.12);
            glm::dvec3 direction=waypoint-local.state.pos;
            direction.y=0.0;
            const double distance=glm::length(direction);
            if(distance>1e-6) input.move=direction/distance;
            glm::dvec3 enemy=stage.spawnPads[1]-local.state.pos;
            enemy.y=0.0;
            input.aimYaw=std::atan2(enemy.x,enemy.z);
            input.aimPitch=0.025*std::sin(phase*0.41);
            input.aimPoint=stage.spawnPads[1]+glm::dvec3(0.0,1.0,0.0);
            input.hasAimPoint=true;
            const auto action=simulationStep%240;
            input.fire=action<150;
            input.squid=action>=160&&action<210;
            input.jump=action>=72&&action<78;
            ++simulationStep;
        } else {
            ControllerInput controls;
            controls.mouseDX=std::exchange(pendingMouseX,0.0);controls.mouseDY=std::exchange(pendingMouseY,0.0);
            controls.forward=glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_UP)==GLFW_PRESS;
            controls.backward=glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_DOWN)==GLFW_PRESS;
            controls.left=glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_LEFT)==GLFW_PRESS;
            controls.right=glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT)==GLFW_PRESS;
            controls.gamepadConnected=hasPad;controls.usingPad=usingPad;
            if(hasPad){controls.leftStickX=pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X];controls.leftStickY=pad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];controls.rightStickX=pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];controls.rightStickY=pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];}
            if(jumpMapOpen)controls={};
            controls.fire=!jumpMapOpen&&(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS||(hasPad&&pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>-.4f));
            controller.Update(dt,controls,nativeApp?ui.profile().settings:PlayerSettings{},match,camera,geometry);
            const auto& pose=camera.pose();
            const glm::dvec3 forward(std::sin(camera.yaw),0,std::cos(camera.yaw));
            const glm::dvec3 right(-std::cos(camera.yaw),0,std::sin(camera.yaw));
            input.jump=!jumpMapOpen&&(glfwGetKey(window,GLFW_KEY_SPACE)==GLFW_PRESS||(hasPad&&pad.buttons[GLFW_GAMEPAD_BUTTON_A]));
            input.squid=!jumpMapOpen&&(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS||(hasPad&&pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>-.4f));
            const bool left=controls.fire;
            if(!left) fireBlocked=false;
            input.fire=!jumpMapOpen&&left&&!fireBlocked;
            const bool sub=!jumpMapOpen&&(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS||(hasPad&&pad.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER]));
            input.sub=sub;
            if(subResumeSync&&!jumpMapOpen) {
                if(!sub) {
                    input.sub=false;input.squid=true;input.move=glm::dvec3(0);input.fire=false;
                    subResumeSync=false;
                }
            }
            const bool special=!jumpMapOpen&&(glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS||(hasPad&&(pad.buttons[GLFW_GAMEPAD_BUTTON_Y]||pad.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB])));
            input.special=special&&!specialResumeBlock;
            if(specialResumeBlock&&!special) specialResumeBlock=false;
            const bool cheer=!jumpMapOpen&&(glfwGetKey(window,GLFW_KEY_C)==GLFW_PRESS||(hasPad&&pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]));
            input.cheer=cheer&&!cheerWas;
            cheerWas=cheer;
            const auto hit=geometry.raycast(pose.eye,pose.forward,90.0,true);
            input.aimPoint=hit.hit?hit.point:pose.eye+pose.forward*90.0;
            input.hasAimPoint=true;
            input.aimYaw=camera.yaw;
            input.aimPitch=camera.pitch;
            for(int digit=0;digit<=9;++digit){
                const bool held=glfwGetKey(window,GLFW_KEY_0+digit)==GLFW_PRESS;
                if(jumpMapOpen&&held&&!jumpTargetWasHeld[static_cast<std::size_t>(digit)]){
                    controller.SelectSuperJumpMapTarget(match,digit);
                }
                jumpTargetWasHeld[static_cast<std::size_t>(digit)]=held;
            }
            const bool mapTarget=!jumpMapOpen&&local.specialPose().mapTarget;
            input.hasMapTarget=mapTarget;
            if(mapTarget) input.mapTarget=input.aimPoint;
            input.specialConfirm=mapTarget&&left&&!fireBlocked&&!leftWas;
            leftWas=left;
        }
        match.tick(dt);
        if(match.phase()!=previousPhase){
            switch(match.phase()){
            case MatchPhase::Intro:screenController.MatchState("intro");break;
            case MatchPhase::Playing:screenController.MatchState("playing");break;
            case MatchPhase::Finish:screenController.MatchState("finish");break;
            case MatchPhase::Judge:screenController.MatchState("judge");break;
            case MatchPhase::Results:screenController.MatchState("results");break;
            }
            previousPhase=match.phase();
        }
        auto& player=match.local();
        if(!previousAlive&&player.state.alive){
            screenController.Respawn();
            controller.LaunchQueuedSuperJump(match);
        }
        previousAlive=player.state.alive;
        if(jumpMapOpen&&match.phase()!=MatchPhase::Playing){jumpMapOpen=false;if(!paused)captureMouse(false);}
        const auto special=player.specialPose();
        if(special.active&&!previousSpecial)screenController.Special(special.kind);
        previousSpecial=special.active;
        const auto jump=player.superJumpState();
        const bool flight=jump&&jump->flight;
        if(flight&&!previousJump)screenController.Jump("flight");
        previousJump=flight;
        if(match.phase()==MatchPhase::Playing){
            const int count=static_cast<int>(std::ceil(match.remaining()));
            if(count<=10&&count<previousCountdown)screenController.Countdown(count);
            previousCountdown=count;
        }
        if(!benchmark) {
            if(match.phase()==MatchPhase::Judge) {
                judgeElapsed+=dt;
                if(judgeElapsed>=2.6) match.finishJudging();
            } else judgeElapsed=0.0;
            if(match.phase()==MatchPhase::Results&&!resultReported) {
                if(const auto& result=match.result())
                    std::cout<<"MATCH_RESULT winner="<<(result->winner==0?"orange":"blue")
                             <<" orange="<<result->coverage[0]*100.0<<" blue="<<result->coverage[1]*100.0<<'\n';
                captureMouse(false);
                resultReported=true;
                if(nativeApp&&store&&!options.practice&&match.result()){
                    auto profile=ui.profile();
                    MatchRewardInput reward;reward.matchId=matchId;reward.mode=options.mode;
                    reward.won=match.result()->boss?match.result()->boss->win:match.result()->winner==match.local().team();
                    reward.turf=match.local().turfPainted();reward.splats=match.local().splats();
                    if(match.result()->zones){reward.zoneTurf=match.local().zoneTurfPainted();reward.knockout=match.result()->zones->reason=="knockout";}
                    if(match.boss()){reward.bossDamage=match.boss()->squadDamage()[0];reward.splats+=match.boss()->squadSplats()[0];}
                    const auto earned=ApplyMatchReward(profile,reward);
                    ui.SetProfile(profile);ui.SetReward(earned);
                    try{store->save(profile);ui.SetSaveError({});}
                    catch(const std::exception& error){ui.SetSaveError(error.what());}
                }
            }
        }
        if(!benchmark||options.benchmarkPlayer) camera.tick(dt,CameraActor(local),geometry);
        const auto fxCamera=benchmark&&!options.benchmarkPlayer?overviewEye:camera.pose().eye;
        worldEffects.Update(dt,match.world(),fxCamera);
        deviceRenderer.Update(dt,match.world(),fxCamera);
        const auto& events=worldEffects.lastEvents();
        projectileRenderer.Update(dt,match.world(),events);
        const auto forward=benchmark&&!options.benchmarkPlayer?glm::normalize(target-fxCamera):camera.pose().forward;
        gameAudio.Update(dt,match,events,{fxCamera.x,fxCamera.y,fxCamera.z},{forward.x,forward.y,forward.z});
        pendingVisualEvents.insert(pendingVisualEvents.end(),events.begin(),events.end());
    };
    const CharacterLightState lighting=environment.Lights();
    for(std::size_t i=0;i<match.actors().size();++i) {
        auto* actor=match.actors()[i].get();
        const auto& roster=match.roster()[i];
        auto live=std::make_unique<LiveCharacter>();
        live->style=roster.style;
        live->pose=std::make_unique<CharacterPose>(art.data(),art.data().rig(
            roster.style.hair,roster.style.hat,roster.style.brows),roster.name,actor->mainId());
        live->pose->setStyle(roster.style);
        live->pose->setSub(actor->subId());
        live->pose->setColor(teamColors[static_cast<std::size_t>(actor->team())]);
        live->pose->trigger(CharacterTrigger{"spawn"});
        live->scene.inWorld=true;
        live->scene.isLocal=roster.local;
        live->scene.team=actor->team();
        live->scene.lifeLevel=2;
        live->scene.cameraPosition=overviewEye;
        live->scene.footstep=[actor,&gameAudio,&worldEffects](int,glm::dvec3 position,double speed){worldEffects.Footstep(*actor,position,speed);gameAudio.Footstep(*actor,{position.x,position.y,position.z},speed);};
        live->scene.actors.resize(match.actors().size());
        live->scene.raycast=[&geometry](glm::dvec3 origin,glm::dvec3 direction,double distance) {
            const auto hit=geometry.raycast(origin,direction,distance,true);
            return CharacterGroundHit{hit.hit,hit.dist,hit.point,hit.normal};
        };
        auto* state=live.get();
        auto* pose=live->pose.get();
        actor->setMuzzleSource([pose](int side){return side>0?pose->frame().muzzleLeft:pose->frame().muzzle;});
        characters.push_back(std::move(live));
        actor->setPoseTriggerSink([pose,actor,&gameAudio](const CharacterTrigger& event) {
            pose->trigger(event);
            gameAudio.ActorTrigger(*actor,event);
        });
        actor->setPoseFrameSink([&,actor,state,pose,i](double step) {
            const auto& current=actor->state;
            const auto combat=actor->combatPose();
            const auto special=actor->specialPose();
            CharacterAnimState animation;
            animation.position=actor->visualPos();
            if(special.kind=="crab"&&!special.roll) animation.position.y+=special.seat;
            animation.yaw=current.yaw;
            animation.aimPitch=actor->input.aimPitch;
            animation.speed=glm::length(glm::dvec2(current.vel.x,current.vel.z));
            const double c=std::cos(current.yaw),s=std::sin(current.yaw);
            const double horizontalSpeed=std::hypot(current.vel.x,current.vel.z);
            if(horizontalSpeed>0.1) {
                animation.localMove={(-current.vel.x*c+current.vel.z*s)/horizontalSpeed,
                                     (current.vel.x*s+current.vel.z*c)/horizontalSpeed};
            }
            animation.vy=current.vel.y;
            animation.hp=std::clamp(current.hp/100.0,0.0,1.0);
            animation.form=current.squid?(current.climbing?CharacterForm::Climb:
                current.submerged?CharacterForm::Swim:CharacterForm::Squid):CharacterForm::Kid;
            if(special.body||special.noSquid) animation.form=CharacterForm::Kid;
            animation.grounded=current.grounded;
            animation.firing=combat.firing||special.firing>0.0;
            animation.rolling=combat.rolling;
            animation.subAim=combat.subAim||special.raise;
            animation.subPropHidden=special.kind=="booyah"&&special.active&&!special.thrown;
            animation.lowInk=current.ink<18.0;
            animation.inEnemyInk=current.grounded&&current.groundTeam==2&&!current.submerged&&!special.inkProof;
            animation.invuln=current.invuln>0.0;
            animation.visible=current.alive&&special.kind!="kraken"&&!(special.kind=="crab"&&special.roll);
            animation.charge=combat.charge;
            animation.ink=current.ink/100.0;
            animation.special=special.active?std::clamp(special.remaining,0.0,1.0):
                std::clamp(current.specialCharge/actor->weapon().number("specialCost",190.0),0.0,1.0);
            const double hurt=std::clamp(std::max(current.hurtFlash,1.0-current.hp/100.0)*
                (current.hp<100.0?1.0:0.0),0.0,1.0);
            pose->setHurt(hurt,teamColors[static_cast<std::size_t>(1-actor->team())]);
            CharacterRunnerState runner;
            runner.charging=combat.charging;
            runner.firing=combat.firing;
            runner.streaming=combat.streaming;
            runner.dodge=combat.dodgeTime>0.0;
            runner.lockT=combat.lockTime;
            runner.charge=combat.charge;
            runner.streamT=combat.burstTime;
            runner.hand=combat.hand;
            runner.sinceHand=combat.sinceHand;runner.hasSinceHand=true;
            runner.leaping=combat.leaping;
            runner.open=runner.canopyOpen=combat.canopyOpen;
            runner.canopyGrow=combat.canopyGrow;
            runner.canopyHp=combat.canopyHp;runner.canopyHpMax=combat.canopyHpMax;
            runner.canopyShake=combat.canopyShake;runner.canopyFlash=combat.canopyFlash;
            runner.canopyRegrowK=combat.canopyRegrowK;runner.canopyOpening=combat.canopyOpening;
            runner.canopyState=combat.canopyState;
            runner.bladeFlashTime=combat.bladeFlashTime;
            runner.mittsLandTime=combat.mittsLandTime;
            animation.runner=runner;

            auto& scene=state->scene;
            scene.cameraPosition=!benchmark||options.benchmarkPlayer ? camera.pose().eye : overviewEye;
            for(std::size_t j=0;j<match.actors().size();++j) {
                const auto& other=*match.actors()[j];
                scene.actors[j]={j+1,other.team(),j!=i&&other.state.alive,other.visualPos()};
            }
            pose->update(step,animation,scene);
        });
    }

    std::unique_ptr<PerformanceCapture> capture;
    if(!options.benchmarkReport.empty())
        capture=std::make_unique<PerformanceCapture>(options.benchmarkReport,std::string(stage.id),options.mode,options.benchmarkSeed,
            options.width,options.height,options.benchmarkWarmup,options.benchmarkFrames,options.benchmarkPlayer,graphics);
    if(!benchmark&&!options.frames) captureMouse(true);
    glEnable(GL_DEPTH_TEST);
    double lodPrevious=glfwGetTime();
    while(!glfwWindowShouldClose(window)) {
        if(capture) capture->beginFrame();
        glfwPollEvents();
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if(width<=0||height<=0) {if(capture)throw std::runtime_error("Benchmark framebuffer is unavailable");glfwWaitEventsTimeout(0.05);continue;}
        if(capture && (width!=1920 || height!=1080)) throw std::runtime_error("Benchmark framebuffer must be exactly 1920x1080; got "+std::to_string(width)+"x"+std::to_string(height));
        const double now=glfwGetTime();
        const double visualDt=paused?0.0:(capture||options.frames)?dt:std::clamp(now-previous,0.0,0.1);
        audio.Update();
        if(!benchmark&&!options.frames) {
            hasPad=false;
            for(int joystick=GLFW_JOYSTICK_1;joystick<=GLFW_JOYSTICK_LAST;++joystick)
                if(glfwGetGamepadState(joystick,&pad)){hasPad=true;break;}
            if(hasPad){
                for(int i=0;i<4;++i)if(std::abs(pad.axes[i])>.15f)usingPad=true;
                for(const auto button:pad.buttons)if(button)usingPad=true;
                if(pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>-.4f||pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>-.4f)usingPad=true;
            }
            const bool escape=glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS||(hasPad&&pad.buttons[GLFW_GAMEPAD_BUTTON_START]);
            if(match.phase()==MatchPhase::Playing && escape&&!escapeWas) {
                if(!paused)pauseMatch();else if(ui.preview().screen==UiScreen::Pause)resumeMatch();
            }
            escapeWas=escape;
            const bool gear=options.practice&&glfwGetKey(window,GLFW_KEY_L)==GLFW_PRESS;
            if(gear&&!gearWas&&!paused){pauseMatch();ui.ShowPracticeGear();}
            gearWas=gear;
            if(!paused&&match.phase()==MatchPhase::Playing&&!glfwGetWindowAttrib(window,GLFW_FOCUSED)) pauseMatch();
            const bool mapHeld=!paused&&match.phase()==MatchPhase::Playing&&!match.local().specialPose().mapTarget&&
                (glfwGetKey(window,GLFW_KEY_TAB)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_M)==GLFW_PRESS);
            if(mapHeld!=jumpMapOpen){
                jumpMapOpen=mapHeld;
                if(jumpMapOpen)captureMouse(false);
                else if(!paused&&match.phase()==MatchPhase::Playing)captureMouse(true);
            }
            if(!paused && match.phase()==MatchPhase::Playing) {
                double x=0.0,y=0.0;
                glfwGetCursorPos(window,&x,&y);
                const auto settings=nativeApp?ui.profile().settings:PlayerSettings{};
                pendingMouseX+=x-mouseX;pendingMouseY+=y-mouseY;
                if(x!=mouseX||y!=mouseY)usingPad=false;
                camera.horizontalFovDegrees=settings.fov;
                camera.shake=settings.reducedMotion?0:settings.cameraShake;
                mouseX=x;mouseY=y;
            }
        }
        if(capture||options.frames) tick();
        else if(!paused) {
            accumulator+=std::clamp(now-previous,0.0,0.1);
            while(accumulator+1e-12>=dt) {tick();accumulator-=dt;}
        }
        previous=now;
        renderer.UploadInk(stage,paint,visualDt);
        const glm::dvec3 renderEye=benchmark&&!options.benchmarkPlayer?overviewEye:camera.pose().eye;
        renderer.UpdateSwimWake(visualDt,match.world(),renderEye);
        const glm::dvec3 renderForward=benchmark&&!options.benchmarkPlayer?glm::normalize(target-overviewEye):camera.pose().forward;
        glm::mat4 projection,view;
        if(benchmark&&!options.benchmarkPlayer) {
            projection=glm::perspective(glm::radians(48.0f),static_cast<float>(width)/height,0.15f,6500.0f);
            view=glm::lookAt(glm::vec3(overviewEye),glm::vec3(target),glm::vec3(0,1,0));
        } else {
            projection=glm::perspective(glm::radians(static_cast<float>(camera.pose().verticalFovDegrees)),static_cast<float>(width)/height,0.15f,6500.0f);
            view=glm::lookAt(glm::vec3(renderEye),glm::vec3(renderEye+renderForward),glm::vec3(0,1,0));
        }
        const auto vp=projection*view;
        const double lodStep=benchmark?dt:paused?0.0:std::clamp(now-lodPrevious,0.0,0.1);
        lodPrevious=now;
        environment.Update(lodStep);
        std::vector<CharacterLodFrame> lodFrames(characters.size());
        for(std::size_t i=0;i<characters.size();++i){
            auto& character=*characters[i];const auto& pose=character.pose->frame();
            lodFrames[i]=character.lod.update(CharacterLod::screenPixels(pose.root.matrix(),renderEye,
                projection[1][1],height),character.scene.isLocal,lodStep);
        }
        std::vector<DeviceActorPose> deviceActors;deviceActors.reserve(characters.size());
        for(std::size_t i=0;i<characters.size();i++){const auto& character=*characters[i];const auto special=match.actors()[i]->specialPose();deviceActors.push_back({match.actors()[i].get(),&character.pose->frame(),&character.style,special.active&&special.kind=="jetpack"?art.attachment(character.pose->frame(),character.style,"tank/group"):glm::dmat4(1)});}
        int shadowDraws=0;
        if(graphics.shadows){
        directionalShadow.Begin(stage.bounds,glm::dvec3(lighting.sunDirection),graphics.shadowMapSize);
        GLboolean colorMask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        shadowDraws=renderer.DrawShadow(glm::mat4(directionalShadow.matrix()));
        shadowDraws+=environment.DrawShadow(directionalShadow.matrix());
        const CharacterShadowState shadowCaster{true,0,directionalShadow.matrix(),0.0,
            directionalShadow.bias(),directionalShadow.normalBias(),directionalShadow.radius(),directionalShadow.size()};
        for(std::size_t i=0;i<characters.size();++i){
            const auto& actor=*match.actors()[i];auto& character=*characters[i];const auto& pose=character.pose->frame();const auto& lod=lodFrames[i];
            const auto special=actor.specialPose();const bool hideTank=special.active&&special.kind=="jetpack";
            if(lod.transitioning){
                const double fade=std::max(1e-4,lod.fade);
                shadowDraws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),CharacterTierName(lod.tier),
                    directionalShadow.view(),directionalShadow.projection(),renderEye,lighting,{fade,1.0,lod.fade<0.5,hideTank},&shadowCaster));
                shadowDraws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),CharacterTierName(lod.to),
                    directionalShadow.view(),directionalShadow.projection(),renderEye,lighting,{fade,-1.0,lod.fade>=0.5,hideTank},&shadowCaster));
            }else shadowDraws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),CharacterTierName(lod.tier),
                directionalShadow.view(),directionalShadow.projection(),renderEye,lighting,{0,1,true,hideTank},&shadowCaster));
        }
        if(bossRenderer&&match.boss())shadowDraws+=bossRenderer->DrawShadow(*match.boss(),glm::mat4(directionalShadow.matrix()));
        shadowDraws+=deviceRenderer.DrawShadow(directionalShadow.matrix(),deviceActors);
        glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        directionalShadow.End();
        }

        for(const auto& event:pendingVisualEvents){
            if(event.kind=="bulletImpact")screenController.Impact(event.pos,event.team==match.local().team(),false);
            else if(event.kind=="explosion"||event.kind=="burst"||event.kind=="slamWave"||event.kind=="stormStart"){
                const auto color=event.team==0?teamColors[0]:teamColors[1];
                screenController.Blast(event.pos,std::clamp(event.radius/5.0,0.15,1.2),color);
            }
        }
        pendingVisualEvents.clear();
        const auto& local=match.local();const auto& player=local.state;const auto combat=local.combatPose();const auto special=local.specialPose();
        ScreenEffectsInput screenInput;screenInput.alive=player.alive;screenInput.paused=paused;screenInput.onEnemy=player.groundTeam==2;
        screenInput.specialActive=special.active;screenInput.charging=combat.charging;screenInput.hp=player.hp;
        screenInput.reducedMotion=ui.setup().reducedMotion;
        screenInput.horizontalSpeed=glm::length(glm::dvec2(player.vel.x,player.vel.z));screenInput.verticalSpeed=player.vel.y;
        screenInput.charge=combat.charge;screenInput.invulnerability=player.invuln;screenInput.remaining=match.remaining();
        screenInput.width=width;screenInput.height=height;screenInput.ownColor=teamColors[static_cast<std::size_t>(local.team())];
        screenInput.enemyColor=teamColors[static_cast<std::size_t>(1-local.team())];screenInput.cameraPosition=renderEye;
        screenInput.viewProjection=glm::dmat4(vp);screenInput.actorPosition=local.visualPos();
        screenInput.form=player.climbing?"climb":player.submerged?"swim":player.squid?"squid":"kid";
        switch(match.phase()){
        case MatchPhase::Intro:screenInput.phase="intro";break;case MatchPhase::Playing:screenInput.phase="playing";break;
        case MatchPhase::Finish:screenInput.phase="finish";break;case MatchPhase::Judge:screenInput.phase="judge";break;
        case MatchPhase::Results:screenInput.phase="results";break;}
        const auto jump=local.superJumpState();if(jump){screenInput.jumpPhase=jump->flight?"flight":jump->home?"home":"";screenInput.jumpTime=jump->elapsed;screenInput.jumpDuration=jump->duration;}
        ScreenEffects screen=screenController.Update(lodStep,screenInput);

        const int renderWidth=std::max(1,int(std::round(width*graphics.renderScale))),renderHeight=std::max(1,int(std::round(height*graphics.renderScale)));
        pipeline.Begin(renderWidth,renderHeight,projection,glm::inverse(view),0.15f,6500.0f);
        const int reflectionDraws=environment.RenderReflection(view,projection,glm::vec3(renderEye),renderWidth,renderHeight,[&](const auto& reflectedView,const auto& reflectedProjection,const auto& reflectedEye){
            return renderer.Draw(glm::dmat4(reflectedView),glm::dmat4(reflectedProjection),glm::dvec3(reflectedEye),lighting,false,graphics.shadows?&directionalShadow:nullptr,true);
        });
        glViewport(0,0,renderWidth,renderHeight);
        glClearColor(0.64f,0.81f,0.9f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        int draws=shadowDraws+reflectionDraws+renderer.Draw(glm::dmat4(view),glm::dmat4(projection),renderEye,lighting,false,graphics.shadows?&directionalShadow:nullptr);
        draws+=environment.DrawSea(view,projection,glm::vec3(renderEye),graphics.shadows?&directionalShadow:nullptr);
        draws+=environment.DrawScenery(view,projection,glm::vec3(renderEye));
        draws+=environment.DrawSky(view,projection,glm::vec3(renderEye));
        CharacterShadowState receiver{false,graphics.shadows?directionalShadow.texture():0,directionalShadow.matrix(),1.0,
            directionalShadow.bias(),directionalShadow.normalBias(),directionalShadow.radius(),directionalShadow.size()};
        for(std::size_t i=0;i<characters.size();++i) {
            const auto& actor=*match.actors()[i];
            auto& character=*characters[i];
            const auto& pose=character.pose->frame();
            const auto& lod=lodFrames[i];
            const auto activeSpecial=actor.specialPose();const bool hideTank=activeSpecial.active&&activeSpecial.kind=="jetpack";
            if(lod.transitioning) {
                const double fade=std::max(1e-4,lod.fade);
                draws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),
                    CharacterTierName(lod.tier),glm::dmat4(view),glm::dmat4(projection),renderEye,lighting,
                    {fade,1.0,lod.fade<0.5,hideTank},&receiver));
                draws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),
                    CharacterTierName(lod.to),glm::dmat4(view),glm::dmat4(projection),renderEye,lighting,
                    {fade,-1.0,lod.fade>=0.5,hideTank},&receiver));
            } else draws+=static_cast<int>(art.draw(pose,character.style,actor.mainId(),actor.subId(),
                    CharacterTierName(lod.tier),glm::dmat4(view),glm::dmat4(projection),renderEye,lighting,{0,1,true,hideTank},&receiver));
        }
        if(bossRenderer&&match.boss()){
            draws+=bossRenderer->Draw(*match.boss(),view,projection,glm::vec3(renderEye),glm::vec3(teamColors[0]),&lighting);
            draws+=bossRenderer->DrawHazards(*match.boss(),geometry,vp,glm::vec3(teamColors[0]));
        }
        if(zoneRenderer&&match.zones()&&match.phase()!=MatchPhase::Results)draws+=zoneRenderer->Draw(*match.zones(),view,projection,renderEye,0,match.zones()->ended(),match.world().time());
        draws+=worldEffects.Draw(view,projection,glm::vec3(renderEye),&lighting);
        draws+=projectileRenderer.Draw(view,projection,renderEye,lighting);
        draws+=deviceRenderer.Draw(view,projection,renderEye,lighting,deviceActors,&receiver);
        if(graphics.postEffects)draws+=lensField.Render(screenController.lens(),renderWidth,renderHeight);
        screen.lensTexture=lensField.texture();screen.lensTexel=lensField.texel();
        screen.enabled=screen.enabled&&graphics.postEffects;
        draws+=pipeline.End(GradeSettings::Theme(std::string(stage.theme)),screen,width,height);
        if(!capture||options.benchmarkPlayer) {
            if(showcase&&match.phase()==MatchPhase::Results&&match.result()){
                std::vector<std::size_t> order;
                for(std::size_t i=0;i<match.actors().size();++i)if(match.actors()[i]->team()==match.local().team())order.push_back(i);
                if(match.boss())std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return match.boss()->squadDamage()[a]>match.boss()->squadDamage()[b];});
                std::vector<PlayerShowcase::PodiumPlayer> podium;
                for(const auto i:order){if(podium.size()==4)break;const auto& a=*match.actors()[i];podium.push_back({match.roster()[i].name,i==0?ui.profile().style:match.roster()[i].style,a.mainId(),a.subId()});}
                const bool won=match.result()->boss?match.result()->boss->win:match.result()->winner==match.local().team();
                const auto p=ui.preview();
                if(p.type==UiPreviewType::Podium&&p.width>0&&p.height>0)
                    showcase->drawPodium(podium,won,{int(p.x*width),int((1-p.y-p.height)*height),int(p.width*width),int(p.height*height)},now,ui.profile().settings.reducedMotion);
            }
            const bool gearOpen=options.practice&&ui.preview().screen==UiScreen::PracticeGear;
            std::vector<UiJumpMapTarget> jumpTargets;
            if(jumpMapOpen){
                for(const auto& target:controller.SuperJumpMapTargets(match))
                    jumpTargets.push_back({target.key,target.x,target.z,target.label,target.available});
            }
            ui.SetJumpMap(jumpMapOpen,std::move(jumpTargets),controller.QueuedSuperJumpKey(),controller.QueuedSuperJumpLabel());
            if(gearOpen&&showcase){const auto p=ui.preview();if(p.width>0&&p.height>0)showcase->drawPlayer(ui.profile(),ui.setup().main,ui.setup().sub,{int(p.x*width),int((1-p.y-p.height)*height),int(p.width*width),int(p.height*height)},now,ui.profile().settings.reducedMotion);}
            const auto hudAction=gearOpen?ui.RenderMenu(width,height):ui.RenderHud(stage,paint,match,width,height,paused,nativeApp,options.practice);
            const int jumpMapSelection=ui.TakeJumpMapSelection();
            if(jumpMapSelection>=0)controller.SelectSuperJumpMapTarget(match,jumpMapSelection);
            if(hudAction==UiAction::Resume){
                if(options.practice){
                    auto& local=match.local();const auto& profile=ui.profile();
                    local.setLoadout(profile.weapon,profile.sub,profile.special);
                    auto& character=*characters.front();character.style=profile.style;
                    *character.pose=CharacterPose(art.data(),art.data().rig(profile.style.hair,profile.style.hat,profile.style.brows),profile.name,profile.weapon);
                    character.pose->setStyle(profile.style);character.pose->setSub(profile.sub);character.pose->setColor(teamColors[0]);
                }
                resumeMatch();
            }
            else if(hudAction==UiAction::StageSelect)matchExit=MatchExit::StageSelect;
            else if(hudAction==UiAction::Rematch)matchExit=MatchExit::Rematch;
            else if(hudAction==UiAction::PracticeStage)matchExit=MatchExit::PracticeStage;
            else if(hudAction==UiAction::PracticeReset){paint.clear();worldEffects.Clear();screenController.Reset();}
            else if(hudAction==UiAction::PracticeFill){auto& s=match.local().state;s.hp=100;s.ink=100;s.specialCharge=match.local().weapon().number("specialCost",190);}
            else if(hudAction==UiAction::PracticeSpawn){match.local().spawnAt(stage.spawnPads[0],0);camera.reset(CameraActor(match.local()));}
            else if(hudAction==UiAction::Close)glfwSetWindowShouldClose(window,1);
            if(matchExit!=MatchExit::Close) break;
        }
        if(options.frames&&++frame>=options.frames) {
            if(!options.matchCapture.empty())Capture(options.matchCapture,width,height);
            break;
        }
        if(capture) capture->endGpu();
        glfwSwapBuffers(window);
        if(nativeApp&&!options.frames)LimitFrame(now,ui.profile().settings.fpsCap);
        if(capture) {
            const PerformanceCapture::FrameCounts counts{
                static_cast<int>(match.actors().size()),draws,
                static_cast<int>([&]{const auto c=worldEffects.counts();return c.drops+c.puffs+c.glows+c.rings+c.shells+c.beams+c.world+screenController.lens().particles().size();}()),
                static_cast<int>(match.world().projectiles().size())};
            if(capture->endFrame(counts)) {capture->write();break;}
        }
    }
    if(!benchmark) captureMouse(false);
    gameAudio.EndMatch();
    audio.Clear();
    return matchExit;
}

void NativeMenu(GLFWwindow* window,const Options& options,const StageData& initial,
                const std::filesystem::path& characterAssets,GameUi& ui,AudioEngine& audio,GameAudio& gameAudio) {
    ProfileStore store;
    WindowMode windowMode;
    try{ui.SetProfile(store.load());}
    catch(const std::exception& error){ui.SetProfile({});ui.SetSaveError(error.what());}
    audio.SetVolumes(float(ui.profile().settings.master),float(ui.profile().settings.music),float(ui.profile().settings.sfx));
    windowMode.apply(window,ui.profile().settings.fullscreen);
    SetInkPalette(ProfilePaletteColors(ui.profile().settings.palette,ui.profile().settings.colorblind));
    ui.SetProfileSink([&](const PlayerProfile& profile){
        windowMode.apply(window,profile.settings.fullscreen);
        try{store.save(profile);ui.SetSaveError({});}
        catch(const std::exception& error){ui.SetSaveError(error.what());}
    });
    const StageData* selected=&initial;
    ui.ShowTitle(std::string(initial.id));
    int loadingWidth=0,loadingHeight=0;glfwGetFramebufferSize(window,&loadingWidth,&loadingHeight);
    if(loadingWidth>0&&loadingHeight>0){
        glViewport(0,0,loadingWidth,loadingHeight);glClearColor(.96f,.97f,.95f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);ui.RenderMenu(loadingWidth,loadingHeight);glfwSwapBuffers(window);
    }
    PlayerShowcase showcase(characterAssets);
    gameAudio.Menu(true);
    int frame=0;
    while(!glfwWindowShouldClose(window)) {
        const double frameStart=glfwGetTime();
        SetInkPalette(ProfilePaletteColors(ui.profile().settings.palette,ui.profile().settings.colorblind));
        glfwPollEvents();
        audio.Update();
        int width=0,height=0;
        glfwGetFramebufferSize(window,&width,&height);
        if(width<=0||height<=0) {glfwWaitEventsTimeout(0.05);continue;}
        glViewport(0,0,width,height);
        glClearColor(.96f,.97f,.95f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        const auto preview=ui.preview();
        if(preview.type!=UiPreviewType::None&&preview.type!=UiPreviewType::Podium&&preview.width>0&&preview.height>0)
            showcase.drawPlayer(ui.profile(),ui.setup().main,ui.setup().sub,
                {int(preview.x*width),int((1-preview.y-preview.height)*height),int(preview.width*width),int(preview.height*height)},frameStart,ui.profile().settings.reducedMotion);
        const auto action=ui.RenderMenu(width,height);
        const auto& selection=ui.setup();
        for(const auto& candidate:ReferenceStages())if(candidate.mode==(selection.mode=="zones"?"zones":"turf")&&!candidate.onlineOnly&&candidate.id==selection.stage)selected=&candidate;
        if(action==UiAction::Close) {glfwSetWindowShouldClose(window,1);break;}
        if(options.frames&&++frame>=options.frames) {
            if(!options.uiCapture.empty())Capture(options.uiCapture,width,height);
            std::cout<<"NATIVE_MENU state=stage-select stage="<<selected->id<<" frames="<<frame<<'\n';
            break;
        }
        glfwSwapBuffers(window);
        if(action==UiAction::Start||action==UiAction::Practice) {
            Options game=options;
            game.stage=std::string(selected->id);
            game.practice=action==UiAction::Practice||selection.mode=="practice";
            game.mode=game.practice?"turf":selection.mode;game.main=selection.main;game.sub=selection.sub;game.special=selection.special;game.difficulty=selection.difficulty;
            game.duration=game.practice?0:selection.duration;
            game.match=true;
            game.native=false;
            gameAudio.Menu(false);
            StageData gameStage=*selected;gameStage.theme=selection.theme=="dusk"?"sunset":"day";
            MatchExit exit;
            do{exit=MatchLoop(window,game,gameStage,characterAssets,ui,audio,gameAudio,true,&store,&showcase);}while(exit==MatchExit::Rematch&&!glfwWindowShouldClose(window));
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
        const auto options=ParseOptions(argc,argv);
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
        if(!std::filesystem::exists(assetRoot/"reference/characters"))assetRoot=std::filesystem::current_path()/"assets";
        if(options.native||options.match) {
            const auto assets=assetRoot/"reference/characters";
            AudioEngine audio(assetRoot/"audio");
            GameAudio gameAudio(audio);
            if(!audio.Initialize())std::cerr<<"AUDIO unavailable: "<<audio.lastError()<<'\n';
            GameUi ui(window,assetRoot);
            ui.SetSoundSink([&](const std::string& name){audio.Play(name);});
            ui.SetVolumeSink([&](float master,float music,float sfx){audio.SetVolumes(master,music,sfx);});
            if(options.native)NativeMenu(window,options,*found,assets,ui,audio,gameAudio);
            else MatchLoop(window,options,*found,assets,ui,audio,gameAudio);
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
