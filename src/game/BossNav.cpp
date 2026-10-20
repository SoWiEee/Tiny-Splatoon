#include "BossNav.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>

namespace restoration {
namespace {
constexpr double step=0.5,planWall=2.9,planFloor=1.8,stride=1.35;
struct Body { double z,r; };
constexpr Body wallBody[]={{2.3,2.5},{-1.8,2.6}};
constexpr Body floorBody[]={{2.2,1.45},{0.0,1.5},{-2.3,1.45}};
class SourceHeap {
public:
    void push(int id,double priority){
        std::size_t i=ids.size();ids.push_back(id);pr.push_back(priority);
        while(i>0){const std::size_t j=(i-1)>>1;if(pr[j]<=priority)break;ids[i]=ids[j];pr[i]=pr[j];i=j;}
        ids[i]=id;pr[i]=priority;
    }
    int pop(){
        const int top=ids.front(),last=ids.back();const double lastPriority=pr.back();ids.pop_back();pr.pop_back();
        if(!ids.empty()){
            std::size_t i=0;const std::size_t n=ids.size();
            for(;;){
                const std::size_t l=i*2+1,r=l+1;std::size_t m=i;double mp=lastPriority;
                if(l<n&&pr[l]<mp){m=l;mp=pr[l];}
                if(r<n&&pr[r]<mp){m=r;mp=pr[r];}
                if(m==i)break;
                ids[i]=ids[m];pr[i]=pr[m];i=m;
            }
            ids[i]=last;pr[i]=lastPriority;
        }
        return top;
    }
    bool empty()const{return ids.empty();}
private:
    std::vector<int> ids;
    std::vector<double> pr;
};
}

BossNav::BossNav(const StageData& stage,const StageGeometry& geometry)
    :stage_(stage),geometry_(geometry) {
    x0_=stage.bounds.x+step*0.5;z0_=stage.bounds.z+step*0.5;
    nx_=static_cast<int>(std::floor((stage.bounds.y-stage.bounds.x)/step));
    nz_=static_cast<int>(std::floor((stage.bounds.w-stage.bounds.z)/step));
    const int count=nx_*nz_;
    floor_.resize(count);kind_.resize(count);plan_.resize(count);
    pads_=stage.spawnPads;
    std::map<int,int> histogram;
    for(int i=0;i<count;++i){
        const auto [x,z]=xz(i);
        const double ground=geometry.groundHeight(x,z,30);
        if(ground>-5)++histogram[static_cast<int>(std::round(ground*4))];
    }
    int frequency=-1;
    for(const auto& [bin,n]:histogram)if(n>frequency){frequency=n;floorY_=bin/4.0;}
    for(int i=0;i<count;++i){
        const auto [x,z]=xz(i);
        const double ground=geometry.groundHeight(x,z,floorY_+stride);
        int kind=0;
        if(!(ground>=floorY_-0.55))kind=2;
        else {
            floor_[i]=static_cast<float>(ground);
            for(double h:{stride+0.1,2.6,3.8,5.2})
                if(geometry.pointInside({x,floorY_+h,z})){kind=1;break;}
        }
        if(kind!=1)for(int t=0;t<2;++t)
            if(std::hypot(x-pads_[t].x,z-pads_[t].z)<stage.spawnBarrier+(t==0?3.5:1.0))kind=3;
        kind_[i]=static_cast<std::uint8_t>(kind);
        if(kind)floor_[i]=static_cast<float>(floorY_);
    }
    wallDistance_=field(false);dropDistance_=field(true);
    std::vector<int> component(count,-1);
    int bestComponent=-1,serial=0;double bestScore=0.0;
    const double span=std::hypot(stage.bounds.y-stage.bounds.x,stage.bounds.w-stage.bounds.z);
    for(int i=0;i<count;++i){
        if(component[i]>=0||wallDistance_[i]<planWall||dropDistance_[i]<planFloor)continue;
        std::vector<int> stack{i};component[i]=serial;
        int size=0;double sx=0,sz=0;
        while(!stack.empty()){
            const int k=stack.back();stack.pop_back();++size;
            const auto [x,z]=xz(k);sx+=x;sz+=z;
            const int ix=k%nx_,iz=k/nx_;
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
                const int jx=ix+dx,jz=iz+dz;
                if(jx<0||jz<0||jx>=nx_||jz>=nz_)continue;
                const int j=jz*nx_+jx;
                if(component[j]<0&&wallDistance_[j]>=planWall&&dropDistance_[j]>=planFloor){component[j]=serial;stack.push_back(j);}
            }
        }
        const double score=size*(0.4+std::hypot(sx/size-pads_[0].x,sz/size-pads_[0].z)/span);
        if(score>bestScore){bestScore=score;bestComponent=serial;}
        ++serial;
    }
    for(int i=0;i<count;++i)if(component[i]==bestComponent&&bestComponent>=0){plan_[i]=1;planIds_.push_back(i);}
}

