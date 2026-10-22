#include "CharacterShader.hpp"
#include "FacePaint.hpp"
#include "StageGeometry.hpp"
#include "StageRenderer.hpp"
#include "ReferenceData.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
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

using Pixels = std::vector<unsigned char>;
Pixels render(restoration::StageRenderer& renderer, const glm::dmat4& view, const glm::dmat4& projection,
              glm::dvec3 eye, const restoration::CharacterLightState& lights, const std::filesystem::path& output) {
    glViewport(0, 0, 512, 512);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.115f, 0.16f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const int draws = renderer.Draw(view, projection, eye, lights);
    check(draws >= 2, "Source stage renderer did not draw the level and PropKit dressing");
    Pixels pixels(512 * 512 * 4);
    glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::size_t nonBackground = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (std::abs(static_cast<int>(pixels[i]) - 29) > 10 || std::abs(static_cast<int>(pixels[i + 1]) - 41) > 10 ||
            std::abs(static_cast<int>(pixels[i + 2]) - 48) > 10) ++nonBackground;
    }
    check(nonBackground > 1200, "Source stage render changed too few pixels");
    std::filesystem::create_directories(output.parent_path());
    stbi_flip_vertically_on_write(1);
    check(stbi_write_png(output.string().c_str(), 512, 512, 4, pixels.data(), 512 * 4) != 0,
          "Could not save source stage render evidence");
    const GLenum error = glGetError();
    check(error == GL_NO_ERROR, "Source stage rendering generated OpenGL error " + std::to_string(error));
    return pixels;
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(512, 512, "Source stage visual test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    int result = 0;
    try {
        using namespace restoration;
        const StageData* stage = nullptr;
        for (const auto& candidate : ReferenceStages())
            if (candidate.id == "tidewater" && candidate.mode == "turf") { stage = &candidate; break; }
        check(stage != nullptr, "Missing source Turf stage fixture");
        const auto assetRoot = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/reference/stages");
        const auto outputRoot = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("build/captures/stage-visual");
        StageGeometry geometry(*stage);
        FacePaint paint(*stage, geometry);
        auto renderer = std::make_unique<StageRenderer>(assetRoot);
        renderer->Load(*stage, 0, paint);
        const GLenum loadError = glGetError();
        check(loadError == GL_NO_ERROR, "Source stage asset upload generated OpenGL error " + std::to_string(loadError));
        CharacterLightState lights;
        lights.ambient = {0.31f, 0.34f, 0.38f};
        lights.sunDirection = glm::normalize(glm::vec3(-0.42f, 0.84f, -0.34f));
        lights.sunColor = {1.65f, 1.5f, 1.32f};
        lights.skyDirection = {0, 1, 0}; lights.skyColor = {0.38f, 0.51f, 0.65f};
        lights.groundColor = {0.20f, 0.17f, 0.14f};
        const double cx = (stage->bounds.x + stage->bounds.y) * 0.5, cz = (stage->bounds.z + stage->bounds.w) * 0.5;
        const glm::dvec3 overviewEye(cx + 53.0, 54.0, cz + 67.0), overviewTarget(cx, 2.0, cz);
        const auto overviewView = glm::lookAt(overviewEye, overviewTarget, glm::dvec3(0, 1, 0));
        const auto overviewProjection = glm::perspective(glm::radians(56.0), 1.0, 0.1, 280.0);
        render(*renderer, overviewView, overviewProjection, overviewEye, lights, outputRoot / "stage-overview.png");

        int faceId = -1;
        glm::dvec3 center;
        double paintU=0,paintV=0;
        glm::dvec3 swimDirection(0.0, 0.0, 1.0);
        glm::dvec3 paintEye(0.0);
        glm::dmat4 paintView(1.0), paintProjection(1.0);
        float inkWet=0.0f,inkDry=0.0f;
        std::size_t changedPixels = 0;
        for (int i = 0; i < static_cast<int>(stage->faces.size()); ++i) {
            const auto& face = stage->faces[i];
            if (!face.paintable || !face.turf || face.normal.y < 0.8 || face.width < 2.0 || face.height < 2.0) continue;
            for(double u : {0.2,0.4,0.6,0.8}) {
                for(double v : {0.2,0.4,0.6,0.8}) {
                    const auto point=face.origin+face.u*(face.width*u)+face.v*(face.height*v);
                    const auto eye=point+glm::dvec3(2.4,5.2,6.4);
                    bool clear=true;
                    for(double du : {-1.3,0.0,1.3}) for(double dv : {-1.3,0.0,1.3})
                        clear=clear&&geometry.los(eye,point+face.u*du+face.v*dv+face.normal*0.04);
                    if(clear){faceId=i;center=point;paintU=face.width*u;paintV=face.height*v;break;}
                }
                if(faceId>=0)break;
            }
            if(faceId>=0)break;
        }
        check(faceId >= 0, "No broad upward paintable floor face in the stage fixture");
        SplatOptions options; options.kind = SplatKind::Shot;
        const auto& swimFace=stage->faces[faceId];
        swimDirection=glm::dvec3(swimFace.u.x,0.0,swimFace.u.z);
        if(glm::dot(swimDirection,swimDirection)<1e-8)swimDirection=glm::dvec3(swimFace.v.x,0.0,swimFace.v.z);
        check(glm::dot(swimDirection,swimDirection)>1e-8,"No horizontal direction on the upward swim surface");
        swimDirection=glm::normalize(swimDirection);
        check(paint.splat(center + glm::dvec3(0, 0.03, 0), 1.3, 0, 0.37, options) > 0.0,
              "CPU gameplay paint fixture failed to record a floor splat");
        renderer->UploadInk(*stage, paint, 0.1);
        const glm::vec4 wet = renderer->ReadVisualInk(faceId, paintU, paintV);
        inkWet=wet.g;
        check(wet.a > 0.05f && wet.g > 0.65f, "Source GPU paint atlas did not contain a wet, covered splat");
        paintEye = center + glm::dvec3(2.4, 5.2, 6.4);
        paintView = glm::lookAt(paintEye, center, glm::dvec3(0, 1, 0));
        paintProjection = glm::perspective(glm::radians(48.0), 1.0, 0.05, 60.0);
        const Pixels wetImage = render(*renderer, paintView, paintProjection, paintEye, lights, outputRoot / "stage-wet-ink.png");
        for (int frame = 0; frame < 20; ++frame) renderer->UploadInk(*stage, paint, 0.25);
        const glm::vec4 dry = renderer->ReadVisualInk(faceId, paintU, paintV);
        inkDry=dry.g;
        check(dry.a > 0.05f && dry.g + 0.12f < wet.g, "Source GPU ink did not retain coverage while drying over time");
        const Pixels dryImage = render(*renderer, paintView, paintProjection, paintEye, lights, outputRoot / "stage-dry-ink.png");
        for (std::size_t i = 0; i < wetImage.size(); i += 4)
            if (std::abs(static_cast<int>(wetImage[i]) - dryImage[i]) + std::abs(static_cast<int>(wetImage[i + 1]) - dryImage[i + 1]) +
                std::abs(static_cast<int>(wetImage[i + 2]) - dryImage[i + 2]) > 20) ++changedPixels;
        check(changedPixels > 100, "Ink wet-to-dry source shading did not change enough visible render pixels");

        renderer.reset();
        const auto renderSwimScenario = [&](bool submerged, const std::filesystem::path& output, bool ripple=false) {
            FacePaint scenarioPaint(*stage, geometry);
            check(scenarioPaint.splat(center + glm::dvec3(0, 0.03, 0), 1.3, 0, 0.37, options) > 0.0,
                  "CPU gameplay swim fixture failed to record its floor splat");
            StageRenderer scenarioRenderer(assetRoot);
            scenarioRenderer.Load(*stage, 0, scenarioPaint);
            scenarioRenderer.UploadInk(*stage, scenarioPaint, 0.1);
            for (int frame = 0; frame < 20; ++frame) scenarioRenderer.UploadInk(*stage, scenarioPaint, 0.25);
            scenarioRenderer.UploadInk(*stage, scenarioPaint, 0.05);

            CombatWorld wakeWorld(*stage, geometry, scenarioPaint);
            std::unique_ptr<PlayerSimulation> swimmer;
            if (submerged) {
                swimmer = std::make_unique<PlayerSimulation>(*stage, geometry, scenarioPaint, 0, &wakeWorld);
                swimmer->setLocal(true);
                swimmer->state.vel = swimDirection * 4.0;
                swimmer->state.alive = true;
                swimmer->state.grounded = false;
                swimmer->state.squid = true;
                swimmer->state.submerged = true;
            }
            if(ripple)scenarioPaint.requestVisualRipple({center,0.012,0.15,1.5,0.85});
            for (int frame = 0; frame < 7; ++frame) {
                scenarioRenderer.UploadInk(*stage, scenarioPaint, 0.05);
                if (swimmer) {
                    swimmer->state.pos = center + swimDirection * (0.2 * frame);
                    scenarioRenderer.UpdateSwimWake(0.05, wakeWorld, paintEye);
                }
            }
            return render(scenarioRenderer, paintView, paintProjection, paintEye, lights, output);
        };
        const Pixels noWakeImage = renderSwimScenario(false, outputRoot / "stage-swim-wake-before.png");
        const Pixels wakeImage = renderSwimScenario(true, outputRoot / "stage-swim-wake-after.png");
        std::size_t visibleWakePixels = 0;
        for (std::size_t i = 0; i < wakeImage.size(); i += 4)
            if (std::abs(static_cast<int>(wakeImage[i]) - noWakeImage[i]) +
                std::abs(static_cast<int>(wakeImage[i + 1]) - noWakeImage[i + 1]) +
                std::abs(static_cast<int>(wakeImage[i + 2]) - noWakeImage[i + 2]) > 12) ++visibleWakePixels;
        check(visibleWakePixels > 20, "Production level shader did not visibly render the submerged swim wake");        const Pixels rippleImage=renderSwimScenario(false,outputRoot/"stage-ink-ripple.png",true);
        std::size_t ripplePixels=0;
        for(std::size_t i=0;i<rippleImage.size();i+=4)
            if(std::abs(int(rippleImage[i])-noWakeImage[i])+std::abs(int(rippleImage[i+1])-noWakeImage[i+1])+std::abs(int(rippleImage[i+2])-noWakeImage[i+2])>8)++ripplePixels;
        check(ripplePixels>20,"Production ink surface must visibly carry footstep/dive ripple packets");
        std::cout << "STAGE_VISUAL_GPU sourcePbr=pass capturedMaps=pass bevels=sourceGeometry inkWet=" << inkWet
                  << " inkDry=" << inkDry << " visibleInkPixels=" << changedPixels << " visibleWakePixels=" << visibleWakePixels
                  << " renderer=" << glGetString(GL_RENDERER) << '\n';
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
