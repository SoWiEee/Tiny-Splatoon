#include "BossRenderer.hpp"
#include "BossPose.hpp"
#include "BossSimulation.hpp"
#include "StageGeometry.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

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
struct Vertex {float position[3],normal[3];std::uint8_t joints[4];float weights[4],material[4];};
struct LineVertex {glm::vec3 position;glm::vec4 color;};
struct GpuMesh {GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;int material=0;};
struct GpuModel {std::vector<GpuMesh> meshes;};
glm::mat4 fmat(const glm::dmat4& matrix){return glm::mat4(matrix);}
const char* bossVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in uvec4 joints;
layout(location=3) in vec4 weights;
layout(location=4) in vec4 material;
uniform mat4 uModel,uView,uProjection;
uniform mat4 uBones[100];
out vec3 vNormal;out vec3 vWorld;out vec3 vBind;out vec4 vMaterial;
void main(){
    mat4 skin=uBones[joints.x]*weights.x+uBones[joints.y]*weights.y+
              uBones[joints.z]*weights.z+uBones[joints.w]*weights.w;
    vec4 p=skin*vec4(position,1.0);vec4 world=uModel*p;
    vWorld=world.xyz;vBind=position;vMaterial=material;
    vNormal=normalize(mat3(uModel)*mat3(skin)*normal);
    gl_Position=uProjection*uView*world;
})GLSL";
const char* bossFragment=R"GLSL(#version 450 core
in vec3 vNormal;in vec3 vWorld;in vec3 vBind;in vec4 vMaterial;
uniform int uMaterial;uniform int uPhase;uniform bool uDead;uniform float uTime;uniform vec3 uInk,uWeak;
out vec4 outColor;
vec3 linear(vec3 c){return pow(c,vec3(2.2));}
float noise(vec3 p){return fract(sin(dot(floor(p),vec3(127.1,311.7,74.7)))*43758.5453);}
void main(){
    float cls=floor(vMaterial.x+.5),param=floor(vMaterial.y+.5),ao=clamp(vMaterial.z,.18,1.0),extra=floor(vMaterial.w+.5);
    vec3 c;float emissive=0.0,rough=.72;
    if(uMaterial==0){
        if(cls<.5){float corr=.92+.08*sin(vBind.x*34.0);float wear=smoothstep(.62,.94,noise(vBind*5.0));c=mix(vec3(.025,.245,.215),vec3(.28,.105,.045),wear*.62)*corr;rough=.76;}
        else if(cls<1.5){c=vec3(.18,.21,.205);rough=.54;}
        else if(cls<2.5){c=vec3(.3,.24,.12);rough=.64;}
        else if(cls<5.5){c=vec3(.26,.11,.055);}
        else if(cls<6.5){c=vec3(.22,.13,.075);}
        else if(cls<7.5){c=linear(uInk)*.75;rough=.27;}
        else c=vec3(.105,.11,.115);
    }else if(uMaterial==1){
        float tone=clamp(param,0.0,1.0);c=mix(vec3(.10,.18,.16),vec3(.42,.34,.22),tone);c*=.88+.12*noise(vBind*8.0);rough=.49;
    }else if(uMaterial==2){c=cls<1.5?vec3(.33,.18,.13):vec3(.25,.13,.105);rough=.68;}
    else if(uMaterial==3){c=linear(uPhase>=2?uWeak:uInk)*1.35;emissive=1.4;rough=.2;}
    else if(uMaterial==4){
        if(cls<.5)c=vec3(.055,.06,.055);else if(cls<1.5)c=vec3(.30,.27,.19);else if(cls<3.5)c=vec3(.20,.22,.21);else if(cls<4.5)c=vec3(.13,.23,.1);else if(cls<5.5)c=mix(vec3(.12,.22,.1),linear(uInk),.3);else c=linear(uInk);rough=.78;
    }else{
        if(cls<.5)c=mix(vec3(.06,.22,.20),vec3(.19,.28,.25),clamp(param,0.0,1.0));
        else if(cls<1.5)c=linear(uWeak)*.8;else c=linear(uInk)*1.4;emissive=cls>=2.0?1.2:0.0;rough=.52;
    }
    float rust=smoothstep(.74,.97,noise(vBind*3.2+4.0));
    if(uMaterial==0&&cls<2.5)c=mix(c,vec3(.23,.072,.025),rust*.38);
    vec3 n=normalize(vNormal),l=normalize(vec3(-.4,.85,.35)),v=normalize(vec3(0,0,1));
    float diffuse=.28+.72*max(dot(n,l),0.0);float spec=pow(max(dot(reflect(-l,n),v),0.0),mix(48.0,10.0,rough))*mix(.12,.38,1.0-rough);
    vec3 color=c*(diffuse*ao)+(spec+emissive)*linear((uMaterial==3||cls>=6.0)?uInk:vec3(1.0));
    if(!uDead&&uPhase>=2&&cls<.5&&extra>1.0){float glow=.15+.12*sin(uTime*4.0);color+=linear(uWeak)*glow;}
    outColor=vec4(color,1.0);
})GLSL";
const char* lineVertex=R"GLSL(#version 450 core
layout(location=0) in vec3 position;layout(location=1) in vec4 color;uniform mat4 vp;out vec4 vColor;
void main(){vColor=color;gl_Position=vp*vec4(position,1.0);})GLSL";
const char* lineFragment=R"GLSL(#version 450 core
in vec4 vColor;out vec4 outColor;void main(){outColor=vColor;})GLSL";
int materialIndex(const std::string& name){if(name=="steel")return 0;if(name=="cara")return 1;if(name=="flesh")return 2;if(name=="eye")return 3;if(name=="junk")return 4;return 5;}
}

