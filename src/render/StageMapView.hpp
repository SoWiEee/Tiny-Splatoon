#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <cstdint>

namespace restoration {
class EnvironmentRenderer;
class StageRenderer;

struct StageMapCamera {
    glm::dvec3 eye{0};
    glm::dvec3 target{0};
    glm::dmat4 view{1};
    glm::dmat4 projection{1};
    double distance=0;
    double zShift=0;
};

// Mirrors cameraRig.js' diorama pitch, FOV, clipping window, and stage-corner fit.
StageMapCamera FitStageMapCamera(glm::dvec4 bounds,double aspect,bool flipForBlueTeam=false);

class StageMapView {
public:
    static constexpr int Width=256;
    static constexpr int Height=384;

    explicit StageMapView(glm::dvec4 bounds,bool flipForBlueTeam=false);
    ~StageMapView();
    StageMapView(const StageMapView&)=delete;
    StageMapView& operator=(const StageMapView&)=delete;

    GLuint texture() const { return color_; }
    const StageMapCamera& camera() const { return camera_; }
    glm::dmat4 viewProjection() const { return camera_.projection*camera_.view; }

    // The native texture refreshes at 8 Hz; dirty paint is picked up on the next frame in that cadence.
    bool Render(StageRenderer& stage,EnvironmentRenderer& environment,
                std::uint64_t paintRevision,double now);

private:
    StageMapCamera camera_;
    GLuint framebuffer_=0,color_=0,depth_=0;
    std::uint64_t lastPaintRevision_=~std::uint64_t(0);
    double lastRenderTime_=-1.0;
};
} // namespace restoration
