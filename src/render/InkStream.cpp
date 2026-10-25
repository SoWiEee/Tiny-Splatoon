#include "InkStream.hpp"
#include "StageGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace restoration {
namespace {
constexpr double step=1.0/120,pi=3.141592653589793;
double smooth(double a,double b,double x){const auto t=std::clamp((x-a)/(b-a),0.0,1.0);return t*t*(3-2*t);}
bool finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&glm::length(p)<1e5;}
glm::dvec3 side(glm::dvec3 v){const auto t=glm::length(v)>1e-6?glm::normalize(v):glm::dvec3(0,0,1);return glm::normalize(glm::cross(t,std::abs(t.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0)));}
bool valid(const InkStreamEmitter& e){return e.active&&finite(e.position)&&finite(e.velocity)&&glm::length(e.velocity)<=200&&
    std::isfinite(e.radius)&&e.radius>=.02&&e.radius<=.3&&std::isfinite(e.life)&&e.life>=.1&&e.life<=2&&
    std::isfinite(e.straight)&&e.straight>=0&&e.straight<=2&&std::isfinite(e.gravity)&&e.gravity>=0&&e.gravity<=100&&
    std::isfinite(e.drag)&&e.drag>=0&&e.drag<=10&&std::isfinite(e.seed);}
}
void InkStream::Clear(){time_=accumulator_=0;nextChain_=1;sources_.clear();chains_.clear();pieces_.clear();drops_.clear();splashes_.clear();stats_={};}
void InkStream::Update(double dt,const std::vector<InkStreamEmitter>& emitters,const StageGeometry& geometry){
    splashes_.clear();if(!std::isfinite(dt)||dt<=0)return;
    dt=std::min(dt,.25);accumulator_+=dt;
    std::vector<InkStreamEmitter> before=emitters,input=emitters;
    for(auto& e:before){const auto it=sources_.find(e.id);if(it!=sources_.end()&&it->second.active)e=it->second.previous;}
    double elapsed=0;
    while(accumulator_+1e-10>=step){
        elapsed+=step;const double t=std::min(1.0,elapsed/dt);
        for(std::size_t i=0;i<input.size();i++){
            input[i]=emitters[i];input[i].position=glm::mix(before[i].position,emitters[i].position,t);
            input[i].velocity=glm::mix(before[i].velocity,emitters[i].velocity,t);
        }
        Step(input,geometry);accumulator_-=step;
    }
    Rebuild();
}
void InkStream::Step(const std::vector<InkStreamEmitter>& emitters,const StageGeometry& geometry){
    time_+=step;
    const auto drop=[&](glm::dvec3 p,glm::dvec3 v,double radius,int team){
        if(drops_.size()<512)drops_.push_back({glm::vec3(p),glm::vec3(v),float(radius),0,.28f,team});
    };
    for(auto& d:drops_){d.age+=float(step);const glm::dvec3 old(d.position);d.velocity.y-=float(17*step);
        const auto next=old+glm::dvec3(d.velocity)*step;if(geometry.segment(old,next,true).hit)d.age=d.life;else d.position=glm::vec3(next);}
    drops_.erase(std::remove_if(drops_.begin(),drops_.end(),[](const auto& d){return d.age>=d.life;}),drops_.end());
    for(auto& chain:chains_){
        for(auto& n:chain.nodes){
            if(n.hit)continue;
            const auto old=n.pos;const double age=time_-n.born;
            if(age>n.straight){n.vel.y-=n.gravity*step;n.vel.x*=1-n.drag*step;n.vel.z*=1-n.drag*step;}
            const auto next=n.pos+n.vel*step;const auto hit=geometry.segment(old,next,true);
            if(hit.hit){
                n.hit=true;n.hitAt=time_;n.pos=hit.point+hit.normal*(n.radius*.7);++stats_.impacts;
                auto& source=sources_[chain.emitter];
                if(time_-source.splashAt>=.04){
                    source.splashAt=time_;const double seed=std::fmod(n.sequence*.61803398875,1.0);
                    splashes_.push_back({hit.point,hit.normal,n.radius*1.7,seed,chain.team});
                    const auto lateral=side(hit.normal);
                    for(int sign:{-1,1})drop(n.pos+hit.normal*.02,hit.normal*2.5+lateral*double(sign)*1.5,n.radius*.35,chain.team);
                }
            }else n.pos=next;
        }
        chain.nodes.erase(std::remove_if(chain.nodes.begin(),chain.nodes.end(),[&](const auto& n){return time_-n.born>=n.life||(n.hit&&time_-n.hitAt>=.065);}),chain.nodes.end());
    }
    // A neck loses its cross-section before the chain is split; surviving nodes keep their trajectories.
    const auto count=chains_.size();
    for(std::size_t c=0;c<count&&chains_.size()<128;c++){
        auto& chain=chains_[c];
        for(std::size_t i=1;i+1<chain.nodes.size();i++){
            const auto& n=chain.nodes[i];
            if(n.sequence%n.period!=n.seam||n.hit||time_-n.born<n.closeAge)continue;
            Chain older;older.id=nextChain_++;older.emitter=chain.emitter;older.team=chain.team;
            older.nodes.assign(chain.nodes.begin(),chain.nodes.begin()+i);
            const auto left=older.nodes.back(),right=chain.nodes[i+1];
            const auto lateral=side(n.vel);
            drop(left.pos,left.vel+lateral*(glm::length(left.vel)*.05),left.radius*.7,chain.team);
            drop(right.pos,right.vel-lateral*(glm::length(right.vel)*.05),right.radius*.7,chain.team);
            chain.nodes.erase(chain.nodes.begin(),chain.nodes.begin()+i+1);
            chains_.push_back(std::move(older));++stats_.splits;break;
        }
    }
    std::set<std::uint64_t> active;
    for(const auto& e:emitters){
        if(active.size()>=32)break;if(!valid(e)||active.count(e.id))continue;active.insert(e.id);
        auto& source=sources_[e.id];
        const bool turn=source.active&&(glm::length(e.position-source.previous.position)>1||glm::dot(e.velocity,source.previous.velocity)<.3*glm::length(e.velocity)*glm::length(source.previous.velocity));
        auto chain=std::find_if(chains_.begin(),chains_.end(),[&](const auto& c){return c.id==source.chain;});
        if(!source.active||turn||chain==chains_.end()){
            if(chains_.size()>=128)continue;
            source.chain=nextChain_++;source.sequence=static_cast<std::uint64_t>(std::fmod(std::abs(e.seed),1.0)*4);chains_.push_back({source.chain,e.id,e.team,{}});chain=std::prev(chains_.end());
        }
        if(chain->nodes.size()<256){
            Node node{e.position,e.velocity,time_,e.radius,e.straight,e.gravity,e.drag,e.life,source.sequence++};
            const double speed=std::max(1.0,glm::length(e.velocity));
            node.period=std::clamp(int(std::round(1.2/(speed*step))),5,12);node.seam=std::max(1,node.period-4);
            node.closeAge=std::clamp(2.0/speed,.065,.16);chain->nodes.push_back(node);++stats_.emitted;
            // Sparse lateral spray adds speed cues without reconnecting separate jets.
            if(source.sequence%12==4){const auto lateral=side(e.velocity);const double seed=std::fmod(std::abs(e.seed),1.0);
                for(int sign:{-1,1})drop(e.position+lateral*(sign*e.radius),e.velocity*.9+lateral*(sign*glm::length(e.velocity)*(.05+.025*seed)),e.radius*(.22+.06*seed),e.team);}
        }
        source.previous=e;
    }
    for(auto& entry:sources_)entry.second.active=active.count(entry.first)>0;
    chains_.erase(std::remove_if(chains_.begin(),chains_.end(),[](const auto& c){return c.nodes.empty();}),chains_.end());
    for(auto it=sources_.begin();it!=sources_.end();){const bool live=std::any_of(chains_.begin(),chains_.end(),[&](const auto& c){return c.emitter==it->first;});if(!it->second.active&&!live)it=sources_.erase(it);else ++it;}
}
void InkStream::Rebuild(){
    pieces_.clear();
    for(const auto& chain:chains_){
        InkStreamPiece piece;piece.id=chain.id;piece.team=chain.team;
        for(const auto& n:chain.nodes){
            const double age=time_-n.born;
            double radius=n.radius*(.3+.7*smooth(0,.025,age))*(1-smooth(n.life*.65,n.life,age));
            if(n.hit)radius*=1-smooth(0,.065,time_-n.hitAt);
            const int offset=int(n.sequence%n.period)-n.seam;
            if(std::abs(offset)<=2){const double seamAge=age+offset*step;radius*=1-smooth(n.closeAge*.45,n.closeAge,seamAge)*std::exp(-offset*offset*.8);}
            const double phase=double(n.sequence)*.37+time_*12;
            radius*=1+.08*std::sin(phase);
            const auto p=n.pos+side(n.vel)*(n.radius*.08*std::sin(phase*.7));
            if(!piece.points.empty()&&glm::length(glm::dvec3(piece.points.back().position)-p)<.002){piece.points.back().radius=std::max(piece.points.back().radius,float(radius));continue;}
            piece.points.push_back({glm::vec3(p),float(std::max(.0002,radius))});
        }
        if(piece.points.empty())continue;
        // Bounded Hermite interpolation followed by uniform arc-length samples.
        std::vector<InkStreamPoint> dense{piece.points.front()},resampled{piece.points.front()};
        for(std::size_t i=0;i+1<piece.points.size();i++){
            const auto& a=piece.points[i?i-1:i];const auto& b=piece.points[i];const auto& c=piece.points[i+1];const auto& d=piece.points[std::min(i+2,piece.points.size()-1)];
            const int steps=std::clamp(int(std::ceil(glm::length(c.position-b.position)/.04f)),2,32);
            for(int j=1;j<=steps;j++){const float t=float(j)/steps,t2=t*t,t3=t2*t;
                auto p=(2*t3-3*t2+1)*b.position+(t3-2*t2+t)*.5f*(c.position-a.position)+(-2*t3+3*t2)*c.position+(t3-t2)*.5f*(d.position-b.position);
                p=glm::clamp(p,glm::min(b.position,c.position),glm::max(b.position,c.position));
                dense.push_back({p,b.radius+(c.radius-b.radius)*t});}
        }
        float carry=0;constexpr float spacing=.06f;
        for(std::size_t i=1;i<dense.size()&&resampled.size()<256;i++){
            auto a=dense[i-1];const auto b=dense[i];float length=glm::length(b.position-a.position);
            while(length>1e-6f&&carry+length>=spacing&&resampled.size()<256){const float t=std::clamp((spacing-carry)/length,0.f,1.f);a={glm::mix(a.position,b.position,t),a.radius+(b.radius-a.radius)*t};resampled.push_back(a);carry=0;length=glm::length(b.position-a.position);}
            carry+=length;
        }
        if(resampled.size()<256&&glm::length(resampled.back().position-dense.back().position)>.005f)resampled.push_back(dense.back());
        piece.points=std::move(resampled);pieces_.push_back(std::move(piece));
    }
}
}
