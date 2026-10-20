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

#include <algorithm>
#include <cmath>
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
        const int drawCalls = effects.Draw(view, projection, eye);
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
