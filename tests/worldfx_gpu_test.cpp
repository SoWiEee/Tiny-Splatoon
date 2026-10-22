#include "../src/game/ArsenalData.hpp"
#include "../src/game/CharacterData.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "../src/render/WorldEffects.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/world/StageGeometry.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void charge(restoration::PlayerSimulation& actor) {
    actor.state.invuln = 0.0;
    actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
}

std::size_t countShape(const std::vector<restoration::WorldFxParticle>& items,
                       restoration::WorldFxShape shape) {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(),
        [shape](const auto& item) { return item.shape == shape && item.alpha > 0.0f; }));
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(128, 128, "World FX source geometry smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int result = 0;
    try {
        using namespace restoration;
        const StageData* stage = nullptr;
        for (const auto& candidate : ReferenceStages())
            if (candidate.id == "tidewater" && candidate.mode == "turf") stage = &candidate;
        check(stage != nullptr, "Missing source Turf stage for World FX GPU test");

        StageGeometry geometry(*stage);
        FacePaint paint(*stage, geometry);
        CombatWorld world(*stage, geometry, paint);
        const glm::dvec3 anchor = stage->spawnPads[0];

        auto tornadoActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        tornadoActor->spawnAt(anchor, 0.0);
        tornadoActor->setLoadout("shooter", "bomb", "strike");
        charge(*tornadoActor);
        tornadoActor->input.aimYaw = 0.0;
        tornadoActor->input.special = true;
        tornadoActor->tick(1.0 / 60.0);
        check(tornadoActor->specialPose().active && tornadoActor->specialPose().kind == "strike",
              "Strike did not activate through PlayerSimulation input");
        tornadoActor->input.special = false;
        tornadoActor->input.hasMapTarget = true;
        tornadoActor->input.mapTarget = anchor + glm::dvec3(0.0, 0.0, 1.2);
        tornadoActor->input.specialConfirm = true;
        for (int frame = 0; frame < 24 && world.visualObjects().empty(); ++frame)
            tornadoActor->tick(1.0 / 60.0);
        const double strikeFlight = GetEquipment(EquipmentCategory::Special, "strike").number("flight");
        world.tick(strikeFlight + 1.0 / 60.0);
        world.tick(0.1);
        const auto tornadoObjects = world.visualObjects();
        check(std::any_of(tornadoObjects.begin(), tornadoObjects.end(),
                          [](const auto& object) { return object.kind == "tornado"; }),
              "Strike source path did not produce a persistent tornado object");

        auto bubbleActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        bubbleActor->spawnAt(anchor, 0.0);
        bubbleActor->setLoadout("shooter", "bomb", "blower");
        charge(*bubbleActor);
        bubbleActor->input.special = true;
        bubbleActor->tick(1.0 / 60.0);
        check(bubbleActor->specialPose().active && bubbleActor->specialPose().kind == "blower",
              "Blower did not activate through PlayerSimulation input");
        bubbleActor->input.special = false;
        bubbleActor->input.fire = true;
        bubbleActor->tick(1.0 / 60.0);
        const auto bubbleObjects = world.visualObjects();
        check(std::any_of(bubbleObjects.begin(), bubbleObjects.end(),
                          [](const auto& object) { return object.kind == "bubble" && object.held; }),
              "Blower firing did not create its persistent held bubble");

        auto speakerActor = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        speakerActor->spawnAt(anchor, 0.0);
        speakerActor->setLoadout("shooter", "bomb", "wail");
        charge(*speakerActor);
        speakerActor->input.special = true;
        speakerActor->tick(1.0 / 60.0);
        check(speakerActor->specialPose().active && speakerActor->specialPose().kind == "wail",
              "Wail did not activate through PlayerSimulation input");
        speakerActor->input.special = false;
        for (int frame = 0; frame < 18; ++frame) speakerActor->tick(1.0 / 60.0);
        speakerActor->input.fire = true;
        speakerActor->tick(1.0 / 60.0);
        const auto speakerObjects = world.visualObjects();
        check(std::any_of(speakerObjects.begin(), speakerObjects.end(),
                          [](const auto& object) { return object.kind == "speaker"; }),
              "Wail firing did not place its source speaker prop");

        CharacterData characterData;
        {
            FacePaint walkPaint(*stage,geometry);
            CombatWorld walkWorld(*stage,geometry,walkPaint);
            PlayerSimulation walker(*stage,geometry,walkPaint,0,&walkWorld);
            walker.spawnAt(anchor,0);
            walker.state.grounded=true;walker.state.squid=false;walker.state.submerged=false;
            walker.state.groundTeam=1;walker.state.vel={5,0,0};
            WorldEffects steps(characterData);steps.SetPaintSurface(&walkPaint);
            steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            walker.state.pos.x+=0.8;
            steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            check(steps.counts().shells==0,"Wet footsteps must not emit a generic explosion shell on the floor");
            check(!walkPaint.takeVisualRipples().empty(),"Wet foot plants must ripple the ink surface");
            const auto gridBefore=walkPaint.grid();const auto versionBefore=walkPaint.version();
            for(int i=0;i<90;++i)steps.Update(1.0/60.0,walkWorld,anchor+glm::dvec3(0,4,7));
            const auto specks=walkPaint.takeVisualSplats();
            check(!specks.empty(),"Landing walking droplets must enter the cosmetic paint atlas path");
            check(std::all_of(specks.begin(),specks.end(),[](const auto& item){return item.cosmetic&&item.kind==SplatKind::Speck;}),"Walking droplets must remain cosmetic specks");
            check(walkPaint.grid()==gridBefore&&walkPaint.version()==versionBefore,"Walking spray must not claim scoring ink cells");
            check(steps.counts().rings==0,"Landed droplets must not leave circular fallback decals when paint is connected");
            walker.state.groundTeam=2;steps.Footstep(walker,walker.state.pos,5);
            walker.state.groundTeam=0;
            const glm::vec3 closeEye=glm::vec3(walker.state.pos)+glm::vec3(0,0.7f,1.5f);
            steps.Update(1.0/60.0,walkWorld,closeEye);
            check(steps.counts().glows>0,"Enemy-ink footsteps must exercise the source bubble-glow draw path");
            const auto closeView=glm::lookAt(closeEye,glm::vec3(walker.state.pos)+glm::vec3(0,0.05f,0),glm::vec3(0,1,0));
            const auto closeProjection=glm::perspective(glm::radians(45.f),1.f,0.01f,20.f);
            glViewport(0,0,128,128);glEnable(GL_DEPTH_TEST);glClearColor(.2f,.24f,.3f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            steps.Draw(closeView,closeProjection,closeEye);
            std::vector<unsigned char> glowPixels(128*128*4);glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,glowPixels.data());
            std::filesystem::create_directories("build/captures/worldfx");
            stbi_flip_vertically_on_write(1);
            check(stbi_write_png("build/captures/worldfx/footstep-glow.png",128,128,4,glowPixels.data(),128*4)!=0,"Could not save footstep glow evidence");
            std::vector<glm::vec2> dropCenters;
            for(const auto& particle:steps.liveItems())if(particle.shape==WorldFxShape::Drop){const auto clip=closeProjection*closeView*glm::vec4(particle.pos,1);if(clip.w>0)dropCenters.push_back((glm::vec2(clip)/clip.w*.5f+.5f)*128.f);}
            int litPixels=0;
            for(int y=0;y<128;++y)for(int x=0;x<128;++x){
                const bool drop=std::any_of(dropCenters.begin(),dropCenters.end(),[&](auto c){return std::abs(c.x-(x+.5f))<7&&std::abs(c.y-(y+.5f))<7;});
                if(drop)continue;
                const auto offset=(y*128+x)*4;
                check(glowPixels[offset+2]>=75,"Bubble glow quads must add light without dark rectangular backgrounds");
                if(glowPixels[offset]>54||glowPixels[offset+1]>64||glowPixels[offset+2]>80)++litPixels;
            }
            check(litPixels>0,"Source bubble glows must remain visible outside the tiny droplet silhouettes");
        }
        WorldEffects effects(characterData);
        const glm::dvec3 camera(anchor.x, anchor.y + 5.2, anchor.z - 11.5);
        effects.Update(1.0 / 60.0, world, camera);
        const auto& items = effects.worldItems();
        check(countShape(items, WorldFxShape::Swirl) >= 2,
              "Persistent tornado did not emit its two source funnel shapes");
        check(countShape(items, WorldFxShape::Speaker) >= 1,
              "Persistent wail did not emit the source speaker shape");
        check(countShape(items, WorldFxShape::SourceBeam) >= 1,
              "Persistent wail did not emit the source speaker beam shape");
        check(countShape(items, WorldFxShape::Bubble) >= 1,
              "Persistent blower did not emit the source bubble shape");

        glViewport(0, 0, 128, 128);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.025f, 0.035f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const glm::vec3 eye(camera);
        const glm::vec3 target(anchor.x, anchor.y + 1.3, anchor.z + 1.0);
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        const glm::mat4 projection = glm::perspective(glm::radians(58.0f), 1.0f, 0.1f, 80.0f);
        glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);
        glBlendEquationSeparate(GL_FUNC_ADD,GL_MAX);
        const int drawCalls = effects.Draw(view, projection, eye);
        GLint restoredAlphaSrc=0,restoredAlphaDst=0,restoredAlphaEquation=0;
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&restoredAlphaSrc);glGetIntegerv(GL_BLEND_DST_ALPHA,&restoredAlphaDst);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&restoredAlphaEquation);
        check(restoredAlphaSrc==GL_ZERO&&restoredAlphaDst==GL_ONE&&restoredAlphaEquation==GL_MAX,"FX normal/additive passes must restore the caller's independent alpha blend state");
        check(drawCalls > 0, "WorldEffects submitted no real source mesh draws");
        std::vector<unsigned char> pixels(128 * 128 * 4);
        glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::size_t painted = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            if (std::abs(int(pixels[i]) - 6) > 2 || std::abs(int(pixels[i + 1]) - 9) > 2 ||
                std::abs(int(pixels[i + 2]) - 15) > 2) ++painted;
        check(painted > 8, "Source world meshes did not change visible framebuffer pixels");
        const GLenum error = glGetError();
        check(error == GL_NO_ERROR, "WorldEffects source shader/mesh draw returned GL error "
              + std::to_string(static_cast<unsigned int>(error)));

        std::cout << "WORLD_FX_GPU sourceShapes=tornado,speaker,beam,bubble draws=" << drawCalls
                  << " renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
