#pragma once
#include "CharacterData.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include <memory>

namespace restoration {
// Source meshes and material programs are shared across all actors.
class CharacterArt {
public:
    explicit CharacterArt(std::filesystem::path directory="assets/reference/characters");
    ~CharacterArt();
    const CharacterData& data() const { return data_; }
    std::size_t draw(const CharacterPoseFrame& frame,const CharacterStyle& style,
                     const std::string& main,const std::string& sub,const std::string& lod,
                     const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 camera,
                     const CharacterLightState& lighting);
private:
    struct GpuMesh;
    const GpuMesh& mesh(const std::string& file,const std::string& program);
    CharacterData data_;
    std::map<std::string,std::unique_ptr<CharacterShader>> programs_;
    std::map<std::string,const CharacterMaterialDefinition*> materials_;
    std::map<std::pair<std::string,std::string>,std::unique_ptr<GpuMesh>> meshes_;
    unsigned dfg_=0;
};
}
