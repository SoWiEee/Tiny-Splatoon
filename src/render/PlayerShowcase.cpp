#include "PlayerShowcase.hpp"
#include "TeamPalette.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace restoration {
namespace {

CharacterLightState showcaseLighting() {
    CharacterLightState light;
    light.ambient={0.16f,0.16f,0.16f};light.sunDirection={-0.4f,0.85f,0.35f};
    light.sunColor={2.5f,2.4f,2.3f};light.skyColor={0.65f,0.75f,1.0f};light.groundColor={0.2f,0.15f,0.1f};
    return light;
}

void validate(PlayerShowcase::Viewport viewport,double time) {
    if(viewport.width<=0||viewport.height<=0)throw std::invalid_argument("Showcase viewport must have positive size");
    if(!std::isfinite(time))throw std::invalid_argument("Showcase time must be finite");
}

double backOut(double value,double strength) {
    const double t=std::clamp(value,0.0,1.0)-1.0;
    return 1.0+t*t*((strength+1.0)*t+strength);
}

class GlState {
public:
    GlState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFramebuffer_);
        glGetIntegerv(GL_VIEWPORT,viewport_.data());glGetIntegerv(GL_SCISSOR_BOX,scissor_.data());
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunc_);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask_);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE,&depthClear_);glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture_);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vertexArray_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer_);
        glGetIntegerv(GL_BLEND_SRC_RGB,&blendSrcRgb_);glGetIntegerv(GL_BLEND_DST_RGB,&blendDstRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&blendSrcAlpha_);glGetIntegerv(GL_BLEND_DST_ALPHA,&blendDstAlpha_);
        glGetIntegerv(GL_CULL_FACE_MODE,&cullFace_);glGetIntegerv(GL_FRONT_FACE,&frontFace_);
        depth_=glIsEnabled(GL_DEPTH_TEST);scissorEnabled_=glIsEnabled(GL_SCISSOR_TEST);
        blend_=glIsEnabled(GL_BLEND);cull_=glIsEnabled(GL_CULL_FACE);
        programPointSize_=glIsEnabled(GL_PROGRAM_POINT_SIZE);
        for(std::size_t i=0;i<textures_.size();++i){
            glActiveTexture(GL_TEXTURE0+static_cast<GLenum>(i+6));
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures_[i]);
        }
        glActiveTexture(activeTexture_);
    }
    ~GlState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
        glScissor(scissor_[0],scissor_[1],scissor_[2],scissor_[3]);
        glDepthFunc(static_cast<GLenum>(depthFunc_));glDepthMask(depthMask_);glClearDepth(depthClear_);
        setEnabled(GL_DEPTH_TEST,depth_);setEnabled(GL_SCISSOR_TEST,scissorEnabled_);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_),static_cast<GLenum>(blendDstRgb_),
                            static_cast<GLenum>(blendSrcAlpha_),static_cast<GLenum>(blendDstAlpha_));
        setEnabled(GL_BLEND,blend_);glCullFace(static_cast<GLenum>(cullFace_));
        glFrontFace(static_cast<GLenum>(frontFace_));setEnabled(GL_CULL_FACE,cull_);
        setEnabled(GL_PROGRAM_POINT_SIZE,programPointSize_);
        for(std::size_t i=0;i<textures_.size();++i){
            glActiveTexture(GL_TEXTURE0+static_cast<GLenum>(i+6));glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures_[i]));
        }
        glActiveTexture(activeTexture_);glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vertexArray_));glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(arrayBuffer_));
    }
private:
    static void setEnabled(GLenum capability,GLboolean enabled){if(enabled)glEnable(capability);else glDisable(capability);}
    GLint drawFramebuffer_=0,readFramebuffer_=0,depthFunc_=GL_LESS,activeTexture_=GL_TEXTURE0,program_=0,vertexArray_=0,arrayBuffer_=0;
    GLint blendSrcRgb_=GL_ONE,blendDstRgb_=GL_ZERO,blendSrcAlpha_=GL_ONE,blendDstAlpha_=GL_ZERO;
    GLint cullFace_=GL_BACK,frontFace_=GL_CCW;
    GLboolean depthMask_=GL_TRUE,depth_=GL_FALSE,scissorEnabled_=GL_FALSE,blend_=GL_FALSE,cull_=GL_FALSE,programPointSize_=GL_FALSE;
    GLdouble depthClear_=1.0;
    std::array<GLint,4> viewport_{},scissor_{};
    std::array<GLint,2> textures_{};
};

