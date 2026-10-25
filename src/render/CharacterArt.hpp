#pragma once
#include "CharacterData.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include <memory>

namespace restoration {
struct CharacterShadowState {
    bool depthOnly=false;
    unsigned texture=0;
    glm::dmat4 matrix{1.0};
    double intensity=1.0,bias=0.0,normalBias=0.0,radius=1.0;
    int size=4096;
};
// Source meshes and material programs are shared across all actors.
class CharacterArt {
public:
    struct LodDraw { double fade=0.0, side=1.0; bool includeProps=true,hideTank=false; };
    explicit CharacterArt(std::filesystem::path directory="assets/reference/characters",bool linearOutput=false,bool environment=false);
    ~CharacterArt();
    const CharacterData& data() const { return data_; }
    glm::dmat4 attachment(const CharacterPoseFrame& frame,const CharacterStyle& style,const std::string& binding) const;
    std::size_t draw(const CharacterPoseFrame& frame,const CharacterStyle& style,
                     const std::string& main,const std::string& sub,const std::string& lod,
                     const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 camera,
                     const CharacterLightState& lighting,LodDraw lodDraw={0.0,1.0,true,false},
                     const CharacterShadowState* shadow=nullptr);
private:
    struct GpuMesh;
    const GpuMesh& mesh(const std::string& file,const std::string& program,bool shadow);
    CharacterData data_;
    std::map<std::string,std::unique_ptr<CharacterShader>> programs_;
    std::map<std::string,std::unique_ptr<CharacterShader>> shadowPrograms_;
    std::map<std::string,const CharacterMaterialDefinition*> materials_;
    std::map<std::pair<std::string,std::string>,std::unique_ptr<GpuMesh>> meshes_;
    unsigned dfg_=0;
};
}
