#include "BattleFeedbackElement.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace restoration {
namespace {
using Color=Rml::ColourbPremultiplied;
constexpr float pi=3.14159265358979323846f;

Color paint(Rml::Colourb c,float opacity=1.f) {
    c.alpha=static_cast<Rml::byte>(std::clamp(std::lround(c.alpha*std::clamp(opacity,0.f,1.f)),0l,255l));
    return c.ToPremultiplied();
}
Rml::Colourb mix(Rml::Colourb a,Rml::Colourb b,float t) {
    const auto channel=[t](int x,int y){return static_cast<Rml::byte>(std::clamp(std::lround(x+(y-x)*t),0l,255l));};
    return {channel(a.red,b.red),channel(a.green,b.green),channel(a.blue,b.blue),channel(a.alpha,b.alpha)};
}
void triangle(Rml::Mesh& mesh,Rml::Vector2f a,Rml::Vector2f b,Rml::Vector2f c,Color ca,Color cb,Color cc) {
    const int i=static_cast<int>(mesh.vertices.size());
    mesh.vertices.push_back({a,ca,{0,0}});mesh.vertices.push_back({b,cb,{0,0}});mesh.vertices.push_back({c,cc,{0,0}});
    mesh.indices.insert(mesh.indices.end(),{i,i+1,i+2});
}
void quad(Rml::Mesh& mesh,Rml::Vector2f a,Rml::Vector2f b,Rml::Vector2f c,Rml::Vector2f d,Color color) {
    triangle(mesh,a,b,c,color,color,color);triangle(mesh,a,c,d,color,color,color);
}
void line(Rml::Mesh& mesh,Rml::Vector2f a,Rml::Vector2f b,float width,Color color) {
    const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
    if(length<=1e-4f||width<=0)return;
    const float nx=-dy/length*width*.5f,ny=dx/length*width*.5f;
    quad(mesh,{a.x-nx,a.y-ny},{b.x-nx,b.y-ny},{b.x+nx,b.y+ny},{a.x+nx,a.y+ny},color);
}
void strokeArc(Rml::Mesh& mesh,float cx,float cy,float radius,float width,float start,float end,Color color) {
    if(radius<=0||width<=0||end<=start)return;
    const int segments=std::clamp(static_cast<int>(std::ceil((end-start)*radius/3.5f)),1,256);
    const float inner=std::max(0.f,radius-width*.5f),outer=radius+width*.5f;
    for(int i=0;i<segments;++i){
        const float a=start+(end-start)*i/segments,b=start+(end-start)*(i+1)/segments;
        quad(mesh,{cx+std::cos(a)*outer,cy+std::sin(a)*outer},{cx+std::cos(b)*outer,cy+std::sin(b)*outer},
             {cx+std::cos(b)*inner,cy+std::sin(b)*inner},{cx+std::cos(a)*inner,cy+std::sin(a)*inner},color);
    }
}
void disc(Rml::Mesh& mesh,float cx,float cy,float radius,Color color) {
    if(radius<=0)return;
    constexpr int segments=32;
    for(int i=0;i<segments;++i){
        const float a=2*pi*i/segments,b=2*pi*(i+1)/segments;
        triangle(mesh,{cx,cy},{cx+std::cos(a)*radius,cy+std::sin(a)*radius},{cx+std::cos(b)*radius,cy+std::sin(b)*radius},color,color,color);
    }
}
void directionArc(Rml::Mesh& mesh,float cx,float cy,float angle,float age,Rml::Colourb color) {
    const float t=std::clamp(age/1.6f,0.f,1.f),fade=age<=1.f?1.f:std::clamp((1.6f-age)/.6f,0.f,1.f);
    if(fade<=0)return;
    const float r0=80.f+21.f*t,r1=89.f+21.f*t,half=26.f*pi/180.f;
    const float start=angle-half,end=angle+half;
    const Rml::Colourb dark{12,9,20,190};
    const auto base=mix(color,{255,255,255,color.alpha},.32f);
    // The dark underlay gives the filled arc the same separated edge as the SVG outline.
    strokeArc(mesh,cx,cy,(r0+r1)*.5f,13.f,start-.025f,end+.025f,paint({12,9,20,190},fade));
    strokeArc(mesh,cx,cy,(r0+r1)*.5f,9.f,start,end,paint(base,fade));
    const float tip=r1+12.f,perp=std::sin(.16f)*tip*.48f;
    const Rml::Vector2f forward{std::cos(angle),std::sin(angle)},side{-std::sin(angle),std::cos(angle)};
    const Rml::Vector2f at{cx+forward.x*(r1-1.f),cy+forward.y*(r1-1.f)};
    const Rml::Vector2f point{cx+forward.x*tip,cy+forward.y*tip};
    const Rml::Vector2f left{at.x+side.x*perp,at.y+side.y*perp},right{at.x-side.x*perp,at.y-side.y*perp};
    triangle(mesh,{cx+forward.x*(r1-3),cy+forward.y*(r1-3)},left,point,paint(dark,fade),paint(dark,fade),paint(dark,fade));
    triangle(mesh,{cx+forward.x*(r1-3),cy+forward.y*(r1-3)},point,right,paint(dark,fade),paint(dark,fade),paint(dark,fade));
    triangle(mesh,at,left,point,paint(base,fade),paint(base,fade),paint(base,fade));
    triangle(mesh,at,point,right,paint(base,fade),paint(base,fade),paint(base,fade));
    // A short bright edge keeps the direction cue legible over bright stages.
    strokeArc(mesh,cx,cy,r1,1.2f,start,end,paint({255,255,255,220},fade));
}
} // namespace

