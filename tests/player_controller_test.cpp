#include "../src/game/FollowCamera.hpp"
#include "../src/game/MatchSimulation.hpp"
#include "../src/game/NavGraph.hpp"
#include "../src/game/PlayerController.hpp"
#include "../src/game/PlayerProfile.hpp"
#include "../src/world/FacePaint.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace restoration;

namespace {
constexpr double pi = 3.14159265358979323846;
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

StageData makeStage() {
    StageData stage{};
    stage.id = "controller-test";
    stage.name = "Controller test";
    stage.mode = "turf";
    stage.bounds = {-20.0, 20.0, -20.0, 20.0};
    stage.spawnPads = {glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 12.0)};
    BlockData floor{};
    floor.center = {0.0, -0.5, 0.0};
    floor.half = {20.0, 0.5, 20.0};
    floor.axes = {glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)};
    floor.faces.fill(-1);
    floor.faces[2] = 0;
    floor.solid = true;
    stage.blocks.push_back(floor);
    FaceData ground{};
    ground.origin = {-20.0, 0.0, -20.0};
    ground.u = {1, 0, 0};
    ground.v = {0, 0, 1};
    ground.normal = {0, 1, 0};
    ground.width = ground.height = 40.0;
    ground.block = 0;
    ground.paintable = ground.turf = true;
    stage.faces.push_back(ground);
    return stage;
}

MatchOptions options(bool practice) {
    MatchOptions value;
    value.practice = practice;
    value.main = "shooter";
    value.playerName = "Controller test";
    return value;
}

struct Harness {
    StageData stage = makeStage();
    StageGeometry geometry{stage};
    FacePaint paint{stage, geometry, 2048};
    NavGraph nav{stage, geometry};
    MatchSimulation match;

    explicit Harness(bool practice) : match(stage, geometry, paint, nav, options(practice)) {
        for (auto& actor : match.actors()) actor->state.invuln = 0.0;
    }
};

