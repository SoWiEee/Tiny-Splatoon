#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>

namespace restoration {
struct StagePropAnimation {
    std::string kind;
    glm::dmat4 base{1.0};
    double speed=0,phase=0,rate=0,lo=0,hi=0;
    glm::dmat4 matrix(double time) const {
        auto m=base;
        if(kind=="y"||kind=="z")m=glm::rotate(base,time*speed+phase,kind=="z"?glm::dvec3(0,0,1):glm::dvec3(0,1,0));
        // Source InstancedMesh uploads each resulting matrix to Float32 storage.
        return glm::dmat4(glm::mat4(m));
    }
    double brightness(double time) const {
        if(kind!="blink")return 1;
        const double s=.5+.5*std::sin(time*rate*6.283185307179586+phase),k=s*s*(3-2*s);
        return lo+(hi-lo)*(k>.6?1:k/.6*.25);
    }
    void cloth(double time,float flex,glm::vec3 wave,glm::vec3& p,glm::vec3& n) const {
        const glm::vec3 origin{glm::mat4(base)[3]};
        const float cph=origin.x*.83f+origin.z*.61f+origin.y*.37f;
        const float ca=float(time)*wave.x+cph+p.x*wave.y,cb=float(time)*wave.x*2.3f+cph*1.7f+p.y*3.f;
        p.z+=flex*wave.z*(std::sin(ca)+.35f*std::sin(cb));
        n=glm::normalize(n+glm::vec3(-flex*wave.z*wave.y*std::cos(ca),-flex*wave.z*1.05f*std::cos(cb),0));
    }
};
}
