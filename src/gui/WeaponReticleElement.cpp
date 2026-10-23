#include "WeaponReticleElement.hpp"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <array>
#include <cmath>
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
void triangle(Rml::Mesh& mesh,Point a,Point b,Point c,Color color) {
    const int i=static_cast<int>(mesh.vertices.size());
    mesh.vertices.push_back({{a.x,a.y},color,{0,0}});
    mesh.vertices.push_back({{b.x,b.y},color,{0,0}});
    mesh.vertices.push_back({{c.x,c.y},color,{0,0}});
    mesh.indices.insert(mesh.indices.end(),{i,i+1,i+2});
}
void quad(Rml::Mesh& mesh,Point a,Point b,Point c,Point d,Color color) {
    triangle(mesh,a,b,c,color);triangle(mesh,a,c,d,color);
}
void lineRaw(Rml::Mesh& mesh,Point a,Point b,float width,Color color) {
    const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
    if(len<1e-4f||width<=0)return;
    const float nx=dy/len*width*.5f,ny=-dx/len*width*.5f;
    quad(mesh,{a.x+nx,a.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{a.x-nx,a.y-ny},color);
}
void line(Rml::Mesh& mesh,Point a,Point b,float width,Color color) {
    const Color outline=tint({0,0,0,255},static_cast<float>(color.alpha)/255.0f*.65f);
    lineRaw(mesh,a,b,width+1.4f,outline);lineRaw(mesh,a,b,width,color);
}
void stroke(Rml::Mesh& mesh,const std::vector<Point>& points,float width,Color color) {
    for(std::size_t i=1;i<points.size();++i)line(mesh,points[i-1],points[i],width,color);
}
std::vector<Point> quadratic(Point a,Point control,Point b,int steps=24) {
    std::vector<Point> result;result.reserve(static_cast<std::size_t>(steps+1));
    for(int i=0;i<=steps;++i){const float t=static_cast<float>(i)/steps,u=1-t;result.push_back({u*u*a.x+2*u*t*control.x+t*t*b.x,u*u*a.y+2*u*t*control.y+t*t*b.y});}
    return result;
}
void strokeFraction(Rml::Mesh& mesh,const std::vector<Point>& points,float width,Color color,float fraction) {
    if(points.size()<2)return;
    fraction=std::clamp(fraction,0.f,1.f);
    float total=0;for(std::size_t i=1;i<points.size();++i)total+=std::hypot(points[i].x-points[i-1].x,points[i].y-points[i-1].y);
    float left=total*fraction;
    for(std::size_t i=1;i<points.size()&&left>0;++i){const auto a=points[i-1],b=points[i];const float length=std::hypot(b.x-a.x,b.y-a.y);if(length<=left){line(mesh,a,b,width,color);left-=length;}else{const float t=left/length;line(mesh,a,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t},width,color);break;}}
}
void arc(Rml::Mesh& mesh,Point center,float rx,float ry,float start,float sweep,float width,Color color) {
    if(rx<=0||ry<=0||std::abs(sweep)<1e-4f)return;
    const int n=std::max(2,static_cast<int>(std::ceil(std::abs(sweep)*std::max(rx,ry)/4.0f)));
    Point previous{center.x+std::cos(start)*rx,center.y+std::sin(start)*ry};
    for(int i=1;i<=n;++i){const float a=start+sweep*static_cast<float>(i)/n;Point next{center.x+std::cos(a)*rx,center.y+std::sin(a)*ry};line(mesh,previous,next,width,color);previous=next;}
}
void circle(Rml::Mesh& mesh,Point center,float radius,float width,Color color) { arc(mesh,center,radius,radius,0,Tau,width,color); }
void dashedCircle(Rml::Mesh& mesh,Point center,float radius,float width,float dash,float gap,float offset,Color color) {
    if(radius<=0||dash<=0||gap<0)return;
    const float dashAngle=dash/radius,gapAngle=gap/radius;
    for(float at=offset;at<Tau;at+=dashAngle+gapAngle) {
        float start=at;while(start<0)start+=Tau;
        arc(mesh,center,radius,radius,start,std::min(dashAngle,Tau-start),width,color);
        if(start+dashAngle>Tau)arc(mesh,center,radius,radius,0,start+dashAngle-Tau,width,color);
    }
}
void discFill(Rml::Mesh& mesh,Point center,float radius,Color color) {
    constexpr int n=16;
    for(int i=0;i<n;++i){const float a=Tau*i/n,b=Tau*(i+1)/n;triangle(mesh,center,{center.x+std::cos(a)*radius,center.y+std::sin(a)*radius},{center.x+std::cos(b)*radius,center.y+std::sin(b)*radius},color);}
}
void disc(Rml::Mesh& mesh,Point center,float radius,Color color) {
    discFill(mesh,center,radius+1.0f,tint({0,0,0,255},static_cast<float>(color.alpha)/255.0f*.65f));discFill(mesh,center,radius,color);
}
void tick(Rml::Mesh& mesh,Point center,float angle,float spread,float width,Color color) {
    const float radius=17+spread;
    const Point a{center.x+std::cos(angle)*(radius-5.5f),center.y+std::sin(angle)*(radius-5.5f)};
    const Point b{center.x+std::cos(angle)*(radius+5.5f),center.y+std::sin(angle)*(radius+5.5f)};
    line(mesh,a,b,width,color);
}
void tickSet(Rml::Mesh& mesh,Point center,float spread,float width,Color color) {
    for(int i=0;i<4;++i)tick(mesh,center,Pi*.5f*i,spread,width,color);
}
void diamond(Rml::Mesh& mesh,Point center,float radius,float width,Color color) {
    const std::array<Point,5> p{{{center.x,center.y-radius},{center.x+radius,center.y},{center.x,center.y+radius},{center.x-radius,center.y},{center.x,center.y-radius}}};
    for(std::size_t i=1;i<p.size();++i)line(mesh,p[i-1],p[i],width,color);
}
enum class Kind { Shooter,Blaster,Roller,Charger,Dualies,Slosher,Splatling,Bow,Mitts,Brolly,Blade };
Kind kindFor(const std::string& weapon) {
    if(weapon=="blaster")return Kind::Blaster;
    if(weapon=="roller"||weapon=="brush")return Kind::Roller;
    if(weapon=="charger")return Kind::Charger;
    if(weapon=="twins")return Kind::Dualies;
    if(weapon=="bucket")return Kind::Slosher;
    if(weapon=="spinner")return Kind::Splatling;
    if(weapon=="bow")return Kind::Bow;
    if(weapon=="mitts")return Kind::Mitts;
    if(weapon=="brolly")return Kind::Brolly;
    if(weapon=="blade")return Kind::Blade;
    return Kind::Shooter;
}
}

