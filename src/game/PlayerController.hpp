#pragma once

#include <array>

namespace restoration {

struct PlayerSettings;
class MatchSimulation;
class FollowCamera;
class StageGeometry;
class PlayerSimulation;

// One desktop input snapshot, taken once by the platform layer. Gamepad axes use
// GLFW's standard order: left X/Y followed by right X/Y, with triggers kept
// separately as axes 4 and 5. Buttons use GLFW's
// standard gamepad mapping indices (A through D-pad Left).
struct ControllerInput {
    double mouseDX = 0.0, mouseDY = 0.0;
    bool forward = false, backward = false, left = false, right = false;
    double leftStickX = 0.0, leftStickY = 0.0;
    double rightStickX = 0.0, rightStickY = 0.0;
    double leftTrigger = -1.0, rightTrigger = -1.0;
    std::array<bool, 15> gamepadButtons{};
    bool gamepadConnected = false;
    bool usingPad = false; // The input layer's last-device choice, used for aim-assist strength.

    // Action state is carried with the snapshot for the app loop; Update leaves
    // the corresponding PlayerInput action fields to the caller.
    bool jump = false, squid = false, fire = false;
    bool sub = false, special = false, cheer = false;
};

// Translates desktop look and movement into the local actor intent. Action
// buttons are intentionally caller-owned; only move, aimYaw/aimPitch, and the
// camera yaw/pitch are updated.
class PlayerController {
public:
    void Update(double dt, const ControllerInput& input, const PlayerSettings& settings,
                MatchSimulation& match, FollowCamera& camera, const StageGeometry& geometry);

private:
    struct AssistState {
        const PlayerSimulation* target = nullptr;
        double yaw = 0.0, pitch = 0.0, closeness = 0.0, strength = 0.0;
        bool has = false;
    };

    double edgeTime_ = 0.0;
    double padLookX_ = 0.0, padLookY_ = 0.0;
    AssistState assist_{};
};

} // namespace restoration
