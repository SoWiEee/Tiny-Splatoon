#include "BossRenderer.hpp"
#include "TeamPalette.hpp"
#include "BossPose.hpp"
#include "BossSimulation.hpp"
#include "StageGeometry.hpp"
#include "CharacterShader.hpp"
#include "CharacterRenderData.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <zlib.h>

namespace restoration {
namespace {
GLuint compile(GLenum type,const char* source){
    const GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096]{};glGetShaderInfoLog(shader,sizeof(log),nullptr,log);glDeleteShader(shader);throw std::runtime_error(std::string("Boss shader: ")+log);}return shader;
}
GLuint link(const char* vertex,const char* fragment){
    const GLuint vs=compile(GL_VERTEX_SHADER,vertex);GLuint fs=0;
    try{fs=compile(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(vs);throw;}
    const GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);glDeleteProgram(program);throw std::runtime_error(std::string("Boss program: ")+log);}return program;
}
struct Vertex {float position[3],normal[3],uv[2];std::uint8_t joints[4];float weights[4],material[4];};
struct LineVertex {glm::vec3 position;glm::vec4 color;};
struct GpuMesh {GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;std::string material;};
struct GpuModel {std::vector<GpuMesh> meshes;};
const char* lineVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;layout(location=1) in vec4 color;uniform mat4 vp;out vec4 vColor;
void main(){vColor=color;gl_Position=vp*vec4(position,1.0);})GLSL";
const char* lineFragment=R"GLSL(#version 450 core
in vec4 vColor;out vec4 outColor;void main(){outColor=vColor;})GLSL";
}

