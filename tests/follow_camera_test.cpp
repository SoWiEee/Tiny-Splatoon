#include "../src/game/FollowCamera.hpp"
#include "fixtures/camera_fixtures.hpp"

#include <cmath>
#include <iostream>

using namespace restoration;

namespace {
bool near(double a, double b) { return std::abs(a - b) < 0.003; }
bool near(glm::dvec3 a, glm::dvec3 b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
StageData stage(bool blocked) {
    StageData result{};
    result.bounds = {-20, 20, -20, 20};
    const std::array<glm::dvec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<int, 6> faces{{-1, -1, -1, -1, -1, -1}};
    result.blocks.push_back({{0, -0.5, 0}, {20, 0.5, 20}, axes, faces,
                             true, false, false, false, false, false, false});
    if (blocked) result.blocks.push_back({{0, 1.5, -2.75}, {2, 1.5, 0.25}, axes, faces,
                                          true, false, false, false, false, false, false});
    return result;
}
}

int main() {
    const StageData openStage = stage(false), blockedStage = stage(true);
    const StageGeometry open(openStage), blocked(blockedStage);
    FollowCamera camera;
    int failures = 0;
    for (std::size_t i = 0; i < camera_fixtures::frames.size(); ++i) {
        const auto& f = camera_fixtures::frames[i];
        CameraTarget target;
        target.position = f.position;
        target.visualPosition = f.visualPosition;
        target.velocity = f.velocity;
        target.grounded = f.grounded;
        target.climbing = f.climbing;
        target.squid = f.squid;
        target.swimming = f.swimming;
        target.landT = f.landT;
        target.landSpeed = f.landSpeed;
        if (f.reset) { camera.yaw = f.yaw; camera.pitch = f.pitch; camera.reset(target); }
        if (f.recoil != 0) camera.recoil(f.recoil);
        camera.tick(f.dt, target, f.blocked ? blocked : open);
        const auto& actual = camera.pose();
        if (near(actual.eye, f.eye) && near(actual.forward, f.forward) &&
            near(actual.verticalFovDegrees, f.verticalFovDegrees)) continue;
        ++failures;
        std::cerr << "camera fixture " << i << ": eye " << actual.eye.x << ',' << actual.eye.y << ',' << actual.eye.z
                  << " vs " << f.eye.x << ',' << f.eye.y << ',' << f.eye.z << "; forward "
                  << actual.forward.x << ',' << actual.forward.y << ',' << actual.forward.z << " vs "
                  << f.forward.x << ',' << f.forward.y << ',' << f.forward.z << "; FOV "
                  << actual.verticalFovDegrees << " vs " << f.verticalFovDegrees << '\n';
    }
    if (failures) std::cerr << failures << " camera reference failures\n";
    else std::cout << camera_fixtures::frames.size() << " camera reference frames passed\n";
    return failures ? 1 : 0;
}
