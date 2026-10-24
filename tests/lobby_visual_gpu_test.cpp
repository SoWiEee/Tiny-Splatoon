#include "../src/game/LobbyPresentation.hpp"
#include "../src/gui/GameUi.hpp"
#include "../src/render/LobbyEffects.hpp"
#include "../src/render/LobbyRenderer.hpp"
#include "../src/render/LobbyView.hpp"
#include "../src/render/PlayerShowcase.hpp"
#include "../src/render/RenderPipeline.hpp"
#include "../src/render/TeamPalette.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace restoration;
using Pixels = std::vector<unsigned char>;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void checkGl(const std::string& where) {
    const GLenum error = glGetError();
    check(error == GL_NO_ERROR, where + " generated OpenGL error " + std::to_string(error));
}

network::Member makeMember(network::MemberId id, int team, std::string name) {
    network::Member member;
    member.id = id;
    member.team = team;
    member.autoTeam = false;
    member.ready = true;
    member.name = std::move(name);
    return member;
}

network::RoomView makeRoom(std::size_t count) {
    network::RoomView view;
    view.phase = network::RoomPhase::Lobby;view.code="ABCDE";
    view.selfId = 100;
    view.hostId = 100;
    view.members.push_back(makeMember(100, 0, "Local"));
    for (std::size_t i = 1; i < count; ++i)
        view.members.push_back(makeMember(static_cast<network::MemberId>(100 + i), i < 4 ? 0 : 1,
                                          "Player " + std::to_string(i + 1)));
    return view;
}

void applyRippleEvents(LobbyRenderer& scene, const std::vector<LobbyPresentationEvent>& events, double time) {
    for (const auto& event : events)
        if (event.kind == "land" || event.kind == "pop" || event.kind == "dive" || event.kind == "superjump")
            scene.AddRipple(event.position, time, event.strength);
}

std::vector<LobbyPresentationEvent> initialize(LobbyPresentation& presentation, const network::RoomView& view,
                                               const PlayerProfile& profile, LobbyRenderer& scene,
                                               LobbyEffects& effects, double time) {
    presentation.Update(view, profile, 0.0);
    auto events = presentation.takeEvents();
    effects.Update(presentation.actors(), events, 0.0, time);
    applyRippleEvents(scene, events, time);
    return events;
}

std::vector<LobbyPresentationEvent> advance(LobbyPresentation& presentation, const network::RoomView& view,
                                            const PlayerProfile& profile, LobbyRenderer& scene,
                                            LobbyEffects& effects, double& time, double duration) {
    std::vector<LobbyPresentationEvent> latest;
    while (duration > 1e-8) {
        const double dt = std::min(0.1, duration);
        presentation.Update(view, profile, dt);
        time += dt;
        duration -= dt;
        latest = presentation.takeEvents();
        effects.Update(presentation.actors(), latest, dt, time);
        applyRippleEvents(scene, latest, time);
    }
    return latest;
}

std::size_t coloredPixels(const Pixels& pixels) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const auto lo = std::min({pixels[i], pixels[i + 1], pixels[i + 2]});
        const auto hi = std::max({pixels[i], pixels[i + 1], pixels[i + 2]});
        if (hi > 32 && hi - lo > 12) ++count;
    }
    return count;
}

std::size_t pixelDifferences(const Pixels& first, const Pixels& second) {
    check(first.size() == second.size(), "Lobby comparison captures have different sizes");
    std::size_t count = 0;
    for (std::size_t i = 0; i < first.size(); i += 4)
        if (std::abs(int(first[i]) - int(second[i])) + std::abs(int(first[i + 1]) - int(second[i + 1])) +
            std::abs(int(first[i + 2]) - int(second[i + 2])) > 24) ++count;
    return count;
}

void savePng(const std::filesystem::path& path, int width, int height, const Pixels& pixels) {
    std::filesystem::create_directories(path.parent_path());
    stbi_flip_vertically_on_write(1);
    check(stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0,
          "Could not save lobby capture: " + path.string());
}

