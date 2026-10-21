#pragma once

#include "CharacterShader.hpp"
#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace restoration {
struct StageData;
class EnvironmentScene;
class DirectionalShadow;

class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(std::filesystem::path assets="assets/reference/environment");
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&)=delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&)=delete;
    void Load(const StageData& stage);
    void SetReflectionsEnabled(bool enabled){reflectionsEnabled_=enabled;}
    void Update(double dt);
    int DrawSky(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye) const;
    int DrawSea(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye,const DirectionalShadow* shadow=nullptr) const;
    int DrawScenery(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye) const;
    int DrawShadow(const glm::dmat4& lightViewProjection) const;
    CharacterLightState Lights() const;
    void SetReflectionTexture(GLuint texture,const glm::mat4& matrix);
    int RenderReflection(const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye,int width,int height,
        const std::function<int(const glm::mat4&,const glm::mat4&,const glm::vec3&)>& draw);
private:
    struct Rect { float x,z,hx,hz,ax,az; };
    std::filesystem::path assets_;
    std::unique_ptr<EnvironmentScene> scenery_;
    std::map<std::string,std::vector<float>> theme_;
    std::vector<Rect> decks_,wet_;
    GLuint skyProgram_=0,seaProgram_=0,skyVao_=0,skyVbo_=0,seaVao_=0,seaVbo_=0,seaEbo_=0;
    GLuint cloud_=0,wave_=0,pmrem_=0,foam_=0,reflection_=0,deckField_=0;
    std::map<std::string,std::vector<float>> deckUniforms_;
    GLuint planarColor_=0,planarFbo_=0,planarDepth_=0,farCube_=0;
    int reflectionWidth_=0,reflectionHeight_=0;
    GLsizei skyVertices_=0,seaIndices_=0;
    glm::mat4 reflectionMatrix_{1.0f};
    glm::vec4 bounds_{0.0f};
    glm::vec4 foamRect_{0.0f};
    float foamRange_=8;
    double time_=0;
    bool marina_=false,reflectionsEnabled_=true;
    void uniforms(GLuint program,const glm::vec3& eye) const;
    double waterHeight(double x,double z,double time) const;
    void bakeFarReflection();
    void bakeDeckField(const std::string& stage);
};
} // namespace restoration
