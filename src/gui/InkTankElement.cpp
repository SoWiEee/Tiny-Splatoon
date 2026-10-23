#include "InkTankElement.hpp"
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/ComputedValues.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace restoration {
namespace {
using Color = Rml::ColourbPremultiplied;

Color rgba(int r,int g,int b,int a=255) { return Rml::Colourb{static_cast<Rml::byte>(r),static_cast<Rml::byte>(g),static_cast<Rml::byte>(b),static_cast<Rml::byte>(a)}.ToPremultiplied(); }
Rml::Colourb blend(Rml::Colourb a,Rml::Colourb b,float t) {
    const auto c=[t](int x,int y){return static_cast<Rml::byte>(std::clamp(std::lround(x+(y-x)*t),0l,255l));};
    return {c(a.red,b.red),c(a.green,b.green),c(a.blue,b.blue),c(a.alpha,b.alpha)};
}
Rml::Colourb shade(Rml::Colourb c,float amount) {
    const auto target=static_cast<Rml::byte>(amount>0?255:0);
    return blend(c,Rml::Colourb{target,target,target,c.alpha},std::abs(amount));
}
Color premul(Rml::Colourb c) { return c.ToPremultiplied(); }
Rml::Colourb gradient(Rml::Colourb base,float x) {
    const auto dark=shade(base,-.30f),light=shade(base,.42f);
    return x<.45f?blend(dark,base,x/.45f):blend(base,light,(x-.45f)/.55f);
}
void triangle(Rml::Mesh& m,Rml::Vector2f a,Rml::Vector2f b,Rml::Vector2f c,Color ca,Color cb,Color cc) {
    const int i=static_cast<int>(m.vertices.size());
    m.vertices.push_back({a,ca,{0,0}});m.vertices.push_back({b,cb,{0,0}});m.vertices.push_back({c,cc,{0,0}});
    m.indices.insert(m.indices.end(),{i,i+1,i+2});
}
void quad(Rml::Mesh& m,Rml::Vector2f a,Rml::Vector2f b,Rml::Vector2f c,Rml::Vector2f d,Color ca,Color cb,Color cc,Color cd) {
    triangle(m,a,b,c,ca,cb,cc);triangle(m,a,c,d,ca,cc,cd);
}
std::vector<Rml::Vector2f> rounded(float x,float y,float w,float h,float r) {
    std::vector<Rml::Vector2f> p;p.reserve(36);
    const std::array<Rml::Vector2f,4> centers{{{x+w-r,y+r},{x+w-r,y+h-r},{x+r,y+h-r},{x+r,y+r}}};
    const std::array<float,4> starts{{-90,0,90,180}};
    for(std::size_t corner=0;corner<4;++corner)for(int i=0;i<=8;++i){const float a=(starts[corner]+90.f*i/8.f)*.01745329252f;p.push_back({centers[corner].x+std::cos(a)*r,centers[corner].y+std::sin(a)*r});}
    return p;
}
void fillPolygon(Rml::Mesh& mesh,const std::vector<Rml::Vector2f>& p,const std::function<Color(float)>& colorAt) {
    Rml::Vector2f center{};for(const auto& v:p){center.x+=v.x;center.y+=v.y;}center.x/=static_cast<float>(p.size());center.y/=static_cast<float>(p.size());
    const Color cc=colorAt(center.x);
    for(std::size_t i=0;i<p.size();++i)triangle(mesh,center,p[i],p[(i+1)%p.size()],cc,colorAt(p[i].x),colorAt(p[(i+1)%p.size()].x));
}
float topAt(float x,float left,float top,float width,float radius) {
    const float center=std::clamp(x,left+radius,left+width-radius),dx=x-center;
    return top+radius-std::sqrt(std::max(0.f,radius*radius-dx*dx));
}
float bottomAt(float x,float left,float top,float height,float width,float radius) {
    const float center=std::clamp(x,left+radius,left+width-radius),dx=x-center;
    return top+height-radius+std::sqrt(std::max(0.f,radius*radius-dx*dx));
}
void disc(Rml::Mesh& mesh,float x,float y,float r,Color color) {
    constexpr int n=8;for(int i=0;i<n;++i){const float a=6.2831853f*i/n,b=6.2831853f*(i+1)/n;triangle(mesh,{x,y},{x+std::cos(a)*r,y+std::sin(a)*r},{x+std::cos(b)*r,y+std::sin(b)*r},color,color,color);}
}
void rect(Rml::Mesh& mesh,float x,float y,float w,float h,Color c) { quad(mesh,{x,y},{x+w,y},{x+w,y+h},{x,y+h},c,c,c,c); }
} // namespace

InkTankElement::InkTankElement(const Rml::String& tag):Rml::Element(tag) {}

void InkTankElement::Reset() {
    initialized_=haveYaw_=haveVelocity_=false;bubbleCount_=0;level_=1;time_=slosh_=sloshVelocity_=0;previousInk_=1;bubbleCredit_=0;
    bubbles_.fill(Bubble{});geometry_=Rml::Geometry{};
}

void InkTankElement::Update(const InkTankFrame& frame) {
    frame_=frame;
    frame_.ink=std::isfinite(frame.ink)?std::clamp(frame.ink,0.0,100.0):0.0;
    frame_.subCost=std::isfinite(frame.subCost)?std::clamp(frame.subCost,0.0,100.0):0.0;
    frame_.yaw=std::isfinite(frame.yaw)?frame.yaw:0.0;frame_.vx=std::isfinite(frame.vx)?frame.vx:0.0;frame_.vz=std::isfinite(frame.vz)?frame.vz:0.0;
    const double dt=std::clamp(std::isfinite(frame.dt)?frame.dt:0.0,0.0,1.0/15.0),ink=frame_.ink/100.0;
    if(!initialized_){level_=previousInk_=ink;previousYaw_=frame_.yaw;previousVx_=frame_.vx;previousVz_=frame_.vz;initialized_=haveYaw_=haveVelocity_=true;}
    else {
        time_+=dt;
        if(!frame_.reducedMotion){
            double drive=0;
            if(haveYaw_&&dt>0){const double dy=std::atan2(std::sin(frame_.yaw-previousYaw_),std::cos(frame_.yaw-previousYaw_));drive+=std::clamp(dy/dt,-8.0,8.0)*.55;}
            if(haveVelocity_&&dt>0){const double ax=(frame_.vx-previousVx_)/dt,az=(frame_.vz-previousVz_)/dt;drive+=std::clamp((ax*std::cos(frame_.yaw)-az*std::sin(frame_.yaw))*.05,-3.0,3.0);}
            sloshVelocity_+=(-90.0*slosh_-7.5*sloshVelocity_-drive*6.0)*dt;
            slosh_=std::clamp(slosh_+sloshVelocity_*dt,-.6,.6);
        } else slosh_=sloshVelocity_=0;
        const bool rising=ink>previousInk_+1e-4;
        if(rising&&!frame_.reducedMotion&&bubbleCount_<bubbles_.size()){
            bubbleCredit_=std::min(14.0,bubbleCredit_+dt*(ink-previousInk_>dt*.2?40.0:12.0));
            while(bubbleCredit_>=1.0&&bubbleCount_<bubbles_.size()){
                randomState_=randomState_*1664525u+1013904223u;const float a=(randomState_>>8)*(1.f/16777216.f);
                randomState_=randomState_*1664525u+1013904223u;const float b=(randomState_>>8)*(1.f/16777216.f);
                randomState_=randomState_*1664525u+1013904223u;const float c=(randomState_>>8)*(1.f/16777216.f);
                bubbles_[bubbleCount_++]={.2f+a*.6f,0,.6f+b*1.4f,.5f+c*.7f};bubbleCredit_-=1.0;
            }
        } else bubbleCredit_=0;
        if(!frame_.reducedMotion)for(std::size_t i=0;i<bubbleCount_;){auto& b=bubbles_[i];b.y+=b.v*static_cast<float>(dt);if(b.y>1.05f){bubbles_[i]=bubbles_[--bubbleCount_];}else ++i;}
        level_+=(ink-level_)*(1.0-std::exp(-dt*14.0));previousInk_=ink;previousYaw_=frame_.yaw;previousVx_=frame_.vx;previousVz_=frame_.vz;
    }
}

void InkTankElement::OnRender() {
    const auto size=GetBox().GetSize();const float w=size.x,h=size.y;
    if(w<=1||h<=1||!GetRenderManager())return;
    Rml::Mesh mesh;const float pad=std::min(3.f,w*.14f),x=pad,y=pad,bw=w-2*pad,bh=h-2*pad,r=std::min(bw*.5f,bh*.5f);
    if(bw<=0||bh<=0)return;
    const auto body=rounded(x,y,bw,bh,r);
    const Color glass=rgba(14,11,22,159),white=rgba(255,255,255,255);
    fillPolygon(mesh,body,[glass](float){return glass;});

    const float lvl=y+bh*static_cast<float>(1.0-level_),tilt=static_cast<float>(frame_.reducedMotion?0:slosh_*bw*.9);
    const float amp=frame_.reducedMotion?0.f:1.2f+std::abs(static_cast<float>(sloshVelocity_))*2.2f;
    constexpr int waveSegments=10;const float phase=static_cast<float>(frame_.reducedMotion?0:time_);
    for(int i=0;i<waveSegments;++i){
        const float u0=static_cast<float>(i)/waveSegments,u1=static_cast<float>(i+1)/waveSegments,x0=x+bw*u0,x1=x+bw*u1;
        const auto wave=[&](float u){return lvl+(u-.5f)*tilt+std::sin(u*6.5f+phase*7.f)*amp*.5f+std::sin(u*11.f-phase*9.5f)*amp*.25f;};
        const float b0=bottomAt(x0,x,y,bh,bw,r),b1=bottomAt(x1,x,y,bh,bw,r);
        const float y0=std::clamp(wave(u0),topAt(x0,x,y,bw,r),b0),y1=std::clamp(wave(u1),topAt(x1,x,y,bw,r),b1);
        if(y0>=b0&&y1>=b1)continue;
        const auto inkColor=[&](float u,float depth){auto c=gradient(frame_.color,u);c=blend(c,{255,255,255,c.alpha},.18f*(1.f-std::clamp(depth/.15f,0.f,1.f)));c=blend(c,shade(frame_.color,-.55f),.4f*depth);return premul(c);};
        quad(mesh,{x0,y0},{x1,y1},{x1,b1},{x0,b0},inkColor(u0,0),inkColor(u1,0),inkColor(u1,1),inkColor(u0,1));
    }

    if(!frame_.reducedMotion){
        const Color bubble=rgba(255,255,255,153);
        for(std::size_t i=0;i<bubbleCount_;++i){const auto& b=bubbles_[i];const float bx=x+b.x*bw+std::sin(b.y*20.f),by=y+bh-r-b.y*bh;
            if(bx<x||bx>x+bw||by<=lvl+2||by<topAt(bx,x,y,bw,r)||by>bottomAt(bx,x,y,bh,bw,r))continue;
            disc(mesh,bx,by,b.r,bubble);
        }
    }
    const float cost=static_cast<float>(frame_.subCost/100.0);
    if(cost>0){const float sy=y+bh*(1.f-cost);rect(mesh,x-1,sy+1.2f,bw+2,1.f,rgba(0,0,0,140));rect(mesh,x-1,sy-1.2f,bw+2,2.4f,frame_.ink<frame_.subCost?rgba(255,77,94):white);}
    const auto gloss=rounded(x+bw*.2f,y+bh*.07f,bw*.16f,bh*.4f,bw*.08f);
    fillPolygon(mesh,gloss,[](float){return rgba(255,255,255,128);});
    for(int pass=0;pass<2;++pass){const float half=(pass==0?3.4f:1.8f)*.5f;const Color color=pass==0?rgba(12,9,20,204):(frame_.low&&((!frame_.reducedMotion&&std::sin(time_*14)>0)||frame_.reducedMotion)?rgba(255,61,94):white);
        for(std::size_t i=0;i<body.size();++i){const auto a=body[i],b=body[(i+1)%body.size()];const float dx=b.x-a.x,dy=b.y-a.y,len=std::max(std::hypot(dx,dy),1e-4f),nx=dy/len*half,ny=-dx/len*half;quad(mesh,{a.x+nx,a.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{a.x-nx,a.y-ny},color,color,color,color);}
    }
    const float opacity=GetComputedValues().opacity();
    for(auto& vertex:mesh.vertices)vertex.colour=vertex.colour*opacity;
    geometry_=GetRenderManager()->MakeGeometry(std::move(mesh));
    geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
}

} // namespace restoration