void beginViewport(PlayerShowcase::Viewport viewport) {
    glViewport(viewport.x,viewport.y,viewport.width,viewport.height);
    glEnable(GL_SCISSOR_TEST);glScissor(viewport.x,viewport.y,viewport.width,viewport.height);
    glDepthMask(GL_TRUE);glClearDepth(1.0);glClear(GL_DEPTH_BUFFER_BIT);
}
}

struct PlayerShowcase::Actor {
    std::string name,main,sub,dance;
    CharacterStyle style{};
    std::unique_ptr<CharacterPose> pose;
    double lastTime=0.0;
    bool hasTime=false,landTriggered=false;

    void configure(CharacterArt& art,const std::string& newName,const CharacterStyle& newStyle,
                   const std::string& newMain,const std::string& newSub,const std::string& newDance) {
        const bool newRig=!pose||newName!=name||newStyle.hair!=style.hair||newStyle.hat!=style.hat||newStyle.brows!=style.brows;
        if(newRig){
            pose=std::make_unique<CharacterPose>(art.data(),art.data().rig(newStyle.hair,newStyle.hat,newStyle.brows),newName,newMain);
            pose->setSub(newSub);
            name=newName;main=newMain;sub=newSub;style=newStyle;dance.clear();hasTime=false;
        } else {
            if(main!=newMain)pose->setWeapon(newMain);
            if(sub!=newSub)pose->setSub(newSub);
            main=newMain;sub=newSub;style=newStyle;
        }
        pose->setStyle(newStyle);pose->setColor(InkColors[0]);
        if(dance!=newDance){
            pose->setDance(newDance);
            dance=newDance;
        }
    }

    void advance(double time,bool reducedMotion,glm::dvec3 position,double yaw,
                 double velocityY=0.0,bool grounded=true,bool visible=true) {
        const double dt=hasTime?std::clamp(time-lastTime,0.0,0.1):0.0;
        lastTime=time;hasTime=true;
        CharacterAnimState state;state.position=position;state.yaw=yaw;state.vy=velocityY;
        state.grounded=grounded;state.visible=visible;
        CharacterSceneInput scene;
        pose->update(reducedMotion?0.0:dt,state,scene);
    }
};

struct PlayerShowcase::Stage {
    struct Vertex {float x,y,z,nx,ny,nz,shade,glow;};
    struct Mesh {
        GLuint vao=0,vbo=0,ebo=0;GLsizei count=0;
        ~Mesh(){if(vao)glDeleteVertexArrays(1,&vao);if(vbo)glDeleteBuffers(1,&vbo);if(ebo)glDeleteBuffers(1,&ebo);}
        Mesh()=default;Mesh(const Mesh&)=delete;Mesh& operator=(const Mesh&)=delete;
    };
    struct Particle {float x,y,z,r,g,b,size,shape,alpha;};
    CharacterShader shader;
    CharacterShader particleShader;
    std::array<Mesh,4> bodies,coats;
    GLuint particleVao=0,particleVbo=0;

