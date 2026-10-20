#pragma once
#include <array>
#include <functional>
#include <vector>

namespace restoration {
class LensInk {
public:
    enum class Kind { Drop,Arm,Satellite,Trail };
    struct Particle {
        Kind kind=Kind::Drop;int channel=0;
        double x=0,y=0,r=0,rx=0,ry=0,rotation=0,intensity=0,initialIntensity=0,life=0,stick=99,pop=.09,grow=0;
        double vx=0,vy=0,age=0,trail=0,hold=0,seed=0,mass=0;bool slide=false;
    };
    static constexpr int Capacity=220;
    explicit LensInk(std::function<double()> random={});
    void clear();
    void splat(int channel,double x,double y,double size,double aspect,int arms=6,int satellites=6,double life=2.1);
    void droplet(int channel,double x,double y,double radius,double slide=.25,double life=1.1);
    void update(double dt,double aspect);
    const std::vector<Particle*>& particles()const{return active_;}
private:
    double random(),between(double a,double b);
    Particle& add(Kind kind,int channel,double x,double y,double r,double rx,double ry,double rotation,double intensity,double life,double stick=99,double pop=.09);
    void remove(std::size_t index);
    std::array<Particle,Capacity> storage_;
    std::vector<Particle*> active_,free_;
    std::function<double()> random_;
    unsigned seed_=0x19a016u;
};
}
