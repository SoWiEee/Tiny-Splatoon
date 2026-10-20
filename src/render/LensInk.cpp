#include "LensInk.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace restoration {
namespace {constexpr double tau=6.2831853071795864769;}
LensInk::LensInk(std::function<double()> random):random_(std::move(random)){
    active_.reserve(Capacity);free_.reserve(Capacity);for(int i=Capacity-1;i>=0;i--)free_.push_back(&storage_[i]);
}
double LensInk::random(){if(random_)return random_();seed_=seed_*1664525u+1013904223u;return seed_/4294967296.;}
double LensInk::between(double a,double b){return a+random()*(b-a);}
void LensInk::remove(std::size_t i){free_.push_back(active_[i]);active_.erase(active_.begin()+i);}
void LensInk::clear(){while(!active_.empty()){free_.push_back(active_.back());active_.pop_back();}}
LensInk::Particle& LensInk::add(Kind kind,int channel,double x,double y,double r,double rx,double ry,double rotation,double intensity,double life,double stick,double pop){
    if(active_.size()>=Capacity){
        std::size_t k=0;double best=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<active_.size();i++){const auto& p=*active_[i];const double score=p.intensity+(p.kind==Kind::Trail?0:10);if(score<best){best=score;k=i;}}
        remove(k);
    }
    auto* p=free_.back();free_.pop_back();
    // The reference retains mass on pooled objects; only drop recipes assign it.
    const double mass=p->mass;*p=Particle{};p->mass=mass;p->seed=random()*100;
    p->kind=kind;p->channel=channel;p->x=x;p->y=y;p->r=r;p->rx=rx;p->ry=ry;p->rotation=rotation;
    p->initialIntensity=p->intensity=intensity;p->life=life;p->stick=stick;p->pop=pop;p->grow=pop>0?0:1;active_.push_back(p);return *p;
}
void LensInk::droplet(int channel,double x,double y,double r,double slide,double life){auto& p=add(Kind::Drop,channel,x,y,r,r,r,0,1.3,life,slide,.05);p.mass=.3;}
void LensInk::splat(int ch,double cx,double cy,double size,double aspect,int arms,int sats,double life){
    if(!std::isfinite(aspect)||aspect<=0||size<=0||arms<0||sats<0)throw std::invalid_argument("Invalid lens splat");
    const double rotation=random()*tau;
    const auto point=[&](double a,double d){return std::array<double,2>{cx+std::cos(a)*d/aspect,cy+std::sin(a)*d};};
    const double coreLife=life*between(.95,1.15),coreStick=between(.25,.55);
    auto& core=add(Kind::Drop,ch,cx,cy,size*.92,size*.92,size*.92,0,1.4,coreLife,coreStick);core.mass=1;
    const int lumps=3+int(random()*2);
    for(int i=0;i<lumps;i++){
        const double a=rotation+double(i)/lumps*tau+between(-.5,.5),d=size*between(.3,.55);const auto xy=point(a,d);
        const double r=size*between(.5,.72),l=life*between(.75,1),stick=between(.5,1.2);
        add(Kind::Drop,ch,xy[0],xy[1],r,r,r,0,1.25,l,stick).mass=.6;
    }
    for(int i=0;i<arms;i++){
        const double a=rotation+(i+between(-.3,.3))/arms*tau;const bool longArm=random()<.3;
        const double length=size*(longArm?between(1.35,1.8):between(.85,1.2));const auto neck=point(a,length*.5);
        const double rx=size*between(.2,.3)*(longArm?.8:1),l=life*between(.6,.85);
        add(Kind::Arm,ch,neck[0],neck[1],size*.3,rx,length*.5,a-tau/4,1.3,l);
        const auto bulb=point(a,length);const double r=size*(longArm?between(.26,.34):between(.3,.42)),bl=life*between(.7,1),stick=between(.4,1.1);
        add(Kind::Drop,ch,bulb[0],bulb[1],r,r,r,0,1.35,bl,stick).mass=.4;
    }
    for(int i=0;i<sats;i++){
        const double a=random()*tau,d=size*between(1.45,2.5),r=size*between(.07,.16);const auto xy=point(a,d);const bool stretched=random()<.4;const double l=between(.5,1.3);
        add(Kind::Satellite,ch,xy[0],xy[1],r,r*(stretched?.7:1),r*(stretched?1.9:1),a-tau/4,1.3,l,99,.05);
    }
}
void LensInk::update(double dt,double aspect){
    if(!std::isfinite(dt)||dt<0||!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Invalid lens update");
    for(int i=int(active_.size())-1;i>=0;i--){
        auto& p=*active_[i];p.age+=dt;if(p.grow<1)p.grow=std::min(1.,p.grow+dt/std::max(.01,p.pop));
        const double fade=p.age>p.life?1-(p.age-p.life)/(p.kind==Kind::Trail?.9:.55):1;
        p.intensity=p.initialIntensity*std::clamp(p.kind==Kind::Trail?1-p.age/p.life:fade,0.,1.);
        if(p.intensity<=.01){remove(i);continue;}
        if(p.kind==Kind::Drop){
            if(!p.slide&&p.age>p.stick&&p.r>.012)p.slide=true;
            if(p.slide){
                p.hold-=dt;if(p.hold<=0&&random()<dt*.45)p.hold=between(.06,.22);
                const double gravity=p.hold>0?0:1.05*(p.r/.05)*(.55+.45*(p.mass==0?1:p.mass));
                p.vy-=gravity*dt;p.vy*=std::exp(-dt*(p.hold>0?12:2.1));p.vx=std::sin(p.age*2.3+p.seed)*.006+std::sin(p.age*6.1+p.seed*2)*.003;
                const double dx=p.vx*dt,dy=p.vy*dt;p.x+=dx;p.y+=dy;const double speed=-p.vy;p.trail+=std::hypot(dx*aspect,dy);
                if(p.trail>p.r*.55){p.trail=0;add(Kind::Trail,p.channel,p.x,p.y+p.r*.45,p.r*.55,p.r*.42,p.r*.72,0,.5,1.2,99,0);p.r*=.965;}
                p.ry=p.r*(1+std::min(.9,speed*9));p.rx=p.r*(1-std::min(.25,speed*2.5));
                if(p.r<.01){p.slide=false;p.life=std::min(p.life,p.age+.2);}
            }else p.rx=p.ry=p.r;
            if(p.y<-.1){const auto it=std::find(active_.begin(),active_.end(),&p);if(it!=active_.end())remove(std::size_t(it-active_.begin()));}
        }
    }
}
}