Pixels render(LobbyRenderer& scene, LobbyEffects& effects, PlayerShowcase& showcase, RenderPipeline& pipeline,
              const std::vector<LobbyActorState>& actors, const std::vector<LobbyPresentationEvent>& events,
              bool hub, double time, int width, int height, bool drawActors,
              bool drawLobbyEffects = true, bool requireEffects = false) {
    const auto camera = FitLobbyView(scene.Layout(), hub, width, height);
    const auto& sourceCamera = hub ? scene.Layout().hubCamera : scene.Layout().lobbyCamera;
    pipeline.Begin(width, height, glm::mat4(camera.projection), glm::mat4(glm::inverse(camera.view)),
                   static_cast<float>(sourceCamera.nearPlane), static_cast<float>(sourceCamera.farPlane));
    glClearColor(0.025f, 0.035f, 0.06f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if(drawActors)showcase.updateLobby(actors,time,camera.eye,false,events);
    int shadowPasses=0;
    const int draws = scene.Draw(camera.view, camera.projection, camera.eye, time, InkColors[0], InkColors[1],
        [&](const glm::dmat4& reflectedView,const glm::dmat4& reflectedProjection,glm::dvec3 reflectedEye){
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
            const auto shadow=scene.KeyShadowState();
            if(drawActors)showcase.drawLobby(actors,{viewport[0],viewport[1],viewport[2],viewport[3]},reflectedView,reflectedProjection,reflectedEye,scene.Lights(),[&](glm::dvec3 p){return scene.LightingAt(p);},&shadow);
            if(drawLobbyEffects)effects.Draw(reflectedView,reflectedProjection,reflectedEye,scene.Lights(),time);
        },[&](const glm::dmat4& lightView,const glm::dmat4& lightProjection,glm::dvec3 lightEye,const CharacterShadowState& shadow){
            ++shadowPasses;check(shadow.depthOnly,"Lobby key shadow callback must use the depth-only character path");
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
            if(drawActors)showcase.drawLobby(actors,{viewport[0],viewport[1],viewport[2],viewport[3]},lightView,lightProjection,lightEye,scene.Lights(),{},&shadow);
        });
    const auto shadow=scene.KeyShadowState();
    check(shadow.texture?shadowPasses==1:shadowPasses==0,"Lobby key shadow callback count must match the enabled pass");
    check(draws > 0, "Source lobby scene emitted no draw calls");
    if (drawActors)
        showcase.drawLobby(actors, {0, 0, width, height}, camera.view, camera.projection, camera.eye, scene.Lights(),[&](glm::dvec3 p){return scene.LightingAt(p);},&shadow);
    const int effectDraws = drawLobbyEffects ? effects.Draw(camera.view, camera.projection, camera.eye, scene.Lights(), time) : 0;
    if (requireEffects) check(effectDraws > 0, "Lobby effects emitted no draw calls for the sampled animation");
    ScreenEffects screenEffects;
    check(pipeline.End(GradeSettings{}, screenEffects, width, height) > 0, "Lobby post pipeline emitted no draw calls");
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    Pixels pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    checkGl("Lobby frame");
    const auto pixelCount=static_cast<std::size_t>(width)*height;
    check(coloredPixels(pixels)>pixelCount/30,"Lobby frame contains too few colored pixels");
    std::size_t clipped=0;
    for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>240&&pixels[i+1]>240&&pixels[i+2]>240)++clipped;
    check(clipped<pixelCount/4,"Lobby lighting clips more than a quarter of the blue-hour scene");
    return pixels;
}

void checkSettledHeads(const LobbyLayout& layout, const std::vector<LobbyActorState>& actors, int width, int height) {
    check(actors.size() == 8, "Settled lobby must contain all eight actors");
    const auto camera = FitLobbyView(layout, false, width, height);
    const double u = std::min(width * 0.01, height * 0.017778);
    const double left = std::max(width * 0.34, 390.0) + 1.2 * u;
    const double right = width - 2.4 * u;
    const double top = 0.1 * height + 4.2 * u;
    const double bottom = 0.91 * height;
    std::array<bool, 8> marks{};
    for (const auto& actor : actors) {
        check(actor.visible && actor.form == "kid", "Settled room actor is not visible in its kid pose");
        check(actor.mark >= 0 && actor.mark < 8 && !marks[static_cast<std::size_t>(actor.mark)],
              "Settled room has a missing or duplicate lineup mark");
        marks[static_cast<std::size_t>(actor.mark)] = true;
        const auto clip = camera.projection * camera.view * glm::dvec4(actor.position + glm::dvec3(0.0, 1.64, 0.0), 1.0);
        check(clip.w > 0.0, "Settled actor head is behind the lobby camera");
        const double x = (clip.x / clip.w + 1.0) * width * 0.5;
        const double y = (1.0 - clip.y / clip.w) * height * 0.5;
        check(x >= left && x <= right && y >= top && y <= bottom,
              "Settled actor head falls outside the free lobby pane");
    }
}

