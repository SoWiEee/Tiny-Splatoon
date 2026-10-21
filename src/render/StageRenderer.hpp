#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace restoration {
struct StageData;
class FacePaint;
class PlayerSimulation;
class CombatWorld;
class DirectionalShadow;
struct CharacterLightState;

class StageRenderer {
public:
    explicit StageRenderer(std::filesystem::path stageAssets="assets/reference/stages",bool environment=false);
    ~StageRenderer();
    StageRenderer(const StageRenderer&)=delete;
    StageRenderer& operator=(const StageRenderer&)=delete;
    void Load(const StageData& stage,int display,const FacePaint& paint);
    void UploadInk(const StageData& stage,FacePaint& paint,double dt=0.0);
    void VerifyInk(const StageData& stage,const FacePaint& paint) const;
    glm::vec4 ReadVisualInk(int faceId,double u,double v) const;
    int Draw(const glm::mat4& vp,const glm::vec3& eye,bool wireframe,bool linearOutput=false,const DirectionalShadow* shadow=nullptr);
    int Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& lights,
             bool wireframe=false,const DirectionalShadow* shadow=nullptr,bool reflection=false);
    int DrawShadow(const glm::mat4& lightViewProjection);
    void LoadActors(const PlayerSimulation& player);
    void UpdateSwimWake(double dt,const CombatWorld& world,glm::dvec3 camera);
private:
    struct Source;
    std::filesystem::path stageAssets_;
    std::unique_ptr<Source> source_;
    bool environment_=false;
    int display_=0;
    GLuint vao=0,vbo=0,program=0,ink=0;
    GLsizei vertices=0;
    const StageData* uploadedStage=nullptr;
    std::vector<std::uint8_t> uploadedGrid;
};
} // namespace restoration
