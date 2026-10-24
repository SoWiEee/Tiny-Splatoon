#include "WorldFxParticleRenderer.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace restoration {
namespace {
struct GpuInstance {
    std::array<float, 4> pos{};
    std::array<float, 4> velocity{};
    std::array<float, 4> color{};
    std::array<float, 4> misc{};
    std::array<float, 4> axis{};
};
struct MeshData { std::vector<float> positions; std::vector<std::uint32_t> indices; };
struct BossBlobVertex {std::array<float,3> position{},normal{};std::array<float,2> uv{};};
struct BossBlobMeshData {std::vector<BossBlobVertex> vertices;std::vector<std::uint32_t> indices;};

class MeshReader {
public:
    explicit MeshReader(const std::filesystem::path& path):in_(path,std::ios::binary) {
        if(!in_)throw std::runtime_error("Cannot read source world FX mesh: "+path.string());
        char magic[8]{};in_.read(magic,8);
        if(!in_||std::string(magic,8)!="TSWFX001")throw std::runtime_error("Invalid source world FX mesh: "+path.string());
    }
    std::uint32_t u32(){std::array<unsigned char,4>b{};in_.read(reinterpret_cast<char*>(b.data()),4);if(!in_)throw std::runtime_error("Truncated source world FX mesh");return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    float f32(){const auto bits=u32();float v;std::memcpy(&v,&bits,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite source world FX vertex");return v;}
    void end(){if(in_.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Trailing source world FX mesh data");}
private: std::ifstream in_;
};

MeshData readMesh(const std::filesystem::path& path) {
    MeshReader reader(path);const auto vertices=reader.u32(),indices=reader.u32();
    if(!vertices||vertices>10000000||!indices||indices>30000000||indices%3)throw std::runtime_error("Invalid source world FX mesh dimensions");
    MeshData data;data.positions.resize(std::size_t(vertices)*3);data.indices.resize(indices);
    for(auto& v:data.positions)v=reader.f32();
    for(auto& i:data.indices){i=reader.u32();if(i>=vertices)throw std::runtime_error("Source world FX index out of range");}
    reader.end();return data;
}
class BossBlobMeshReader {
public:
    explicit BossBlobMeshReader(const std::filesystem::path& path):in_(path,std::ios::binary) {
        if(!in_)throw std::runtime_error("Cannot read source BossFX pop mesh: "+path.string());
        char magic[8]{};in_.read(magic,8);
        if(!in_||std::string(magic,8)!="TSBOSSFX")throw std::runtime_error("Invalid source BossFX pop mesh: "+path.string());
    }
    std::uint32_t u32(){std::array<unsigned char,4>b{};in_.read(reinterpret_cast<char*>(b.data()),4);if(!in_)throw std::runtime_error("Truncated source BossFX pop mesh");return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    float f32(){const auto bits=u32();float v;std::memcpy(&v,&bits,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite source BossFX pop vertex");return v;}
    void end(){if(in_.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Trailing source BossFX pop mesh data");}
private:std::ifstream in_;
};
BossBlobMeshData readBossBlobMesh(const std::filesystem::path& path){
    BossBlobMeshReader reader(path);const auto vertices=reader.u32(),indices=reader.u32();
    if(!vertices||vertices>100000||!indices||indices>300000||indices%3)throw std::runtime_error("Invalid source BossFX pop mesh dimensions");
    BossBlobMeshData data;data.vertices.resize(vertices);data.indices.resize(indices);
    for(auto& v:data.vertices){for(auto& x:v.position)x=reader.f32();for(auto& x:v.normal)x=reader.f32();for(auto& x:v.uv)x=reader.f32();}
    for(auto& i:data.indices){i=reader.u32();if(i>=vertices)throw std::runtime_error("Source BossFX pop index out of range");}
    reader.end();return data;
}
float tOf(const WorldFxParticle& p){return p.life>0?p.age/p.life:0;}
std::array<float,4> v4(glm::vec3 v,float w){return {v.x,v.y,v.z,w};}
glm::vec3 safeUnit(glm::vec3 v,glm::vec3 fallback){const float n=glm::length(v);return n>1e-6f?v/n:fallback;}

struct ScopedGlState {
    GLint program=0,vao=0,arrayBuffer=0,srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0,
          equationRgb=0,equationAlpha=0,cullMode=0,frontFace=0,depthFunc=0,
          activeTexture=GL_TEXTURE0,texture6=0,texture9=0;
    GLfloat polygonFactor=0,polygonUnits=0;
    GLboolean depthMask=GL_TRUE;
    GLboolean blend=GL_FALSE,cull=GL_FALSE,depth=GL_FALSE,alphaCoverage=GL_FALSE,polygonOffset=GL_FALSE;
    ScopedGlState(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
        glActiveTexture(GL_TEXTURE6);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture6);
        glActiveTexture(GL_TEXTURE9);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture9);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&equationRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equationAlpha);glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);glGetIntegerv(GL_FRONT_FACE,&frontFace);glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&polygonFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&polygonUnits);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);depth=glIsEnabled(GL_DEPTH_TEST);alphaCoverage=glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);polygonOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    }
    ~ScopedGlState(){
        glBindVertexArray(static_cast<GLuint>(vao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(arrayBuffer));glUseProgram(static_cast<GLuint>(program));
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture6));
        glActiveTexture(GL_TEXTURE9);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture9));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBlendEquationSeparate(static_cast<GLenum>(equationRgb),static_cast<GLenum>(equationAlpha));glBlendFuncSeparate(static_cast<GLenum>(srcRgb),static_cast<GLenum>(dstRgb),static_cast<GLenum>(srcAlpha),static_cast<GLenum>(dstAlpha));
        glCullFace(static_cast<GLenum>(cullMode));glFrontFace(static_cast<GLenum>(frontFace));glDepthFunc(static_cast<GLenum>(depthFunc));glPolygonOffset(polygonFactor,polygonUnits);glDepthMask(depthMask);
        auto set=[](GLenum cap,GLboolean enabled){if(enabled)glEnable(cap);else glDisable(cap);};
        set(GL_BLEND,blend);set(GL_CULL_FACE,cull);set(GL_DEPTH_TEST,depth);set(GL_SAMPLE_ALPHA_TO_COVERAGE,alphaCoverage);set(GL_POLYGON_OFFSET_FILL,polygonOffset);
    }
};
}