struct BossRenderer::State {
    bool environment=false;
    explicit State(std::filesystem::path path):assets(std::move(path)),pose(assets/"model.bin"){}
    std::filesystem::path assets;
    BossPose pose;
    GLuint lineProgram=0,lineVao=0,lineVbo=0,boneTexture=0,dfgTexture=0,stencilTexture=0;
    std::map<std::string,std::unique_ptr<CharacterShader>> shaders;
    GpuModel hull,crablet;
    bool ready=false;
    ~State(){
        for(auto* model:{&hull,&crablet})for(auto& mesh:model->meshes){if(mesh.ebo)glDeleteBuffers(1,&mesh.ebo);if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);}
        if(lineVbo)glDeleteBuffers(1,&lineVbo);if(lineVao)glDeleteVertexArrays(1,&lineVao);if(lineProgram)glDeleteProgram(lineProgram);
        const GLuint textures[]={boneTexture,dfgTexture,stencilTexture};glDeleteTextures(3,textures);
    }
    GLuint readStencil(){
        std::ifstream file(assets/"shaders"/"stencil.tex.z",std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
        if(bytes.size()<24||std::string(bytes.begin(),bytes.begin()+8)!="TSTEX001")throw std::runtime_error("Invalid source Boss stencil header");
        const auto u32=[&](std::size_t at){return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);};
        const auto width=u32(8),height=u32(12),layers=u32(16),srgb=u32(20);
        if(width!=1024||height!=1024||layers!=1||srgb!=0)throw std::runtime_error("Invalid source Boss stencil dimensions");
        std::vector<unsigned char> pixels(std::size_t(width)*height*4);uLongf size=static_cast<uLongf>(pixels.size());
        if(uncompress(pixels.data(),&size,bytes.data()+24,static_cast<uLong>(bytes.size()-24))!=Z_OK||size!=pixels.size())throw std::runtime_error("Invalid compressed source Boss stencil");
        GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glGenerateMipmap(GL_TEXTURE_2D);return texture;
    }
    void loadShader(const std::string& name){
        const auto directory=assets/"shaders";
        auto shader=std::make_unique<CharacterShader>(CharacterShader::readSource(directory/(name+".vert")),CharacterShader::readSource(directory/(name+".frag")),true,environment);
        shader->use();std::ifstream defaults(directory/(name+".uniforms"));std::string key;std::size_t count;
        if(!defaults)throw std::runtime_error("Missing source Boss material defaults: "+name);
        while(defaults>>key>>count){if(!count||count>64)throw std::runtime_error("Invalid source Boss uniform shape: "+key);std::vector<double> values(count);for(auto& value:values)if(!(defaults>>value))throw std::runtime_error("Truncated source Boss material defaults: "+key);shader->uniform(key,values);}
        shader->uniform("uStencil",8);shader->uniform("dfgLUT",6);shader->uniform("boneTexture",7);
        shaders.emplace(name,std::move(shader));
    }
    void ensureGpu(){
        if(ready)return;
        GLint previousProgram=0,previousVao=0,previousBuffer=0,previousActive=0,previousTexture=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previousVao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previousBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&previousActive);glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
        for(const auto* name:{"steel","cara","flesh","eye","junk","crablet"})loadShader(name);
        lineProgram=link(lineVertex,lineFragment);
        glGenTextures(1,&boneTexture);glBindTexture(GL_TEXTURE_2D,boneTexture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        const std::array<float,100*16> identity=[](){std::array<float,100*16> data{};for(std::size_t i=0;i<100;++i)for(std::size_t d=0;d<4;++d)data[i*16+d*5]=1;return data;}();
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,4,100,0,GL_RGBA,GL_FLOAT,identity.data());
        glGenTextures(1,&dfgTexture);glBindTexture(GL_TEXTURE_2D,dfgTexture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,16,16,0,GL_RG,GL_HALF_FLOAT,CharacterDfg.data());
        stencilTexture=readStencil();
        const auto upload=[&](const BossModelAsset& source,GpuModel& target){
            for(const auto& mesh:source.meshes){
                std::vector<Vertex> vertices;vertices.reserve(mesh.vertices.size());
                for(const auto& v:mesh.vertices){Vertex out{};for(int i=0;i<3;++i){out.position[i]=v.position[i];out.normal[i]=v.normal[i];}for(int i=0;i<2;++i)out.uv[i]=v.uv[i];for(int i=0;i<4;++i){out.joints[i]=v.joints[i];out.weights[i]=v.weights[i];out.material[i]=v.material[i];}vertices.push_back(out);}
                GpuMesh gpu;gpu.count=static_cast<GLsizei>(mesh.indices.size());gpu.material=mesh.name;
                const auto& shader=*shaders.at(mesh.name);
                glGenVertexArrays(1,&gpu.vao);glGenBuffers(1,&gpu.vbo);glGenBuffers(1,&gpu.ebo);glBindVertexArray(gpu.vao);
                glBindBuffer(GL_ARRAY_BUFFER,gpu.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh.indices.size()*sizeof(std::uint32_t),mesh.indices.data(),GL_STATIC_DRAW);
                const auto attribute=[&](const char* name,GLint size,GLenum type,std::size_t offset){const GLint location=shader.attribute(name);if(location<0)return;glEnableVertexAttribArray(static_cast<GLuint>(location));glVertexAttribPointer(static_cast<GLuint>(location),size,type,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offset));};
                attribute("position",3,GL_FLOAT,offsetof(Vertex,position));attribute("normal",3,GL_FLOAT,offsetof(Vertex,normal));attribute("uv",2,GL_FLOAT,offsetof(Vertex,uv));
                attribute("skinIndex",4,GL_UNSIGNED_BYTE,offsetof(Vertex,joints));attribute("skinWeight",4,GL_FLOAT,offsetof(Vertex,weights));attribute("aM",4,GL_FLOAT,offsetof(Vertex,material));
                glBindVertexArray(0);target.meshes.push_back(gpu);
            }
        };
        upload(pose.hullAsset(),hull);upload(pose.crabletAsset(),crablet);
        glGenVertexArrays(1,&lineVao);glGenBuffers(1,&lineVbo);glBindVertexArray(lineVao);glBindBuffer(GL_ARRAY_BUFFER,lineVbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(LineVertex),reinterpret_cast<void*>(offsetof(LineVertex,position)));
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(LineVertex),reinterpret_cast<void*>(offsetof(LineVertex,color)));
        glBindVertexArray(static_cast<GLuint>(previousVao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(previousBuffer));glUseProgram(static_cast<GLuint>(previousProgram));
        glActiveTexture(static_cast<GLenum>(previousActive));glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture));ready=true;
    }
    void drawModel(const GpuModel& model,const BossRigPose& rig,const glm::dmat4& view,const glm::dmat4& projection,
                   glm::dvec3 camera,const CharacterLightState& lights){
        if(rig.skin.size()>100)throw std::runtime_error("Boss skin palette exceeds source material limit");
        std::array<float,100*16> bones{};for(std::size_t i=0;i<100;++i)for(std::size_t d=0;d<4;++d)bones[i*16+d*5]=1;
        for(std::size_t i=0;i<rig.skin.size();++i){const auto* matrix=glm::value_ptr(rig.skin[i]);for(std::size_t j=0;j<16;++j)bones[i*16+j]=static_cast<float>(matrix[j]);}
        glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,boneTexture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,100,GL_RGBA,GL_FLOAT,bones.data());
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,dfgTexture);glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,stencilTexture);
        static const std::vector<double> identityMatrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for(const auto& mesh:model.meshes){
            auto& shader=*shaders.at(mesh.material);shader.use();
            shader.matrices(rig.root,view,projection,camera);shader.lights(lights,view);
            shader.uniform("boneTexture",7);shader.uniform("bindMatrix",identityMatrix);shader.uniform("bindMatrixInverse",identityMatrix);
            shader.uniform("dfgLUT",6);shader.uniform("uStencil",8);
            for(const auto& [name,values]:pose.materialUniforms())shader.uniform(name,values);
            shader.uniform("uInk",{InkColors[1].r,InkColors[1].g,InkColors[1].b});
            shader.uniform("uWeak",{InkColors[0].r,InkColors[0].g,InkColors[0].b});
            glBindVertexArray(mesh.vao);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);
        }
    }
};

