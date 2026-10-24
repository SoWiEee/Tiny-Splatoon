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
#include <glm/gtc/matrix_transform.hpp>
#include <zlib.h>

namespace restoration {
namespace {
constexpr std::size_t BossReachSegments=96;
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
struct HazardVertex {float position[3],normal[3],uv[2],angle,side,reach,radial;};
struct GpuMesh {GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;std::string material;};
struct GpuModel {std::vector<GpuMesh> meshes;};
enum class HazardMaterial { Ring, Fan, Lane, Beam, Barrel };
struct HazardBatch {
    HazardMaterial material=HazardMaterial::Fan;
    std::vector<HazardVertex> vertices;
    glm::dvec3 center{0};
    double radius=0,width=0,alpha=1,time=0,k=0;
};
HazardVertex hazardVertex(glm::dvec3 p=glm::dvec3(0.0),glm::dvec3 n=glm::dvec3(0,1,0),glm::dvec2 uv=glm::dvec2(0.0),double angle=0,double side=0,double reach=0,double radial=0){
    return {{float(p.x),float(p.y),float(p.z)},{float(n.x),float(n.y),float(n.z)},{float(uv.x),float(uv.y)},float(angle),float(side),float(reach),float(radial)};
}
const char* ringVertex=R"GLSL(#version 450 core
layout(location=3) in float aA;layout(location=4) in float aSide;layout(location=5) in float aReach;
uniform mat4 vp;uniform vec3 uC;uniform float uR;uniform float uW;
out float vSide;out float vCut;out float vA;
void main(){float r=max(0.0,uR+aSide*uW);vSide=aSide;vCut=aReach-uR;vA=aA;gl_Position=vp*vec4(uC.x+sin(aA)*r,uC.y,uC.z+cos(aA)*r,1.0);})GLSL";
const char* ringFragment=R"GLSL(#version 450 core
uniform vec3 uColor;uniform float uAlpha;uniform float uTime;in float vSide;in float vCut;in float vA;out vec4 outColor;
void main(){if(vCut<0.0)discard;float x=vSide*.5+.5;float band=smoothstep(0.0,.35,x)*(1.0-smoothstep(.8,1.0,x));float crest=smoothstep(.55,.9,x)*(1.0-smoothstep(.9,1.0,x));float a=max(band*(.75+.25*sin(vA*70.0+uTime*10.0)),crest);outColor=vec4(mix(uColor,vec3(1.0),crest*.45)*1.4,a*uAlpha*smoothstep(0.0,.7,vCut));})GLSL";
const char* fanVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;layout(location=6) in float aR;uniform mat4 vp;out float vR;
void main(){vR=aR;gl_Position=vp*vec4(position,1.0);})GLSL";
const char* fanFragment=R"GLSL(#version 450 core
uniform vec3 uColor;uniform float uAlpha;uniform float uTime;uniform float uK;in float vR;out vec4 outColor;
void main(){float rim=smoothstep(.86,.99,vR);float stripes=step(.6,fract(vR*7.0-uTime*2.2));float a=max(rim*.9,stripes*.35*uK)+.08+.12*uK;outColor=vec4(mix(uColor,vec3(1.0),rim*.2)*1.35,a*uAlpha);})GLSL";
const char* laneVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;layout(location=2) in vec2 uv;uniform mat4 vp;out vec2 vUv;
void main(){vUv=uv;gl_Position=vp*vec4(position,1.0);})GLSL";
const char* laneFragment=R"GLSL(#version 450 core
uniform vec3 uColor;uniform float uAlpha;uniform float uTime;uniform float uK;in vec2 vUv;out vec4 outColor;
void main(){float edge=1.0-smoothstep(0.0,.08,min(vUv.x,1.0-vUv.x));float chev=step(.55,fract((vUv.y*9.0-abs(vUv.x-.5)*2.2)-uTime*1.8));float fill=.16+.22*uK;float a=max(edge*.95,max(chev*(.3+.35*uK),fill))*(1.0-smoothstep(.92,1.0,vUv.y));outColor=vec4(mix(uColor,vec3(1.0),edge*.25)*1.35,a*uAlpha);})GLSL";
const char* beamVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;uniform mat4 vp;void main(){gl_Position=vp*vec4(position,1.0);})GLSL";
const char* beamFragment=R"GLSL(#version 450 core
uniform vec3 uColor;uniform float uAlpha;out vec4 outColor;void main(){outColor=vec4(uColor,uAlpha);})GLSL";
const char* barrelVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;uniform mat4 vp;out vec3 vNormal;out vec3 vPosition;
void main(){vNormal=normal;vPosition=position;gl_Position=vp*vec4(position,1.0);})GLSL";
const char* barrelFragment=R"GLSL(#version 450 core
uniform vec3 uInk;in vec3 vNormal;in vec3 vPosition;out vec4 outColor;
void main(){vec3 n=normalize(vNormal),l=normalize(vec3(-.4,.85,.35));float diffuse=max(dot(n,l),0.0);float spec=pow(max(dot(n,normalize(l+normalize(vec3(.2,.8,.4)))),0.0),9.0);vec3 base=vec3(.254,.072,.032);vec3 lit=base*(.24+.76*diffuse)+uInk*.18+vec3(.19,.13,.095)*spec;outColor=vec4(lit,1.0);})GLSL";
}