    explicit Stage(const std::filesystem::path& path):shader(R"GLSL(#version 450 core
in vec3 position;in vec3 normal;in float shade;in float glow;
uniform mat4 modelMatrix,viewMatrix,projectionMatrix;
out vec3 vNormal;out float vShade,vGlow;
void main(){vec4 world=modelMatrix*vec4(position,1.0);gl_Position=projectionMatrix*viewMatrix*world;vNormal=mat3(modelMatrix)*normal;vShade=shade;vGlow=glow;}
)GLSL",R"GLSL(#version 450 core
in vec3 vNormal;in float vShade,vGlow;
uniform vec3 uBase,uInk;uniform float uCoat;
layout(location=0) out vec4 fragColor;
void main(){float key=max(0.0,dot(normalize(vNormal),normalize(vec3(-.4,.85,.35))));float rim=pow(1.0-max(0.0,normalize(vNormal).z),2.0);vec3 base=mix(uBase*vShade,uInk*vShade,uCoat);base*=.65+.45*key;base+=uInk*vGlow*.48+vec3(rim*.12);fragColor=vec4(base,1.0);}
)GLSL"),particleShader(R"GLSL(#version 450 core
in vec3 position;in vec3 color;in float size;in float shape;in float alpha;
uniform mat4 viewMatrix,projectionMatrix;uniform float viewportHeight;
out vec3 vColor;out float vShape,vAlpha;
void main(){vec4 camera=viewMatrix*vec4(position,1.0);gl_Position=projectionMatrix*camera;gl_PointSize=size*projectionMatrix[1][1]*viewportHeight/max(.1,-camera.z);vColor=color;vShape=shape;vAlpha=alpha;}
)GLSL",R"GLSL(#version 450 core
in vec3 vColor;in float vShape,vAlpha;layout(location=0) out vec4 fragColor;
void main(){vec2 p=gl_PointCoord-.5;if(vShape>.5){if(abs(p.x)>.27||abs(p.y)>.5)discard;}else if(dot(p,p)>.25)discard;fragColor=vec4(vColor,vAlpha);}
)GLSL"){
        auto meshes=readGeometry(path);
        for(std::size_t i=0;i<bodies.size();++i){upload(bodies[i],std::move(meshes[i*2]));upload(coats[i],std::move(meshes[i*2+1]));}
        glGenVertexArrays(1,&particleVao);glBindVertexArray(particleVao);
        glGenBuffers(1,&particleVbo);glBindBuffer(GL_ARRAY_BUFFER,particleVbo);
        const auto attribute=[&](const char* name,int size,std::size_t offset){
            const int location=particleShader.attribute(name);if(location<0)return;
            glEnableVertexAttribArray(location);glVertexAttribPointer(location,size,GL_FLOAT,GL_FALSE,sizeof(Particle),reinterpret_cast<const void*>(offset));
        };
        attribute("position",3,offsetof(Particle,x));attribute("color",3,offsetof(Particle,r));
        attribute("size",1,offsetof(Particle,size));attribute("shape",1,offsetof(Particle,shape));
        attribute("alpha",1,offsetof(Particle,alpha));
        glBindVertexArray(0);
    }
    ~Stage(){if(particleVao)glDeleteVertexArrays(1,&particleVao);if(particleVbo)glDeleteBuffers(1,&particleVbo);}

    static std::vector<std::pair<std::vector<Vertex>,std::vector<std::uint32_t>>> readGeometry(const std::filesystem::path& path){
        std::ifstream input(path,std::ios::binary);
        char magic[8]{},sourceHash[32]{};std::uint32_t meshCount=0;
        if(!input.read(magic,8)||std::memcmp(magic,"IWDRUM1",7)||!input.read(sourceHash,32)||
           !input.read(reinterpret_cast<char*>(&meshCount),4)||meshCount!=8)
            throw std::runtime_error("Unable to load source showcase drum geometry: "+path.string());
        std::vector<std::pair<std::vector<Vertex>,std::vector<std::uint32_t>>> meshes;
        meshes.reserve(meshCount);
        for(std::uint32_t mesh=0;mesh<meshCount;++mesh){
            std::uint32_t vertexCount=0,indexCount=0;
            if(!input.read(reinterpret_cast<char*>(&vertexCount),4)||
               !input.read(reinterpret_cast<char*>(&indexCount),4)||vertexCount>200000||indexCount>1200000)
                throw std::runtime_error("Invalid source showcase drum geometry: "+path.string());
            std::vector<Vertex> vertices(vertexCount);std::vector<std::uint32_t> indices(indexCount);
            if(!input.read(reinterpret_cast<char*>(vertices.data()),vertices.size()*sizeof(Vertex))||
               !input.read(reinterpret_cast<char*>(indices.data()),indices.size()*sizeof(std::uint32_t)))
                throw std::runtime_error("Incomplete source showcase drum geometry: "+path.string());
            meshes.emplace_back(std::move(vertices),std::move(indices));
        }
        return meshes;
    }
    void upload(Mesh& mesh,std::pair<std::vector<Vertex>,std::vector<std::uint32_t>> data){
        glGenVertexArrays(1,&mesh.vao);glBindVertexArray(mesh.vao);
        glGenBuffers(1,&mesh.vbo);glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,data.first.size()*sizeof(Vertex),data.first.data(),GL_STATIC_DRAW);
        glGenBuffers(1,&mesh.ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,data.second.size()*sizeof(std::uint32_t),data.second.data(),GL_STATIC_DRAW);
        const auto attribute=[&](const char* name,int size,std::size_t offset){
            const int location=shader.attribute(name);if(location<0)return;
            glEnableVertexAttribArray(location);glVertexAttribPointer(location,size,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offset));
        };
        attribute("position",3,offsetof(Vertex,x));attribute("normal",3,offsetof(Vertex,nx));
        attribute("shade",1,offsetof(Vertex,shade));attribute("glow",1,offsetof(Vertex,glow));
        mesh.count=static_cast<GLsizei>(data.second.size());
    }
    void draw(std::size_t count,const std::array<double,4>& drumOffsets,
              const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye){
        static const std::array<glm::dvec3,4> slots={{{0,.72,0},{-1.34,.46,-.14},{1.34,.46,-.14},{2.55,.22,-.36}}};
        shader.use();glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        shader.uniform("uBase",{.13,.15,.32});
        shader.uniform("uInk",{SrgbInkColors[0].x,SrgbInkColors[0].y,SrgbInkColors[0].z});
        for(std::size_t i=0;i<count;++i){
            const auto model=glm::translate(glm::dmat4(1),slots[i]+glm::dvec3(0.0,drumOffsets[i],0.0));
            shader.matrices(model,view,projection,eye);
            shader.uniform("uCoat",0);glBindVertexArray(bodies[i].vao);glDrawElements(GL_TRIANGLES,bodies[i].count,GL_UNSIGNED_INT,nullptr);
            shader.uniform("uCoat",1);glBindVertexArray(coats[i].vao);glDrawElements(GL_TRIANGLES,coats[i].count,GL_UNSIGNED_INT,nullptr);
        }
        glBindVertexArray(0);
    }

    void drawParticles(std::size_t count,const std::array<glm::dvec3,4>& slots,double elapsed,bool won,
                       bool reducedMotion,const glm::dmat4& view,const glm::dmat4& projection,int viewportHeight){
        if(reducedMotion||elapsed<0.15)return;
        std::array<Particle,180> particles{};std::size_t used=0;
        const auto add=[&](double x,double y,double z,glm::dvec3 color,double size,double shape,double alpha){
            if(used>=particles.size()||alpha<=0.0)return;
            particles[used++]={static_cast<float>(x),static_cast<float>(y),static_cast<float>(z),
                static_cast<float>(color.x),static_cast<float>(color.y),static_cast<float>(color.z),
                static_cast<float>(size),static_cast<float>(shape),static_cast<float>(alpha)};
        };
        double minX=slots[0].x,maxX=slots[0].x;
        for(std::size_t i=0;i<count;++i){minX=std::min(minX,slots[i].x);maxX=std::max(maxX,slots[i].x);}
        const auto paper=std::array<glm::dvec3,5>{{{1.0,.22,.36},{1.0,.72,.16},{.25,.9,.73},{.38,.64,1.0},{.94,.42,.93}}};
        if(won){
            for(int i=0;i<76;++i){
                const double phase=std::fmod(i*.61803398875,1.0),speed=.2+std::fmod(i*.271,1.0)*.22;
                const double age=std::fmod(elapsed*speed+phase,1.0);
                const double x=minX-.55+std::fmod(i*.754877666,1.0)*(maxX-minX+1.1)+std::sin(elapsed*2.0+i)*.11;
                const double z=-1.25+std::fmod(i*.569840291,1.0)*1.75;
                const auto color=paper[static_cast<std::size_t>(i)%paper.size()];
                add(x,3.2-age*3.6,z,color,.12+(i%3)*.018,1.0,.92);
            }
            for(int burst=0;burst<2;++burst){
                const double age=elapsed-(burst==0?.98:1.30);
                if(age<0.0||age>1.15)continue;
                for(int i=0;i<28;++i){
                    const double phase=i*.61803398875+burst*.17;
                    const double angle=phase*6.283185307179586;
                    const double speed=1.0+std::fmod(i*.417,1.0)*1.7;
                    const double height=2.2+std::fmod(i*.293,1.0)*1.5;
                    const double x=(burst==0?minX-.45:maxX+.45)+std::cos(angle)*speed*age;
                    const double y=1.05+height*age-2.15*age*age;
                    const double z=-.85+std::sin(angle)*.65*age;
                    const auto color=paper[static_cast<std::size_t>(i+burst*2)%paper.size()];
                    add(x,y,z,color,.14+(i%2)*.025,1.0,std::clamp(1.0-age/1.15,0.0,1.0));
                }
            }
        }else{
            for(int i=0;i<46;++i){
                const double phase=std::fmod(i*.61803398875,1.0),age=std::fmod(elapsed*.12+phase,1.0);
                const double x=minX-.4+std::fmod(i*.754877666,1.0)*(maxX-minX+.8);
                const double z=-1.2+std::fmod(i*.569840291,1.0)*1.5;
                const glm::dvec3 color=i%2?glm::dvec3(.48,.38,.63):glm::dvec3(.62,.48,.7);
                add(x,3.0-age*2.5,z,color,.055,0.0,.42);
            }
        }
        if(used==0)return;
        particleShader.use();particleShader.matrices(glm::dmat4(1.0),view,projection,glm::dvec3(0.0));
        particleShader.uniform("viewportHeight",static_cast<double>(viewportHeight));
        glBindVertexArray(particleVao);glBindBuffer(GL_ARRAY_BUFFER,particleVbo);
        glBufferData(GL_ARRAY_BUFFER,used*sizeof(Particle),particles.data(),GL_STREAM_DRAW);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);glDrawArrays(GL_POINTS,0,static_cast<GLsizei>(used));glBindVertexArray(0);
    }
};