struct WorldFxParticleRenderer::Pool {
    std::string name;
    std::unique_ptr<CharacterShader> shader;
    GLuint vao=0,vertex=0,index=0,instances=0;
    GLsizei indexCount=0;
    bool transparent=false,additive=false,alphaToCoverage=false,doubleSided=false,polygonOffset=false;
    float polygonFactor=0,polygonUnits=0;
    std::vector<GpuInstance> items;
    ~Pool(){if(instances)glDeleteBuffers(1,&instances);if(index)glDeleteBuffers(1,&index);if(vertex)glDeleteBuffers(1,&vertex);if(vao)glDeleteVertexArrays(1,&vao);}
};

WorldFxParticleRenderer::WorldFxParticleRenderer(std::filesystem::path assets,std::filesystem::path bossAssets)
    :assets_(std::move(assets)),bossAssets_(std::move(bossAssets)){}

void WorldFxParticleRenderer::initialize(){
    if(pools_.back())return;
    ScopedGlState state;
    const std::array<const char*,6> names={"drops","puffs","glows","rings","shells","beams"};
    for(std::size_t i=0;i<names.size();++i){
        if(pools_[i])continue;
        auto pool=std::make_unique<Pool>();pool->name=names[i];
        const auto base=assets_/names[i];
        pool->shader=std::make_unique<CharacterShader>(CharacterShader::readSource(base.string()+".vert"),CharacterShader::readSource(base.string()+".frag"),true,false,false);
        const auto mesh=readMesh(base.string()+".mesh");pool->indexCount=static_cast<GLsizei>(mesh.indices.size());
        const bool sprite=i==1||i==2;
        pool->transparent=sprite||i==3||i==5;pool->additive=i==2;pool->alphaToCoverage=i==0||i==4;pool->doubleSided=i==3||i==4||i==5;pool->polygonOffset=i==3;
        if(pool->polygonOffset){pool->polygonFactor=-4;pool->polygonUnits=-4;}
        glGenVertexArrays(1,&pool->vao);glBindVertexArray(pool->vao);
        glGenBuffers(1,&pool->vertex);glBindBuffer(GL_ARRAY_BUFFER,pool->vertex);glBufferData(GL_ARRAY_BUFFER,mesh.positions.size()*sizeof(float),mesh.positions.data(),GL_STATIC_DRAW);
        const GLint position=pool->shader->attribute("position");if(position<0)throw std::runtime_error("Source world FX shader has no position attribute: "+pool->name);
        glEnableVertexAttribArray(static_cast<GLuint>(position));glVertexAttribPointer(static_cast<GLuint>(position),3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
        glGenBuffers(1,&pool->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,pool->index);glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh.indices.size()*sizeof(std::uint32_t),mesh.indices.data(),GL_STATIC_DRAW);
        glGenBuffers(1,&pool->instances);
        const std::array<std::pair<const char*,std::size_t>,7> attributes={{{"aPosR",offsetof(GpuInstance,pos)},{"aPosSize",offsetof(GpuInstance,pos)},{"aVelS",offsetof(GpuInstance,velocity)},{"aNrmT",offsetof(GpuInstance,axis)},{"aColA",offsetof(GpuInstance,color)},{"aMisc",offsetof(GpuInstance,misc)},{"aAxis",offsetof(GpuInstance,axis)}}};
        glBindBuffer(GL_ARRAY_BUFFER,pool->instances);
        for(const auto& [name,offset]:attributes){const GLint location=pool->shader->attribute(name);if(location<0)continue;glEnableVertexAttribArray(static_cast<GLuint>(location));glVertexAttribPointer(static_cast<GLuint>(location),4,GL_FLOAT,GL_FALSE,sizeof(GpuInstance),reinterpret_cast<const void*>(offset));glVertexAttribDivisor(static_cast<GLuint>(location),1);}
        pools_[i]=std::move(pool);
    }
}

