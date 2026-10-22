#include "WorldFxRecipes.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace restoration {
namespace {
constexpr double pi=3.1415926535897932384626433832795, tau=2*pi;
constexpr int F_PAINT=1,F_RING=2,F_NOCOL=4,F_QUIET=8,F_MATTE=16;
float f32(double x){return static_cast<float>(x);}
double rand01(std::uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.0;}
double easeOut(double t){return 1.0-(1.0-t)*(1.0-t)*(1.0-t);}
glm::dvec3 mix(glm::dvec3 a,glm::dvec3 b,double t){return a+(b-a)*t;}
glm::dvec3 normalized(glm::dvec3 n){const double l=glm::length(n);return l>0?n/l:glm::dvec3(0,1,0);}
glm::dvec3 cone(glm::dvec3 n,double maxAngle,std::uint32_t& rng){
    n=normalized(n);const double z=1-rand01(rng)*(1-std::cos(maxAngle));const double r=std::sqrt(std::max(0.0,1-z*z)),phi=rand01(rng)*tau;
    glm::dvec3 a=std::abs(n.y)<0.95?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
    a=normalized(glm::cross(a,n));const glm::dvec3 b=glm::cross(n,a);
    return n*z+a*(r*std::cos(phi))+b*(r*std::sin(phi));
}
glm::dvec3 sphere(std::uint32_t& rng){
    const double z=rand01(rng)*2-1,r=std::sqrt(std::max(0.0,1-z*z)),p=rand01(rng)*tau;
    return {r*std::cos(p),z,r*std::sin(p)};
}
glm::vec3 store(glm::dvec3 v){return {f32(v.x),f32(v.y),f32(v.z)};}
void drop(std::vector<WorldFxParticle>& out,glm::dvec3 p,glm::dvec3 v,glm::dvec3 color,double size,double life,double gravity,double stretch,int flags,std::uint32_t& rng){
    WorldFxParticle x;x.shape=WorldFxShape::Drop;x.pos=store(p);x.vel=store(v);x.color=store(color);x.radius=f32(size);x.life=f32(life);x.gravity=f32(gravity);x.stretch=f32(stretch);x.seed=f32(rand01(rng));x.flags=f32(flags);x.renderRadius=x.radius;out.push_back(x);
}
void sprite(std::vector<WorldFxParticle>& out,WorldFxShape shape,glm::dvec3 p,glm::dvec3 v,glm::dvec3 color,double size0,double size1,double life,double alpha,std::uint32_t& rng,
            double drag=2.2,double buoyancy=0,double kind=0,double fadeIn=0.08,double spin=1.2,double wobble=0,double fadeOut=0){
    WorldFxParticle x;x.shape=shape;x.pos=store(p);x.vel=store(v);x.color=store(color);x.radius=f32(size0);x.radius1=f32(size1);x.life=f32(life);x.alpha=f32(alpha);x.drag=f32(drag);x.buoyancy=f32(buoyancy);x.kind=f32(kind);x.fadeIn=f32(fadeIn);x.fadeOut=f32(fadeOut);x.rotation=f32(rand01(rng)*tau);x.rotationVelocity=f32((rand01(rng)-0.5)*spin);x.seed=f32(rand01(rng));x.renderRadius=x.radius;x.renderAlpha=x.alpha;out.push_back(x);
}
void ring(std::vector<WorldFxParticle>& out,glm::dvec3 p,glm::dvec3 n,glm::dvec3 color,double radius,double life,int style,double alpha,double thickness,std::uint32_t& rng,glm::dvec3 velocity=glm::dvec3(0)){
    WorldFxParticle x;x.shape=WorldFxShape::Ring;x.pos=store(p);x.vel=store(velocity);x.normal=store(normalized(n));x.color=store(color);x.radius=f32(radius);x.life=f32(life);x.style=f32(style);x.alpha=f32(alpha);x.length=f32(thickness);x.seed=f32(rand01(rng));x.expand=f32(style==2||style==6?0.55:style==5?0.35:0.22);x.renderRadius=x.radius;x.renderAlpha=x.alpha;out.push_back(x);
}
void shell(std::vector<WorldFxParticle>& out,glm::dvec3 p,glm::dvec3 color,double r0,double r1,double expand,double life,double alpha,int kind,double vy,double wobble,glm::dvec3 axis,double crown,std::uint32_t& rng){
    WorldFxParticle x;x.shape=WorldFxShape::Shell;x.pos=store(p);x.vel={0,f32(vy),0};x.color=store(color);x.radius=f32(r0);x.radius1=f32(r1);x.expand=f32(expand);x.life=f32(life);x.alpha=f32(alpha);x.kind=f32(kind);x.seed=f32(rand01(rng));x.wobble=f32(wobble);x.normal=store(normalized(axis));x.crown=f32(crown);x.renderRadius=x.radius;x.renderAlpha=x.alpha;out.push_back(x);
}
void beam(std::vector<WorldFxParticle>& out,glm::dvec3 p,glm::dvec3 color,double radius,double height,double life,std::uint32_t& rng){
    WorldFxParticle x;x.shape=WorldFxShape::Beam;x.pos=store(p);x.color=store(color);x.radius=f32(radius);x.length=f32(height);x.life=f32(life);x.seed=f32(rand01(rng));x.renderRadius=x.radius;x.renderLength=0;x.renderAlpha=1;out.push_back(x);
}
void eraseAt(std::vector<WorldFxParticle>& items,std::size_t i,WorldFxShape shape){
    for(std::size_t j=items.size();j>i+1;--j)if(items[j-1].shape==shape){items[i]=items[j-1];items.erase(items.begin()+static_cast<std::ptrdiff_t>(j-1));return;}
    items.erase(items.begin()+static_cast<std::ptrdiff_t>(i));
}
std::size_t countShape(const std::vector<WorldFxParticle>& items,WorldFxShape shape){return std::count_if(items.begin(),items.end(),[&](const auto& x){return x.shape==shape;});}
}