PlayerShowcase::PlayerShowcase(std::filesystem::path directory):directory_(directory),art_(std::move(directory)){}
PlayerShowcase::~PlayerShowcase()=default;

void PlayerShowcase::drawPlayer(const PlayerProfile& profile,const std::string& main,const std::string& sub,
                                Viewport viewport,double time,bool reducedMotion) {
    validate(viewport,time);
    const auto weapon=main.empty()?profile.weapon:main;
    const auto secondary=sub.empty()?profile.sub:sub;
    actors_.resize(1);
    if(!actors_.front())actors_.front()=std::make_unique<Actor>();
    auto& actor=*actors_.front();
    const std::string playerName=profile.name.empty()?"preview":profile.name;
    const std::string weaponId=weapon.empty()?"shooter":weapon;
    const std::string subId=secondary.empty()?"bomb":secondary;
    actor.configure(art_,playerName,profile.style,weaponId,subId,"lobby_pose");
    actor.advance(time,reducedMotion,{0.0,0.0,0.0},0.0);

    GlState saved;
    beginViewport(viewport);
    const double aspect=static_cast<double>(viewport.width)/viewport.height;
    constexpr double fov=25.0;
    const double tangent=std::tan(glm::radians(fov)*0.5);
    const double targetY=0.8,halfHeight=0.86,halfWidth=0.52;
    const double distance=std::max(halfHeight/(tangent*0.60),halfWidth/(tangent*aspect*0.85));
    const glm::dvec3 target{0.0,targetY,0.0},eye{distance*0.03,targetY+distance*0.10,distance};
    const auto view=glm::lookAt(eye,target,glm::dvec3(0.0,1.0,0.0));
    const auto projection=glm::perspective(glm::radians(fov),aspect,0.05,90.0);
    art_.draw(actor.pose->frame(),profile.style,weaponId,subId,"hero",view,projection,eye,showcaseLighting());
}