std::pair<double,double> BossNav::xz(int i) const{return {x0_+(i%nx_)*step,z0_+(i/nx_)*step};}
int BossNav::cell(double x,double z) const{
    const int ix=static_cast<int>(std::round((x-x0_)/step)),iz=static_cast<int>(std::round((z-z0_)/step));
    return ix<0||iz<0||ix>=nx_||iz>=nz_?-1:iz*nx_+ix;
}
double BossNav::fieldAt(const std::vector<float>& values,double x,double z) const{
    const int i=cell(x,z);return i<0?0.0:values[i];
}
double BossNav::wallClear(double x,double z) const{return fieldAt(wallDistance_,x,z);}
double BossNav::floorClear(double x,double z) const{return fieldAt(dropDistance_,x,z);}
int BossNav::kindAt(double x,double z) const{const int i=cell(x,z);return i<0?2:kind_[i];}
bool BossNav::isPlan(double x,double z) const{const int i=cell(x,z);return i>=0&&plan_[i];}
double BossNav::floorAt(double x,double z) const{
    const int i=cell(x,z);
    const double floor=i<0||kind_[i]?floorY_:floor_[i];
    return floor-floorY_<0.45?floor:floorY_;
}
bool BossNav::inPad(double x,double z,double margin) const{
    for(const auto& pad:pads_)if(std::hypot(x-pad.x,z-pad.z)<stage_.spawnBarrier+margin)return true;
    return false;
}
std::vector<float> BossNav::field(bool drop) const{
    struct Offset {int x,z;double distance;};
    std::vector<Offset> offsets;
    for(int dz=-14;dz<=14;++dz)for(int dx=-14;dx<=14;++dx){
        const double d=std::hypot(dx,dz)*step;
        if(d<=7)offsets.push_back({dx,dz,d});
    }
    std::stable_sort(offsets.begin(),offsets.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    std::vector<float> result(kind_.size(),7.0f);
    for(int iz=0;iz<nz_;++iz)for(int ix=0;ix<nx_;++ix){
        const int i=iz*nx_+ix;
        const auto match=[&](int k){return drop?k>=2:k==1;};
        if(match(kind_[i])){result[i]=0;continue;}
        for(const auto& o:offsets){
            const int jx=ix+o.x,jz=iz+o.z;
            const bool outside=jx<0||jz<0||jx>=nx_||jz>=nz_;
            if(outside?drop:match(kind_[jz*nx_+jx])){result[i]=static_cast<float>(std::max(0.0,o.distance-step*0.5));break;}
        }
    }
    return result;
}
bool BossNav::poseOk(double x,double z,double yaw) const{
    const double s=std::sin(yaw),c=std::cos(yaw);
    for(const auto& b:wallBody)if(wallClear(x+s*b.z,z+c*b.z)<b.r)return false;
    for(const auto& b:floorBody)if(floorClear(x+s*b.z,z+c*b.z)<b.r)return false;
    return true;
}
bool BossNav::turnOk(double x,double z,double yaw) const{
    const double s=std::sin(yaw),c=std::cos(yaw);
    for(const auto& b:wallBody)if(wallClear(x+s*b.z,z+c*b.z)<b.r)return false;
    return floorClear(x,z)>=floorBody[1].r;
}
BossNav::Cast BossNav::cast(double x,double z,double yaw,double maximum,double delta) const{
    const double s=std::sin(yaw),c=std::cos(yaw);
    Cast out;
    while(out.dist+delta<=maximum&&poseOk(x+s*(out.dist+delta),z+c*(out.dist+delta),yaw))out.dist+=delta;
    if(out.dist+delta<=maximum){
        const auto& f=wallBody[0];const double fx=x+s*(out.dist+f.z),fz=z+c*(out.dist+f.z);
        for(int k=0;k<=8&&!out.wall;++k){
            const double a=-0.9+k*1.8/8;
            const double px=fx+std::sin(yaw+a)*(f.r+0.6),pz=fz+std::cos(yaw+a)*(f.r+0.6);
            const int i=cell(px,pz);
            if(i>=0&&kind_[i]==1)out.wall=true;
        }
    }
    return out;
}
bool BossNav::cellOk(int i,double slack) const{return i>=0&&wallDistance_[i]>=planWall-slack&&dropDistance_[i]>=planFloor-slack;}
bool BossNav::lineOk(double x0,double z0,double x1,double z1,double slack) const{
    const int count=std::max(1,static_cast<int>(std::ceil(std::hypot(x1-x0,z1-z0)/(step*0.8))));
    for(int k=0;k<=count;++k){const double t=double(k)/count;if(!cellOk(cell(x0+(x1-x0)*t,z0+(z1-z0)*t),slack))return false;}
    return true;
}
int BossNav::nearestPlan(double x,double z) const{
    const int ix=static_cast<int>(std::round((x-x0_)/step)),iz=static_cast<int>(std::round((z-z0_)/step));
    for(int radius=0;radius<60;++radius){
        int best=-1,bd=std::numeric_limits<int>::max();
        for(int dz=-radius;dz<=radius;++dz)for(int dx=-radius;dx<=radius;++dx){
            if(std::max(std::abs(dx),std::abs(dz))!=radius)continue;
            const int jx=ix+dx,jz=iz+dz;if(jx<0||jz<0||jx>=nx_||jz>=nz_)continue;
            const int j=jz*nx_+jx;if(!plan_[j])continue;
            const int d=dx*dx+dz*dz;if(d<bd){bd=d;best=j;}
        }
        if(best>=0)return best;
    }
    return planIds_.empty()?-1:planIds_.front();
}
std::vector<glm::dvec2> BossNav::path(double x0,double z0,double x1,double z1) const{
    const int start=nearestPlan(x0,z0),goal=nearestPlan(x1,z1);
    if(start<0||goal<0)return {};
    const auto [gx,gz]=xz(goal);
    SourceHeap open;
    std::vector<float> cost(plan_.size(),std::numeric_limits<float>::infinity());
    std::vector<int> from(plan_.size(),-1);
    std::vector<std::uint8_t> closed(plan_.size());
    const auto heuristic=[&](int i){const auto [x,z]=xz(i);return std::hypot(x-gx,z-gz);};
    cost[start]=0;open.push(start,heuristic(start));
    int iterations=0;
    while(!open.empty()&&iterations++<40000){
        const int cur=open.pop();
        if(cur==goal)break;
        if(closed[cur])continue;closed[cur]=1;
        const int ix=cur%nx_,iz=cur/nx_;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
            if(!dx&&!dz)continue;
            const int jx=ix+dx,jz=iz+dz;if(jx<0||jz<0||jx>=nx_||jz>=nz_)continue;
            const int j=jz*nx_+jx;if(!plan_[j])continue;
            const double candidate=cost[cur]+std::hypot(dx,dz)*step*(1+std::max(0.0,5.0-wallDistance_[j])*0.1+
                std::max(0.0,3.0-dropDistance_[j])*0.15);
            if(candidate<cost[j]){cost[j]=static_cast<float>(candidate);from[j]=cur;open.push(j,candidate+heuristic(j));}
        }
    }
    if(!std::isfinite(cost[goal]))return {};
    std::vector<int> cells;
    for(int i=goal;i>=0&&cells.size()<8000;i=from[i])cells.push_back(i);
    std::reverse(cells.begin(),cells.end());
    std::vector<glm::dvec2> out;
    out.push_back(glm::dvec2(xz(cells.front()).first,xz(cells.front()).second));
    std::size_t i=0;
    while(i<cells.size()-1){
        std::size_t j=cells.size()-1;
        const auto [ax,az]=xz(cells[i]);
        while(j>i+1){const auto [qx,qz]=xz(cells[j]);if(lineOk(ax,az,qx,qz))break;--j;}
        const auto [qx,qz]=xz(cells[j]);out.push_back({qx,qz});i=j;
    }
    return out;
}
glm::dvec2 BossNav::randomPoint(double value) const{
    if(planIds_.empty())return {0,0};
    const auto [x,z]=xz(planIds_[std::min(planIds_.size()-1,static_cast<std::size_t>(value*planIds_.size()))]);
    return {x,z};
}
} // namespace restoration