BossRenderer::BossRenderer(std::filesystem::path assets,bool environment):state_(std::make_unique<State>(std::move(assets))){state_->environment=environment;}
BossRenderer::~BossRenderer()=default;
void BossRenderer::Update(BossSimulation& boss){state_->pose.Update(boss);}
void BossRenderer::SetAudioEventSink(std::function<void(const BossPoseAudioEvent&)> sink){state_->pose.SetAudioEventSink(std::move(sink));}
int BossRenderer::DrawShadow(const BossSimulation& boss,const glm::mat4& lightViewProjection){
    if(!boss.state().visible)return 0;
    state_->ensureGpu();
    GLint oldProgram=0,oldVao=0,oldActive=0,textures[3]{};glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);}glActiveTexture(oldActive);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    const glm::dmat4 identity(1.0),projection(lightViewProjection);CharacterLightState lights;lights.ambient={.22f,.22f,.22f};lights.sunDirection=glm::normalize(glm::vec3(-.4f,.85f,.35f));lights.sunColor={1.0f,.92f,.82f};lights.skyDirection={0,1,0};lights.skyColor={.32f,.42f,.5f};lights.groundColor={.18f,.15f,.12f};
    state_->drawModel(state_->hull,state_->pose.hullbreaker(),identity,projection,glm::dvec3(0),lights);
    for(const auto& crab:state_->pose.crablets()){BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;state_->drawModel(state_->crablet,pose,identity,projection,glm::dvec3(0),lights);}
    glBindVertexArray(static_cast<GLuint>(oldVao));glUseProgram(static_cast<GLuint>(oldProgram));for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i]));}glActiveTexture(oldActive);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    return static_cast<int>(state_->hull.meshes.size()+state_->crablet.meshes.size()*state_->pose.crablets().size());
}
int BossRenderer::Draw(const BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,const glm::vec3& camera,const glm::vec3& squadColor,const CharacterLightState* sceneLights){
    if(!boss.state().visible)return 0;state_->ensureGpu();const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);GLint oldProgram=0,oldVao=0,oldActive=0,textures[3]{};glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);}glActiveTexture(oldActive);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    CharacterLightState lights;lights.ambient={.22f,.22f,.22f};lights.sunDirection=glm::normalize(glm::vec3(-.4f,.85f,.35f));lights.sunColor={1.0f,.92f,.82f};lights.skyDirection={0,1,0};lights.skyColor={.32f,.42f,.5f};lights.groundColor={.18f,.15f,.12f};
    if(sceneLights)lights=*sceneLights;
    (void)squadColor;const glm::dmat4 viewD(view),projectionD(projection);const glm::dvec3 cameraD(camera);
    state_->drawModel(state_->hull,state_->pose.hullbreaker(),viewD,projectionD,cameraD,lights);
    for(const auto& crab:state_->pose.crablets()){
        BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;state_->drawModel(state_->crablet,pose,viewD,projectionD,cameraD,lights);
    }
    glBindVertexArray(static_cast<GLuint>(oldVao));glUseProgram(static_cast<GLuint>(oldProgram));for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i]));}glActiveTexture(oldActive);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    return static_cast<int>(state_->hull.meshes.size()+state_->crablet.meshes.size()*state_->pose.crablets().size());
}