struct BossRenderer::State {
    struct CrabFrame {BossRigPose pose;double age=0;};
    struct CrabPop {BossRigPose pose;double start=0,age=0;bool fxEmitted=false;};
    bool environment=false;
    explicit State(std::filesystem::path path):assets(std::move(path)),pose(assets/"model.bin"){}
    std::filesystem::path assets;
    BossPose pose;
    GLuint ringProgram=0,fanProgram=0,laneProgram=0,beamProgram=0,barrelProgram=0;
    GLuint hazardVao=0,hazardVbo=0,boneTexture=0,dfgTexture=0,stencilTexture=0;
    std::map<std::string,std::unique_ptr<CharacterShader>> shaders;
    GpuModel hull,crablet;
    std::map<int,CrabFrame> liveCrabs;
    std::map<int,CrabPop> crabPops;
    std::vector<glm::dvec3> crabPopEvents;
    double lastCrabletClock=-1;
    bool ready=false;
    ~State(){
        for(auto* model:{&hull,&crablet})for(auto& mesh:model->meshes){if(mesh.ebo)glDeleteBuffers(1,&mesh.ebo);if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);}
        if(hazardVbo)glDeleteBuffers(1,&hazardVbo);if(hazardVao)glDeleteVertexArrays(1,&hazardVao);
        for(const GLuint program:{ringProgram,fanProgram,laneProgram,beamProgram,barrelProgram})if(program)glDeleteProgram(program);
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
        ringProgram=link(ringVertex,ringFragment);fanProgram=link(fanVertex,fanFragment);
        laneProgram=link(laneVertex,laneFragment);beamProgram=link(beamVertex,beamFragment);
        barrelProgram=link(barrelVertex,barrelFragment);
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
        glGenVertexArrays(1,&hazardVao);glGenBuffers(1,&hazardVbo);glBindVertexArray(hazardVao);glBindBuffer(GL_ARRAY_BUFFER,hazardVbo);
        const auto hazardAttribute=[](GLuint location,GLint size,std::size_t offset){glEnableVertexAttribArray(location);glVertexAttribPointer(location,size,GL_FLOAT,GL_FALSE,sizeof(HazardVertex),reinterpret_cast<void*>(offset));};
        hazardAttribute(0,3,offsetof(HazardVertex,position));hazardAttribute(1,3,offsetof(HazardVertex,normal));
        hazardAttribute(2,2,offsetof(HazardVertex,uv));hazardAttribute(3,1,offsetof(HazardVertex,angle));
        hazardAttribute(4,1,offsetof(HazardVertex,side));hazardAttribute(5,1,offsetof(HazardVertex,reach));hazardAttribute(6,1,offsetof(HazardVertex,radial));
        glBindVertexArray(static_cast<GLuint>(previousVao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(previousBuffer));glUseProgram(static_cast<GLuint>(previousProgram));
        glActiveTexture(static_cast<GLenum>(previousActive));glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture));ready=true;
    }
    void drawModel(const GpuModel& model,const BossRigPose& rig,const glm::dmat4& view,const glm::dmat4& projection,
                   glm::dvec3 camera,const CharacterLightState& lights,double crabTime=-1.0,double crabFlash=-1.0){
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
            if(crabTime>=0.0){shader.uniform("uTime",crabTime);shader.uniform("uFlash",std::max(0.0,crabFlash));}
            shader.uniform("uInk",{InkColors[1].r,InkColors[1].g,InkColors[1].b});
            shader.uniform("uWeak",{InkColors[0].r,InkColors[0].g,InkColors[0].b});
            glBindVertexArray(mesh.vao);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);
        }
    }
};