struct BossRenderer::State {
    explicit State(std::filesystem::path path):pose(path/"model.bin"){}
    BossPose pose;
    GLuint program=0,lineProgram=0,lineVao=0,lineVbo=0;
    GpuModel hull,crablet;
    bool ready=false;
    ~State(){
        for(auto* model:{&hull,&crablet})for(auto& mesh:model->meshes){if(mesh.ebo)glDeleteBuffers(1,&mesh.ebo);if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);}
        if(lineVbo)glDeleteBuffers(1,&lineVbo);if(lineVao)glDeleteVertexArrays(1,&lineVao);if(program)glDeleteProgram(program);if(lineProgram)glDeleteProgram(lineProgram);
    }
    void ensureGpu(){
        if(ready)return;program=link(bossVertex,bossFragment);lineProgram=link(lineVertex,lineFragment);
        const auto upload=[&](const BossModelAsset& source,GpuModel& target){
            for(const auto& mesh:source.meshes){
                std::vector<Vertex> vertices;vertices.reserve(mesh.vertices.size());
                for(const auto& v:mesh.vertices){Vertex out{};for(int i=0;i<3;++i){out.position[i]=v.position[i];out.normal[i]=v.normal[i];}for(int i=0;i<4;++i){out.joints[i]=v.joints[i];out.weights[i]=v.weights[i];out.material[i]=v.material[i];}vertices.push_back(out);}
                GpuMesh gpu;gpu.count=static_cast<GLsizei>(mesh.indices.size());gpu.material=materialIndex(mesh.name);
                glGenVertexArrays(1,&gpu.vao);glGenBuffers(1,&gpu.vbo);glGenBuffers(1,&gpu.ebo);glBindVertexArray(gpu.vao);
                glBindBuffer(GL_ARRAY_BUFFER,gpu.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh.indices.size()*sizeof(std::uint32_t),mesh.indices.data(),GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
                glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
                glEnableVertexAttribArray(2);glVertexAttribIPointer(2,4,GL_UNSIGNED_BYTE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,joints)));
                glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,weights)));
                glEnableVertexAttribArray(4);glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,material)));
                glBindVertexArray(0);target.meshes.push_back(gpu);
            }
        };
        upload(pose.hullAsset(),hull);upload(pose.crabletAsset(),crablet);
        glGenVertexArrays(1,&lineVao);glGenBuffers(1,&lineVbo);glBindVertexArray(lineVao);glBindBuffer(GL_ARRAY_BUFFER,lineVbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(LineVertex),reinterpret_cast<void*>(offsetof(LineVertex,position)));
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(LineVertex),reinterpret_cast<void*>(offsetof(LineVertex,color)));glBindVertexArray(0);ready=true;
    }
    void drawModel(const GpuModel& model,const BossRigPose& rig,const glm::mat4& view,const glm::mat4& projection,
                   const glm::vec3& ink,const glm::vec3& weak,int phase,bool dead,float time){
        std::array<glm::mat4,100> bones;bones.fill(glm::mat4(1.0f));
        for(std::size_t i=0;i<std::min(bones.size(),rig.skin.size());++i)bones[i]=fmat(rig.skin[i]);
        const auto root=fmat(rig.root);glUniformMatrix4fv(glGetUniformLocation(program,"uBones[0]"),static_cast<GLsizei>(bones.size()),GL_FALSE,glm::value_ptr(bones[0]));
        glUniformMatrix4fv(glGetUniformLocation(program,"uModel"),1,GL_FALSE,glm::value_ptr(root));
        glUniformMatrix4fv(glGetUniformLocation(program,"uView"),1,GL_FALSE,&view[0][0]);glUniformMatrix4fv(glGetUniformLocation(program,"uProjection"),1,GL_FALSE,&projection[0][0]);
        glUniform3fv(glGetUniformLocation(program,"uInk"),1,&ink[0]);glUniform3fv(glGetUniformLocation(program,"uWeak"),1,&weak[0]);
        glUniform1i(glGetUniformLocation(program,"uPhase"),phase);glUniform1i(glGetUniformLocation(program,"uDead"),dead);glUniform1f(glGetUniformLocation(program,"uTime"),time);
        for(const auto& mesh:model.meshes){glUniform1i(glGetUniformLocation(program,"uMaterial"),mesh.material);glBindVertexArray(mesh.vao);glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);}
    }
};