void prepareTarget(Harness& harness, FollowCamera& camera, PlayerSimulation*& target) {
    auto& local = harness.match.local();
    // Aim the fixture directly down +Z before reset() captures the camera pose;
    // later camera yaw/pitch field edits do not rebuild that cached pose.
    camera.yaw = 0.0;
    camera.pitch = 0.0;
    CameraTarget cameraTarget;
    cameraTarget.position = local.state.pos;
    cameraTarget.visualPosition = local.visualPos();
    cameraTarget.grounded = true;
    camera.reset(cameraTarget);

    target = nullptr;
    for (const auto& candidate : harness.match.actors()) {
        if (candidate->team() == local.team()) continue;
        if (!target) target = candidate.get();
        else candidate->state.alive = false;
    }
    if (!target) throw std::runtime_error("Controller fixture did not create an opposing actor");
    target->state.alive = true;
    target->state.submerged = false;
    target->state.squid = false;
    target->state.smoothY = 0.0;
    target->state.pos = {camera.pose().eye.x, camera.pose().eye.y - 0.95, camera.pose().eye.z + 8.0};
}
}

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };

    constexpr double dt = 1.0 / 60.0;
    {
        Harness harness(true);
        auto& actor = harness.match.local();
        FollowCamera camera;
        camera.yaw = 0.5;
        camera.pitch = 0.0;
        PlayerController controller;
        PlayerSettings settings;
        ControllerInput input;
        input.forward = true;
        input.jump = false;
        input.fire = false;
        actor.input.jump = true;
        actor.input.squid = true;
        actor.input.fire = true;
        actor.input.sub = true;
        actor.input.special = true;
        actor.input.cheer = true;

        controller.Update(dt, input, settings, harness.match, camera, harness.geometry);
        check(near(actor.input.move.x, std::sin(0.5)) && near(actor.input.move.z, std::cos(0.5)),
              "keyboard movement follows camera yaw");
        check(actor.input.jump && actor.input.squid && actor.input.fire && actor.input.sub &&
              actor.input.special && actor.input.cheer,
              "controller leaves caller-owned action fields unchanged");
        check(near(actor.input.aimYaw, camera.yaw) && near(actor.input.aimPitch, camera.pitch),
              "controller publishes camera aim to the local actor");

        input = {};
        input.mouseDX = 100.0;
        input.mouseDY = 50.0;
        camera.yaw = 0.5;
        camera.pitch = 0.2;
        settings.sensitivity = 2.0;
        settings.invertY = true;
        controller.Update(dt, input, settings, harness.match, camera, harness.geometry);
        check(near(camera.yaw, 0.08) && near(camera.pitch, 0.41),
              "mouse sensitivity and inverted Y use source gain");
    }

    {
        Harness harness(true);
        FollowCamera camera;
        PlayerController controller;
        PlayerSettings settings;
        settings.padSensitivity = 1.4;
        ControllerInput input;
        input.gamepadConnected = true;
        input.rightStickX = 1.0;
        controller.Update(dt, input, settings, harness.match, camera, harness.geometry);
        const double filtered = 1.0 - std::exp(-60.0 * dt);
        check(near(camera.yaw, -filtered * 3.6 * 1.4 * dt),
              "gamepad edge response uses source curve, filter, and pad sensitivity");

        PlayerController deadZoneController;
        FollowCamera deadZoneCamera;
        input.rightStickX = 0.05;
        input.leftStickX = 0.05;
        input.leftStickY = 0.0;
        deadZoneController.Update(dt, input, settings, harness.match, deadZoneCamera, harness.geometry);
        check(near(deadZoneCamera.yaw, 0.0) && near(glm::length(harness.match.local().input.move), 0.0),
              "radial stick dead zones suppress small look and movement input");

        PlayerController curveController;
        FollowCamera curveCamera;
        input.rightStickX = 0.11 + (0.96 - 0.11) * 0.5;
        input.leftStickX = 0.0;
        const double response = 0.62 * std::pow(0.5 / 0.75, 1.6);
        curveController.Update(dt, input, settings, harness.match, curveCamera, harness.geometry);
        check(near(curveCamera.yaw, -(response * filtered) * 3.6 * 1.4 * dt),
              "gamepad look uses the source two-stage response curve");
    }

    {
        Harness harness(false);
        auto& local = harness.match.local();
        FollowCamera camera;
        PlayerSimulation* target = nullptr;
        prepareTarget(harness, camera, target);
        PlayerController controller;
        PlayerSettings settings;
        settings.aimAssistMouse = true;
        ControllerInput input;
        input.mouseDX = 100.0;
        controller.Update(dt, input, settings, harness.match, camera, harness.geometry);
        const double mouseAssistYaw = camera.yaw;
        check(near(mouseAssistYaw, -100.0 * 0.0021 * 0.79),
              "mouse aim assist applies the gentler half-strength target friction");

        camera.yaw = 0.0;
        camera.pitch = 0.0;
        PlayerController padController;
        settings.aimAssistMouse = false;
        settings.aimAssist = 0.0;
        input = {};
        input.gamepadConnected = true;
        input.usingPad = true;
        input.rightStickX = 1.0;
        padController.Update(dt, input, settings, harness.match, camera, harness.geometry);
        const double noPadAssistYaw = camera.yaw;
        check(near(noPadAssistYaw, -(1.0 - std::exp(-60.0 * dt)) * 3.6 * dt),
              "zero pad aimAssist leaves gamepad look unattenuated");

        camera.yaw = 0.0;
        camera.pitch = 0.0;
        PlayerController assistedPadController;
        settings.aimAssist = 1.0;
        assistedPadController.Update(dt, input, settings, harness.match, camera, harness.geometry);
        check(near(camera.yaw, -(1.0 - std::exp(-60.0 * dt)) * 3.6 * 0.58 * dt),
              "pad aimAssist setting applies full-strength target friction");

        // Keep the same target selected, then move its aim point. Tracking
        // assist carries 42% of its angular motion times strength and closeness.
        camera.yaw = 0.0;
        camera.pitch = 0.0;
        PlayerController trackingController;
        settings.aimAssist = 1.0;
        input.rightStickX = 0.0;
        input.fire = false;
        trackingController.Update(dt, input, settings, harness.match, camera, harness.geometry);
        target->state.pos.x += 0.25;
        target->state.pos.y += 0.2;
        input.fire = true;
        trackingController.Update(dt, input, settings, harness.match, camera, harness.geometry);
        const glm::dvec3 delta{0.25, 0.2, 8.0};
        const double distance = glm::length(delta);
        const double angle = std::acos(delta.z / distance);
        const double cone = std::clamp(std::atan2(1.0, distance), 2.5 * pi / 180.0, 10.0 * pi / 180.0);
        const double closeness = std::clamp((1.0 - angle / cone) * 1.3, 0.0, 1.0);
        const double share = 0.42 * closeness;
        const double targetYaw = std::atan2(delta.x, delta.z);
        const double targetPitch = std::asin(delta.y / distance);
        check(near(camera.yaw, targetYaw * share, 1e-8) &&
              near(camera.pitch, targetPitch * share * 0.7, 1e-8),
              "tracking assist uses source rotational share and pitch weighting");
        check(&local == &harness.match.local(), "match fixture retains its local actor");
    }

    if (failures) return 1;
    std::cout << "Native controller movement, look curves, and aim assist passed\n";
    return 0;
}