BattleFeedbackElement::BattleFeedbackElement(const Rml::String& tag):Rml::Element(tag) {}

void BattleFeedbackElement::Update(const BattleFeedbackFrame& frame) { frame_=frame; }

void BattleFeedbackElement::OnRender() {
    const auto size=GetBox().GetSize();
    if(size.x<=1||size.y<=1||!GetRenderManager())return;
    const float cx=size.x*.5f,cy=size.y*.5f;
    Rml::Mesh mesh;mesh.vertices.reserve(1200);mesh.indices.reserve(1200);

    if(frame_.invuln>0){
        const float spin=frame_.reducedMotion?0.f:static_cast<float>(std::remainder(std::isfinite(frame_.time)?frame_.time:0.0,4.0))*pi*.5f;
        const Color shield=paint(frame_.color,.9f);
        constexpr int dashes=42;
        for(int i=0;i<dashes;++i){
            const float start=spin+2*pi*i/dashes,end=start+2*pi/dashes*.6f;
            strokeArc(mesh,cx,cy,36.f,2.5f,start,end,shield);
        }
    }

    if(std::isfinite(frame_.hitAge)&&frame_.hitAge>=0&&frame_.hitAge<.24){
        const float t=static_cast<float>(frame_.hitAge/.24),fade=1.f-t;
        const float damage=static_cast<float>(frame_.hitDamage>0?frame_.hitDamage:36.0);
        const int chain=std::clamp(frame_.hitChain,0,6);
        const float scale=std::clamp(.8f+damage/90.f,.8f,1.6f)*(1.f+chain*.06f);
        const bool heavy=damage>=60.f;
        const Rml::Colourb hit=mix({255,255,255,255},frame_.color,chain*.11f);
        const float length=(heavy?15.f:12.f)*(1.f+.3f*(1.f-t))*scale;
        const float width=(heavy?5.f:3.5f)*scale;
        const float radius=(10.f+8.f*t+chain*(1.f+t))*scale;
        for(int i=0;i<4;++i){
            const float angle=pi*.25f+i*pi*.5f,dx=std::cos(angle),dy=std::sin(angle);
            const Rml::Vector2f a{cx+dx*radius,cy+dy*radius},b{cx+dx*(radius+length),cy+dy*(radius+length)};
            line(mesh,a,b,width+2.5f,paint({0,0,0,190},fade));line(mesh,a,b,width,paint(hit,fade));
        }
        if(heavy&&t<.22f){
            const float p=t/.22f;
            strokeArc(mesh,cx,cy,16.5f+27.f*p,3.5f,0,2*pi,paint({0,0,0,160},(1.f-p)*.95f));
            strokeArc(mesh,cx,cy,16.5f+27.f*p,2.5f-1.5f*p,0,2*pi,paint({255,255,255,255},(1.f-p)*.95f));
        }
    }

    if(std::isfinite(frame_.killAge)&&frame_.killAge>=0&&frame_.killAge<.55){
        const float t=static_cast<float>(frame_.killAge/.55),fade=std::clamp(1.f-t,0.f,1.f);
        const float radius=9.f+48.f*t,width=4.f-3.f*t;
        strokeArc(mesh,cx,cy,radius,width+4.f,0,2*pi,paint({12,9,20,180},fade));
        strokeArc(mesh,cx,cy,radius,width,0,2*pi,paint(frame_.color,fade));
        const float scale=1.6f-.6f*t,ym=-12.f-16.f*t;
        for(int i=0;i<2;++i){
            const float angle=pi*.25f+i*pi*.5f,dx=std::cos(angle)*10.f*scale,dy=std::sin(angle)*10.f*scale;
            const Rml::Vector2f a{cx-dx,cy+ym-dy},b{cx+dx,cy+ym+dy};
            line(mesh,a,b,8.f*scale,paint({12,9,20,210},fade));line(mesh,a,b,5.f*scale,paint(frame_.color,fade));
        }
    }

    const std::size_t count=std::min<std::size_t>(frame_.directions.size(),6);
    for(std::size_t i=0;i<count;++i){
        const auto& direction=frame_.directions[i];
        if(std::isfinite(direction.angle)&&std::isfinite(direction.age)&&direction.age>=0&&direction.age<1.6)
            directionArc(mesh,cx,cy,static_cast<float>(std::remainder(direction.angle,2*pi)),static_cast<float>(direction.age),direction.color);
    }

    if(mesh.vertices.empty())return;
    const float opacity=GetComputedValues().opacity();
    for(auto& vertex:mesh.vertices)vertex.colour=vertex.colour*opacity;
    geometry_=GetRenderManager()->MakeGeometry(std::move(mesh));
    geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
}

