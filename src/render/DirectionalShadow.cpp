#include "DirectionalShadow.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace restoration {
DirectionalShadow::~DirectionalShadow(){if(texture_)glDeleteTextures(1,&texture_);if(fbo_)glDeleteFramebuffers(1,&fbo_);}
void DirectionalShadow::Begin(glm::dvec4 b,glm::dvec3 direction,int size){
    if(active_)throw std::logic_error("Shadow frame already active");
    GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);
    if(size<=0||size>maxSize||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z)||!std::isfinite(b.w)||b.y<=b.x||b.w<=b.z||!std::isfinite(glm::length(direction))||glm::length(direction)<1e-8)throw std::invalid_argument("Invalid shadow arena or direction");
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw_);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead_);glGetIntegerv(GL_VIEWPORT,viewport_);
    depthTest_=glIsEnabled(GL_DEPTH_TEST);cull_=glIsEnabled(GL_CULL_FACE);scissor_=glIsEnabled(GL_SCISSOR_TEST);GLboolean mask;glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);depthMask_=mask;
    if(!fbo_)glGenFramebuffers(1,&fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
    if(size_!=size){
        if(texture_)glDeleteTextures(1,&texture_);glGenTextures(1,&texture_);glBindTexture(GL_TEXTURE_2D,texture_);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH_COMPONENT32F,size,size);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);const float border[]={1,1,1,1};glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,texture_,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);size_=size;
    }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousDraw_);glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead_);throw std::runtime_error("Incomplete shadow framebuffer");}
    direction=glm::normalize(direction);const glm::dvec3 target((b.x+b.y)/2,0,(b.z+b.w)/2),position=target+direction*160.;
    view_=glm::lookAt(position,target,std::abs(direction.y)>.9999?glm::dvec3(0,0,1):glm::dvec3(0,1,0));
    glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());
    for(double x:{b.x-6,b.y+6})for(double y:{-1.9,14.})for(double z:{b.z-6,b.w+6}){const glm::dvec3 p=view_*glm::dvec4(x,y,z,1);lo=glm::min(lo,p);hi=glm::max(hi,p);}
    const double nearPlane=std::max(.5,-hi.z-30),farPlane=-lo.z+2;
    projection_=glm::ortho(lo.x,hi.x,lo.y,hi.y,nearPlane,farPlane);
    bias_=-.025/(farPlane-nearPlane);normalBias_=std::max(.012,std::max(hi.x-lo.x,hi.y-lo.y)/size*1.4);radius_=2.2*(size/4096.)+.8;
    glViewport(0,0,size,size);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);active_=true;
}
void DirectionalShadow::End(){
    if(!active_)throw std::logic_error("No shadow frame to end");
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousDraw_);glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead_);glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
    if(depthTest_)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);if(cull_)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(scissor_)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);glDepthMask(depthMask_?GL_TRUE:GL_FALSE);active_=false;
}
const char* DirectionalShadow::SamplingGLSL(){return R"GLSL(
uniform sampler2DShadow sunShadow;
uniform mat4 sunShadowMatrix;
uniform float sunShadowBias;
uniform float sunShadowNormalBias;
uniform float sunShadowRadius;
float sampleSunShadow(vec3 worldPosition,vec3 worldNormal){
    vec4 light=sunShadowMatrix*vec4(worldPosition+worldNormal*sunShadowNormalBias,1.);
    vec3 p=light.xyz/light.w*.5+.5;p.z+=sunShadowBias;
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return 1.;
    vec2 ts=1./vec2(textureSize(sunShadow,0))*max(sunShadowRadius*.55,.6);
    float sum=0.;for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)sum+=texture(sunShadow,vec3(p.xy+vec2(x,y)*ts,p.z));
    return sum*(1./9.);
}
)GLSL";}
}