BossRenderer::BossRenderer(std::filesystem::path assets,bool environment):state_(std::make_unique<State>(std::move(assets))){state_->environment=environment;}
BossRenderer::~BossRenderer()=default;
void BossRenderer::Update(BossSimulation& boss){
    const double clock=boss.state().clock;
    if(state_->lastCrabletClock>=0&&clock<state_->lastCrabletClock){state_->liveCrabs.clear();state_->crabPops.clear();state_->crabPopEvents.clear();}
    state_->lastCrabletClock=clock;
    state_->pose.Update(boss);
    std::map<int,State::CrabFrame> nextLive;
    for(const auto& crab:boss.crablets()){
        if(crab.dead){
            const auto previous=state_->liveCrabs.find(crab.id);
            if(previous!=state_->liveCrabs.end()&&state_->crabPops.find(crab.id)==state_->crabPops.end()){
                State::CrabPop pop;pop.pose=previous->second.pose;pop.start=clock;pop.age=previous->second.age;
                state_->crabPops.emplace(crab.id,std::move(pop));
            }
            continue;
        }
        const auto pose=std::find_if(state_->pose.crablets().begin(),state_->pose.crablets().end(),[&](const auto& item){return item.id==crab.id;});
        if(pose!=state_->pose.crablets().end()){
            State::CrabFrame frame;frame.pose.root=pose->root;frame.pose.skin=pose->skin;frame.age=crab.age;
            nextLive.emplace(crab.id,std::move(frame));
        }
    }
    state_->liveCrabs=std::move(nextLive);
    for(auto it=state_->crabPops.begin();it!=state_->crabPops.end();){
        const double age=clock-it->second.start;
        if(age<0){it=state_->crabPops.erase(it);continue;}
        if(age>=.14){
            if(!it->second.fxEmitted){state_->crabPopEvents.emplace_back(glm::dvec3(it->second.pose.root[3]));it->second.fxEmitted=true;}
            it=state_->crabPops.erase(it);
        }else ++it;
    }
}
std::vector<glm::dvec3> BossRenderer::TakeCrabletPopEvents(){auto events=std::move(state_->crabPopEvents);state_->crabPopEvents.clear();return events;}
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
    std::size_t crabCount=0;
    for(const auto& crab:state_->pose.crablets()){
        const auto state=std::find_if(boss.crablets().begin(),boss.crablets().end(),[&](const auto& item){return item.id==crab.id;});
        BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;
        state_->drawModel(state_->crablet,pose,identity,projection,glm::dvec3(0),lights,state==boss.crablets().end()?0.0:state->age,0.0);
        ++crabCount;
    }
    for(const auto& [id,pop]:state_->crabPops){(void)id;const double age=boss.state().clock-pop.start;if(age<0||age>=.14)continue;const double u=age/.14;
        BossRigPose pose=pop.pose;pose.root=pose.root*glm::scale(glm::dmat4(1),glm::dvec3(1+.45*u*u));
        state_->drawModel(state_->crablet,pose,identity,projection,glm::dvec3(0),lights,pop.age+age,u);++crabCount;
    }
    glBindVertexArray(static_cast<GLuint>(oldVao));glUseProgram(static_cast<GLuint>(oldProgram));for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i]));}glActiveTexture(oldActive);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    return static_cast<int>(state_->hull.meshes.size()+state_->crablet.meshes.size()*crabCount);
}
int BossRenderer::Draw(const BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,const glm::vec3& camera,const glm::vec3& squadColor,const CharacterLightState* sceneLights){
    if(!boss.state().visible)return 0;state_->ensureGpu();const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);GLint oldProgram=0,oldVao=0,oldActive=0,textures[3]{};glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);}glActiveTexture(oldActive);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    CharacterLightState lights;lights.ambient={.22f,.22f,.22f};lights.sunDirection=glm::normalize(glm::vec3(-.4f,.85f,.35f));lights.sunColor={1.0f,.92f,.82f};lights.skyDirection={0,1,0};lights.skyColor={.32f,.42f,.5f};lights.groundColor={.18f,.15f,.12f};
    if(sceneLights)lights=*sceneLights;
    (void)squadColor;const glm::dmat4 viewD(view),projectionD(projection);const glm::dvec3 cameraD(camera);
    state_->drawModel(state_->hull,state_->pose.hullbreaker(),viewD,projectionD,cameraD,lights);
    std::size_t crabCount=0;
    for(const auto& crab:state_->pose.crablets()){
        const auto crabState=std::find_if(boss.crablets().begin(),boss.crablets().end(),[&](const auto& item){return item.id==crab.id;});
        BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;
        state_->drawModel(state_->crablet,pose,viewD,projectionD,cameraD,lights,crabState==boss.crablets().end()?0.0:crabState->age,0.0);
        ++crabCount;
    }
    for(const auto& [id,pop]:state_->crabPops){(void)id;const double age=boss.state().clock-pop.start;if(age<0||age>=.14)continue;const double u=age/.14;
        BossRigPose pose=pop.pose;pose.root=pose.root*glm::scale(glm::dmat4(1),glm::dvec3(1+.45*u*u));
        state_->drawModel(state_->crablet,pose,viewD,projectionD,cameraD,lights,pop.age+age,u);++crabCount;
    }
    glBindVertexArray(static_cast<GLuint>(oldVao));glUseProgram(static_cast<GLuint>(oldProgram));for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE6+i);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[i]));}glActiveTexture(oldActive);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    return static_cast<int>(state_->hull.meshes.size()+state_->crablet.meshes.size()*crabCount);
}