void PlayerShowcase::beginResults(double startTime) {
    if(!std::isfinite(startTime))throw std::invalid_argument("Showcase result start time must be finite");
    resultStartTime_=startTime;resultsBegun_=true;hasPodiumKey_=false;podiumNames_.clear();
    for(auto& actor:actors_)if(actor){actor->landTriggered=false;actor->hasTime=false;}
}

PlayerShowcase::PodiumMotion PlayerShowcase::podiumMotion(double elapsed,std::size_t slot,bool won,bool reducedMotion) {
    if(!std::isfinite(elapsed)||elapsed<0.0)throw std::invalid_argument("Showcase result elapsed time must be finite and non-negative");
    if(slot>=4)throw std::out_of_range("Showcase podium slot is outside the source layout");
    PodiumMotion motion;
    if(reducedMotion){motion.landed=won;return motion;}
    const double rise=backOut((elapsed-.04*slot)/.72,1.15);
    motion.drumOffset=-2.4*(1.0-rise);
    if(!won)return motion;
    constexpr std::array<double,4> landAt={.46,.59,.72,.90};
    const double fallStart=landAt[slot]-.5;
    if(elapsed<fallStart){motion.visible=false;return motion;}
    const double progress=std::clamp((elapsed-fallStart)/.5,0.0,1.0);
    motion.playerOffset=3.1*(1.0-progress*progress);
    motion.velocityY=-12.4*progress;
    motion.airborne=progress<1.0;
    motion.landed=!motion.airborne;
    const double landAge=std::max(0.0,elapsed-landAt[slot]);
    motion.squash=motion.airborne?1+.1*std::clamp(-motion.velocityY/12.,0.,1.):1.0-.15*std::exp(-6.0*landAge)*std::sin(14.0*landAge);
    return motion;
}

