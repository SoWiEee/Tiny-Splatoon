#include "CharacterArt.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace restoration {
namespace {
glm::dvec3 tankUp(const glm::dmat4& parent) {
    if(glm::determinant(parent)==0)return {0,1,0};
    glm::dmat3 m(parent);
    for(int c=0;c<3;++c){double scale=glm::length(m[c]);if(c==0&&glm::determinant(parent)<0)scale=-scale;m[c]/=scale;}
    // Match Three's decompose/Quaternion.invert even when nonuniform scale leaves shear.
    const double trace=m[0][0]+m[1][1]+m[2][2];
    glm::dquat q;
    if(trace>0){const double s=0.5/std::sqrt(trace+1.0);q={0.25/s,(m[1][2]-m[2][1])*s,(m[2][0]-m[0][2])*s,(m[0][1]-m[1][0])*s};}
    else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){const double s=2*std::sqrt(1+m[0][0]-m[1][1]-m[2][2]);q={(m[1][2]-m[2][1])/s,0.25*s,(m[1][0]+m[0][1])/s,(m[2][0]+m[0][2])/s};}
    else if(m[1][1]>m[2][2]){const double s=2*std::sqrt(1+m[1][1]-m[0][0]-m[2][2]);q={(m[2][0]-m[0][2])/s,(m[1][0]+m[0][1])/s,0.25*s,(m[2][1]+m[1][2])/s};}
    else {const double s=2*std::sqrt(1+m[2][2]-m[0][0]-m[1][1]);q={(m[0][1]-m[1][0])/s,(m[2][0]+m[0][2])/s,(m[2][1]+m[1][2])/s,0.25*s};}
    return glm::conjugate(q)*glm::dvec3(0,1,0);
}
}
struct CharacterArt::GpuMesh {
    GLuint vao=0,index=0;
    std::vector<GLuint> attributes;
    GLsizei count=0;
    ~GpuMesh(){if(index)glDeleteBuffers(1,&index);if(!attributes.empty())glDeleteBuffers(static_cast<GLsizei>(attributes.size()),attributes.data());if(vao)glDeleteVertexArrays(1,&vao);}
};

CharacterArt::CharacterArt(std::filesystem::path directory,bool linearOutput,bool environment):data_(directory) {
    for(const auto& material:CharacterMaterials()){
        const auto shaders=directory/"shaders";
        programs_.emplace(material.key,std::make_unique<CharacterShader>(CharacterShader::readSource(shaders/(material.key+".vert")),CharacterShader::readSource(shaders/(material.key+".frag")),linearOutput,environment));
        const auto shadowShaders=directory/"shaders-shadow";
        if(std::filesystem::exists(shadowShaders/(material.key+".vert")))
            shadowPrograms_.emplace(material.key,std::make_unique<CharacterShader>(CharacterShader::readSource(shadowShaders/(material.key+".vert")),CharacterShader::readSource(shadowShaders/(material.key+".frag")),linearOutput,environment));
        materials_.emplace(material.key,&material);
    }
    glGenTextures(1,&dfg_);glBindTexture(GL_TEXTURE_2D,dfg_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());
}
CharacterArt::~CharacterArt(){if(dfg_)glDeleteTextures(1,&dfg_);}

const CharacterArt::GpuMesh& CharacterArt::mesh(const std::string& file,const std::string& program,bool shadow) {
    const auto key=std::make_pair(file,(shadow?"shadow/":"")+program);
    const auto found=meshes_.find(key);if(found!=meshes_.end())return *found->second;
    const auto& source=data_.mesh(file);const auto& shader=*(shadow?shadowPrograms_.at(program):programs_.at(program));
    auto gpu=std::make_unique<GpuMesh>();glGenVertexArrays(1,&gpu->vao);glBindVertexArray(gpu->vao);
    for(const auto& attribute:source.attributes){
        const int location=shader.attribute(attribute.name);if(location<0)continue;
        GLuint buffer=0;glGenBuffers(1,&buffer);gpu->attributes.push_back(buffer);
        glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,attribute.values.size()*sizeof(float),attribute.values.data(),GL_STATIC_DRAW);
        glEnableVertexAttribArray(location);glVertexAttribPointer(location,attribute.components,GL_FLOAT,GL_FALSE,0,nullptr);
    }
    glGenBuffers(1,&gpu->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu->index);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,source.indices.size()*sizeof(std::uint32_t),source.indices.data(),GL_STATIC_DRAW);
    gpu->count=static_cast<GLsizei>(source.indices.size());glBindVertexArray(0);
    return *meshes_.emplace(key,std::move(gpu)).first->second;
}