int BossRenderer::DrawHazards(const BossSimulation& boss,const StageGeometry& geometry,const glm::mat4& viewProjection,const glm::vec3&){
    constexpr double pi=3.14159265358979323846,tau=2*pi;
    constexpr int reachSegments=static_cast<int>(BossReachSegments);
    const glm::dvec3 color(InkColors[1].r,InkColors[1].g,InkColors[1].b);
    std::vector<HazardBatch> batches;
    const auto addTriangle=[](auto& out,const HazardVertex& a,const HazardVertex& b,const HazardVertex& c){out.push_back(a);out.push_back(b);out.push_back(c);};
    const auto reachFor=[&](glm::dvec3 center,double maxRadius){
        std::array<double,BossReachSegments> reach{};
        for(int i=0;i<reachSegments;++i){const double angle=(i+.5)/reachSegments*tau,sx=std::sin(angle),sz=std::cos(angle);double r=0;
            while(r<maxRadius&&(r<1.2||boss.nav().kindAt(center.x+sx*r,center.z+sz*r)==0))r+=.35;
            reach[static_cast<std::size_t>(i)]=std::min(r,maxRadius);
        }
        return reach;
    };
    const auto addFan=[&](glm::dvec3 center,double angle0,double angle1,const auto& radius,double alpha,double time,double k){
        HazardBatch batch;batch.material=HazardMaterial::Fan;batch.alpha=alpha;batch.time=time;batch.k=k;
        const int segments=std::max(8,static_cast<int>(std::ceil(std::abs(angle1-angle0)/tau*reachSegments)));
        const auto centerVertex=hazardVertex(center,glm::dvec3(0,1,0),glm::dvec2(0.0),0,0,0,0);HazardVertex previous{};
        for(int i=0;i<=segments;++i){const double t=double(i)/segments,angle=angle0+(angle1-angle0)*t,radiusAt=std::max(0.0,radius(angle));
            const auto edge=hazardVertex(glm::dvec3(center.x+std::sin(angle)*radiusAt,center.y,center.z+std::cos(angle)*radiusAt),glm::dvec3(0,1,0),glm::dvec2(0.0),0,0,0,1);
            if(i)addTriangle(batch.vertices,centerVertex,previous,edge);previous=edge;
        }
        if(!batch.vertices.empty())batches.push_back(std::move(batch));
    };
    const auto addRing=[&](glm::dvec3 center,double radius,double width,const std::array<double,BossReachSegments>& reach,double alpha,double time){
        HazardBatch batch;batch.material=HazardMaterial::Ring;batch.center=center;batch.radius=radius;batch.width=width;batch.alpha=alpha;batch.time=time;
        for(int i=0;i<reachSegments;++i){const double a0=double(i)/reachSegments*tau,a1=double(i+1)/reachSegments*tau,cut=reach[static_cast<std::size_t>(i)];
            const auto v00=hazardVertex(glm::dvec3(0.0),glm::dvec3(0.0),glm::dvec2(0.0),a0,-1,cut),v01=hazardVertex(glm::dvec3(0.0),glm::dvec3(0.0),glm::dvec2(0.0),a0,1,cut),v10=hazardVertex(glm::dvec3(0.0),glm::dvec3(0.0),glm::dvec2(0.0),a1,-1,cut),v11=hazardVertex(glm::dvec3(0.0),glm::dvec3(0.0),glm::dvec2(0.0),a1,1,cut);
            addTriangle(batch.vertices,v00,v01,v10);addTriangle(batch.vertices,v10,v01,v11);
        }
        batches.push_back(std::move(batch));
    };
    const auto addLane=[&](glm::dvec3 start,glm::dvec3 forward,double length,double alpha,double time,double k){
        HazardBatch batch;batch.material=HazardMaterial::Lane;batch.alpha=alpha;batch.time=time;batch.k=k;
        const glm::dvec3 side(forward.z,0,-forward.x);constexpr double halfWidth=2.5;
        const glm::dvec3 left=start-side*halfWidth,right=start+side*halfWidth,endLeft=left+forward*length,endRight=right+forward*length;
        addTriangle(batch.vertices,hazardVertex(left,glm::dvec3(0,1,0),glm::dvec2(0,0)),hazardVertex(right,glm::dvec3(0,1,0),glm::dvec2(1,0)),hazardVertex(endLeft,glm::dvec3(0,1,0),glm::dvec2(0,1)));
        addTriangle(batch.vertices,hazardVertex(endLeft,glm::dvec3(0,1,0),glm::dvec2(0,1)),hazardVertex(right,glm::dvec3(0,1,0),glm::dvec2(1,0)),hazardVertex(endRight,glm::dvec3(0,1,0),glm::dvec2(1,1)));
        batches.push_back(std::move(batch));
    };
    const auto addBeam=[&](glm::dvec3 start,glm::dvec3 end,double radius){
        HazardBatch batch;batch.material=HazardMaterial::Beam;const glm::dvec3 axis=glm::normalize(end-start);
        glm::dvec3 side=glm::cross(axis,glm::dvec3(0,1,0));if(glm::dot(side,side)<1e-8)side=glm::cross(axis,glm::dvec3(1,0,0));side=glm::normalize(side);const glm::dvec3 up=glm::normalize(glm::cross(side,axis));
        for(int i=0;i<12;++i){const double a0=i*tau/12,a1=(i+1)*tau/12;const auto n0=side*std::cos(a0)+up*std::sin(a0),n1=side*std::cos(a1)+up*std::sin(a1);
            const auto p00=hazardVertex(start+n0*radius,n0),p01=hazardVertex(start+n1*radius,n1),p10=hazardVertex(end+n0*radius,n0),p11=hazardVertex(end+n1*radius,n1);
            addTriangle(batch.vertices,p00,p01,p10);addTriangle(batch.vertices,p10,p01,p11);
        }
        batches.push_back(std::move(batch));
    };
    const auto addBarrel=[&](glm::dvec3 position,double rotationX,double rotationZ){
        HazardBatch batch;batch.material=HazardMaterial::Barrel;
        const glm::dmat4 rotation=glm::rotate(glm::dmat4(1),rotationX,glm::dvec3(1,0,0))*glm::rotate(glm::dmat4(1),rotationZ,glm::dvec3(0,0,1));
        const auto transformPoint=[&](glm::dvec3 p){return position+glm::dvec3(rotation*glm::dvec4(p,1));};
        const auto transformNormal=[&](glm::dvec3 n){return glm::normalize(glm::dvec3(rotation*glm::dvec4(n,0)));};
        constexpr double barrelRadius=.42,halfHeight=.475;
        for(int i=0;i<16;++i){const double a0=i*tau/16,a1=(i+1)*tau/16;const glm::dvec3 n0(std::cos(a0),0,std::sin(a0)),n1(std::cos(a1),0,std::sin(a1));
            const auto b0=transformPoint(glm::dvec3(n0.x*barrelRadius,-halfHeight,n0.z*barrelRadius)),b1=transformPoint(glm::dvec3(n1.x*barrelRadius,-halfHeight,n1.z*barrelRadius));
            const auto t0=transformPoint(glm::dvec3(n0.x*barrelRadius,halfHeight,n0.z*barrelRadius)),t1=transformPoint(glm::dvec3(n1.x*barrelRadius,halfHeight,n1.z*barrelRadius));
            addTriangle(batch.vertices,hazardVertex(b0,transformNormal(n0)),hazardVertex(b1,transformNormal(n1)),hazardVertex(t0,transformNormal(n0)));
            addTriangle(batch.vertices,hazardVertex(t0,transformNormal(n0)),hazardVertex(b1,transformNormal(n1)),hazardVertex(t1,transformNormal(n1)));
            const auto bottom=hazardVertex(transformPoint(glm::dvec3(0,-halfHeight,0)),transformNormal(glm::dvec3(0,-1,0))),top=hazardVertex(transformPoint(glm::dvec3(0,halfHeight,0)),transformNormal(glm::dvec3(0,1,0)));
            addTriangle(batch.vertices,bottom,hazardVertex(b1,transformNormal(glm::dvec3(0,-1,0))),hazardVertex(b0,transformNormal(glm::dvec3(0,-1,0))));
            addTriangle(batch.vertices,top,hazardVertex(t0,transformNormal(glm::dvec3(0,1,0))),hazardVertex(t1,transformNormal(glm::dvec3(0,1,0))));
        }
        batches.push_back(std::move(batch));
    };
    const auto beamLength=[&](const BossMove& move,double angle){
        const double quant=std::round(angle*90)/90;const auto ray=geometry.raycast({move.origin.x,move.point.y+1,move.origin.z},{std::sin(quant),0,std::cos(quant)},17,true);
        return ray.hit?ray.dist:17.0;
    };
    double time=0;
    for(const auto& move:boss.moves()){
        const double u=boss.state().clock-move.t0;if(u<0)continue;time=boss.state().clock;
        const bool tele=u<move.tele;
        if(move.id==BossMoveId::Slam){
            if(u<move.tele+.15){const double k=std::clamp(u/move.tele,0.0,1.0);addFan(move.point+glm::dvec3(0,.04,0),0,tau,[](double){return 2.8;},.4+.35*k,time,k);}
            const auto reach=reachFor(move.point,19.0);
            for(const double offset:move.rings){const double start=move.tele+offset,radius=2.4+9.5*(u-start);if(u<start||radius>19)continue;
                const double x=std::clamp((radius-15.0)/4.0,0.0,1.0),smooth=x*x*(3.0-2.0*x);addRing(move.point+glm::dvec3(0,.06,0),radius,.65,reach,1.0-smooth,time);}
        }else if(move.id==BossMoveId::Barrage){
            for(const auto& barrel:move.barrels){const double land=barrel.throwAt+1.3;
                if(u>land+.05)continue;
                if(u>std::min(move.tele*.4,barrel.throwAt-.5)){const double k=std::clamp(1.0-(land-u)/1.8,0.0,1.0);addFan(glm::dvec3(barrel.x,barrel.y+.04,barrel.z),0,tau,[](double){return 2.4;},.2+.5*k,time,k);}
                if(u>=barrel.throwAt){const double k=(u-barrel.throwAt)/1.3,peak=5+.35*std::hypot(barrel.x-move.origin.x,barrel.z-move.origin.z);
                    const glm::dvec3 position(move.origin.x+(barrel.x-move.origin.x)*k,move.origin.y+(barrel.y-move.origin.y)*k+peak*4*k*(1-k),move.origin.z+(barrel.z-move.origin.z)*k);
                    addBarrel(position,k*9+barrel.x,k*6);}
            }
        }else if(move.id==BossMoveId::Sweep){
            if(u>move.tele+move.act)continue;
            const auto reach=reachFor({move.origin.x,move.point.y,move.origin.z},17.0);const double lo=std::min(move.a0,move.a1),hi=std::max(move.a0,move.a1);
            const auto radius=[&](double angle){const double wrapped=std::fmod(std::fmod(angle,tau)+tau,tau);const auto segment=static_cast<std::size_t>(std::floor(wrapped/tau*reachSegments))%reachSegments;return std::max(2.0,std::min(beamLength(move,angle),reach[segment]));};
            const double k=u<move.tele?u/move.tele:1.0-(u-move.tele)/move.act;
            addFan(glm::dvec3(move.origin.x,move.point.y+.05,move.origin.z),lo,hi,radius,.55+.45*std::min(1.0,k*1.5),time,std::clamp(k,0.0,1.0));
            if(u>=move.tele){const double angle=BossSimulation::beamAngle(move,boss.state().clock),length=beamLength(move,angle),w=.32+.06*std::sin(time*40);
                addBeam(move.origin,{move.origin.x+std::sin(angle)*length,move.point.y+.3,move.origin.z+std::cos(angle)*length},w);}
        }else if(move.id==BossMoveId::Charge){
            if(u>move.tele+move.act)continue;
            const double k=std::clamp(u/move.tele,0.0,1.0),distance=BossSimulation::chargeDistance(move,boss.state().clock),length=std::max(.5,move.length+4.2-distance);
            const glm::dvec3 forward(std::sin(move.yaw),0,std::cos(move.yaw));
            addLane(move.point+forward*distance+glm::dvec3(0,.05,0),forward,length,tele?.35+.65*k:.9,time,k);
        }else if(move.id==BossMoveId::Frenzy){
            if(u>=move.tele+move.act)continue;
            const auto reach=reachFor(move.point,8.8);const auto radius=[&](double angle){const double wrapped=std::fmod(angle,tau);const auto segment=static_cast<std::size_t>(std::floor(wrapped/tau*reachSegments))%reachSegments;return reach[segment];};
            const double k=tele?std::clamp(u/move.tele,0.0,1.0):1.0;addFan(move.point+glm::dvec3(0,.05,0),0,tau,radius,.5+.5*k,time*(tele?1.0:2.5),k);
        }
    }
    if(batches.empty())return 0;
    state_->ensureGpu();
    GLint oldProgram=0,oldVao=0,oldBuffer=0,oldDepthFunc=0,srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0;GLboolean oldDepthMask=GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glGetIntegerv(GL_DEPTH_FUNC,&oldDepthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    const GLboolean oldDepth=glIsEnabled(GL_DEPTH_TEST),oldBlend=glIsEnabled(GL_BLEND),oldCull=glIsEnabled(GL_CULL_FACE),oldOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLfloat oldOffsetFactor=0,oldOffsetUnits=0;glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&oldOffsetFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&oldOffsetUnits);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-4,-4);glBindVertexArray(state_->hazardVao);glBindBuffer(GL_ARRAY_BUFFER,state_->hazardVbo);
    const auto uniform1=[](GLuint program,const char* name,double value){const GLint loc=glGetUniformLocation(program,name);if(loc>=0)glUniform1f(loc,static_cast<float>(value));};
    const auto uniform3=[](GLuint program,const char* name,glm::vec3 value){const GLint loc=glGetUniformLocation(program,name);if(loc>=0)glUniform3fv(loc,1,&value[0]);};
    const auto uniformMatrix=[](GLuint program,const char* name,const glm::mat4& value){const GLint loc=glGetUniformLocation(program,name);if(loc>=0)glUniformMatrix4fv(loc,1,GL_FALSE,&value[0][0]);};
    for(const auto& batch:batches){
        GLuint program=state_->fanProgram;
        if(batch.material==HazardMaterial::Ring)program=state_->ringProgram;
        else if(batch.material==HazardMaterial::Lane)program=state_->laneProgram;
        else if(batch.material==HazardMaterial::Beam)program=state_->beamProgram;
        else if(batch.material==HazardMaterial::Barrel)program=state_->barrelProgram;
        glUseProgram(program);uniformMatrix(program,"vp",viewProjection);uniform3(program,"uColor",glm::vec3(color));uniform3(program,"uInk",glm::vec3(color));
        uniform1(program,"uAlpha",batch.alpha);uniform1(program,"uTime",batch.time);uniform1(program,"uK",batch.k);
        if(batch.material==HazardMaterial::Ring){const GLint c=glGetUniformLocation(program,"uC");if(c>=0)glUniform3f(c,float(batch.center.x),float(batch.center.y),float(batch.center.z));uniform1(program,"uR",batch.radius);uniform1(program,"uW",batch.width);}
        glBufferData(GL_ARRAY_BUFFER,batch.vertices.size()*sizeof(HazardVertex),batch.vertices.data(),GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(batch.vertices.size()));
    }
    glBindVertexArray(static_cast<GLuint>(oldVao));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(oldBuffer));glUseProgram(static_cast<GLuint>(oldProgram));
    glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);glDepthFunc(oldDepthFunc);glDepthMask(oldDepthMask);glPolygonOffset(oldOffsetFactor,oldOffsetUnits);
    if(oldDepth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(oldBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(oldCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(oldOffset)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);
    return static_cast<int>(batches.size());
}
} // namespace restoration
