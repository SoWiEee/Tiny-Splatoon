#pragma once

#include "FacePaint.hpp"
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace restoration {
// paint.js' bounded, camera-prioritized table; birth times share the ink shader clock.
class PaintRipples {
public:
    static constexpr int Capacity=24;
    void reset(){positions_.fill(glm::vec4(0));parameters_.fill(glm::vec4(0));scores_.fill(0);pack();}
    PaintRipples(){reset();}
    bool near(glm::dvec3 pos,double radius,double clock)const{
        const double reach=0.55+radius*0.6;
        for(int i=0;i<Capacity;++i){
            const double age=clock-positions_[i].w;
            if(age<0||age>0.16||parameters_[i].x<0.0036+0.0036*std::min(radius,3.0)*0.7)continue;
            if(glm::length(glm::dvec3(positions_[i])-pos)<reach)return true;
        }
        return false;
    }
    bool add(const VisualRipple& ripple,double clock,std::optional<glm::dvec3> camera={}){
        const double d2=camera?glm::dot(ripple.position-*camera,ripple.position-*camera):0;
        if(d2>38*38)return false;
        const double score=ripple.amplitude/(1+d2*0.012);
        int best=-1;double bestScore=INFINITY;
        for(int i=0;i<Capacity;++i){
            const double age=clock-positions_[i].w,life=parameters_[i].w;
            if(age>=life||age<-1){best=i;bestScore=-1;break;}
            const double remaining=scores_[i]*(1-age/life);
            if(remaining<bestScore){bestScore=remaining;best=i;}
        }
        if(best<0||bestScore>score)return false;
        positions_[best]=glm::vec4(ripple.position,clock);
        parameters_[best]={float(ripple.amplitude),float(ripple.wavelength),float(ripple.speed),float(ripple.life)};
        scores_[best]=float(score);pack();return true;
    }
    const std::vector<double>& positions()const{return positionValues_;}
    const std::vector<double>& parameters()const{return parameterValues_;}
private:
    void pack(){
        positionValues_.resize(Capacity*4);parameterValues_.resize(Capacity*4);
        for(int i=0;i<Capacity;++i)for(int c=0;c<4;++c){positionValues_[i*4+c]=positions_[i][c];parameterValues_[i*4+c]=parameters_[i][c];}
    }
    std::array<glm::vec4,Capacity> positions_{},parameters_{};
    std::array<float,Capacity> scores_{};
    std::vector<double> positionValues_,parameterValues_;
};
}