HudRingElement::HudRingElement(const Rml::String& tag):Rml::Element(tag) {}

void HudRingElement::Update(double fraction,Rml::Colourb color,Rml::Colourb background) {
    fraction_=std::isfinite(fraction)?std::clamp(fraction,0.0,1.0):0.0;
    color_=color;background_=background;
}

void HudRingElement::OnRender() {
    const auto size=GetBox().GetSize();
    if(size.x<=1||size.y<=1||!GetRenderManager())return;
    Rml::Mesh mesh;mesh.vertices.reserve(768);mesh.indices.reserve(768);
    const float cx=size.x*.5f,cy=size.y*.5f,radius=std::min(size.x,size.y)*.44f,width=std::min(size.x,size.y)*.1f;
    disc(mesh,cx,cy,std::max(0.f,radius-width*.5f),paint(background_));
    if(background_.alpha){
        const auto track=mix(background_,{255,255,255,background_.alpha},.18f);
        strokeArc(mesh,cx,cy,radius,width,0,2*pi,paint(track));
    }
    if(fraction_>0){
        const float start=-pi*.5f,end=start+2*pi*static_cast<float>(fraction_);
        const Color fg=paint(color_);
        strokeArc(mesh,cx,cy,radius,width,start,end,fg);
        const float cap=width*.5f;
        disc(mesh,cx+std::cos(start)*radius,cy+std::sin(start)*radius,cap,fg);
        disc(mesh,cx+std::cos(end)*radius,cy+std::sin(end)*radius,cap,fg);
    }
    const float opacity=GetComputedValues().opacity();
    for(auto& vertex:mesh.vertices)vertex.colour=vertex.colour*opacity;
    geometry_=GetRenderManager()->MakeGeometry(std::move(mesh));
    geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
}

} // namespace restoration
