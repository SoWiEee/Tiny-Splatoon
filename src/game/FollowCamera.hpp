#pragma once

#include "StageGeometry.hpp"

namespace restoration {

struct CameraTarget {
    glm::dvec3 position{0.0};
    glm::dvec3 visualPosition{0.0};
    glm::dvec3 velocity{0.0};
    bool grounded = true;
    bool climbing = false;
    bool squid = false;
    bool swimming = false;
    double landT = 99.0;
    double landSpeed = 0.0;
};

struct CameraPose {
    glm::dvec3 eye{0.0};
    glm::dvec3 forward{0.0, 0.0, 1.0};
    double verticalFovDegrees = 52.1;
};

// Normal third-person follow from CameraRig; mode blends, special, and super jump are pending.
class FollowCamera {
public:
    double yaw = 0.0, pitch = -0.1;
    double horizontalFovDegrees = 82.0, shake = 1.0;
    void reset(const CameraTarget& target);
    void tick(double dt, const CameraTarget& target, const StageGeometry& geometry);
    void recoil(double amount);
    const CameraPose& pose() const { return pose_; }

private:
    struct Spring {
        double x = 0.0, v = 0.0;
        void reset(double value) { x = value; v = 0.0; }
        double step(double target, double omega, double dt);
    };

    Spring sx_, sy_, sz_, boom_{4.5, 0.0}, height_{1.85, 0.0};
    Spring dip_, side_, lensLift_, recoil_;
    CameraPose pose_;
    double wantDist_ = 4.5, lastTY_ = 0.0, vyF_ = 0.0, takeoffY_ = 0.0;
    double landSeen_ = 99.0, fovKick_ = 0.0, shoulder_ = 0.0;
    bool hasLastTY_ = false;
};

} // namespace restoration
