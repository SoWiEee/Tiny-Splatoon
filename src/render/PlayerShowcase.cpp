#include "PlayerShowcase.hpp"
#include "TeamPalette.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

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
    GLboolean depthMask_=GL_TRUE,depth_=GL_FALSE,scissorEnabled_=GL_FALSE,blend_=GL_FALSE,cull_=GL_FALSE;
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
    bool hasTime=false;

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
            if(newDance=="victory")pose->trigger({"land",9.0});
            dance=newDance;
        }
    }

    void advance(double time,bool reducedMotion,glm::dvec3 position,double yaw) {
        const double dt=hasTime?std::clamp(time-lastTime,0.0,0.1):0.0;
        lastTime=time;hasTime=true;
        CharacterAnimState state;state.position=position;state.yaw=yaw;
        CharacterSceneInput scene;
        pose->update(reducedMotion?0.0:dt,state,scene);
    }
};

PlayerShowcase::PlayerShowcase(std::filesystem::path directory):art_(std::move(directory)){}
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

void PlayerShowcase::drawPodium(const std::vector<PodiumPlayer>& roster,bool won,Viewport viewport,
                                double time,bool reducedMotion) {
    if(roster.empty())return;
    validate(viewport,time);
    // The source showcase defines four result positions; larger rosters use those first four slots.
    static const std::array<glm::dvec3,4> positions={{{0.0,0.72,0.0},{-1.34,0.46,-0.14},{1.34,0.46,-0.14},{2.55,0.22,-0.36}}};
    static constexpr std::array<double,4> yaws={0.0,0.2,-0.2,-0.34};
    const std::size_t count=std::min(roster.size(),positions.size());
    actors_.resize(count);
    double minX=positions[0].x,maxX=positions[0].x,maxY=positions[0].y+1.62;
    for(std::size_t i=0;i<count;++i){
        if(!actors_[i])actors_[i]=std::make_unique<Actor>();
        const auto& player=roster[i];
        const auto name=player.name.empty()?"winner":player.name;
        const auto main=player.main.empty()?"shooter":player.main;
        const auto sub=player.sub.empty()?"bomb":player.sub;
        actors_[i]->configure(art_,name,player.style,main,sub,won?"victory":"defeat");
        actors_[i]->advance(time,reducedMotion,positions[i],yaws[i]);
        minX=std::min(minX,positions[i].x);maxX=std::max(maxX,positions[i].x);
        maxY=std::max(maxY,positions[i].y+1.62);
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
    for(std::size_t i=0;i<count;++i){
        const auto& player=roster[i];
        art_.draw(actors_[i]->pose->frame(),player.style,
                  player.main.empty()?"shooter":player.main,player.sub.empty()?"bomb":player.sub,
                  "hero",view,projection,eye,light);
    }
}

} // namespace restoration