std::vector<WorldFxParticle> BuildWorldFxRecipe(std::string_view name,glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,double radius,std::uint32_t& rng,double waterY,glm::dvec3 camera,const WorldFxRippleSink& ripple){
    std::vector<WorldFxParticle> out;normal=normalized(normal);const glm::dvec3 up(0,1,0),white(1);
    if(name=="footstepOwn"||name=="footstepEnemy"||name=="footstepDry"){
        const double speed=radius,dx=normal.x,dz=normal.z;
        if(name=="footstepDry"){
            if(speed<3.2)return out;
            const glm::dvec3 dust(0.4793201830913402,0.3967552307153359,0.2746773120495699);
            const glm::dvec3 grain(0.48514994004665124,0.4232676699760063,0.33245153633549385);
            const double vx=-dx*0.6+(rand01(rng)-0.5)*0.3,vy=0.3+rand01(rng)*0.2,vz=-dz*0.6+(rand01(rng)-0.5)*0.3;
            const double s0=0.1+rand01(rng)*0.05,s1=0.34+rand01(rng)*0.14,life=0.5+rand01(rng)*0.2;
            sprite(out,WorldFxShape::Puff,{pos.x-dx*0.1,pos.y+0.08,pos.z-dz*0.1},{vx,vy,vz},dust,s0,s1,life,0.42,rng,3.2,0.15,4,0.04,1.2);
            const auto heading=normalized(glm::dvec3(-dx*0.7,0.75,-dz*0.7));
            for(int i=0;i<2;++i){const auto d=cone(heading,0.9,rng);const double sp=1.5*(0.6+rand01(rng)*0.7),size=0.01+rand01(rng)*0.012,l=0.3+rand01(rng)*0.2;drop(out,pos+up*0.03,d*sp,grain,size,l,1.1,1,F_NOCOL|F_MATTE,rng);}
            return out;
        }
        const bool own=name=="footstepOwn";const double k=std::clamp(speed/7.0,0.3,1.2);
        const auto heading=normalized(glm::dvec3(-dx*0.55,1,-dz*0.55));
        const int n=own?std::max(1,int(std::floor(2+2.5*k+0.5))):3;
        for(int i=0;i<n;++i){const auto d=cone(heading,own?0.75:0.6,rng);const double sp=own?1.4+rand01(rng)*1.5*k:0.9+rand01(rng)*0.9;const double size=own?0.018+rand01(rng)*0.022:0.026+rand01(rng)*0.022;drop(out,pos+up*0.03,d*sp,color,size,0.6,own?1:1.3,own?1.2:0.8,0,rng);}
        const auto point=pos+up*0.01;
        const double amp=own?0.0035+0.002*k:0.003,wl=own?0.1:0.08,sp=own?0.9:0.55,life=own?0.55:0.5;
        if(ripple)ripple(point,amp,wl,sp,life);else ring(out,point,up,own?mix(color,white,0.2):color,sp*life*0.8+0.1,life*0.7,1,0.45,1,rng);
        if(!own){
            const auto bubble=mix(color,white,0.35)*1.3;const int count=1+(rand01(rng)<0.5?1:0);
            for(int i=0;i<count;++i){const double a=rand01(rng)*tau,r=0.12+rand01(rng)*0.25,vy=0.22+rand01(rng)*0.2,s0=0.035+rand01(rng)*0.03,s1=0.07+rand01(rng)*0.03,l=0.28+rand01(rng)*0.18;sprite(out,WorldFxShape::Glow,{pos.x+std::cos(a)*r,pos.y+0.04,pos.z+std::sin(a)*r},{0,vy,0},bubble,s0,s1,l,0.85,rng,1,0,21,0.05,0);}
            if(rand01(rng)<0.3){const double ox=(rand01(rng)-0.5)*0.3,oz=(rand01(rng)-0.5)*0.3,vx=(rand01(rng)-0.5)*0.4,vy=0.8+rand01(rng)*0.6,vz=(rand01(rng)-0.5)*0.4,size=0.018+rand01(rng)*0.012;drop(out,pos+glm::dvec3(ox,0.04,oz),{vx,vy,vz},color,size,0.4,1,1,F_QUIET,rng);}
        }
    }else if(name=="formPop"){
        const bool toSquid=radius>0.5;const double y=pos.y+(toSquid?0.35:0.55);
        const auto puffColor=mix(color,white,0.35);
        sprite(out,WorldFxShape::Puff,{pos.x,y,pos.z},{0,toSquid?-0.2:0.6,0},puffColor,0.22,0.62,0.26,0.38,rng,5);
        const auto ringColor=mix(color,white,0.4);
        ring(out,{pos.x,y,pos.z},normalized(camera-glm::dvec3(pos.x,y,pos.z)),ringColor,0.5,0.16,7,0.7,1,rng);
        constexpr int count=5;
        for(int i=0;i<count;i++){
            const double a=(double(i)/count)*tau+rand01(rng)*0.6;
            const double speed=1.4+rand01(rng)*1.2;
            const double vy=1.2+rand01(rng)*1.6;
            const double size=0.022+rand01(rng)*0.02;
            drop(out,{pos.x,y,pos.z},{std::cos(a)*speed,vy,std::sin(a)*speed},color,size,0.6,1,1,0,rng);
        }
    }else if(name=="dive"){
        const double speed=radius;
        const glm::dvec3 p(pos.x,pos.y+0.01,pos.z);
        const glm::dvec3 tangent(0,0,1),bitangent(1,0,0);
        const int count=std::max(1,static_cast<int>(std::floor((10+speed*0.6)+0.5)));
        const double a0=rand01(rng)*tau;
        for(int i=0;i<count;i++){
            const double a=a0+(double(i)/count)*tau+(rand01(rng)-0.5)*0.5;
            const double c=std::cos(a),s=std::sin(a);
            const double upAmount=0.7+rand01(rng)*0.9,outward=0.6+rand01(rng)*0.6;
            const double sp=(2.2+std::min(2.5,speed*0.18))*(0.6+rand01(rng)*0.6);
            const glm::dvec3 d=(tangent*c+bitangent*s)*outward+up*upAmount;
            const double size=0.036*(0.55+rand01(rng)*0.8);
            drop(out,p+up*0.04,d*sp,color,size,1.2,1,1,0,rng);
        }
        if(speed>3)shell(out,{pos.x,pos.y+0.04,pos.z},color,0.12,0.34+std::min(0.25,speed*0.02),0.06,0.2,0.95,0,0,0.2,up,1,rng);
        const auto rippleColor=mix(color,white,0.2);
        if(ripple)ripple(p,0.008+std::min(0.006,speed*0.0006),0.15,1.5,0.85);
        else ring(out,p,up,rippleColor,1.5*0.85*0.8+0.1,0.85*0.7,1,0.45,1,rng);
        const auto bubbleColor=mix(color,white,0.55)*1.1;
        for(int i=0;i<3;i++){
            const double ox=(rand01(rng)-0.5)*0.5,oy=0.05+rand01(rng)*0.05,oz=(rand01(rng)-0.5)*0.5;
            const double vy=0.3+rand01(rng)*0.3;
            const double size0=0.04+rand01(rng)*0.04,size1=0.08+rand01(rng)*0.04;
            const double life=0.3+rand01(rng)*0.25,fadeIn=0.04+rand01(rng)*0.1;
            sprite(out,WorldFxShape::Glow,pos+glm::dvec3(ox,oy,oz),{0,vy,0},bubbleColor,size0,size1,life,0.8,rng,1,0,21,fadeIn,0);
        }
    }else if(name=="emerge"){
        constexpr int count=8;
        for(int i=0;i<count;i++){
            const auto d=cone(up,0.55,rng);
            const double speed=2.5+rand01(rng)*2;
            const double ox=(rand01(rng)-0.5)*0.25,oz=(rand01(rng)-0.5)*0.25;
            const double size=0.022+rand01(rng)*0.026;
            drop(out,{pos.x+ox,pos.y+0.08,pos.z+oz},d*speed,color,size,0.9,1,1.3,0,rng);
        }
        const auto rippleColor=mix(color,white,0.2);
        if(ripple)ripple({pos.x,pos.y+0.01,pos.z},0.007,0.14,1.3,0.75);
        else ring(out,{pos.x,pos.y+0.01,pos.z},up,rippleColor,1.3*0.75*0.8+0.1,0.75*0.7,1,0.45,1,rng);
    }else if(name=="wake"){
        if(glm::dot(pos-camera,pos-camera)>=45.0*45.0)return out;
        const glm::dvec3 dir=normalized(normal),right(dir.z,0,-dir.x);
        const double speed=radius,k=std::min(1.0,speed/11.8);
        const int count=static_cast<int>(std::floor((0.5+3.0*k*k)+rand01(rng)));
        for(int i=0;i<count;i++){
            const double back=speed*(0.06+rand01(rng)*0.1);
            const double upward=1.5+rand01(rng)*1.3+1.5*k;
            const double side=(rand01(rng)-0.5)*(0.7+0.9*k);
            const glm::dvec3 velocity=-dir*back+up*upward+right*side;
            const double size=0.016+rand01(rng)*0.018+0.012*k;
            drop(out,pos-dir*0.34+up*0.05,velocity,color,size,0.6,1,1.7,0,rng);
        }
        if(rand01(rng)<0.28+0.08){
            const double behindX=0.35+rand01(rng)*0.6,jitterX=(rand01(rng)-0.5)*0.3;
            const double behindZ=0.35+rand01(rng)*0.6,jitterZ=(rand01(rng)-0.5)*0.3;
            const double vy=0.22,size0=0.045+rand01(rng)*0.04,size1=0.09;
            const double life=0.3+rand01(rng)*0.15;
            const auto bubbleColor=mix(color,white,0.5)*1.1;
            sprite(out,WorldFxShape::Glow,{pos.x-dir.x*behindX+jitterX,pos.y+0.05,pos.z-dir.z*behindZ+jitterZ},
                {0,vy,0},bubbleColor,size0,size1,life,0.7,rng,1,0,21,0.03,0);
        }
    }else if(name=="burst"){
        constexpr int count=12;const double size=0.1;
        const glm::dvec3 at=pos+normal*0.04;
        for(int i=0;i<count;i++){
            const glm::dvec3 dir=cone(normal,0.9*pi*0.5,rng);const double speed=4*(0.5+rand01(rng)*0.8),sz=size*(0.35+rand01(rng)*0.6);
            drop(out,at,dir*speed,color,sz,1.6+rand01(rng)*0.6,1,1.3,0,rng);
            if(rand01(rng)<0.45){const double s2=speed*(0.7+rand01(rng)*0.5);const double jx=(rand01(rng)-0.5)*0.8,jy=(rand01(rng)-0.5)*0.8,jz=(rand01(rng)-0.5)*0.8;const glm::dvec3 v=dir*s2+glm::dvec3(jx,jy,jz);drop(out,at,v,color,sz*0.4,1.2,1,1.3,0,rng);}
        }
        const double splash=size*(1.6+0.5*std::sqrt(double(count)));
        if(count>=4&&glm::dot(pos-camera,pos-camera)<24.0*24.0){const auto atShell=pos+normal*(splash*0.12);shell(out,atShell,color,splash*0.35,splash*1.1,0.07,0.17+splash*0.12,0.95,0,0,0.2,normal,1,rng);}
    }else if(name=="explosion"){
        const double R=radius,Rv=std::min(R,2.4),spd=std::sqrt(R/3.0);
        sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),color*4.5,Rv*0.4,Rv*0.85,0.12,1,rng,2.2,0,6,0);
        sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),color*1.15,Rv*0.9,Rv*1.6,0.17,0.5,rng,2.2,0,0,0);
        shell(out,pos,color,Rv*0.2,Rv*0.78,0.09,0.34,1,0,0,0.2,up,0,rng);
        for(int i=0;i<26;i++){auto d=sphere(rng);d.y=std::abs(d.y)*0.85+0.12;d=normalized(d);const double speed=(6+rand01(rng)*7)*spd,o=Rv*0.3;const glm::dvec3 p{pos.x+d.x*o,pos.y+d.y*o,pos.z+d.z*o};const double size=0.03+rand01(rng)*0.03;const int flags=rand01(rng)<0.3?F_PAINT:0;drop(out,p,d*speed,color,size,1.6,1,1.5,flags,rng);}
        for(int i=0;i<6;i++){const double a=rand01(rng)*tau,speed=(2.2+rand01(rng)*2.6)*spd,vy=3.2+rand01(rng)*3;const glm::dvec3 v{std::cos(a)*speed,vy,std::sin(a)*speed};const double size=0.07+rand01(rng)*0.035;drop(out,glm::dvec3(pos.x,pos.y+0.2,pos.z),v,color,size,2.2,1,1,F_PAINT,rng);}
        for(int i=0;i<18;i++){auto d=sphere(rng);d.y=std::abs(d.y)*0.7+0.2;d=normalized(d);const double speed=(4+rand01(rng)*6)*spd,size=0.011+rand01(rng)*0.016;drop(out,pos,d*speed,color,size,1.0,1,1.3,0,rng);}
        if(pos.y-(R+1.5)<=waterY&&pos.y>=waterY){const glm::vec3 point(pos.x,f32(waterY+0.01),pos.z);const double v=2.1+0.4*R,life=1.05,ringRadius=v*life*0.8+0.1;ring(out,point,up,color,ringRadius,life*0.7,1,0.45,1,rng);}
        const auto puffColor=mix(color,white,0.42);
        for(int i=0;i<4;i++){auto d=sphere(rng);d.y=std::abs(d.y)*0.5;const double offset=R*(0.2+rand01(rng)*0.4);const glm::dvec3 p=pos+d*offset*glm::dvec3(1,0.6,1);const glm::dvec3 v(d.x*1.8,0.5+rand01(rng)*0.6,d.z*1.8);const double s0=Rv*(0.25+rand01(rng)*0.15),s1=Rv*(0.55+rand01(rng)*0.25),life=0.55+rand01(rng)*0.3;sprite(out,WorldFxShape::Puff,p,v,puffColor,s0,s1,life,0.12,rng,2.8,0.2);}
    }else if(name=="slamLaunch"){
        const glm::dvec3 p(pos.x,pos.y+0.02,pos.z);ring(out,p,up,color,1.5,0.4,0,0.9,1.1,rng);
        const auto rippleColor=mix(color,white,0.2);ring(out,p,up,rippleColor,1.8*0.9*0.8+0.1,0.9*0.7,1,0.45,1,rng);
        for(int i=0;i<18;i++){const glm::dvec3 d=cone(up,0.45,rng);const double sp=5+rand01(rng)*5;const double ox=(rand01(rng)-0.5)*0.4,oz=(rand01(rng)-0.5)*0.4;const glm::dvec3 at(pos.x+ox,pos.y+0.1,pos.z+oz);const double size=0.035+rand01(rng)*0.045;drop(out,at,d*sp,color,size,1.4,1,1.3,0,rng);}
        const auto puffColor=mix(color,white,0.35);
        for(int i=0;i<3;i++){const double vx=(rand01(rng)-0.5)*2,vy=1.5+rand01(rng),vz=(rand01(rng)-0.5)*2;const glm::dvec3 v(vx,vy,vz);sprite(out,WorldFxShape::Puff,glm::dvec3(pos.x,pos.y+0.3,pos.z),v,puffColor,0.3,0.9,0.5,0.32,rng,3);}
    }else if(name=="slamWave"){
        glm::dvec3 center(pos.x,waterY+0.01,pos.z),n=up;const double R=radius;
        const auto dust=mix(glm::dvec3(0.4793201830913402,0.3967552307153359,0.2746773120495699),color,0.25);
        ring(out,center,n,color,R*1.45,0.6,0,0.95,1.4,rng);
        ring(out,center,n,color,3.2*1.3*0.8+0.1,1.3*0.7,1,0.45,1,rng);
        ring(out,center,n,dust,R*1.1,0.7,5,0.55,1,rng);
        const double a0=rand01(rng)*tau;
        for(int i=0;i<14;i++){const double a=a0+(double(i)/14)*tau+(rand01(rng)-0.5)*0.4,sp=6*(0.6+rand01(rng)*0.6);const glm::dvec3 p(center.x+std::cos(a)*0.25,center.y+0.12+rand01(rng)*0.12,center.z+std::sin(a)*0.25);const glm::dvec3 v(std::cos(a)*sp,0.3+rand01(rng)*0.5,std::sin(a)*sp);const double s0=0.16+rand01(rng)*0.1,s1=0.5+rand01(rng)*0.35,life=0.55+rand01(rng)*0.35;sprite(out,WorldFxShape::Puff,p,v,dust,s0,s1,life,0.3,rng,3.2,0.25,4,0.05,1.2);}
        const int count=20;
        for(int i=0;i<count;i++){const double a=(double(i)/count)*tau+rand01(rng)*0.3,sp=5+rand01(rng)*4;const glm::dvec3 p(center.x+std::cos(a)*0.6,center.y+0.3,center.z+std::sin(a)*0.6),v(std::cos(a)*sp,4+rand01(rng)*3,std::sin(a)*sp);const double size=0.1+rand01(rng)*0.06;drop(out,p,v,color,size,2.2,1,1.1,F_PAINT,rng);}
        beam(out,center,color*1.2,1.3,5.5,0.6,rng);
    }else if(name=="stormStart"){
        const auto mist=mix(color,white,0.2);for(int i=0;i<12;i++){const double a=rand01(rng)*tau,r=rand01(rng)*radius*0.6;const glm::dvec3 p(pos.x+std::cos(a)*r,pos.y+(rand01(rng)-0.5)*0.6,pos.z+std::sin(a)*r),v(std::cos(a)*3.4,(rand01(rng)-0.3)*1.5,std::sin(a)*3.4);sprite(out,WorldFxShape::Puff,p,v,mist,radius*0.25,radius*0.6,0.7+rand01(rng)*0.4,0.4,rng,2.5,0,0,0.05,0.4);}
        const glm::dvec3 glow=color*3.0;sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),glow,radius*0.5,radius*1.3,0.25,1,rng,0,0,3,0);
        const glm::dvec3 toCam=normalized(camera-pos);ring(out,pos,toCam,color,radius*1.4,0.3,7,0.8,1.3,rng);
    }else if(name=="rainSheet"){
        const double dt=1.0/60.0;const int count=int(std::floor(radius*radius*3.2*dt+rand01(rng)));
        const auto rain=mix(color,white,0.35);for(int i=0;i<count;i++){const double a=rand01(rng)*tau,r=std::sqrt(rand01(rng))*radius*0.95;const glm::dvec3 p(pos.x+std::cos(a)*r,pos.y-0.4-rand01(rng)*0.5,pos.z+std::sin(a)*r),v(0.35,-19-rand01(rng)*4,0.12);const double size=0.022+rand01(rng)*0.012;drop(out,p,v,rain,size,1,1,5.5,F_QUIET,rng);}
    }else throw std::invalid_argument("Unknown source World FX recipe");
    return out;
}