std::size_t CharacterArt::draw(const CharacterPoseFrame& frame,const CharacterStyle& style,
    const std::string& main,const std::string& sub,const std::string& lod,
    const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 camera,const CharacterLightState& lighting,
    LodDraw lodDraw,const CharacterShadowState* shadow) {
    const bool useShadowShader=shadow&&!shadowPrograms_.empty();
    if(style.skin<0||style.skin>=9||style.eyes<0||style.eyes>=8)throw std::invalid_argument("Unknown source character palette");
    if(lod!="hero"&&lod!="game"&&lod!="far")throw std::invalid_argument("Unknown source character LOD");
    const auto& rig=data_.rig(style.hair,style.hat,style.brows);
    const auto kid=frame.root.matrix()*frame.model.matrix()*frame.kid.matrix();
    std::array<glm::dmat4,CharacterBoneCount> boneWorld;
    for(std::size_t b=0;b<boneWorld.size();++b)boneWorld[b]=kid*frame.skinMatrices[b]*glm::inverse(rig.bones[b].inverseBind);
    struct Draw {const CharacterNode* node;glm::dmat4 world,parent;glm::dvec3 scale;std::string program;double depth;const CharacterPartPose* part;bool body;};
    std::vector<Draw> draws;
    std::map<std::string,const CharacterPartPose*> parts;
    for(const auto& part:frame.parts)parts[part.name]=&part;
    const std::string body="body/"+std::to_string(style.hair)+"/"+std::to_string(style.hat)+"/"+std::to_string(style.brows)+"/"+lod;
    for(const auto& id:{body,"main/"+main,"sub/"+sub}){
        const bool isBody=id==body;
        if(!isBody&&!lodDraw.includeProps)continue;
        const auto& model=data_.model(id);
        std::vector<glm::dmat4> world;std::vector<bool> visible;
        world.reserve(model.nodes.size());visible.reserve(model.nodes.size());
        for(const auto& node:model.nodes){
            CharacterTransform local=node.local;
            const CharacterPartPose* part=nullptr;
            const auto p=parts.find(node.binding);if(p!=parts.end()){part=p->second;local=part->local;}
            if(node.binding=="root")local=frame.root;
            else if(node.binding=="model")local=frame.model;
            else if(node.binding=="kid")local=frame.kid;
            else if(node.binding=="squid")local=frame.squid;
            const auto parent=node.bone>=0?boneWorld[node.bone]:node.parent>=0?world[node.parent]:glm::dmat4(1.0);
            world.push_back(parent*local.matrix());
            visible.push_back(local.visible&&(node.parent<0||visible[node.parent])&&(node.bone<0||frame.kid.visible));
            if(!visible.back()||node.mesh.empty()||(lodDraw.hideTank&&node.binding.rfind("tank/",0)==0))continue;
            auto program=node.material;
            if((program=="brollyInk"||program=="brollyCream")&&part&&part->transparent.value_or(false))program+="_local";
            if(!node.skinned&&materials_.at(program)->skinned)program+="_plain";
            if(!materials_.count(program))throw std::runtime_error("Missing source material variant: "+program);
            const auto projected=projection*view*world.back()[3];
            draws.push_back({&node,world.back(),parent,local.scale,program,projected.z/projected.w,part,isBody});
        }
    }
    std::stable_sort(draws.begin(),draws.end(),[&](const Draw& a,const Draw& b){
        const bool at=materials_.at(a.program)->transparent,bt=materials_.at(b.program)->transparent;
        if(at!=bt)return !at;
        if(a.node->renderOrder!=b.node->renderOrder)return a.node->renderOrder<b.node->renderOrder;
        return at?a.depth>b.depth:false;
    });
    auto material=frame.material;
    const auto outfit=CharacterOutfit(style.outfit);
    material.shirt=outfit.shirt;material.shorts=outfit.shorts;material.shoe=outfit.shoe;material.sole=outfit.sole;
    material.sock=outfit.sock;material.strap=outfit.strap;material.pattern=outfit.pattern;
    material.iris=CharacterIris[style.eyes][0];material.iris2=CharacterIris[style.eyes][1];
    material.freckle=CharacterSkinLighting[style.skin].freckle;
    for(const auto& draw:draws){
        const auto& definition=*materials_.at(draw.program);auto& shader=*(useShadowShader?shadowPrograms_.at(draw.program):programs_.at(draw.program));
        shader.use();for(const auto& [name,value]:definition.uniforms)shader.uniform(name,value);
        shader.uniform("uLodFade",{draw.body?lodDraw.fade:0.0,lodDraw.side});
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,dfg_);shader.uniform("dfgLUT",6.0);
        shader.material(material);shader.lights(lighting,view);shader.matrices(draw.world,view,projection,camera);
        if(useShadowShader){
            const bool receive=shadow&&!shadow->depthOnly&&shadow->texture!=0;
            shader.uniform("receiveShadow",receive?1.0:0.0);
            if(receive){
                const glm::dmat4 bias(glm::dmat4(
                    glm::dvec4(0.5,0.0,0.0,0.0),glm::dvec4(0.0,0.5,0.0,0.0),
                    glm::dvec4(0.0,0.0,0.5,0.0),glm::dvec4(0.5,0.5,0.5,1.0)));
                const auto matrix=bias*shadow->matrix;
                std::vector<double> matrixData(16);
                for(int c=0;c<4;++c)for(int r=0;r<4;++r)matrixData[c*4+r]=matrix[c][r];
                shader.uniform("directionalShadowMatrix[0]",matrixData);
                shader.uniform("directionalLightShadows[0].shadowIntensity",shadow->intensity);
                shader.uniform("directionalLightShadows[0].shadowBias",shadow->bias);
                shader.uniform("directionalLightShadows[0].shadowNormalBias",shadow->normalBias);
                shader.uniform("directionalLightShadows[0].shadowRadius",shadow->radius);
                shader.uniform("directionalLightShadows[0].shadowMapSize",std::vector<double>{static_cast<double>(shadow->size),static_cast<double>(shadow->size)});
                glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,shadow->texture);
                shader.uniform("directionalShadowMap[0]",8.0);
            }else{
                shader.uniform("directionalLightShadows[0].shadowIntensity",0.0);
                shader.uniform("directionalLightShadows[0].shadowBias",0.0);
                shader.uniform("directionalLightShadows[0].shadowNormalBias",0.0);
                shader.uniform("directionalLightShadows[0].shadowRadius",0.0);
                shader.uniform("directionalLightShadows[0].shadowMapSize",std::vector<double>{1.0,1.0});
            }
        }
        if(draw.node->material=="skin"){
            const auto& skin=CharacterSkin[style.skin];const auto& lighting=CharacterSkinLighting[style.skin];
            shader.uniform("diffuse",{skin.x,skin.y,skin.z});shader.uniform("uSkinLum",lighting.luminance);
            shader.uniform("uSSSW",{lighting.scatter.x,lighting.scatter.y,lighting.scatter.z});shader.uniform("uSSSTint",{lighting.tint.x,lighting.tint.y,lighting.tint.z});
        }
        if(draw.node->material=="fill"||draw.node->material=="ink"||draw.node->material=="squid")shader.uniform("diffuse",{material.team.x,material.team.y,material.team.z});
        if(draw.node->material=="fill")shader.uniform("emissive",{material.tankEmissive.x,material.tankEmissive.y,material.tankEmissive.z});
        if(draw.node->material=="glow"){
            shader.uniform("diffuse",{material.team.x*0.3,material.team.y*0.3,material.team.z*0.3});
            shader.uniform("emissive",{material.team.x*material.coilEmissiveIntensity,material.team.y*material.coilEmissiveIntensity,material.team.z*material.coilEmissiveIntensity});
        }
        if(draw.part){
            shader.uniform("emissive",{draw.part->emissive.x*draw.part->emissiveIntensity,draw.part->emissive.y*draw.part->emissiveIntensity,draw.part->emissive.z*draw.part->emissiveIntensity});
            if(draw.part->color){const auto c=*draw.part->color;shader.uniform("diffuse",{c.x,c.y,c.z});if(draw.node->material=="brollyInk")shader.uniform("sheenColor",{c.x*0.5+0.5,c.y*0.5+0.5,c.z*0.5+0.5});}
            if(draw.part->opacity)shader.uniform("opacity",*draw.part->opacity);
            for(const auto& [name,value]:draw.part->uniforms)shader.uniform(name,value);
        }
        if(draw.node->material=="fill"){
            // Source onBeforeRender levels the free surface in the tank parent's frame.
            const auto up=tankUp(draw.parent);
            shader.uniform("uUpL",{up.x,up.y,up.z});
            shader.uniform("uSY",std::max(1e-3,draw.scale.y/std::max(1e-4,(draw.scale.x+draw.scale.z)*0.5)));
        }
        if(draw.node->skinned){shader.bones(frame.skinMatrices);const auto inv=glm::inverse(draw.node->bind);std::vector<double> bind(16),inverse(16);for(int c=0;c<4;++c)for(int r=0;r<4;++r){bind[c*4+r]=draw.node->bind[c][r];inverse[c*4+r]=inv[c][r];}shader.uniform("bindMatrix",bind);shader.uniform("bindMatrixInverse",inverse);}
        if(definition.transparent){glEnable(GL_BLEND);if(definition.blending==2)glBlendFunc(GL_SRC_ALPHA,GL_ONE);else glBlendFuncSeparate(definition.premultipliedAlpha?GL_ONE:GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}else glDisable(GL_BLEND);
        if(definition.side==2)glDisable(GL_CULL_FACE);else {glEnable(GL_CULL_FACE);glCullFace(definition.side==1?GL_FRONT:GL_BACK);}
        glFrontFace(glm::determinant(draw.world)<0?GL_CW:GL_CCW);
        glDepthFunc(definition.depthFunc==6?GL_GREATER:GL_LEQUAL);
        if(definition.depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthMask(definition.depthWrite?GL_TRUE:GL_FALSE);
        const auto& gpu=mesh(draw.node->mesh,draw.program,useShadowShader);glBindVertexArray(gpu.vao);glDrawElements(GL_TRIANGLES,gpu.count,GL_UNSIGNED_INT,nullptr);
    }
    glBindVertexArray(0);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glFrontFace(GL_CCW);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    return draws.size();
}
glm::dmat4 CharacterArt::attachment(const CharacterPoseFrame& frame,const CharacterStyle& style,const std::string& binding)const{
    const auto& rig=data_.rig(style.hair,style.hat,style.brows);
    const auto& model=data_.model("body/"+std::to_string(style.hair)+"/"+std::to_string(style.hat)+"/"+std::to_string(style.brows)+"/game");
    const auto kid=frame.root.matrix()*frame.model.matrix()*frame.kid.matrix();
    std::map<std::string,const CharacterPartPose*> parts;for(const auto& part:frame.parts)parts[part.name]=&part;
    std::vector<glm::dmat4> world;world.reserve(model.nodes.size());
    for(const auto& node:model.nodes){
        auto local=node.local;const auto found=parts.find(node.binding);if(found!=parts.end())local=found->second->local;
        if(node.binding=="root")local=frame.root;else if(node.binding=="model")local=frame.model;else if(node.binding=="kid")local=frame.kid;
        const auto parent=node.bone>=0?kid*frame.skinMatrices[node.bone]*glm::inverse(rig.bones[node.bone].inverseBind):node.parent>=0?world[node.parent]:glm::dmat4(1);
        world.push_back(parent*local.matrix());if(node.binding==binding)return world.back();
    }
    throw std::runtime_error("Missing source character attachment: "+binding);
}
}
