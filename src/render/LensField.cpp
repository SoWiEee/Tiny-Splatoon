#include "LensField.hpp"
#include "CharacterShader.hpp"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace restoration {
struct LensField::State {
    CharacterShader shader;
    GLuint vao=0,vertices=0,instances=0,fbo=0,texture=0;int width=0,height=0;
    explicit State(const std::filesystem::path& assets):shader(CharacterShader::readSource(assets/"lens.vert"),CharacterShader::readSource(assets/"lens.frag")){
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vertices);glBindBuffer(GL_ARRAY_BUFFER,vertices);
        const float quad[]={-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0};glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
        const auto position=shader.attribute("position");glEnableVertexAttribArray(position);glVertexAttribPointer(position,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
        glGenBuffers(1,&instances);glBindBuffer(GL_ARRAY_BUFFER,instances);glBufferData(GL_ARRAY_BUFFER,LensInk::Capacity*8*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
        for(int i=0;i<2;i++){const auto a=shader.attribute(i?"iB":"iA");glEnableVertexAttribArray(a);glVertexAttribPointer(a,4,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(i*4*sizeof(float)));glVertexAttribDivisor(a,1);}
        glBindVertexArray(0);glGenFramebuffers(1,&fbo);
    }
    ~State(){if(texture)glDeleteTextures(1,&texture);if(fbo)glDeleteFramebuffers(1,&fbo);if(instances)glDeleteBuffers(1,&instances);if(vertices)glDeleteBuffers(1,&vertices);if(vao)glDeleteVertexArrays(1,&vao);}
};
LensField::LensField(std::filesystem::path assets):state_(std::make_unique<State>(assets)){}
LensField::~LensField()=default;
unsigned LensField::texture()const{return state_->texture;}
glm::vec2 LensField::texel()const{return {state_->width?1.f/state_->width:.25f,state_->height?1.f/state_->height:.25f};}
int LensField::Render(const LensInk& ink,int w,int h,double scale){
    if(w<=0||h<=0||!std::isfinite(scale)||scale<=0||scale>1)throw std::invalid_argument("Invalid lens target dimensions");
    auto& s=*state_;GLint drawFbo,readFbo,viewport[4],srcRgb,dstRgb,srcAlpha,dstAlpha,eqRgb,eqAlpha;GLfloat clearColor[4];GLboolean depthMask;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_VIEWPORT,viewport);glGetFloatv(GL_COLOR_CLEAR_VALUE,clearColor);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);
    const bool blend=glIsEnabled(GL_BLEND),depth=glIsEnabled(GL_DEPTH_TEST),cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,s.fbo);const int width=std::max(64,int(std::floor(w*scale+.5))),height=std::max(36,int(std::floor(h*scale+.5)));
    if(s.width!=width||s.height!=height){
        if(s.texture)glDeleteTextures(1,&s.texture);glGenTextures(1,&s.texture);glBindTexture(GL_TEXTURE_2D,s.texture);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA16F,width,height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.texture,0);s.width=width;s.height=height;
    }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);throw std::runtime_error("Incomplete lens field framebuffer");}
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDepthMask(GL_FALSE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    const auto& parts=ink.particles();
    if(!parts.empty()){
        std::array<float,LensInk::Capacity*8> data{};std::size_t n=0;
        for(const auto* p:parts){const double ease=1-std::pow(1-std::clamp(p->grow,0.,1.),3),growth=p->grow<1?.35+.65*ease*(1+.12*std::sin(p->grow*3.141592653589793)):1;
            for(double v:{p->x,p->y,p->rx*growth,p->ry*growth,p->rotation,p->intensity,double(p->channel),0.})data[n++]=float(v);}
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE);s.shader.use();s.shader.uniform("uAspect",double(w)/h);glBindVertexArray(s.vao);glBindBuffer(GL_ARRAY_BUFFER,s.instances);glBufferSubData(GL_ARRAY_BUFFER,0,n*sizeof(float),data.data());glDrawArraysInstanced(GL_TRIANGLES,0,6,static_cast<GLsizei>(parts.size()));
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glClearColor(clearColor[0],clearColor[1],clearColor[2],clearColor[3]);glDepthMask(depthMask);
    glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);glBlendEquationSeparate(eqRgb,eqAlpha);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    return parts.empty()?0:1;
}
}