int BossRenderer::DrawHazards(const BossSimulation& boss,const StageGeometry& geometry,const glm::mat4& viewProjection,const glm::vec3&){
    std::vector<LineVertex> vertices;vertices.reserve(16000);
    const auto segment=[&](glm::dvec3 a,glm::dvec3 b,glm::vec4 color){vertices.push_back({glm::vec3(a),color});vertices.push_back({glm::vec3(b),color});};
    const auto circle=[&](double x,double y,double z,double radius,glm::vec4 color,int n=72){if(!(radius>0))return;for(int i=0;i<n;++i){const double a=i*2*3.14159265358979323846/n,b=(i+1)*2*3.14159265358979323846/n;segment({x+std::cos(a)*radius,y,z+std::sin(a)*radius},{x+std::cos(b)*radius,y,z+std::sin(b)*radius},color);}};
    for(const auto& move:boss.moves()){
        const double u=boss.state().clock-move.t0;if(u<0||u>move.end()+1.5)continue;
        const bool tele=u<move.tele,active=u>=move.tele&&u<move.tele+move.act;
        const glm::vec4 color=tele?glm::vec4(1.0f,.62f,.08f,.74f):active?glm::vec4(1.0f,.13f,.055f,.75f):glm::vec4(.1f,.55f,1.0f,.55f);
        if(move.id==BossMoveId::Slam){
            for(const double offset:move.rings){const double age=u-move.tele-offset;if(age>=-.35&&age<1.65){const double radius=std::max(.5,2.4+9.5*age);circle(move.point.x,move.point.y+.08,move.point.z,radius,color);}}
            if(tele)circle(move.point.x,move.point.y+.1,move.point.z,2.9,color);
        }else if(move.id==BossMoveId::Barrage){
            for(const auto& barrel:move.barrels)if(u>=barrel.throwAt-.65&&u<barrel.throwAt+1.3)circle(barrel.x,barrel.y+.06,barrel.z,2.35,color);
        }else if(move.id==BossMoveId::Sweep){
            double angle=BossSimulation::beamAngle(move,boss.state().clock),length=18.0;
            const auto ray=geometry.raycast(move.origin,{std::sin(angle),0,std::cos(angle)},length,true);if(ray.hit)length=ray.dist;
            const glm::dvec3 end=move.origin+glm::dvec3(std::sin(angle),0,std::cos(angle))*length;
            segment(move.origin,end,color);segment(end+glm::dvec3(.22,.02,.22),end-glm::dvec3(.22,.02,.22),color);
        }else if(move.id==BossMoveId::Charge){
            const double distance=BossSimulation::chargeDistance(move,boss.state().clock),length=active?distance:move.length;
            const glm::dvec3 f(std::sin(move.yaw),0,std::cos(move.yaw)),side(f.z,0,-f.x),start=move.point;
            segment(start+side*1.45,start+f*length+side*1.45,color);segment(start-side*1.45,start+f*length-side*1.45,color);
        }else if(move.id==BossMoveId::Frenzy){
            for(int i=0;i<8;++i){const double angle=move.rot0+i*3.14159265358979323846/4;const glm::dvec3 a=move.point+glm::dvec3(std::sin(angle)*1.2,0,std::cos(angle)*1.2),b=move.point+glm::dvec3(std::sin(angle)*9,0,std::cos(angle)*9);segment(a,b,color);}
        }
    }
    if(vertices.empty())return 0;state_->ensureGpu();GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);GLint srcRgb=0,dstRgb=0;glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);
    glUseProgram(state_->lineProgram);glUniformMatrix4fv(glGetUniformLocation(state_->lineProgram,"vp"),1,GL_FALSE,&viewProjection[0][0]);glBindVertexArray(state_->lineVao);glBindBuffer(GL_ARRAY_BUFFER,state_->lineVbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(LineVertex),vertices.data(),GL_STREAM_DRAW);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDrawArrays(GL_LINES,0,static_cast<GLsizei>(vertices.size()));glDepthMask(GL_TRUE);glBindVertexArray(0);glUseProgram(static_cast<GLuint>(oldProgram));
    glBlendFunc(srcRgb,dstRgb);if(!blend)glDisable(GL_BLEND);if(!depth)glDisable(GL_DEPTH_TEST);return 1;
}
} // namespace restoration
