#include "SpecialGaugeElement.hpp"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace restoration {
namespace {
using Color=Rml::ColourbPremultiplied;
struct Point { float x=0,y=0; };
constexpr float Pi=3.14159265358979323846f,Tau=2.0f*Pi;

Color tint(Rml::Colourb color,float opacity=1) {
    color.alpha=static_cast<Rml::byte>(std::clamp(std::lround(color.alpha*opacity),0l,255l));
    return color.ToPremultiplied();
}
Color rgba(int r,int g,int b,int a=255) {
    return tint({static_cast<Rml::byte>(r),static_cast<Rml::byte>(g),static_cast<Rml::byte>(b),static_cast<Rml::byte>(a)});
}
Rml::Colourb blend(Rml::Colourb a,Rml::Colourb b,float t) {
    const auto c=[t](int x,int y){return static_cast<Rml::byte>(std::clamp(std::lround(x+(y-x)*t),0l,255l));};
    return {c(a.red,b.red),c(a.green,b.green),c(a.blue,b.blue),c(a.alpha,b.alpha)};
}
Rml::Colourb shade(Rml::Colourb color,float amount) {
    const auto target=static_cast<Rml::byte>(amount>0?255:0);
    return blend(color,{target,target,target,color.alpha},std::abs(amount));
}
void triangle(Rml::Mesh& mesh,Point a,Point b,Point c,Color color) {
    const int i=static_cast<int>(mesh.vertices.size());
    mesh.vertices.push_back({{a.x,a.y},color,{0,0}});mesh.vertices.push_back({{b.x,b.y},color,{0,0}});mesh.vertices.push_back({{c.x,c.y},color,{0,0}});
    mesh.indices.insert(mesh.indices.end(),{i,i+1,i+2});
}
void quad(Rml::Mesh& mesh,Point a,Point b,Point c,Point d,Color color) {
    triangle(mesh,a,b,c,color);triangle(mesh,a,c,d,color);
}
void lineRaw(Rml::Mesh& mesh,Point a,Point b,float width,Color color) {
    const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
    if(length<1e-4f||width<=0)return;
    const float nx=dy/length*width*.5f,ny=-dx/length*width*.5f;
    quad(mesh,{a.x+nx,a.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{a.x-nx,a.y-ny},color);
}
void arc(Rml::Mesh& mesh,Point center,float radius,float start,float sweep,float width,Color color) {
    const int steps=std::max(2,static_cast<int>(std::ceil(std::abs(sweep)*radius/3.f)));
    Point previous{center.x+std::cos(start)*radius,center.y+std::sin(start)*radius};
    for(int i=1;i<=steps;++i){const float angle=start+sweep*static_cast<float>(i)/steps;Point next{center.x+std::cos(angle)*radius,center.y+std::sin(angle)*radius};lineRaw(mesh,previous,next,width,color);previous=next;}
}
void disc(Rml::Mesh& mesh,Point center,float radius,Color color) {
    constexpr int steps=32;
    for(int i=0;i<steps;++i){const float a=Tau*i/steps,b=Tau*(i+1)/steps;triangle(mesh,center,{center.x+std::cos(a)*radius,center.y+std::sin(a)*radius},{center.x+std::cos(b)*radius,center.y+std::sin(b)*radius},color);}
}
std::vector<Point> outline(Point center,float radius) {
    constexpr int steps=48;std::vector<Point> points;points.reserve(steps);
    for(int i=0;i<steps;++i){const float a=Tau*i/steps,r=radius*(1.f+.045f*std::sin(3*a+.3f)+.025f*std::cos(5*a));points.push_back({center.x+std::cos(a)*r,center.y+std::sin(a)*r});}
    return points;
}
void fill(Rml::Mesh& mesh,const std::vector<Point>& polygon,Color color) {
    Point center{};for(auto p:polygon){center.x+=p.x;center.y+=p.y;}center.x/=static_cast<float>(polygon.size());center.y/=static_cast<float>(polygon.size());
    for(std::size_t i=0;i<polygon.size();++i)triangle(mesh,center,polygon[i],polygon[(i+1)%polygon.size()],color);
}
std::pair<float,float> verticalBounds(const std::vector<Point>& polygon,float x) {
    float top=std::numeric_limits<float>::max(),bottom=std::numeric_limits<float>::lowest();
    for(std::size_t i=0;i<polygon.size();++i){const Point a=polygon[i],b=polygon[(i+1)%polygon.size()];
        if((x<std::min(a.x,b.x))||(x>std::max(a.x,b.x))||std::abs(b.x-a.x)<1e-5f)continue;
        const float y=a.y+(b.y-a.y)*(x-a.x)/(b.x-a.x);top=std::min(top,y);bottom=std::max(bottom,y);
    }
    return {top,bottom};
}
void liquid(Rml::Mesh& mesh,const std::vector<Point>& body,Point center,float radius,float level,float phase,Rml::Colourb base,float opacity) {
    constexpr int columns=28;const float left=center.x-radius,right=center.x+radius,surface=center.y+radius*(1.0f-level*1.82f);
    for(int i=0;i<columns;++i){const float u0=static_cast<float>(i)/columns,u1=static_cast<float>(i+1)/columns,x0=left+(right-left)*u0,x1=left+(right-left)*u1;
        const float w0=std::sin(u0*Tau*1.7f+phase*6.f)*radius*.035f+std::sin(u0*Tau*2.8f-phase*4.f)*radius*.016f;
        const float w1=std::sin(u1*Tau*1.7f+phase*6.f)*radius*.035f+std::sin(u1*Tau*2.8f-phase*4.f)*radius*.016f;
        const auto [top0,bottom0]=verticalBounds(body,x0);const auto [top1,bottom1]=verticalBounds(body,x1);
        if(top0>bottom0||top1>bottom1)continue;
        const float y0=std::clamp(surface+w0,top0,bottom0),y1=std::clamp(surface+w1,top1,bottom1);
        if(y0>=bottom0&&y1>=bottom1)continue;
        const auto colorAt=[&](float u,float depth){auto color=shade(base,-.2f+.24f*u);color=blend(color,{255,255,255,color.alpha},.15f*(1.f-depth));return tint(color,opacity);};
        quad(mesh,{x0,y0},{x1,y1},{x1,bottom1},{x0,bottom0},colorAt(u0,0));
    }
}
void gloss(Rml::Mesh& mesh,Point center,float radius,Color color) {
    disc(mesh,{center.x-radius*.28f,center.y-radius*.52f},radius*.105f,color);
    lineRaw(mesh,{center.x-radius*.55f,center.y-radius*.58f},{center.x-radius*.34f,center.y-radius*.66f},radius*.07f,color);
}
void subBadge(Rml::Mesh& mesh,Point center,float radius,float fillAmount,Rml::Colourb team,float opacity,double time,bool reducedMotion,double reject) {
    disc(mesh,center,radius+2.0f,rgba(7,6,12,220));disc(mesh,center,radius*.91f,rgba(34,30,44,244));
    const Color muted=rgba(255,255,255,45),ready=rgba(255,255,255,230);
    arc(mesh,center,radius*.88f,-Pi*.5f,Tau,2.1f,muted);
    arc(mesh,center,radius*.88f,-Pi*.5f,Tau*fillAmount,2.8f,fillAmount>=.999f?ready:tint(team,opacity));
    const float shake=reject>0&&!reducedMotion?std::sin(static_cast<float>(time*60))*static_cast<float>(reject)*radius*.12f:0.f;
    Point p{center.x+shake,center.y};const float s=radius*.37f;
    if(reject>0){
        disc(mesh,p,s*1.4f,rgba(255,70,88,245));
        lineRaw(mesh,{p.x-s*.75f,p.y-s*.75f},{p.x+s*.75f,p.y+s*.75f},s*.28f,rgba(255,255,255,255));
        lineRaw(mesh,{p.x+s*.75f,p.y-s*.75f},{p.x-s*.75f,p.y+s*.75f},s*.28f,rgba(255,255,255,255));
    }else{
        // The exported sub icon sits over this center; the native outline keeps it legible as a badge.
        disc(mesh,p,s*.78f,tint(team,opacity));
        lineRaw(mesh,{p.x-s*.35f,p.y-s*.22f},{p.x+s*.35f,p.y+s*.22f},s*.16f,rgba(255,255,255,220));
    }
}
}

SpecialGaugeElement::SpecialGaugeElement(const Rml::String& tag):Rml::Element(tag) {}

void SpecialGaugeElement::Reset() {
    frame_=SpecialGaugeFrame{};geometry_=Rml::Geometry{};time_=gainFlash_=readyFlash_=rejectFlash_=previousCharge_=0;
    initialized_=previousReady_=previousRejected_=false;
}

void SpecialGaugeElement::Update(const SpecialGaugeFrame& frame) {
    frame_=frame;
    frame_.charge=std::isfinite(frame.charge)?std::clamp(frame.charge,0.0,1.0):0.0;
    frame_.subFill=std::isfinite(frame.subFill)?std::clamp(frame.subFill,0.0,1.0):0.0;
    frame_.dt=std::isfinite(frame.dt)?std::clamp(frame.dt,0.0,1.0/15.0):0.0;
    if(!std::isfinite(frame.time))frame_.time=0;
    time_=std::max(time_+frame_.dt,frame_.time);
    if(!initialized_){previousCharge_=frame_.charge;previousReady_=frame_.ready;initialized_=true;}
    else if(frame_.charge>previousCharge_+.035&&frame_.charge<.999&&!frame_.reducedMotion)gainFlash_=.32;
    if(frame_.ready&&!previousReady_&&!frame_.reducedMotion)readyFlash_=.7;
    if(frame_.rejected&&!previousRejected_)rejectFlash_=.42;
    gainFlash_=std::max(0.0,gainFlash_-frame_.dt);readyFlash_=std::max(0.0,readyFlash_-frame_.dt);rejectFlash_=std::max(0.0,rejectFlash_-frame_.dt);
    previousCharge_=frame_.charge;previousReady_=frame_.ready;previousRejected_=frame_.rejected;
}

void SpecialGaugeElement::OnRender() {
    const auto size=GetBox().GetSize();if(size.x<=1||size.y<=1||!GetRenderManager())return;
    const float scale=std::min(size.x,size.y),radius=scale*.39f;
    const Point center{size.x*.55f,size.y*.43f};const auto body=outline(center,radius);Rml::Mesh mesh;
    const float motion=frame_.reducedMotion?0.f:static_cast<float>(time_);
    const float readyPulse=frame_.reducedMotion?1.f:.76f+.24f*std::sin(motion*5.f);
    if(frame_.ready){
        const float halo=std::clamp(.28f*readyPulse,0.f,.35f);
        arc(mesh,center,radius*1.16f,-Pi*.5f,Tau,scale*.035f,tint(frame_.color,halo));
    }
    if(frame_.active){
        const int dashes=18;const float offset=frame_.reducedMotion?0.f:-motion*1.55f;
        for(int i=0;i<dashes;++i){const float a=Tau*i/dashes+offset;arc(mesh,center,radius*1.1f,a,Tau/dashes*.58f,scale*.035f,tint(shade(frame_.color,.42f),.95f));}
    }
    fill(mesh,body,rgba(14,11,22,218));
    liquid(mesh,body,center,radius,static_cast<float>(frame_.charge),frame_.reducedMotion?0.f:motion,frame_.color,1.0f);
    gloss(mesh,center,radius,rgba(255,255,255,65));
    for(std::size_t i=0;i<body.size();++i){const auto a=body[i],b=body[(i+1)%body.size()];lineRaw(mesh,a,b,scale*.07f,rgba(5,4,10,230));lineRaw(mesh,a,b,scale*.035f,rgba(255,255,255,240));}
    if(gainFlash_>0){const float t=static_cast<float>(gainFlash_/.32);arc(mesh,center,radius*1.04f,-Pi*.5f,Tau,scale*(.025f+.07f*t),tint(shade(frame_.color,.55f),t));}
    if(readyFlash_>0){const float t=static_cast<float>(1.0-readyFlash_/.7),fade=static_cast<float>(readyFlash_/.7);arc(mesh,center,radius*(1.08f+.65f*t),-Pi*.5f,Tau,scale*.025f,tint(shade(frame_.color,.5f),fade));
        if(!frame_.reducedMotion)for(int i=0;i<12;++i){const float a=Tau*i/12+motion*.12f;const Point a0{center.x+std::cos(a)*radius*1.17f,center.y+std::sin(a)*radius*1.17f},a1{center.x+std::cos(a)*radius*1.38f,center.y+std::sin(a)*radius*1.38f};lineRaw(mesh,a0,a1,scale*.028f,tint(shade(frame_.color,.65f),fade));}}
    const float badgeRadius=radius*.42f;const Point badge{center.x-radius*.65f,center.y+radius*.65f};
    subBadge(mesh,badge,badgeRadius,static_cast<float>(frame_.subFill),frame_.color,1,time_,frame_.reducedMotion,rejectFlash_>0?rejectFlash_/.42:0);
    if(rejectFlash_>0)arc(mesh,center,radius*1.02f,-Pi*.5f,Tau,scale*.055f,tint({255,65,82,255},static_cast<float>(rejectFlash_/.42)*.9f));
    const float opacity=GetComputedValues().opacity();for(auto& vertex:mesh.vertices)vertex.colour=vertex.colour*opacity;
    geometry_=GetRenderManager()->MakeGeometry(std::move(mesh));geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
}

} // namespace restoration