void captureBothSizes(const std::string& name, const std::filesystem::path& output,
                      LobbyRenderer& scene, LobbyEffects& effects, PlayerShowcase& showcase, RenderPipeline& pipeline,
                      const LobbyPresentation& presentation, const std::vector<LobbyPresentationEvent>& events,
                      double time, bool compareActors = false) {
    for (const auto [width, height] : {std::pair{1280, 720}, std::pair{1920, 1080}}) {
        if (name == "settled-8") checkSettledHeads(scene.Layout(), presentation.actors(), width, height);
        if (compareActors) {
            const auto sceneOnly = render(scene, effects, showcase, pipeline, presentation.actors(), events,
                                          presentation.hubMode(), time, width, height, false);
            const auto withActors = render(scene, effects, showcase, pipeline, presentation.actors(), events,
                                           presentation.hubMode(), time, width, height, true);
            check(pixelDifferences(sceneOnly, withActors) > 100,
                  name + " actors did not add visible character pixels at " + std::to_string(width) + "x" + std::to_string(height));
            savePng(output / (name + "-" + std::to_string(width) + "x" + std::to_string(height) + ".png"),
                    width, height, withActors);
        } else {
            const auto pixels = render(scene, effects, showcase, pipeline, presentation.actors(), events,
                                       presentation.hubMode(), time, width, height, true);
            savePng(output / (name + "-" + std::to_string(width) + "x" + std::to_string(height) + ".png"),
                    width, height, pixels);
        }
    }
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(1920, 1080, "Lobby visual GPU test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }

    int result = 0;
    try {
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        check(framebufferWidth >= 1920 && framebufferHeight >= 1080,
              "Hidden GLFW window must support 1920x1080 captures (actual=" + std::to_string(framebufferWidth) + "x" +
                  std::to_string(framebufferHeight) + ")");
        const std::filesystem::path root = argc > 1 ? argv[1] : ".";
        const std::filesystem::path output = argc > 2 ? argv[2] : "build/captures/lobby-visual";
        LobbyRenderer scene(root / "assets/reference/lobby");
        LobbyEffects effects(root / "assets/reference");
        PlayerShowcase showcase(root / "assets/reference/characters");
        RenderPipeline pipeline(root / "assets/reference/post");
        pipeline.Configure(4, true, true, true);
        checkGl("Lobby asset loading");
        PlayerProfile profile;
        profile.name = "Local";
        profile.weapon = "shooter";
        profile.sub = "bomb";

        network::RoomView offline;
        offline.selfId = 100;
        offline.hostId = 100;

        double time = 0.0;
        LobbyPresentation hub(scene.Layout());
        const auto hubEvents = initialize(hub, offline, profile, scene, effects, time);
        check(hub.hubMode() && hub.actors().size() == 1 && hub.actors().front().visible,
              "Hub sample did not produce its visible local actor");
        captureBothSizes("hub", output, scene, effects, showcase, pipeline, hub, hubEvents, time);

        LobbyPresentation entry(scene.Layout());
        initialize(entry, offline, profile, scene, effects, time);
        auto twoPlayerRoom = makeRoom(1);twoPlayerRoom.members[0].ready=false;
        initialize(entry,twoPlayerRoom,profile,scene,effects,time);
        advance(entry,twoPlayerRoom,profile,scene,effects,time,3.0);
        twoPlayerRoom.members.push_back(makeMember(101,0,"Arriving player"));twoPlayerRoom.members.back().ready=false;
        auto entryEvents = initialize(entry, twoPlayerRoom, profile, scene, effects, time);
        check(!entry.hubMode(), "Room sample did not switch to the lobby camera");
        entryEvents = advance(entry, twoPlayerRoom, profile, scene, effects, time, 3.0);
        check(entry.actors().size() == 2, "Swimming entry sample must contain the local player and one remote");
        check(effects.counts().trailDomes > 0, "Swimming entry did not produce a lobby ink trail");
        const auto remote = std::find_if(entry.actors().begin(), entry.actors().end(),
                                         [](const auto& actor) { return !actor.local; });
        check(remote != entry.actors().end() && remote->visible && remote->form == "swim" && remote->dance.empty(),
              "Remote entry sample must preserve its swimming form and empty dance");
        captureBothSizes("swim-entry", output, scene, effects, showcase, pipeline, entry, entryEvents, time);
        auto idleActors = entry.actors();
        auto idleRemote = std::find_if(idleActors.begin(), idleActors.end(), [](const auto& actor) { return !actor.local; });
        idleRemote->form = "kid";
        idleRemote->dance = "menu_idle";
        idleRemote->speed = 0.0;
        idleRemote->velocityWorld = glm::dvec3(0.0);
        PlayerShowcase idleShowcase(root / "assets/reference/characters");
        const auto swimPixels = render(scene, effects, showcase, pipeline, entry.actors(), entryEvents,
                                       entry.hubMode(), time, 1280, 720, true, true, true);
        const auto swimNoEffects = render(scene, effects, showcase, pipeline, entry.actors(), entryEvents,
                                          entry.hubMode(), time, 1280, 720, true, false);
        check(pixelDifferences(swimPixels, swimNoEffects) > 10,
              "Swimming lobby trail did not change the rendered pixels");
        const auto idlePixels = render(scene, effects, idleShowcase, pipeline, idleActors, entryEvents,
                                       entry.hubMode(), time, 1280, 720, true);
        check(pixelDifferences(swimPixels, idlePixels) > 100,
              "Remote swim with empty dance rendered the same pixels as its idle pose");

        LobbyPresentation room(scene.Layout());
        initialize(room, offline, profile, scene, effects, time);
        auto eightPlayerRoom = makeRoom(8);
        auto roomEvents = initialize(room, eightPlayerRoom, profile, scene, effects, time);
        roomEvents = advance(room, eightPlayerRoom, profile, scene, effects, time, 15.0);
        check(room.actors().size() == 8, "Eight-player lobby did not retain every actor after settling");
        for (const auto& actor : room.actors()) check(actor.visible && actor.form == "kid", "Eight-player lobby did not settle");
        captureBothSizes("settled-8", output, scene, effects, showcase, pipeline, room, roomEvents, time, true);
        scene.SetKeyShadowEnabled(false);
        const auto noShadow=render(scene,effects,showcase,pipeline,room.actors(),{},false,time,1280,720,true);
        scene.SetKeyShadowEnabled(true);
        const auto withShadow=render(scene,effects,showcase,pipeline,room.actors(),{},false,time,1280,720,true);
        check(pixelDifferences(noShadow,withShadow)>30,"Enabling the source key shadow must change visible scene pixels");

        {
            GameUi overlay(window,root/"assets");overlay.SetProfile(profile);overlay.ShowOnline();overlay.SetOnlineState(eightPlayerRoom);
            for(const auto size:std::array<std::array<int,2>,2>{{{1280,720},{1920,1080}}}){
                const int w=size[0],h=size[1];const auto camera=FitLobbyView(scene.Layout(),false,w,h);
                render(scene,effects,showcase,pipeline,room.actors(),roomEvents,false,time,w,h,true);
                std::vector<UiLobbyPlate> plates;
                for(const auto& actor:room.actors()){
                    const auto clip=camera.projection*camera.view*glm::dvec4(actor.position+glm::dvec3(0,1.8,0),1);
                    if(clip.w>0)plates.push_back({actor.id,float((clip.x/clip.w+1)*.5),float((1-clip.y/clip.w)*.5),actor.visible});
                }
                overlay.SetLobbyPresentation(true,-1,false,plates);overlay.RenderMenu(w,h);
                Pixels composed(std::size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,composed.data());checkGl("Online room overlay");
                savePng(output/("online-room-"+std::to_string(w)+"x"+std::to_string(h)+".png"),w,h,composed);
            }
        }

        eightPlayerRoom.phase = network::RoomPhase::Starting;
        eightPlayerRoom.matchEpoch = 1;
        roomEvents = advance(room, eightPlayerRoom, profile, scene, effects, time, 2.1);
        check(room.countdown() == 1 && room.launching(), "Starting sample did not reach its countdown frame");
        captureBothSizes("countdown", output, scene, effects, showcase, pipeline, room, roomEvents, time);

        eightPlayerRoom.phase = network::RoomPhase::Match;
        roomEvents = advance(room, eightPlayerRoom, profile, scene, effects, time, 1.2);
        check(room.countdown() == 0 && room.launching() && !room.launchComplete(),
              "Launch sample did not capture the superjump handoff");
        check(effects.counts().particles > 0, "Launch sample did not produce its native lobby effect");
        captureBothSizes("launch", output, scene, effects, showcase, pipeline, room, roomEvents, time);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