void PlayerShowcase::drawPodium(const std::vector<PodiumPlayer>& roster,bool won,Viewport viewport,
                                double time,bool reducedMotion) {
    if(roster.empty())return;
    validate(viewport,time);
    // The source showcase defines four result positions; larger rosters use those first four slots.
    static const std::array<glm::dvec3,4> positions={{{0.0,0.72,0.0},{-1.34,0.46,-0.14},{1.34,0.46,-0.14},{2.55,0.22,-0.36}}};
    static constexpr std::array<double,4> yaws={0.0,0.2,-0.2,-0.34};
    const std::size_t count=std::min(roster.size(),positions.size());
    std::vector<std::string> names;names.reserve(count);
    for(std::size_t i=0;i<count;++i)names.push_back(roster[i].name.empty()?"winner":roster[i].name);
    const bool setupChanged=hasPodiumKey_&&(podiumWon_!=won||podiumNames_!=names);
    if(!resultsBegun_||setupChanged){
        resultStartTime_=time;resultsBegun_=true;
        for(auto& actor:actors_)if(actor){actor->landTriggered=false;actor->hasTime=false;}
    }
    podiumWon_=won;podiumNames_=std::move(names);hasPodiumKey_=true;
    const double elapsed=reducedMotion?0.0:std::max(0.0,time-resultStartTime_);
    actors_.resize(count);
    std::array<PodiumMotion,4> motions{};std::array<double,4> drumOffsets{};
    double minX=positions[0].x,maxX=positions[0].x,maxY=positions[0].y+1.62;
    for(std::size_t i=0;i<count;++i){
        if(!actors_[i])actors_[i]=std::make_unique<Actor>();
        const auto& player=roster[i];
        const auto name=player.name.empty()?"winner":player.name;
        const auto main=player.main.empty()?"shooter":player.main;
        const auto sub=player.sub.empty()?"bomb":player.sub;
        motions[i]=podiumMotion(elapsed,i,won,reducedMotion);drumOffsets[i]=motions[i].drumOffset;
        auto& actor=*actors_[i];
        actor.configure(art_,name,player.style,main,sub,won?(motions[i].landed?"victory":""):"defeat");
        if(won&&motions[i].landed&&!reducedMotion&&!actor.landTriggered){actor.pose->trigger({"land",9.0});actor.landTriggered=true;}
        actor.advance(time,reducedMotion,positions[i]+glm::dvec3(0.0,motions[i].playerOffset,0.0),yaws[i],
                      motions[i].velocityY,!motions[i].airborne,motions[i].visible);
        minX=std::min(minX,positions[i].x);maxX=std::max(maxX,positions[i].x);
        maxY=std::max(maxY,positions[i].y+motions[i].playerOffset+1.62);
    }

    GlState saved;
    beginViewport(viewport);
    const double aspect=static_cast<double>(viewport.width)/viewport.height;
    constexpr double fov=26.0;
    const double tangent=std::tan(glm::radians(fov)*0.5);
    const double focusX=(minX+maxX)*0.5,focusY=maxY*0.5;
    const double halfWidth=(maxX-minX)*0.5+0.58,halfHeight=focusY;
    const double distance=std::max(halfHeight/(tangent*0.78),halfWidth/(tangent*aspect*0.86));
    const glm::dvec3 target{focusX,focusY,0.0},eye{focusX+distance*0.03,focusY+distance*0.10,distance};
    const auto view=glm::lookAt(eye,target,glm::dvec3(0.0,1.0,0.0));
    const auto projection=glm::perspective(glm::radians(fov),aspect,0.05,distance+30.0);
    const auto light=showcaseLighting();
    if(!stage_)stage_=std::make_unique<Stage>(directory_/"showcase-drums.bin");
    stage_->draw(count,drumOffsets,view,projection,eye);
    for(std::size_t i=0;i<count;++i){
        if(!motions[i].visible)continue;
        const auto& player=roster[i];
        auto frame=actors_[i]->pose->frame();
        const double lateralScale=1.0/std::sqrt(std::max(.01,motions[i].squash));
        frame.root.scale*=glm::dvec3(lateralScale,motions[i].squash,lateralScale);
        art_.draw(frame,player.style,
                  player.main.empty()?"shooter":player.main,player.sub.empty()?"bomb":player.sub,
                  "hero",view,projection,eye,light);
    }
    stage_->drawParticles(count,positions,elapsed,won,reducedMotion,view,projection,viewport.height);
}

} // namespace restoration