struct WorldFxParticleRenderer::BossBlobPool {
    std::unique_ptr<CharacterShader> shader;
    GLuint vao=0,vertex=0,index=0,dfg=0;
    GLsizei indexCount=0;
    std::vector<std::pair<std::string,std::vector<double>>> defaults;
    ~BossBlobPool(){if(dfg)glDeleteTextures(1,&dfg);if(index)glDeleteBuffers(1,&index);if(vertex)glDeleteBuffers(1,&vertex);if(vao)glDeleteVertexArrays(1,&vao);}
};
WorldFxParticleRenderer::~WorldFxParticleRenderer()=default;

void WorldFxParticleRenderer::initializeBossBlob(){
    if(bossBlobPool_)return;
    ScopedGlState state;
    const auto base=bossAssets_/"crablet-pop-blob";
    auto pool=std::make_unique<BossBlobPool>();
    pool->shader=std::make_unique<CharacterShader>(CharacterShader::readSource(base.string()+".vert"),CharacterShader::readSource(base.string()+".frag"),true,false,false);
    glActiveTexture(GL_TEXTURE6);glGenTextures(1,&pool->dfg);glBindTexture(GL_TEXTURE_2D,pool->dfg);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());
    {
        std::ifstream defaults(base.string()+".uniforms");std::string key;std::size_t count=0;
        if(!defaults)throw std::runtime_error("Missing source BossFX pop material defaults");
        while(defaults>>key>>count){
            if(!count||count>64)throw std::runtime_error("Invalid source BossFX pop uniform shape: "+key);
            std::vector<double> values(count);for(auto& value:values)if(!(defaults>>value)||!std::isfinite(value))throw std::runtime_error("Truncated source BossFX pop uniform: "+key);
            pool->defaults.emplace_back(std::move(key),std::move(values));
        }
    }
    const auto mesh=readBossBlobMesh(base.string()+".mesh");
    pool->indexCount=static_cast<GLsizei>(mesh.indices.size());
    glGenVertexArrays(1,&pool->vao);glBindVertexArray(pool->vao);
    glGenBuffers(1,&pool->vertex);glBindBuffer(GL_ARRAY_BUFFER,pool->vertex);
    glBufferData(GL_ARRAY_BUFFER,mesh.vertices.size()*sizeof(BossBlobVertex),mesh.vertices.data(),GL_STATIC_DRAW);
    const auto attribute=[&](const char* name,GLint size,std::size_t offset,bool required){
        const GLint location=pool->shader->attribute(name);
        if(location<0){if(required)throw std::runtime_error(std::string("Source BossFX shader is missing ")+name);return;}
        glEnableVertexAttribArray(static_cast<GLuint>(location));glVertexAttribPointer(static_cast<GLuint>(location),size,GL_FLOAT,GL_FALSE,sizeof(BossBlobVertex),reinterpret_cast<const void*>(offset));
    };
    attribute("position",3,offsetof(BossBlobVertex,position),true);attribute("normal",3,offsetof(BossBlobVertex,normal),true);attribute("uv",2,offsetof(BossBlobVertex,uv),false);
    glGenBuffers(1,&pool->index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,pool->index);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh.indices.size()*sizeof(std::uint32_t),mesh.indices.data(),GL_STATIC_DRAW);
    bossBlobPool_=std::move(pool);
}