BossRenderer::BossRenderer(std::filesystem::path assets):state_(std::make_unique<State>(std::move(assets))){}
BossRenderer::~BossRenderer()=default;
void BossRenderer::Update(BossSimulation& boss){state_->pose.Update(boss);}
int BossRenderer::DrawShadow(const BossSimulation& boss,const glm::mat4& lightViewProjection){
    if(!boss.state().visible)return 0;
    state_->ensureGpu();
    GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glUseProgram(state_->program);
    const glm::mat4 identity(1.0f);const glm::vec3 ink(.0258f,.102f,1.0f),weak(1.0f,.258f,.0037f);
    state_->drawModel(state_->hull,state_->pose.hullbreaker(),identity,lightViewProjection,ink,weak,boss.state().phase,boss.state().dead,static_cast<float>(boss.state().clock));
    for(const auto& crab:state_->pose.crablets()){BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;state_->drawModel(state_->crablet,pose,identity,lightViewProjection,ink,weak,boss.state().phase,false,static_cast<float>(boss.state().clock));}
    glBindVertexArray(0);glUseProgram(static_cast<GLuint>(oldProgram));if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    return static_cast<int>(state_->hull.meshes.size()+state_->crablet.meshes.size()*state_->pose.crablets().size());
}
int BossRenderer::Draw(const BossSimulation& boss,const glm::mat4& view,const glm::mat4& projection,const glm::vec3&,const glm::vec3& squadColor){
    if(!boss.state().visible)return 0;state_->ensureGpu();const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),blend=glIsEnabled(GL_BLEND);GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glUseProgram(state_->program);
    const glm::vec3 bossInk(.0258f,.102f,1.0f),weak=glm::vec3(squadColor);state_->drawModel(state_->hull,state_->pose.hullbreaker(),view,projection,bossInk,weak,boss.state().phase,boss.state().dead,static_cast<float>(boss.state().clock));
    for(const auto& crab:state_->pose.crablets()){
        BossRigPose pose;pose.root=crab.root;pose.skin=crab.skin;state_->drawModel(state_->crablet,pose,view,projection,bossInk,weak,boss.state().phase,false,static_cast<float>(boss.state().clock));
    }
    glBindVertexArray(0);glUseProgram(static_cast<GLuint>(oldProgram));if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
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