WeaponReticleElement::WeaponReticleElement(const Rml::String& tag):Rml::Element(tag) {}

void WeaponReticleElement::Reset() {
    frame_=WeaponReticleFrame{};geometry_=Rml::Geometry{};bloom_=0;previousShots_=0;initialized_=false;
}

void WeaponReticleElement::Update(const WeaponReticleFrame& frame) {
    frame_=frame;
    if(frame_.weapon.empty())frame_.weapon="shooter";
    frame_.spreadPx=std::isfinite(frame.spreadPx)?std::clamp(frame.spreadPx,0.0,90.0):0.0;
    frame_.dt=std::isfinite(frame.dt)?std::clamp(frame.dt,0.0,1.0/15.0):0.0;
    frame_.burstDuration=std::isfinite(frame.burstDuration)?std::clamp(frame.burstDuration,0.01,10.0):1.0;
    frame_.pose.charge=std::isfinite(frame.pose.charge)?std::clamp(frame.pose.charge,0.0,1.0):0.0;
    frame_.pose.canopyOpen=std::isfinite(frame.pose.canopyOpen)?std::clamp(frame.pose.canopyOpen,0.0,1.0):0.0;
    frame_.pose.canopyGrow=std::isfinite(frame.pose.canopyGrow)?std::clamp(frame.pose.canopyGrow,0.0,1.0):0.0;
    frame_.pose.canopyHp=std::isfinite(frame.pose.canopyHp)?std::max(0.0,frame.pose.canopyHp):0.0;
    frame_.pose.canopyHpMax=std::isfinite(frame.pose.canopyHpMax)?std::max(0.0,frame.pose.canopyHpMax):0.0;
    frame_.pose.canopyRegrowK=std::isfinite(frame.pose.canopyRegrowK)?std::clamp(frame.pose.canopyRegrowK,0.0,1.0):0.0;
    bloom_=std::max(0.0,bloom_-frame_.dt*5.0);
    if(!initialized_){previousShots_=frame_.shotsFired;initialized_=true;}
    else if(frame_.shotsFired>previousShots_){
        const auto shots=std::min<std::uint64_t>(frame_.shotsFired-previousShots_,8);
        bloom_=std::min(1.0,bloom_+0.18*static_cast<double>(shots));previousShots_=frame_.shotsFired;
    } else if(frame_.shotsFired<previousShots_)previousShots_=frame_.shotsFired;
}

