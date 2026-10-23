#include "StageMapView.hpp"

#include "EnvironmentRenderer.hpp"
#include "StageRenderer.hpp"

#include <stdexcept>

namespace restoration {
namespace {
bool enabled(GLenum capability) { return glIsEnabled(capability)==GL_TRUE; }

struct RenderState {
    GLint drawFramebuffer=0,readFramebuffer=0,viewport[4]{},program=0,vao=0,arrayBuffer=0;
    GLint activeTexture=GL_TEXTURE0,depthFunction=GL_LESS,polygonMode[2]{GL_FILL,GL_FILL};
    GLint textures[10]{},samplers[10]{};
    GLboolean depthMask=GL_TRUE,colorMask[4]{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    GLfloat clearColor[4]{};
    bool depth=false,blend=false,cull=false,scissor=false,srgb=false;
    RenderState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFramebuffer);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFramebuffer);
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);
        glGetIntegerv(GL_POLYGON_MODE,polygonMode);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);
        depth=enabled(GL_DEPTH_TEST);blend=enabled(GL_BLEND);cull=enabled(GL_CULL_FACE);
        scissor=enabled(GL_SCISSOR_TEST);srgb=enabled(GL_FRAMEBUFFER_SRGB);
        for(int unit=0;unit<10;++unit){
            glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[unit]);
            glGetIntegeri_v(GL_SAMPLER_BINDING,unit,&samplers[unit]);
        }
        glActiveTexture(activeTexture);
    }
    ~RenderState() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(readFramebuffer));
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glUseProgram(static_cast<GLuint>(program));glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER,static_cast<GLuint>(arrayBuffer));
        for(int unit=0;unit<10;++unit){
            glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(textures[unit]));
            glBindSampler(unit,static_cast<GLuint>(samplers[unit]));
        }
        glActiveTexture(static_cast<GLenum>(activeTexture));
        if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);else glDisable(GL_FRAMEBUFFER_SRGB);
        glDepthFunc(static_cast<GLenum>(depthFunction));glDepthMask(depthMask);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);
        glPolygonMode(GL_FRONT_AND_BACK,static_cast<GLenum>(polygonMode[0]));
    }
};
}

StageMapView::StageMapView(glm::dvec4 bounds,bool flipForBlueTeam):camera_(FitStageMapCamera(bounds,double(Width)/Height,flipForBlueTeam)) {
    GLint oldTexture=0,oldRenderbuffer=0,oldFramebuffer=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFramebuffer);
    glGenTextures(1,&color_);glBindTexture(GL_TEXTURE_2D,color_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,Width,Height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1,&depth_);glBindRenderbuffer(GL_RENDERBUFFER,depth_);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,Width,Height);
    glGenFramebuffers(1,&framebuffer_);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color_,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth_);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        glBindFramebuffer(GL_FRAMEBUFFER,static_cast<GLuint>(oldFramebuffer));glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(oldRenderbuffer));
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(oldTexture));
        glDeleteFramebuffers(1,&framebuffer_);glDeleteRenderbuffers(1,&depth_);glDeleteTextures(1,&color_);
        framebuffer_=depth_=color_=0;throw std::runtime_error("Incomplete stage map framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER,static_cast<GLuint>(oldFramebuffer));glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(oldRenderbuffer));
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(oldTexture));
}

StageMapView::~StageMapView(){
    if(framebuffer_)glDeleteFramebuffers(1,&framebuffer_);
    if(depth_)glDeleteRenderbuffers(1,&depth_);
    if(color_)glDeleteTextures(1,&color_);
}

bool StageMapView::Render(StageRenderer& stage,EnvironmentRenderer& environment,std::uint64_t paintRevision,double now) {
    if(lastRenderTime_>=0.0&&now-lastRenderTime_<.125)return false;

    RenderState state;
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);glViewport(0,0,Width,Height);
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(.64f,.81f,.9f,1.0f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view(camera_.view),projection(camera_.projection);
    const glm::vec3 eye(camera_.eye);
    environment.DrawSea(view,projection,eye);
    environment.DrawScenery(view,projection,eye);
    stage.Draw(camera_.view,camera_.projection,camera_.eye,environment.Lights());
    environment.DrawSky(view,projection,eye);

    lastPaintRevision_=paintRevision;lastRenderTime_=now;
    return true;
}
} // namespace restoration
