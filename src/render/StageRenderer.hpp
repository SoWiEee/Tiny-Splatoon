#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

namespace restoration {
struct StageData;
class FacePaint;
class PlayerSimulation;
class DirectionalShadow;

class StageRenderer {
public:
    StageRenderer();
    ~StageRenderer();
    StageRenderer(const StageRenderer&)=delete;
    StageRenderer& operator=(const StageRenderer&)=delete;
    void Load(const StageData& stage,int display,const FacePaint& paint);
    void UploadInk(const StageData& stage,const FacePaint& paint);
    void VerifyInk(const StageData& stage,const FacePaint& paint) const;
    int Draw(const glm::mat4& vp,const glm::vec3& eye,bool wireframe,bool linearOutput=false,const DirectionalShadow* shadow=nullptr);
    int DrawShadow(const glm::mat4& lightViewProjection);
    void LoadActors(const PlayerSimulation& player);
private:
    GLuint vao=0,vbo=0,program=0,ink=0;
    GLsizei vertices=0;
    const StageData* uploadedStage=nullptr;
    std::vector<std::uint8_t> uploadedGrid;
};
} // namespace restoration