void WeaponReticleElement::OnRender() {
    const auto size=GetBox().GetSize();
    if(size.x<=1||size.y<=1||!GetRenderManager())return;
    const Point center{size.x*.5f,size.y*.5f};
    const float density=std::max(size.x/240.0f,.01f);
    const Kind kind=kindFor(frame_.weapon);
    const float spread=static_cast<float>(frame_.spreadPx/density),charge=static_cast<float>(frame_.pose.charge);
    const bool full=charge>=.999f,charging=frame_.pose.charging;
    const float scale=1.0f+static_cast<float>(bloom_)*(kind==Kind::Mitts?.24f:.14f);
    float opacity=1.0f;
    if((kind==Kind::Dualies&&frame_.pose.dodgeTime>0)||(kind==Kind::Mitts&&frame_.pose.leaping))opacity=.45f;
    Rml::Mesh mesh;
    const Color white=tint(frame_.color,opacity),ink=tint(frame_.inkColor,opacity),faint=tint(frame_.color,.3f*opacity);
    const Color dark=rgba(0,0,0,150);
    const Color dotColor=white;
    auto dot=[&]{disc(mesh,center,2.5f,dotColor);};
    auto ticks=[&](float amount,float width=3.0f){tickSet(mesh,center,amount,width,white);};

    switch(kind) {
    case Kind::Shooter:
        dot();circle(mesh,center,15.0f,1.8f,tint(frame_.color,.75f*opacity));ticks(spread+static_cast<float>(bloom_)*2.5f);
        break;
    case Kind::Blaster: {
        dot();const float s=1.0f+std::clamp(spread+static_cast<float>(bloom_)*5.0f,0.0f,90.0f)*.012f;
        dashedCircle(mesh,center,23*s,2.6f,19*(Tau*23/100)*s,6*(Tau*23/100)*s,-Tau*9.5f/100,white);
        circle(mesh,center,9*s,1.8f,tint(frame_.color,.75f*opacity));
        break;
    }
    case Kind::Roller: {
        dot();const float y=center.y;
        stroke(mesh,{{center.x-46,y-15},{center.x-56,y-15},{center.x-60,y-11},{center.x-60,y+11},{center.x-56,y+15},{center.x-46,y+15}},2.6f,white);
        stroke(mesh,{{center.x+46,y-15},{center.x+56,y-15},{center.x+60,y-11},{center.x+60,y+11},{center.x+56,y+15},{center.x+46,y+15}},2.6f,white);
        stroke(mesh,quadratic({center.x-30,y+22},{center.x,y+30},{center.x+30,y+22}),1.8f,tint(frame_.color,.75f*opacity));
        break;
    }
    case Kind::Charger: {
        const Color reticle=full?ink:white;
        disc(mesh,center,2.5f,reticle);circle(mesh,center,25,2.0f,tint(full?frame_.inkColor:frame_.color,.45f*opacity));arc(mesh,center,25,25,-Pi*.5f,Tau*charge,4.5f,full?white:ink);
        for(int i=0;i<4;++i){const float a=Pi*.5f*i;line(mesh,{center.x+std::cos(a)*31,center.y+std::sin(a)*31},{center.x+std::cos(a)*36,center.y+std::sin(a)*36},2.2f,full?white:faint);}
        line(mesh,{center.x-52+charge*10,center.y},{center.x-36+charge*10,center.y},2.5f,reticle);
        line(mesh,{center.x+36-charge*10,center.y},{center.x+52-charge*10,center.y},2.5f,reticle);
        line(mesh,{center.x,center.y+34},{center.x,center.y+46},2.5f,reticle);
        break;
    }
    case Kind::Dualies: {
        const bool lock=frame_.pose.turret;const Color reticle=lock?ink:white;disc(mesh,center,2.5f,reticle);const float spreadAmount=std::clamp(spread+static_cast<float>(bloom_)*2.5f,0.0f,90.0f);
        const float gap=lock?6.5f:10.5f;
        circle(mesh,{center.x-gap,center.y},6.2f,1.8f,tint(lock?frame_.inkColor:frame_.color,.75f*opacity));
        circle(mesh,{center.x+gap,center.y},6.2f,1.8f,tint(lock?frame_.inkColor:frame_.color,.75f*opacity));
        tickSet(mesh,center,spreadAmount,3,reticle);
        if(lock)diamond(mesh,center,19,2.4f,ink);
        break;
    }
    case Kind::Slosher: {
        dot();
        stroke(mesh,quadratic({center.x-24,center.y+6},{center.x,center.y-26},{center.x+24,center.y+6}),2.6f,white);
        stroke(mesh,{{center.x-10,center.y+13},{center.x-6,center.y+18},{center.x+6,center.y+18},{center.x+10,center.y+13}},1.8f,tint(frame_.color,.75f*opacity));
        line(mesh,{center.x-24,center.y+6},{center.x-27,center.y+1},1.8f,tint(frame_.color,.75f*opacity));
        line(mesh,{center.x+24,center.y+6},{center.x+27,center.y+1},1.8f,tint(frame_.color,.75f*opacity));
        break;
    }
    case Kind::Splatling: {
        const bool streaming=frame_.pose.streaming;
        const Color reticle=streaming?ink:white;
        disc(mesh,center,2.5f,reticle);
        const float amount=streaming?static_cast<float>(std::clamp(frame_.pose.burstTime/frame_.burstDuration,0.0,1.0)):charge;
        const bool ready=!streaming&&amount>=.999f;
        circle(mesh,center,21,4.5f,tint(streaming?frame_.inkColor:frame_.color,.28f*opacity));
        arc(mesh,center,21,21,-Pi*.5f,Tau*amount,4.5f,(ready||streaming)?ink:tint(frame_.color,opacity));
        for(int i=0;i<8;++i){const float a=Tau*i/8;line(mesh,{center.x+std::cos(a)*17,center.y+std::sin(a)*17},{center.x+std::cos(a)*25,center.y+std::sin(a)*25},1.6f,dark);}
        tick(mesh,center,Pi*.5f,spread+static_cast<float>(bloom_)*2.5f,3,reticle);tick(mesh,center,Pi*1.5f,spread+static_cast<float>(bloom_)*2.5f,3,reticle);
        break;
    }
    case Kind::Bow: {
        const Color reticle=full?ink:white;disc(mesh,center,2.5f,reticle);const float ring1=.45f;
        circle(mesh,center,14.5f,3.4f,tint(full?frame_.inkColor:frame_.color,.3f*opacity));
        circle(mesh,center,22.5f,4.2f,tint(full?frame_.inkColor:frame_.color,.3f*opacity));
        arc(mesh,center,14.5f,14.5f,-Pi*.5f,Tau*std::clamp(charge/ring1,0.0f,1.0f),3.4f,charge>=ring1?ink:white);
        arc(mesh,center,22.5f,22.5f,-Pi*.5f,Tau*std::clamp((charge-ring1)/(1.0f-ring1),0.0f,1.0f),4.2f,full?ink:white);
        const float fan=9.0f-3.0f*charge;
        const Point left=frame_.grounded?Point{center.x-fan,center.y}:Point{center.x,center.y-fan};
        const Point right=frame_.grounded?Point{center.x+fan,center.y}:Point{center.x,center.y+fan};
        disc(mesh,left,2.5f,reticle);disc(mesh,right,2.5f,reticle);
        break;
    }
    case Kind::Mitts: {
        const Color reticle=(full||frame_.clinging)?ink:white;disc(mesh,center,2.5f,reticle);
        const Color leftColor=frame_.clinging?tint((full?frame_.inkColor:frame_.color),.35f*opacity):reticle;
        stroke(mesh,{{center.x+9,center.y-10},{center.x+16,center.y-10},{center.x+19.5f,center.y-6},{center.x+19.5f,center.y+6},{center.x+16,center.y+10},{center.x+9,center.y+10}},2.6f,reticle);
        stroke(mesh,{{center.x-9,center.y-10},{center.x-16,center.y-10},{center.x-19.5f,center.y-6},{center.x-19.5f,center.y+6},{center.x-16,center.y+10},{center.x-9,center.y+10}},2.6f,leftColor);
        line(mesh,{center.x+19.5f,center.y-3},{center.x+22.5f,center.y-3},1.8f,reticle);line(mesh,{center.x+19.5f,center.y+3},{center.x+22.5f,center.y+3},1.8f,reticle);
        line(mesh,{center.x-19.5f,center.y-3},{center.x-22.5f,center.y-3},1.8f,leftColor);line(mesh,{center.x-19.5f,center.y+3},{center.x-22.5f,center.y+3},1.8f,leftColor);
        if(charging){arc(mesh,center,27,27,0,Pi,4,tint(frame_.color,.3f*opacity));
            arc(mesh,center,27,27,0,Pi*.5f*charge,4,full?ink:white);arc(mesh,center,27,27,Pi,-Pi*.5f*charge,4,full?ink:white);}
        break;
    }
    case Kind::Brolly: {
        dot();dashedCircle(mesh,center,7.5f,2.6f,2.4f,3.5f,0,white);
        const float spreadAmount=spread;
        tick(mesh,center,0,spreadAmount*.66f,3,white);tick(mesh,center,Pi*.5f,spreadAmount*.9f,3,white);
        tick(mesh,center,Pi,spreadAmount*.66f,3,white);tick(mesh,center,Pi*1.5f,spreadAmount*.9f,3,white);
        const bool up=frame_.pose.canopyState=="ready";
        const float hp=up&&frame_.pose.canopyHpMax>0?static_cast<float>(frame_.pose.canopyHp/frame_.pose.canopyHpMax):static_cast<float>(frame_.pose.canopyRegrowK);
        const float canopy=std::clamp(hp,0.0f,1.0f);const bool open=up&&frame_.pose.canopyOpen>.5;
        const auto dome=quadratic({center.x-19,center.y+33},{center.x,center.y+15},{center.x+19,center.y+33});
        stroke(mesh,dome,4.2f,tint(frame_.color,(up?0.28f:0.4f)*opacity));
        strokeFraction(mesh,dome,4.2f,open?ink:(canopy<.34f&&up?rgba(255,90,74):white),canopy);
        if(open){const auto launch=quadratic({center.x-15,center.y+25.5f},{center.x,center.y+11},{center.x+15,center.y+25.5f});strokeFraction(mesh,launch,2.4f,ink,static_cast<float>(frame_.pose.canopyOpen));}
        line(mesh,{center.x,center.y+33},{center.x,center.y+37.5f},2.2f,white);
        break;
    }
    case Kind::Blade: {
        const Color reticle=full?ink:white;disc(mesh,center,2.5f,reticle);const float pull=charging?charge*8:0;
        stroke(mesh,quadratic({center.x-26+pull,center.y-17},{center.x-39+pull,center.y},{center.x-26+pull,center.y+17}),2.6f,reticle);
        stroke(mesh,quadratic({center.x+26-pull,center.y-17},{center.x+39-pull,center.y},{center.x+26-pull,center.y+17}),2.6f,reticle);
        stroke(mesh,{{center.x-38,center.y-4},{center.x-44,center.y},{center.x-38,center.y+4}},1.8f,tint(full?frame_.inkColor:frame_.color,.75f*opacity));
        stroke(mesh,{{center.x+38,center.y-4},{center.x+44,center.y},{center.x+38,center.y+4}},1.8f,tint(full?frame_.inkColor:frame_.color,.75f*opacity));
        const auto bar=quadratic({center.x-22,center.y-27},{center.x,center.y-35},{center.x+22,center.y-27});
        if(charging){stroke(mesh,bar,4,tint(full?frame_.inkColor:frame_.color,.35f*opacity));strokeFraction(mesh,bar,4,full?white:ink,charge);}
        break;
    }
    }

    const float elementOpacity=GetComputedValues().opacity(),finalScale=density*scale;
    for(auto& vertex:mesh.vertices){vertex.position.x=center.x+(vertex.position.x-center.x)*finalScale;vertex.position.y=center.y+(vertex.position.y-center.y)*finalScale;vertex.colour=vertex.colour*elementOpacity;}
    geometry_=GetRenderManager()->MakeGeometry(std::move(mesh));
    geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
}

} // namespace restoration