int WorldFxParticleRenderer::Draw(const std::vector<WorldFxParticle>& live,const std::vector<WorldFxParticle>& world,
    const glm::mat4& view,const glm::mat4& projection,const glm::vec3& eye,const CharacterLightState* lights,double time){
    ScopedGlState state;initialize();
    for(auto& pool:pools_)pool->items.clear();
    std::vector<const WorldFxParticle*> bossBlobs;
    const auto append=[&](const std::vector<WorldFxParticle>& particles){
        for(const auto& p:particles){
            if(p.shape==WorldFxShape::BossBlob){bossBlobs.push_back(&p);continue;}
            const int index=static_cast<int>(p.shape);if(index<0||index>=6)continue;
            const float radius=p.renderRadius>0?p.renderRadius:(p.shape==WorldFxShape::Ring?p.radius:0);
            if(!(radius>0))continue;
            GpuInstance item{};const float t=tOf(p);
            item.pos=v4(p.pos,radius);
            switch(p.shape){
            case WorldFxShape::Drop:item.velocity=v4(p.vel,p.renderLength);item.color=v4(p.color,(static_cast<int>(p.flags)&16)?0.0f:1.0f);break;
            case WorldFxShape::Puff:case WorldFxShape::Glow:item.color=v4(p.color,p.renderAlpha);item.misc={p.rotation,p.seed,t,p.kind};break;
            case WorldFxShape::Ring:item.velocity={};item.color=v4(p.color,p.alpha);item.misc={p.seed,p.style,p.length,0};item.axis=v4(p.normal,t);break;
            case WorldFxShape::Shell:item.color=v4(p.color,p.alpha);item.misc={t,p.seed,p.kind,p.renderLength};item.axis=v4(p.normal,p.crown);break;
            case WorldFxShape::Beam:item.color=v4(p.color,p.renderAlpha);item.misc={t,p.renderLength,p.seed,0};break;
            default:continue;
            }
            if(p.alpha<=0&&p.shape!=WorldFxShape::Drop)continue;
            pools_[static_cast<std::size_t>(index)]->items.push_back(item);
        }
    };
    append(live);append(world);
    CharacterLightState fallback{};
    fallback.sunDirection=glm::normalize(glm::vec3(-0.41f,0.83f,-0.38f));fallback.sunColor={1.0f,0.93f,0.84f};
    fallback.skyColor={0.42f,0.62f,0.95f};fallback.groundColor={0.5f,0.46f,0.42f};
    const auto& light=lights?*lights:fallback;
    const glm::mat3 viewRotation(view);
    const auto sunV=safeUnit(viewRotation*light.sunDirection,glm::vec3(0,1,0));
    const auto upV=safeUnit(viewRotation*glm::vec3(0,1,0),glm::vec3(0,1,0));
    int draws=0;
    constexpr std::array<std::size_t,6> drawOrder={0,4,3,1,5,2};
    for(const auto i:drawOrder){auto& pool=*pools_[i];if(pool.items.empty())continue;
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glFrontFace(GL_CCW);
        if(pool.transparent){glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glDepthMask(GL_FALSE);
            glBlendFuncSeparate(GL_SRC_ALPHA,pool.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE, pool.additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);}
        else{glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
        if(pool.doubleSided)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(GL_BACK);}
        if(pool.alphaToCoverage)glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        if(pool.polygonOffset){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(pool.polygonFactor,pool.polygonUnits);}else glDisable(GL_POLYGON_OFFSET_FILL);
        auto& shader=*pool.shader;shader.use();shader.matrices(glm::dmat4(1),glm::dmat4(view),glm::dmat4(projection),glm::dvec3(eye));shader.uniform("uTime",time);
        if(i==0){shader.uniform("uSunDirV",{sunV.x,sunV.y,sunV.z});shader.uniform("uUpV",{upV.x,upV.y,upV.z});shader.uniform("uSunCol",{light.sunColor.r,light.sunColor.g,light.sunColor.b});shader.uniform("uSkyCol",{light.skyColor.r,light.skyColor.g,light.skyColor.b});shader.uniform("uGroundCol",{light.groundColor.r,light.groundColor.g,light.groundColor.b});}
        if(i==4){shader.uniform("uSunDir",{light.sunDirection.x,light.sunDirection.y,light.sunDirection.z});shader.uniform("uSunCol",{light.sunColor.r,light.sunColor.g,light.sunColor.b});shader.uniform("uSkyCol",{light.skyColor.r,light.skyColor.g,light.skyColor.b});}
        glBindVertexArray(pool.vao);glBindBuffer(GL_ARRAY_BUFFER,pool.instances);glBufferData(GL_ARRAY_BUFFER,pool.items.size()*sizeof(GpuInstance),pool.items.data(),GL_STREAM_DRAW);
        glDrawElementsInstanced(GL_TRIANGLES,pool.indexCount,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(pool.items.size()));++draws;
    }
    if(!bossBlobs.empty()){
        initializeBossBlob();auto& pool=*bossBlobPool_;auto& shader=*pool.shader;
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);glDisable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);shader.use();glBindVertexArray(pool.vao);
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,pool.dfg);shader.uniform("dfgLUT",6.0);
        for(const auto& [name,values]:pool.defaults)shader.uniform(name,values);
        shader.lights(light,glm::dmat4(view));
        for(const auto* item:bossBlobs){
            glm::dmat4 orientation(1.0);
            if(item->renderScale.y>=item->renderScale.x*0.5f){
                const glm::dvec3 velocity(item->vel);
                const double speed=glm::length(velocity);
                if(speed>1e-8){
                    const glm::dvec3 forward=velocity/speed,sourceForward(0,0,1);
                    const double r=1.0+glm::dot(sourceForward,forward);
                    glm::dquat rotation;
                    if(r<1e-12)rotation=glm::dquat(0.0,0.0,1.0,0.0); // THREE's deterministic +Z antiparallel axis.
                    else {
                        const glm::dvec3 axis=glm::cross(sourceForward,forward);
                        rotation=glm::normalize(glm::dquat(r,axis.x,axis.y,axis.z));
                    }
                    orientation=glm::mat4_cast(rotation);
                }
            }
            auto model=glm::translate(glm::dmat4(1),glm::dvec3(item->pos))*orientation;
            model=glm::scale(model,glm::dvec3(item->renderScale));
            shader.uniform("diffuse",{item->color.r,item->color.g,item->color.b});
            shader.matrices(model,glm::dmat4(view),glm::dmat4(projection),glm::dvec3(eye));
            glDrawElements(GL_TRIANGLES,pool.indexCount,GL_UNSIGNED_INT,nullptr);++draws;
        }
    }
    return draws;
}
}
