#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace restoration {
struct GradeSettings {
    float saturation=1.08f, vibrance=.12f, contrast=1.07f, lift=0, exposure=1, vignette=.22f;
    glm::vec3 shadowTint{.975f,.99f,1.035f}, highlightTint{1.025f,1,.972f};
    float bloomStrength=.28f, bloomRadius=.45f, bloomThreshold=2.4f;
    float hurt=0, flash=0;
    static GradeSettings Theme(const std::string& theme);
};
// Names follow screenfx.js. Event envelopes are owned by the presentation layer.
struct ScreenEffects {
    bool enabled=true;
    float time=0,speed=0,stretch=0,punch=0,blast=0,blastRing=0,chroma=0;
    glm::vec2 punchPos{.5f},blastPos{.5f};
    glm::vec3 speedTint{1},blastColor{1},lensColorA{1},lensColorB{1},holeRim{1};
    unsigned lensTexture=0;
    glm::vec2 lensTexel{1};
    glm::vec4 edgeInk{0},aura{0},heart{0},urgency{0},swim{0},shimmer{0},flood{0},flash{0},kill{0},charge{0};
    float hurt=0,focus=0,floodDrip=0,floodClear=0,hole=0,desaturation=0,saturation=1;
};

// Owns targets and the HDR -> GTAO -> bloom -> grade -> screen FX -> Neutral/sRGB stack.
// Begin binds the linear scene target; End resolves to the default framebuffer before GUI.
class RenderPipeline {
public:
    explicit RenderPipeline(std::filesystem::path assets="assets/reference/post");
    ~RenderPipeline();
    RenderPipeline(const RenderPipeline&)=delete;
    RenderPipeline& operator=(const RenderPipeline&)=delete;
    void Configure(int samples,bool ao,bool bloom);
    void Begin(int width,int height,const glm::mat4& projection,const glm::mat4& cameraWorld,float nearPlane,float farPlane);
    int End(const GradeSettings& grade={},const ScreenEffects& effects={},int outputWidth=0,int outputHeight=0);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
