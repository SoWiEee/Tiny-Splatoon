#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace restoration {
// Source Environment::_fitShadow; GL lifetime belongs to the active render context.
class DirectionalShadow {
public:
    DirectionalShadow()=default;
    ~DirectionalShadow();
    DirectionalShadow(const DirectionalShadow&)=delete;
    DirectionalShadow& operator=(const DirectionalShadow&)=delete;
    void Begin(glm::dvec4 arenaBounds,glm::dvec3 sunDirection,int size=4096);
    void End();
    unsigned texture()const{return texture_;}
    const glm::dmat4& view()const{return view_;}
    const glm::dmat4& projection()const{return projection_;}
    glm::dmat4 matrix()const{return projection_*view_;}
    double bias()const{return bias_;}
    double normalBias()const{return normalBias_;}
    double radius()const{return radius_;}
    int size()const{return size_;}
    static const char* SamplingGLSL();
private:
    unsigned fbo_=0,texture_=0;
    int size_=0,previousDraw_=0,previousRead_=0,viewport_[4]{};
    bool active_=false,depthTest_=false,cull_=false,scissor_=false,depthMask_=true;
    glm::dmat4 view_{1},projection_{1};
    double bias_=0,normalBias_=0,radius_=0;
};
}