void UpdateWorldFxRecipe(std::vector<WorldFxParticle>& p,double seconds,std::uint32_t& rng,const WorldFxCollider& collider,float waterY,bool onSpeck,bool paintEffects,const WorldFxSpeckSink& speck,const WorldFxRippleSink& ripple){
    const double dt=std::clamp(seconds,0.0,0.05);if(dt<=0)return;int checks=0;
    // drops run before the other pools; collision callbacks can append rings
    for(std::size_t i=0;i<p.size();){auto& x=p[i];if(x.shape!=WorldFxShape::Drop){++i;continue;}
        x.age=f32(double(x.age)+dt);if(x.age>=x.life){eraseAt(p,i,WorldFxShape::Drop);continue;}
        const double drag=1-0.35*dt;x.vel.x=f32(x.vel.x*drag);x.vel.z=f32(x.vel.z*drag);x.vel.y=f32(x.vel.y*drag-double(17)*x.gravity*dt);
        const glm::vec3 old=x.pos,next{f32(old.x+x.vel.x*dt),f32(old.y+x.vel.y*dt),f32(old.z+x.vel.z*dt)};x.pos=next;
        if(!(int(x.flags)&F_NOCOL)&&collider&&checks<1100){++checks;WorldFxHit hit;if(collider(old,next,hit)){
            const auto landed=x;
            if(!(int(landed.flags)&F_MATTE)){
                if(speck&&!(int(landed.flags)&F_PAINT)&&landed.radius>0.012)speck(hit,landed.color,landed.radius);
                if(ripple&&!(int(landed.flags)&F_PAINT)&&((int(landed.flags)&F_RING)||landed.radius>0.03))ripple(hit.point,0.0016+landed.radius*0.05,0.07+landed.radius*0.5,0.75,0.42);
                if(!onSpeck&&!speck&&!(int(landed.flags)&F_PAINT)&&!(int(landed.flags)&F_QUIET))ring(p,hit.point,hit.normal,landed.color,landed.radius*2.3+0.03,0.2+landed.radius*0.6,2,0.95,1,rng);
                if(int(landed.flags)&F_QUIET){eraseAt(p,i,WorldFxShape::Drop);continue;}
                if(landed.radius>0.075&&countShape(p,WorldFxShape::Drop)<2592&&rand01(rng)<0.6){const int n=1+(rand01(rng)<0.5?1:0);for(int k=0;k<n;k++){const glm::vec3 d=cone(hit.normal,1.15,rng);const double sp=1.3+rand01(rng)*1.9;drop(p,{f32(hit.point.x+hit.normal.x*0.03),f32(hit.point.y+hit.normal.y*0.03),f32(hit.point.z+hit.normal.z*0.03)},{f32(d.x*sp),f32(d.y*sp),f32(d.z*sp)},landed.color,landed.radius*0.3,0.45,1,1,F_NOCOL,rng);}}
            }
            eraseAt(p,i,WorldFxShape::Drop);continue;
        }}else if(int(x.flags)&F_NOCOL){ /* the source advances last-check position only for non-colliding drops */ }
        if(old.y>=waterY&&next.y<waterY){eraseAt(p,i,WorldFxShape::Drop);continue;}++i;
    }
    for(auto shape:{WorldFxShape::Puff,WorldFxShape::Glow})for(std::size_t i=0;i<p.size();){auto& x=p[i];if(x.shape!=shape){++i;continue;}
        x.age=f32(double(x.age)+dt);if(x.age>=x.life){eraseAt(p,i,shape);continue;}
        const double drag=std::max(0.0,1-double(x.drag)*dt);x.vel.x=f32(x.vel.x*drag);x.vel.y=f32(x.vel.y*drag+double(x.buoyancy)*dt);x.vel.z=f32(x.vel.z*drag);
        if(x.wobble>0){const double ph=x.age*2.1+double(x.seed)*40;x.vel.x=f32(x.vel.x+std::cos(ph)*x.wobble*dt*2.4);x.vel.z=f32(x.vel.z+std::sin(ph*0.83)*x.wobble*dt*2.4);}
        x.pos={f32(x.pos.x+x.vel.x*dt),f32(x.pos.y+x.vel.y*dt),f32(x.pos.z+x.vel.z*dt)};x.rotation=f32(x.rotation+x.rotationVelocity*dt);++i;
    }
    for(std::size_t i=0;i<p.size();){auto& x=p[i];if(x.shape!=WorldFxShape::Ring){++i;continue;}
        x.age=f32(double(x.age)+dt);if(x.age>=x.life){eraseAt(p,i,WorldFxShape::Ring);continue;}
        if(x.vel.x!=0||x.vel.y!=0||x.vel.z!=0){const double k=std::max(0.0,1-1.6*dt);x.pos={f32(x.pos.x+x.vel.x*dt),f32(x.pos.y+x.vel.y*dt),f32(x.pos.z+x.vel.z*dt)};x.vel={f32(x.vel.x*k),f32(x.vel.y*k),f32(x.vel.z*k)};}++i;
    }
    for(std::size_t i=0;i<p.size();){auto& x=p[i];if(x.shape!=WorldFxShape::Shell){++i;continue;}
        x.age=f32(double(x.age)+dt);if(x.age>=x.life){eraseAt(p,i,WorldFxShape::Shell);continue;}x.pos.y=f32(x.pos.y+x.vel.y*dt);++i;
    }
    for(std::size_t i=0;i<p.size();){auto& x=p[i];if(x.shape!=WorldFxShape::Beam){++i;continue;}
        x.age=f32(double(x.age)+dt);if(x.age>=x.life){eraseAt(p,i,WorldFxShape::Beam);continue;}++i;
    }
    // Values submitted to the source instanced meshes after the source update.
    for(auto& x:p){const double t=x.life>0?double(x.age)/x.life:0;
        if(x.shape==WorldFxShape::Drop){const double speed=std::sqrt(double(x.vel.x)*x.vel.x+double(x.vel.y)*x.vel.y+double(x.vel.z)*x.vel.z);double stretch=1+std::min(speed*0.062*double(x.stretch),2.3*double(x.stretch));stretch*=1+0.14*std::sin(double(x.age)*38+double(x.seed)*20)*std::min(1.0,double(x.age)*6);x.renderRadius=f32(double(x.radius)*std::min(1.0,double(x.age)*22+0.35)*std::min(1.0,(double(x.life)-x.age)/0.12));x.renderLength=f32(std::max(1.0,stretch));}
        else if(x.shape==WorldFxShape::Puff||x.shape==WorldFxShape::Glow){x.renderRadius=f32(double(x.radius)+(double(x.radius1)-x.radius)*easeOut(t));const double fi=x.fadeIn>0?std::min(1.0,double(x.age)/x.fadeIn):1,fo=x.fadeOut>0?std::min(1.0,(double(x.life)-x.age)/x.fadeOut):(x.shape==WorldFxShape::Glow?1-t:1-t*t);x.renderAlpha=f32(double(x.alpha)*fi*fo);}
        else if(x.shape==WorldFxShape::Ring){const double expand=double(x.expand);x.renderRadius=f32(double(x.radius)*(expand+(1.0-expand)*easeOut(t)));}
        else if(x.shape==WorldFxShape::Shell){const double te=std::min(1.0,double(x.age)/x.expand);x.renderRadius=f32(double(x.radius)+(double(x.radius1)-x.radius)*easeOut(te));x.renderAlpha=x.alpha;x.renderLength=f32(double(x.wobble)*(1-te*0.6));}
        else if(x.shape==WorldFxShape::Beam){x.renderLength=f32(double(x.length)*easeOut(std::min(1.0,t*3.2)));x.renderRadius=f32(double(x.radius)*(0.55+0.6*easeOut(std::min(1.0,t*4)))*(1-0.5*t));x.renderAlpha=f32(std::pow(1-t,1.6)*std::min(1.0,t*25));}
    }
}
}
