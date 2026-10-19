#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include "NavGraph.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi=3.14159265358979323846;
glm::dvec3 heldBrollyNormal(const PlayerSimulation& actor) {
    const double pitch=std::clamp(actor.input.aimPitch,-0.5,0.45),cp=std::cos(pitch);
    return {std::sin(actor.input.aimYaw)*cp,std::sin(pitch),std::cos(actor.input.aimYaw)*cp};
}
glm::dvec3 heldBrollyCenter(const PlayerSimulation& actor,const glm::dvec3& normal) {
    return actor.state.pos+glm::dvec3(0,actor.state.smoothY+0.9,0)+normal*0.62;
}
bool segmentShieldHit(glm::dvec3 from,glm::dvec3 to,glm::dvec3 prevC,glm::dvec3 prevN,
                      glm::dvec3 center,glm::dvec3 normal,double radius,glm::dvec3& at) {
    const double a=glm::dot(from-prevC,prevN),b=glm::dot(to-center,normal);
    if((a>0)==(b>0) || std::abs(a-b)<1e-10)return false;
    const double t=a/(a-b);at=from+(to-from)*t;
    return glm::length(at-center)<radius;
}
}
CombatWorld::CombatWorld(const StageData& stage,const StageGeometry& geometry,FacePaint& paint)
    :stage_(stage),geometry_(geometry),paint_(paint),specials_(*this) {}
void CombatWorld::addActor(PlayerSimulation& actor) {
    if(std::find(actors_.begin(),actors_.end(),&actor)==actors_.end()) actors_.push_back(&actor);
}
void CombatWorld::cleanup(PlayerSimulation& actor) {
    specials_.cleanup(actor);
    for (auto& p : projectiles_) {
        if (p.owner == &actor) p.owner = nullptr;
        else p.tagged.erase(std::remove(p.tagged.begin(),p.tagged.end(),&actor),p.tagged.end());
    }
    for (auto& p : pendingProjectiles_) {
        if (p.owner == &actor) p.owner = nullptr;
        else p.tagged.erase(std::remove(p.tagged.begin(),p.tagged.end(),&actor),p.tagged.end());
    }
    for (auto& d : devices_) {
        if (d.owner == &actor) {
            d.owner = d.target = nullptr;
            d.tagged.clear();
            d.live = false;
        } else {
            if (d.target == &actor) d.target = nullptr;
            d.tagged.erase(std::remove(d.tagged.begin(), d.tagged.end(), &actor), d.tagged.end());
        }
    }
    if (!ticking_) {
        projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const auto& p){return !p.owner;}), projectiles_.end());
        devices_.erase(std::remove_if(devices_.begin(), devices_.end(), [](const auto& d){return !d.live;}), devices_.end());
    }
    volleyHits_.erase(std::remove_if(volleyHits_.begin(),volleyHits_.end(),[&](const auto& h){return h.victim==&actor;}),volleyHits_.end());
}
void CombatWorld::removeActor(PlayerSimulation& actor) {
    cleanup(actor);
    if (ticking_) {
        for (auto*& registered : actors_) if (registered == &actor) registered = nullptr;
    } else {
        actors_.erase(std::remove(actors_.begin(),actors_.end(),&actor),actors_.end());
    }
}
void CombatWorld::clear() {
    specials_.clear();
    pendingProjectiles_.clear();
    if (ticking_) {
        for (auto& p : projectiles_) p.owner = nullptr;
        for (auto& d : devices_) { d.owner = d.target = nullptr; d.tagged.clear(); d.live = false; }
        volleyHits_.clear();
    } else {
        projectiles_.clear(); devices_.clear(); volleyHits_.clear();
    }
    time_ = 0;
}
void CombatWorld::launch(ShooterProjectile p) {
    if(!p.owner || !p.owner->state.alive) return;
    if(!p.id)p.id=allocateId();
    (ticking_ ? pendingProjectiles_ : projectiles_).push_back(std::move(p));
}
void CombatWorld::launchCanopy(PlayerSimulation& owner,double hp,glm::dvec3 direction,double heldHeight) {
    if(!owner.state.alive)return;
    if(glm::length(direction)<1e-8)direction={0,0,1};else direction=glm::normalize(direction);
    const auto floor=geometry_.raycast(owner.state.pos+direction*0.9+glm::dvec3(0,1.2,0),{0,-1,0},3.5,true);
    CombatDevice d;d.id=allocateId();d.owner=&owner;d.kind="brolly";d.team=owner.team();d.shield=true;d.live=true;d.state="launched";
    d.pos=floor.hit?floor.point:owner.state.pos+direction*0.9;d.N=direction;d.vel=direction;d.hp=hp;d.hpMax=owner.weapon().number("canopyHp");
    const double theta=52.0*pi/180.0,depth=0.7/std::sin(theta)*(1.0-std::cos(theta));
    d.radius=0.8;d.halfW=0.8;d.planeOff=-depth*0.5;d.speed=owner.weapon().number("launchSpeed");d.left=owner.weapon().number("launchLife");
    d.h0=owner.state.pos.y+heldHeight-d.pos.y;d.age=0;d.C=d.pos+direction*d.planeOff+glm::dvec3(0,0.76,0);
    d.previousC=d.C;d.previousN=d.N;d.hasPreviousShield=true;
    devices_.push_back(std::move(d));
}
bool CombatWorld::canDeploy(const PlayerSimulation& owner,const std::string& id) const {
    if(!owner.state.alive) return false;
    if(id==owner.subId()) {
        GetEquipment(EquipmentCategory::Sub,id);
        if (id=="torpedo" || id=="boomerang")
            return std::none_of(devices_.begin(),devices_.end(),[&](const auto& d){return d.live && d.owner==&owner && d.kind==id;});
        return true;
    }
    if(id==owner.specialId()) {
        GetEquipment(EquipmentCategory::Special,id);
        return owner.state.specialCharge>=owner.weapon().number("specialCost",190);
    }
    return false;
}
void CombatWorld::deploy(PlayerSimulation& owner,const std::string& id,bool special,double charge) {
    if(!canDeploy(owner,id)) return;
    const auto& def=GetEquipment(special?EquipmentCategory::Special:EquipmentCategory::Sub,id);
    if (special) throw std::logic_error("special behavior has not been ported: "+id);
    CombatDevice d; d.id=allocateId();d.owner=&owner;d.kind=id;d.team=owner.team();d.special=false;
    d.pos=owner.state.pos+glm::dvec3(0,special?0.0:1.35,0);d.vel=owner.aimDir()*def.number("throwSpeed",0.0);
    d.hp=def.number("hp",0.0);d.radius=def.number("radius",0.0);d.timer=def.number("fuse",def.number("life",def.number("duration",-1.0)));
    d.shootable=d.hp>0;d.blocksActors=id=="curtain";d.uses=static_cast<int>(def.number("uses",0));d.state="fly";
    if(def.flag("placed",false)) {
        const auto hit=geometry_.raycast(owner.state.pos+glm::dvec3(0,0.4,0),{0,-1,0},3.0,true);
        if(!hit.hit || (hit.block>=0 && (stage_.blocks[hit.block].roof || stage_.blocks[hit.block].rail || stage_.blocks[hit.block].perch))) return;
        const auto count=std::count_if(devices_.begin(),devices_.end(),[&](const auto& x){return x.live && x.owner==&owner && x.kind==id;});
        const auto max=static_cast<std::ptrdiff_t>(def.number("max",1000));
        if(count>=max) {
            auto old=std::find_if(devices_.begin(),devices_.end(),[&](const auto& x){return x.live && x.owner==&owner && x.kind==id;});
            if(old!=devices_.end()) {
                if(id=="mine") explodeDevice(*old);
                else old->live=false;
            }
        }
        d.pos=hit.point;d.normal=hit.normal;d.grounded=true;d.state=id;d.timer=-1;d.C=hit.point;d.N=hit.normal;
        if(id=="curtain") { const auto facing=owner.aimDir(); d.N=glm::normalize(glm::dvec3(facing.x,0,facing.z)); }
        if(id=="mine") { d.timer=def.number("armTime");d.fuse=-1;paint(owner,d.pos+glm::dvec3(0,0.2,0),1.1,owner.random(),{},d.special); }
    } else {
        auto dir=owner.aimDir();
        if(owner.input.hasAimPoint) { const auto to=owner.input.aimPoint-d.pos; if(glm::length(to)>1.0 && glm::dot(to,dir)>=0) dir=glm::normalize(to); }
        if(id=="tracer") {
            const double yaw=std::atan2(dir.x,dir.z);
            const double pitch=std::clamp(std::asin(std::clamp(dir.y,-1.0,1.0))-def.number("pitchDown")*3.14159265358979323846/180.0,-1.5,1.45);
            dir={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
            d.pos=owner.state.pos+glm::dvec3(std::sin(yaw)*0.3,owner.state.smoothY+1.2,std::cos(yaw)*0.3);
            const auto start=owner.state.pos+glm::dvec3(0,owner.state.smoothY+1.0,0);
            if(!geometry_.los(start,d.pos)) d.pos=start;
        }
        if(id=="boomerang") dir.y=0.0;
        if(glm::length(dir)>1e-6) dir=glm::normalize(dir);
        if(id=="curtain") d.N=glm::normalize(glm::dvec3(dir.x,0,dir.z));
        d.vel=dir*(id=="tracer"?def.number("speed",40.0):def.number("throwSpeed",14.0));
        d.timer=def.number("life",def.number("flyLife",-1.0));
        if(id=="seeker") d.state="fly";
        if(id=="shaker") { d.state="fly";d.uses=charge>=1.0?3:charge>=0.5?2:1;d.heading=std::atan2(dir.x,dir.z); }
    }
    if(id=="curtain") { d.blocksActors=true;d.R=def.number("height");d.halfW=def.number("width")*0.5; }
    if(id=="sprinkler" || id=="beacon" || id=="torpedo" || id=="waddle") d.shootable=d.hp>0;
    if(id=="torpedo") {
        d.senseRadius=def.number("lockRange");d.lockRange=d.senseRadius;d.N=owner.aimDir();
        d.speed=def.number("launchSpeed");d.trigger=0.3+0.38;
    }
    if(id=="waddle") { d.senseRadius=def.number("senseRadius");d.trigger=def.number("triggerDist");d.speed=def.number("speed");d.aimY=0.11*1.8;d.left=def.number("life"); }
    if(id=="seeker") {
        d.senseRadius=def.number("seekRange");d.trigger=def.number("triggerDist");d.speed=def.number("speed");
        d.commitDist=def.number("commitDist");d.creep=def.number("creep");
    }
    if(id=="tracer") { d.left=def.number("range");d.uses=0;d.radius=def.number("size");d.trailPoints.emplace_back(d.pos,time_); }
    if(id=="boomerang") { d.state="fly";d.left=def.number("range");d.speed=def.number("range")/def.number("outTime");d.timer=def.number("outTime");d.radius=def.number("hoverRadius"); }
    if(id=="mist" || id=="scan") d.fuse=def.number("fuse");
    if(id=="sticky") d.fuse=def.number("fuse");
    if(id=="bomb") d.fuse=-1;
    if(id=="burst") d.timer=-1;
    devices_.push_back(std::move(d));
}
void CombatWorld::blockActor(PlayerSimulation& actor) {
    for(const auto& d:devices_) {
        if(d.kind=="brolly" && d.live && d.team!=actor.team()) {
            const auto offset=actor.state.pos-d.C;
            const auto tangent=glm::dvec3(-d.N.z,0,d.N.x);
            const double along=glm::dot(offset,tangent),across=glm::dot(offset,d.N),r=0.38+0.18;
            if(std::abs(along)>d.halfW+0.38*0.6 || actor.state.pos.y-d.pos.y < -1.2 || actor.state.pos.y-d.pos.y > 1.6 || std::abs(across)>=r)continue;
            const double side=across>=0?1.0:-1.0;
            actor.state.pos+=d.N*(side*r-across);
            const double vn=glm::dot(actor.state.vel,d.N);if(vn*side<0)actor.state.vel-=d.N*vn;
            continue;
        }
        if(d.kind!="curtain" || d.team==actor.team() || d.state!="curtain") continue;
        const auto offset=actor.state.pos-d.pos;
        const glm::dvec3 tangent(-d.N.z,0,d.N.x);
        const double along=glm::dot(offset,tangent),across=glm::dot(offset,d.N),r=0.38+0.12;
        if(std::abs(along)>d.halfW+0.38 || offset.y<-1.2 || offset.y>d.R || std::abs(across)>r) continue;
        const double side=across>=0?1.0:-1.0;
        actor.state.pos+=d.N*(side*r-across);
        const double vn=glm::dot(actor.state.vel,d.N);
        if(vn*side<0) actor.state.vel-=d.N*vn;
    }
}
bool CombatWorld::blockShot(glm::dvec3 from,glm::dvec3 to,int team,double damage,PlayerSimulation* owner) {
    if(blockCanopyShot(from,to,team,damage))return true;
    for(auto& d:devices_) {
        if(!d.live || d.exploding || d.team==team) continue;
        if(d.kind=="curtain" && d.state=="curtain") {
            const double a=glm::dot(from-d.pos,d.N),b=glm::dot(to-d.pos,d.N);
            if((a>0)==(b>0) || std::abs(a-b)<1e-8) continue;
            const auto at=from+(to-from)*(a/(a-b))-d.pos;
            if(std::abs(glm::dot(at,glm::dvec3(-d.N.z,0,d.N.x)))>d.halfW || at.y<0 || at.y>d.R) continue;
            d.hp-=damage*GetEquipment(EquipmentCategory::Sub,"curtain").number("shotMul");
            if(d.hp<=0) d.live=false;
            return true;
        }
        if(d.kind=="torpedo") {
            const auto segment=to-from;const double len2=glm::dot(segment,segment);
            const double t=len2>1e-9?std::clamp(glm::dot(d.pos-from,segment)/len2,0.0,1.0):0.0;
            if(glm::length(d.pos-(from+segment*t))>0.3) continue;
            d.hp-=damage;if(d.hp<=0)d.live=false;return true;
        }
        if(d.kind=="waddle") {
            if(d.state=="fly") continue;
            const auto hit=StageGeometry::segmentCapsuleDist(from,to,d.pos,0.17,0.4);
            if(hit.dist>=0.2) continue;
            d.hp-=damage;if(d.hp<=0)d.live=false;
            return true;
        }
        if(!d.shootable) continue;
        const auto center=d.pos+glm::dvec3(0,d.kind=="beacon"?0.45:0.35,0);
        const auto hit=StageGeometry::segmentCapsuleDist(from,to,center,0.35,std::max(0.5,d.radius));
        if(hit.dist>=0.38) continue;
        d.hp-=damage;
        if(d.hp<=0) d.live=false;
        return true;
    }
    return owner && specials_.shotHit(from,to,team,damage,owner);
}
bool CombatWorld::blockCanopyShot(glm::dvec3 from,glm::dvec3 to,int team,double damage,const ShooterProjectile* projectile,double* hitDistance) {
    for(const auto& s:shields()) {
        if(!s.live || s.team==team)continue;
        glm::dvec3 prevC=s.C,prevN=s.N;
        if(s.held && s.owner) {
            const auto& k=s.owner->runner_.canopy_;
            if(k.previousShield){prevC=k.previousCenter;prevN=k.previousNormal;}
        } else {
            const auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.id==s.id;});
            if(it!=devices_.end() && it->hasPreviousShield){prevC=it->previousC;prevN=it->previousN;}
        }
        glm::dvec3 at;
        if(!segmentShieldHit(from,to,prevC,prevN,s.C,s.N,s.R,at))continue;
        if(hitDistance)*hitDistance=glm::length(at-from);
        double applied=damage>0?damage:10.0;
        if(projectile) {
            if(projectile->weaponId=="brolly") {
                const auto& w=GetEquipment(EquipmentCategory::Main,"brolly");
                applied=w.number("pelletDamage")+(w.number("pelletDamageFar")-w.number("pelletDamage"))*std::clamp((glm::length(at-projectile->start)-w.number("falloffStart"))/(w.number("falloffEnd")-w.number("falloffStart")),0.0,1.0);
            } else if(projectile->kind=="drop") {
                const double base=projectile->damage>0.0?projectile->damage:10.0;
                const double far=projectile->damageFar>0.0?projectile->damageFar:base;
                const double t=std::clamp(glm::length(at-projectile->start)/7.0,0.0,1.0);
                applied=(base+(far-base)*t)*0.25;
            }
            else if(projectile->kind=="slosh")applied*=0.5;
        }
        if(s.held && s.owner) {
            auto& k=s.owner->runner_.canopy_;k.hp=std::max(0.0,k.hp-applied);k.lastHit=time_;
            k.flash=std::min(1.0,k.flash+0.3+applied/140.0);k.shake=std::min(1.0,k.shake+0.25+applied/120.0);
            if(k.hp<=0) {k.state=CanopyState::Broken;k.regrowTime=GetEquipment(EquipmentCategory::Main,"brolly").number("regrowTime");k.regrowK=0;k.open=0;k.openTime=0;k.grow=0;k.lockRelease=true;k.previousShield=false;}
        } else {
            auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.id==s.id;});
            if(it!=devices_.end()) {it->hp-=applied;if(it->hp<=0)it->live=false;}
        }
        return true;
    }
    return false;
}
double CombatWorld::blockRay(glm::dvec3 from,glm::dvec3 dir,double length,int team,double damage,PlayerSimulation* owner) {
    const auto to=from+dir*length;
    double shieldDistance=length;
    blockCanopyShot(from,to,team,damage,nullptr,&shieldDistance);
    double best=shieldDistance; CombatDevice* hit=nullptr;
    for(auto& d:devices_) {
        if(!d.live || d.exploding || d.team==team) continue;
        if(d.kind=="torpedo") {
            const auto rel=d.pos-from;const double along=glm::dot(rel,dir);if(along<=0 || along>best+0.3)continue;
            const double perpendicular=glm::dot(rel,rel)-along*along;if(perpendicular>0.09)continue;
            const double t=std::max(0.05,along-std::sqrt(std::max(0.0,0.09-perpendicular)));
            if(t<best) {best=t;hit=&d;}
            continue;
        }
        if(d.kind=="waddle") {
            if(d.state=="fly") continue;
            const auto center=d.pos+glm::dvec3(0,0.2,0),rel=center-from;
            const double along=glm::dot(rel,dir);
            if(along<=0 || along>=best) continue;
            const auto closest=from+dir*along;
            if(glm::length(closest-center)<0.24) {best=along;hit=&d;}
            continue;
        }
        if(d.kind!="curtain" || d.state!="curtain") continue;
        const double denom=glm::dot(dir,d.N);if(std::abs(denom)<1e-6) continue;
        const double t=glm::dot(d.pos-from,d.N)/denom;if(t<=0 || t>=best) continue;
        const auto p=from+dir*t-d.pos;
        if(std::abs(glm::dot(p,glm::dvec3(-d.N.z,0,d.N.x)))>d.halfW || p.y<0 || p.y>d.R) continue;
        best=t;hit=&d;
    }
    if(hit) {
        const double applied=hit->kind=="curtain"?damage*GetEquipment(EquipmentCategory::Sub,"curtain").number("shotMul"):damage;
        hit->hp-=applied;
        if(hit->hp<=0) hit->live=false;
    }
    return owner ? std::min(best,specials_.rayHit(from,dir,best,team,damage,owner)) : best;
}
void CombatWorld::damageDevices(glm::dvec3 at,double radius,double damage,int team) {
    for(auto& d:devices_) {
        if(!d.live || d.exploding || d.team==team) continue;
        if(d.kind=="torpedo") {
            if(glm::length(d.pos-at)>radius+0.3) continue;
            if(d.locked && damage>=50) { explodeDevice(d); continue; }
            d.hp-=damage;if(d.hp<=0)d.live=false;
        } else if(d.kind=="waddle") {
            if(d.state=="fly" || glm::length(d.pos-at)>=radius+0.3) continue;
            d.hp-=damage;if(d.hp<=0)d.live=false;
        } else if((d.kind=="curtain" || d.kind=="sprinkler" || d.kind=="beacon") && d.shootable && glm::length(d.pos-at)<radius+0.4) {
            d.hp-=damage;if(d.hp<=0)d.live=false;
        }
    }
}
void CombatWorld::guardActorsWithCanopies(glm::dvec3 at,double radius,int team) {
    const auto active=shields();
    for(auto* actor:actors_) {
        if(!actor || actor->team()==team || !actor->state.alive)continue;
        const auto target=actor->visualPos()+glm::dvec3(0,actor->state.squid?0.3:0.7,0);
        if(glm::length(target-at)>radius+0.6)continue;
        for(const auto& shield:active) {
            if(shield.team!=actor->team())continue;
            auto previousC=shield.C,previousN=shield.N;
            if(shield.held && shield.owner && shield.owner->runner_.canopy_.previousShield) {
                previousC=shield.owner->runner_.canopy_.previousCenter;
                previousN=shield.owner->runner_.canopy_.previousNormal;
            } else if(!shield.held) {
                const auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.id==shield.id;});
                if(it!=devices_.end() && it->hasPreviousShield){previousC=it->previousC;previousN=it->previousN;}
            }
            glm::dvec3 hit;
            if(segmentShieldHit(at,target,previousC,previousN,shield.C,shield.N,shield.R,hit)) {
                actor->state.invuln=std::max(actor->state.invuln,1e-4);
                break;
            }
        }
    }
}
void CombatWorld::damageCanopies(glm::dvec3 at,double radius,int team) {
    const auto active=shields();
    const auto& weapon=GetEquipment(EquipmentCategory::Main,"brolly");
    for(const auto& shield:active) {
        if(shield.team==team)continue;
        const double d=std::max(0.0,glm::length(shield.C-at)-shield.R*0.5);
        if(d>radius)continue;
        const double k=1.0-std::clamp((d-0.8)/std::max(0.1,radius-0.8),0.0,1.0);
        const double damage=35.0+(180.0-35.0)*k*k;
        if(shield.held && shield.owner) {
            auto& canopy=shield.owner->runner_.canopy_;
            canopy.hp=std::max(0.0,canopy.hp-damage);canopy.lastHit=time_;
            canopy.flash=std::min(1.0,canopy.flash+0.3+damage/140.0);canopy.shake=std::min(1.0,canopy.shake+0.25+damage/120.0);
            if(canopy.hp<=0.0) {
                canopy.state=CanopyState::Broken;canopy.regrowTime=weapon.number("regrowTime");canopy.regrowK=0;
                canopy.open=0;canopy.openTime=0;canopy.grow=0;canopy.lockRelease=true;canopy.previousShield=false;
            }
        } else {
            const auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& device){return device.id==shield.id;});
            if(it!=devices_.end()) {it->hp-=damage;if(it->hp<=0)it->live=false;}
        }
    }
}
bool CombatWorld::beaconAnchor(int team,std::uint64_t id,glm::dvec3& destination) const {
    const auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.live && d.kind=="beacon" && d.team==team && (id==0 || d.id==id) && d.uses>0;});
    if(it==devices_.end()) return false;
    destination=it->pos;return true;
}
bool CombatWorld::consumeBeacon(PlayerSimulation& actor,std::uint64_t id) {
    auto it=std::find_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.live && d.kind=="beacon" && d.team==actor.team() && d.id==id && d.uses>0;});
    if(it==devices_.end()) return false;
    if(--it->uses<=0 || actor.subId()=="beacon") it->live=false;
    return true;
}
bool CombatWorld::applyHit(PlayerSimulation& attacker,PlayerSimulation& victim,double damage,const std::string& source) {
    if(attacker.team()==victim.team() || !victim.state.alive) return false;
    return victim.damageFromCombat(damage,&attacker,source);
}
double CombatWorld::paint(PlayerSimulation& owner,glm::dvec3 at,double radius,double seed,const SplatOptions& options,bool special) {
    const double area=paint_.splat(at,radius,owner.team(),seed,options);
    owner.turfPainted_+=area;
    if(!special && !owner.combatPose().special) owner.state.specialCharge=std::min(owner.weapon().number("specialCost",190),owner.state.specialCharge+area*GetEquipment(EquipmentCategory::Special,owner.specialId()).number("chargeRate",0.8));
    return area;
}
void CombatWorld::paintDevice(CombatDevice& device,glm::dvec3 at,double radius) {
    if(!device.owner || radius<=0) return;
    const auto& sub=GetEquipment(EquipmentCategory::Sub,device.kind);
    if(device.kind=="torpedo" && !device.locked) radius=sub.number("fallbackPaint");
    paint(*device.owner,at,radius,device.owner->random(),{},device.special);
    int satellites=4;
    double minRadius=0.55,maxRadius=1.0,minSize=0.5,maxSize=0.9;
    if(device.kind=="torpedo") {
        satellites=device.locked?5:0;
        minSize=0.55;maxSize=1.0;
    }
    for(int i=0;i<satellites;++i) {
        const double angle=device.owner->random()*2*3.14159265358979323846;
        const double r=radius*(minRadius+(maxRadius-minRadius)*device.owner->random());
        const double size=minSize+(maxSize-minSize)*device.owner->random();
        paint(*device.owner,at+glm::dvec3(std::cos(angle)*r,0.3,std::sin(angle)*r),size,device.owner->random(),{},device.special);
    }
}
void CombatWorld::explodeDevice(CombatDevice& d,PlayerSimulation* direct) {
    if(!d.live || d.exploding || !d.owner) { d.live=false;return; }
    d.exploding=true;
    const auto& sub=GetEquipment(EquipmentCategory::Sub,d.kind);
    if(d.kind=="scan" || d.kind=="mist" || d.kind=="curtain" || d.kind=="sprinkler" || d.kind=="beacon") { d.live=false;return; }
    if(d.kind=="waddle") {
        const auto center=d.pos+glm::dvec3(0,0.25,0);
        guardActorsWithCanopies(center,sub.number("radius"),d.team);
        damageCanopies(center,sub.number("radius"),d.team);
        paint(*d.owner,d.pos+glm::dvec3(0,0.2,0),sub.number("paintRadius"),d.owner->random(),{},d.special);
        for(int k=0;k<4;++k) {
            const double angle=d.owner->random()*2*3.14159265358979323846;
            const double radius=sub.number("paintRadius")*(0.55+0.45*d.owner->random());
            const double size=0.5+0.4*d.owner->random();
            paint(*d.owner,center+glm::dvec3(std::cos(angle)*radius,0.3,std::sin(angle)*radius),size,d.owner->random(),{},d.special);
        }
        for(auto* e:actors_) {
            if(!e || e->team()==d.team || !e->state.alive) continue;
            const auto target=e->state.pos+glm::dvec3(0,e->state.squid?0.3:0.7,0);
            const double distance=glm::length(target-center);
            if(distance>sub.number("radius") || !geometry_.los(center+glm::dvec3(0,0.3,0),target)) continue;
            const double k=1-std::clamp((distance-0.8)/(sub.number("radius")-0.8),0.0,1.0);
            applyHit(*d.owner,*e,sub.number("damageMin")+(sub.number("damageMax")-sub.number("damageMin"))*k*k,d.kind);
        }
        damageDevices(center,sub.number("radius"),60,d.team);
        specials_.areaHit(center,sub.number("radius"),sub.number("damageMax"),d.team,d.owner);
        d.live=false;d.exploding=false;return;
    }
    if(d.kind=="burst") {
        guardActorsWithCanopies(d.pos,sub.number("radius"),d.team);
        damageCanopies(d.pos,sub.number("radius"),d.team);
        paintDevice(d,d.pos+glm::dvec3(0,0.2,0),sub.number("paintRadius"));
        if(direct) applyHit(*d.owner,*direct,sub.number("directDamage"),d.kind);
        for(auto* e:actors_) {
            if(!e || e==direct || e->team()==d.team || !e->state.alive) continue;
            const auto target=e->state.pos+glm::dvec3(0,0.7,0);const double dist=glm::length(target-d.pos);
            if(dist>sub.number("radius") || !geometry_.los(d.pos+glm::dvec3(0,0.3,0),target)) continue;
            applyHit(*d.owner,*e,sub.number("splashDamage"),d.kind);
        }
        damageDevices(d.pos,sub.number("radius"),25,d.team);
        specials_.areaHit(d.pos,sub.number("radius"),sub.number("splashDamage"),d.team,d.owner);
        d.live=false;d.exploding=false;return;
    }
    if(d.kind=="mine") {
        const auto center=d.pos+glm::dvec3(0,0.3,0);
        guardActorsWithCanopies(center,sub.number("radius"),d.team);
        damageCanopies(center,sub.number("radius"),d.team);
        paintDevice(d,center,sub.number("paintRadius"));
        for(auto* e:actors_) {
            if(!e || e->team()==d.team || !e->state.alive) continue;
            const auto target=e->state.pos+glm::dvec3(0,0.7,0);
            if(glm::length(target-center)>sub.number("radius") || !geometry_.los(center,target)) continue;
            applyHit(*d.owner,*e,sub.number("damage"),d.kind);
            e->state.track=std::max(e->state.track,sub.number("trackTime"));e->state.trackTeam=d.team;
        }
        damageDevices(center,sub.number("radius"),30,d.team);
        specials_.areaHit(center,sub.number("radius"),sub.number("damage"),d.team,d.owner);
        d.live=false;d.exploding=false;return;
    }
    const double radius=sub.number("radius",2.4),paintRadius=sub.number("paintRadius",radius*0.75);
    const double maxDamage=sub.number("damageMax",sub.number("damage",180)),minDamage=sub.number("damageMin",maxDamage);
    const auto normal=d.kind=="sticky"?d.normal:glm::dvec3(0,1,0);
    const auto center=d.pos+normal*0.2;
    guardActorsWithCanopies(center,radius,d.team);
    damageCanopies(center,radius,d.team);
    const auto floor=geometry_.raycast(center,{0,-1,0},3.5,true);
    auto paintAt=(d.kind=="sticky" || d.kind=="seeker")?center:(floor.hit?floor.point+floor.normal*0.1:center);
    paintDevice(d,paintAt,paintRadius);
    for(auto* e:actors_) {
        if(!e || e->team()==d.team || !e->state.alive) continue;
        const auto target=e->state.pos+glm::dvec3(0,e->state.squid?0.3:0.7,0);const double dist=e==direct?0.0:glm::length(target-center);
        if(dist>radius || !geometry_.los(center+normal*0.1,target)) continue;
        if(d.kind=="torpedo") {
            const double core=sub.number("coreRadius");
            const double damage=dist<=core?maxDamage:maxDamage+(minDamage-maxDamage)*std::clamp((dist-core)/(radius-core),0.0,1.0);
            applyHit(*d.owner,*e,damage,d.kind);
        } else {
            const double k=radius>0.8?1-std::clamp((dist-0.8)/(radius-0.8),0.0,1.0):1.0;
            applyHit(*d.owner,*e,minDamage+(maxDamage-minDamage)*k*k,d.kind);
        }
    }
    const double deviceDamage=d.kind=="shaker"?40.0:d.kind=="boomerang"&&!d.locked?35.0:60.0;
    damageDevices(center,radius,deviceDamage,d.team);
    specials_.areaHit(center,radius,minDamage,d.team,d.owner);
    if(d.kind=="torpedo" && d.locked) {
        const auto volley=newVolley();
        for(int k=0;k<static_cast<int>(sub.number("drops"));++k) {
            const double angle=d.owner->random()*2*3.14159265358979323846+2*3.14159265358979323846*k/sub.number("drops");
            const double horizontal=3.2+d.owner->random()*3.4,vertical=3.2+d.owner->random()*3.2;
            ShooterProjectile p;p.owner=d.owner;p.weaponId="torpedo";p.kind="drop";p.pos=p.start=d.pos+glm::dvec3(std::cos(angle)*0.22,0.15,std::sin(angle)*0.22);
            p.vel={std::cos(angle)*horizontal,vertical,std::sin(angle)*horizontal};p.life=2.4;p.straight=0;p.gravity=21;p.drag=0.8;
            p.radius=sub.number("dropPaint");p.damage=sub.number("dropDamage");p.damageFar=p.damage;p.size=0.1;p.seed=d.owner->random();p.volley=volley;p.victimHitLimit=static_cast<int>(sub.number("dropHits"));p.special=d.special;launch(std::move(p));
        }
    }
    if(d.kind=="shaker" && d.uses>1) {
        --d.uses;d.state="fly";d.age=0;d.timer=sub.number("gap");
        const glm::dvec3 forward(std::sin(d.heading),0,std::cos(d.heading));
        d.vel=forward*sub.number("hopSpeed")+glm::dvec3(0,sub.number("hopUp"),0);
        d.exploding=false;return;
    }
    d.live=false;d.exploding=false;
}
void CombatWorld::burst(PlayerSimulation& owner,glm::dvec3 at,double radius,double maxDamage,double minDamage,double paintRadius,const std::string& id,PlayerSimulation* direct,bool special) {
    guardActorsWithCanopies(at,radius,owner.team());
    damageCanopies(at,radius,owner.team());
    const auto floor=geometry_.raycast(at+glm::dvec3(0,0.2,0),{0,-1,0},3.5);
    if(floor.hit) paint(owner,floor.point+floor.normal*0.1,paintRadius,owner.random(),{},special);
    for(auto* e:actors_) {
        if (!e) continue;
        if(e==direct || e->team()==owner.team() || !e->state.alive) continue;
        const auto target=e->state.pos+glm::dvec3(0,0.7,0); const double d=glm::length(target-at);
        if(d<=radius && geometry_.los(at,target)) applyHit(owner,*e,maxDamage+(minDamage-maxDamage)*d/radius,id);
    }
    damageDevices(at,radius,maxDamage,owner.team());
    specials_.areaHit(at,radius,minDamage,owner.team(),&owner);
}
void CombatWorld::beam(PlayerSimulation& owner,glm::dvec3 from,glm::dvec3 dir,double length,double radius,double damage,double paintRadius,bool pierce,const std::string& id) {
    const double blocked=blockRay(from,dir,length,owner.team(),damage,&owner);
    const auto hit=geometry_.raycast(from,dir,blocked,true); length=hit.hit?hit.dist:blocked;
    const auto end=from+dir*length;
    for(auto* e:actors_) {
        if (!e) continue;
        if(e->team()==owner.team() || !e->state.alive) continue;
        const auto h=StageGeometry::segmentCapsuleDist(from,end,e->visualPos(),e->hitRadius(),e->hitHeight());
        if(h.dist<e->hitRadius()+radius) { applyHit(owner,*e,damage,id); if(!pierce) {length*=h.t;break;} }
    }
    for(double d=0;d<length;d+=0.55) {
        const auto floor=geometry_.raycast(from+dir*d,{0,-1,0},4,true);
        if(floor.hit) paint(owner,floor.point+floor.normal*0.1,paintRadius,owner.random());
    }
    if(hit.hit) paint(owner,hit.point+hit.normal*0.1,paintRadius,owner.random());
}
void CombatWorld::updateCanopies(double dt) {
    for(auto& d:devices_) {
        if(!d.live || d.kind!="brolly")continue;
        if(d.hasPreviousShield) {d.previousC=d.C;d.previousN=d.N;}
        d.age+=dt;d.left=std::max(0.0,d.hpMax>0?d.owner->weapon().number("launchLife")-d.age:0.0);
        if(d.age>=d.owner->weapon().number("launchLife")) {d.live=false;continue;}
        const double speed=d.speed*std::min(1.0,0.35+d.age*3.0),step=speed*dt;
        bool blocked=false;
        for(double h:{0.3,1.0})if(geometry_.raycast(d.pos+glm::dvec3(0,h,0),d.N,0.12+step,true).hit){blocked=true;break;}
        if(!blocked) {
            d.pos+=d.N*step;
            const auto ground=geometry_.raycast(d.pos+glm::dvec3(0,0.6,0),{0,-1,0},1.5,true);
            if(ground.hit && ground.point.y>d.pos.y-0.12-std::max(0.0,-d.vel.y)*dt-0.02 && d.vel.y<=0.01) {d.pos.y=ground.point.y;d.vel.y=0;}
            else {d.vel.y-=24*dt;d.pos.y+=d.vel.y*dt;if(ground.hit && d.pos.y<ground.point.y){d.pos.y=ground.point.y;d.vel.y=0;}}
            if(d.pos.y<PlayerTuning().number("waterY")-0.4){d.live=false;d.hp=0;continue;}
            d.paintAcc+=step;
            if(d.paintAcc>=0.32 && d.vel.y==0) {
                d.paintAcc=0;SplatOptions options;options.kind=SplatKind::Roll;options.stretch=d.N;options.hasStretch=true;
                paint(*d.owner,d.pos+glm::dvec3(0,0.3,0),d.owner->weapon().number("launchPaint"),d.owner->random(),options,d.special);
            }
        }
        const double h=0.76+(d.h0-0.76)*std::max(0.0,1.0-d.age/0.22);
        d.C=d.pos+glm::dvec3(0,h,0)+d.N*d.planeOff;d.R=0.8;d.halfW=0.8;
        d.hasPreviousShield=true;
    }
    for(auto* actor:actors_) {
        if(!actor || actor->mainId()!="brolly")continue;
        auto& k=actor->runner_.canopy_;
        if(!k.id)continue;
        if(k.state==CanopyState::Ready && k.open>=0.55 && k.grow>=0.9 && actor->state.alive && !actor->state.squid) {
            const auto normal=heldBrollyNormal(*actor),center=heldBrollyCenter(*actor,normal);
            if(!k.previousShield){k.previousCenter=center;k.previousNormal=normal;k.previousShield=true;}
        } else k.previousShield=false;
    }
}
void CombatWorld::tick(double dt) {
    if(!(dt>0) || !std::isfinite(dt)) throw std::invalid_argument("combat tick requires positive finite dt");
    time_+=dt;ticking_=true;
    updateCanopies(dt);
    for(auto& p:projectiles_) {
        if(!p.owner) continue;
        if(p.kind=="arrow") {tickArrow(p,dt);continue;}
        if(p.kind=="bladewave") {tickBladeWave(p,dt);continue;}
        if(p.kind=="fist") {tickFist(p,dt);continue;}
        auto& owner=*p.owner;
        p.age+=dt;
        if(p.age<p.delay)continue;
        const auto prev=p.pos;
        if(p.age>p.straight) {p.vel.y-=p.gravity*dt;p.vel*=1-p.drag*dt;}
        p.pos+=p.vel*dt;
        bool done=blockCanopyShot(prev,p.pos,owner.team(),p.damage,&p);
        if(!done) done=specials_.shotHit(prev,p.pos,owner.team(),p.damage,&owner);
        for(auto* e:actors_) {
            if(done)break;
            if (!e) continue;
            if(e->team()==owner.team() || !e->state.alive) continue;
            const auto h=StageGeometry::segmentCapsuleDist(prev,p.pos,e->visualPos(),e->hitRadius(),e->hitHeight());
            if(h.dist>=e->hitRadius()*0.95+p.size) continue;
            if(p.volley && std::count_if(volleyHits_.begin(),volleyHits_.end(),[&](const auto& v){return v.volley==p.volley && v.victim==e;})>=p.victimHitLimit) continue;
            if(p.volley) volleyHits_.push_back({p.volley,e});
            const auto at=prev+(p.pos-prev)*h.t;
            double dmg=p.damage;
            if(p.kind=="drop") dmg+=(p.damageFar-dmg)*std::clamp(glm::length(p.start-at)/7.0,0.0,1.0);
            if(p.weaponId=="brolly") {
                const auto& w=GetEquipment(EquipmentCategory::Main,"brolly");
                dmg=w.number("pelletDamage")+(w.number("pelletDamageFar")-w.number("pelletDamage"))*std::clamp((glm::length(at-p.start)-w.number("falloffStart"))/(w.number("falloffEnd")-w.number("falloffStart")),0.0,1.0);
            }
            if(p.belowFalloff) dmg*=1-std::clamp((p.start.y-(e->state.pos.y+0.8)-0.5)*p.belowFalloff,0.0,p.belowFalloffMax);
            applyHit(owner,*e,dmg,p.weaponId);
            if(p.kind=="blast") {const auto& w=owner.weapon();burst(owner,at,w.number("splashRadius"),w.number("splashDamageMax"),w.number("splashDamageMin"),w.number("impactRadius"),p.weaponId,e,p.special);}
            done=true;break;
        }
        const auto path=p.pos-prev;const double distance=glm::length(path);
        if(!done && distance>0) done=blockShot(prev,p.pos,owner.team(),p.damage);
        if(!done && distance>0) {
            const auto hit=geometry_.raycast(prev,path/distance,distance,true);
            if(hit.hit) {
                SplatOptions options;options.stretch=glm::normalize(p.vel);options.hasStretch=true;options.stretchAmt=0.7;
                paint(owner,hit.point+hit.normal*0.14,p.radius*(0.85+owner.random()*0.3),p.seed,options,p.special);
                if(p.kind=="blast") {const auto& w=owner.weapon();burst(owner,hit.point,w.number("splashRadius"),w.number("splashDamageMax"),w.number("splashDamageMin"),w.number("impactRadius"),p.weaponId,nullptr,p.special);}
                done=true;
            }
        }
        if(!done && p.trailEvery>0) {
            p.trail+=glm::length(p.vel)*dt;
            if(p.trail>p.trailEvery) {
                p.trail=0;const auto hit=geometry_.raycast(p.pos,{0,-1,0},4,true);
                if(hit.hit) {const double radius=p.trailRadius*(0.8+owner.random()*0.4);paint(owner,hit.point+hit.normal*0.1,radius,owner.random(),{},p.special);}
            }
        }
        if(!done && p.age>p.life && p.kind=="blast") {const auto& w=owner.weapon();burst(owner,p.pos,w.number("splashRadius"),w.number("splashDamageMax"),w.number("splashDamageMin"),w.number("impactRadius"),p.weaponId,nullptr,p.special);}
        if(done || p.age>p.life || p.pos.y<-3.4) p.owner=nullptr;
    }
    updateDevices(dt);
    specials_.tickWorld(dt);
    ticking_=false;
    for (auto& p : pendingProjectiles_) projectiles_.push_back(std::move(p));
    pendingProjectiles_.clear();
    projectiles_.erase(std::remove_if(projectiles_.begin(),projectiles_.end(),[](const auto& p){return !p.owner;}),projectiles_.end());
    devices_.erase(std::remove_if(devices_.begin(),devices_.end(),[](const auto& d){return !d.live || !d.owner;}),devices_.end());
    actors_.erase(std::remove(actors_.begin(),actors_.end(),nullptr),actors_.end());
    for(auto* actor:actors_)if(actor && actor->mainId()=="brolly") {
        auto& k=actor->runner_.canopy_;
        if(k.state==CanopyState::Ready && k.open>=0.55 && k.grow>=0.9 && actor->state.alive && !actor->state.squid) {
            const auto normal=heldBrollyNormal(*actor);k.previousCenter=heldBrollyCenter(*actor,normal);k.previousNormal=normal;k.previousShield=true;
        } else k.previousShield=false;
    }
    for(auto& d:devices_)if(d.kind=="brolly" && d.live){d.previousC=d.C;d.previousN=d.N;d.hasPreviousShield=true;}
    volleyHits_.erase(std::remove_if(volleyHits_.begin(),volleyHits_.end(),[&](const auto& h){return std::none_of(projectiles_.begin(),projectiles_.end(),[&](const auto& p){return p.volley==h.volley;});}),volleyHits_.end());
}

void CombatWorld::tickBladeWave(ShooterProjectile& wave,double dt) {
    if(!wave.owner)return;
    auto& owner=*wave.owner;const auto previous=wave.pos;
    wave.age+=dt;const double step=glm::length(wave.vel)*dt;
    wave.pos+=wave.vel*dt;wave.distance+=step;
    if(blockCanopyShot(previous,wave.pos,owner.team(),wave.damage,&wave)
       || specials_.shotHit(previous,wave.pos,owner.team(),wave.damage,&owner)) {wave.owner=nullptr;return;}
    const double hd=std::hypot(wave.vel.x,wave.vel.z);const glm::dvec3 f=hd>1e-8?glm::dvec3(wave.vel.x/hd,0,wave.vel.z/hd):glm::dvec3(0,0,1);
    const double damage=wave.damage+(wave.damageFar-wave.damage)*std::clamp(wave.distance/std::max(0.01,wave.range),0.0,1.0);
    for(auto* actor:actors_) {
        if(!actor || actor==&owner || actor->team()==owner.team() || !actor->state.alive || std::find(wave.tagged.begin(),wave.tagged.end(),actor)!=wave.tagged.end())continue;
        const double dx=actor->state.pos.x-previous.x,dz=actor->state.pos.z-previous.z,along=dx*f.x+dz*f.z;
        if(along< -actor->hitRadius() || along>step+actor->hitRadius() || std::abs(dx*f.z-dz*f.x)>wave.radius+actor->hitRadius())continue;
        const double cy=previous.y+(wave.pos.y-previous.y)*std::clamp(along/std::max(1e-4,step),0.0,1.0);
        const double y=actor->state.pos.y+actor->state.smoothY,h=actor->hitHeight();
        if(y+h<cy-wave.size*0.55 || y>cy+wave.size*0.45)continue;
        wave.tagged.push_back(actor);applyHit(owner,*actor,damage,"blade");
    }
    if(blockShot(previous,wave.pos,owner.team(),damage)) {wave.owner=nullptr;return;}
    const auto path=wave.pos-previous;const double length=glm::length(path);
    if(length>1e-8 && geometry_.raycast(previous,path/length,length,true).hit) {wave.owner=nullptr;return;}
    wave.trail+=step;
    if(wave.trail>=wave.trailEvery) {
        wave.trail=0;const auto ground=geometry_.raycast(wave.pos,{0,-1,0},wave.size*0.52+1.4,true);
        if(ground.hit) {SplatOptions options;options.stretch=f;options.hasStretch=true;options.stretchAmt=0.85;paint(owner,ground.point+ground.normal*0.08,wave.trailRadius,owner.random(),options,wave.special);}
    }
    if(wave.distance>=wave.range || wave.age>=wave.life || wave.pos.y<-3.4)wave.owner=nullptr;
}

void CombatWorld::burstFist(ShooterProjectile& fist,glm::dvec3 at,PlayerSimulation* direct,const glm::dvec3* normal) {
    if(!fist.owner)return;
    const auto paintAt=normal?at+*normal*0.12:at;
    burst(*fist.owner,paintAt,fist.burstRadius,fist.burstDamage,fist.burstEdge,fist.radius,"mitts",direct,fist.special);
}

void CombatWorld::tickFist(ShooterProjectile& fist,double dt) {
    if(!fist.owner)return;
    auto& owner=*fist.owner;const auto previous=fist.pos;fist.age+=dt;fist.pos+=fist.vel*dt;
    const bool shielded=blockCanopyShot(previous,fist.pos,owner.team(),fist.damage,&fist);
    if(shielded) {burstFist(fist,fist.pos,nullptr);fist.owner=nullptr;return;}
    if(specials_.shotHit(previous,fist.pos,owner.team(),fist.damage,&owner)) {burstFist(fist,fist.pos,nullptr);fist.owner=nullptr;return;}
    for(auto* actor:actors_) {
        if(!actor || actor==&owner || actor->team()==owner.team() || !actor->state.alive)continue;
        const auto hit=StageGeometry::segmentCapsuleDist(previous,fist.pos,actor->visualPos(),actor->hitRadius(),actor->hitHeight());
        if(hit.dist>=actor->hitRadius()*0.95+fist.size)continue;
        const auto at=previous+(fist.pos-previous)*hit.t;applyHit(owner,*actor,fist.damage,"mitts");burstFist(fist,at,actor);fist.owner=nullptr;return;
    }
    if(blockShot(previous,fist.pos,owner.team(),fist.damage)) {burstFist(fist,fist.pos,nullptr);fist.owner=nullptr;return;}
    const auto path=fist.pos-previous;const double length=glm::length(path);
    if(length>1e-8) {
        const auto hit=geometry_.raycast(previous,path/length,length,true);
        if(hit.hit) {burstFist(fist,hit.point,nullptr,&hit.normal);fist.owner=nullptr;return;}
    }
    fist.distance+=length;
    if(fist.distance>=fist.range || fist.age>=fist.life) {burstFist(fist,fist.pos,nullptr);fist.owner=nullptr;}
}
void CombatWorld::lodgeArrow(ShooterProjectile& arrow,glm::dvec3 point,glm::dvec3 normal,glm::dvec3 direction) {
    arrow.state="lodged";arrow.age=0;arrow.hitPoint=point;arrow.hitNormal=normal;
    if(glm::length(direction)>1e-8)arrow.flightDir=glm::normalize(direction);
    arrow.pos=point+arrow.flightDir*0.12;
    SplatOptions options;options.stretch=arrow.flightDir;options.hasStretch=true;options.stretchAmt=0.5;
    paint(*arrow.owner,point+normal*0.08,arrow.stickPaint,arrow.seed,options,arrow.special);
}
void CombatWorld::burstArrow(ShooterProjectile& arrow) {
    auto& owner=*arrow.owner;
    const auto center=arrow.hitPoint+arrow.hitNormal*0.18;
    paint(owner,arrow.hitPoint+arrow.hitNormal*0.1,arrow.burstPaint,arrow.seed,{},arrow.special);
    for(int i=0;i<2;++i) {
        const double angle=arrow.seed*20.0+i*2.7,r=arrow.burstPaint*0.55;
        paint(owner,center+glm::dvec3(std::cos(angle)*r,0.1,std::sin(angle)*r),arrow.burstPaint*0.45,arrow.seed+i,{},arrow.special);
    }
    guardActorsWithCanopies(center,arrow.burstRadius,owner.team());
    damageCanopies(center,arrow.burstRadius,owner.team());
    for(auto* actor:actors_) {
        if(!actor || actor->team()==owner.team() || !actor->state.alive)continue;
        const double height=actor->hitHeight();
        const double baseY=actor->state.pos.y+actor->state.smoothY;
        glm::dvec3 target(actor->state.pos.x,baseY,actor->state.pos.z);
        target.y=std::clamp(center.y,baseY+0.25,baseY+std::max(0.3,height-0.25));
        const double distance=std::max(0.0,glm::length(target-center)-actor->hitRadius()*0.6);
        if(distance>arrow.burstRadius || !geometry_.los(center,target))continue;
        const double damage=distance<=arrow.burstInner?arrow.burstDamage:
            arrow.burstDamage+(arrow.burstEdge-arrow.burstDamage)*(distance-arrow.burstInner)/std::max(0.01,arrow.burstRadius-arrow.burstInner);
        applyHit(owner,*actor,damage,"bow");
    }
    damageDevices(center,arrow.burstRadius,arrow.burstEdge,owner.team());
    specials_.areaHit(center,arrow.burstRadius,arrow.burstEdge,owner.team(),&owner);
    arrow.owner=nullptr;
}
void CombatWorld::tickArrow(ShooterProjectile& arrow,double dt) {
    if(!arrow.owner)return;
    if(arrow.state=="lodged") {
        arrow.age+=dt;
        if(arrow.age>=arrow.fuse)burstArrow(arrow);
        return;
    }
    auto& owner=*arrow.owner;
    const auto previous=arrow.pos;
    arrow.age+=dt;
    if(arrow.age>arrow.straight)arrow.vel.y-=arrow.gravity*dt;
    if(arrow.distance>arrow.range) {
        arrow.vel.y-=arrow.diveGravity*dt;
        arrow.vel*=1.0-arrow.drag*dt;
    }
    arrow.pos+=arrow.vel*dt;
    const double speed=glm::length(arrow.vel);
    arrow.distance+=speed*dt;
    if(speed>0.01)arrow.flightDir=arrow.vel/speed;

    if(!arrow.noHit && (blockCanopyShot(previous,arrow.pos,owner.team(),arrow.damage,&arrow)
        || specials_.shotHit(previous,arrow.pos,owner.team(),arrow.damage,&owner))) {
        if(arrow.lodge)lodgeArrow(arrow,arrow.pos,-arrow.flightDir,arrow.flightDir);
        else arrow.owner=nullptr;
        return;
    }

    if(!arrow.noHit)for(auto* actor:actors_) {
        if(!actor || actor->team()==owner.team() || !actor->state.alive)continue;
        const auto hit=StageGeometry::segmentCapsuleDist(previous,arrow.pos,actor->visualPos(),actor->hitRadius(),actor->hitHeight());
        if(hit.dist>=actor->hitRadius()*0.95+0.11)continue;
        const auto at=previous+(arrow.pos-previous)*hit.t;
        applyHit(owner,*actor,arrow.damage,"bow");
        if(!arrow.lodge) {arrow.owner=nullptr;return;}
        arrow.noHit=true;arrow.flightDir=glm::length(arrow.vel)>1e-8?glm::normalize(arrow.vel):arrow.flightDir;
        arrow.pos=at;arrow.vel={arrow.flightDir.x*1.5,-9.0,arrow.flightDir.z*1.5};
        arrow.distance=std::max(arrow.distance,arrow.range);
        return;
    }

    if(blockShot(previous,arrow.pos,owner.team(),arrow.damage)) {
        if(arrow.lodge)lodgeArrow(arrow,arrow.pos,-arrow.flightDir,arrow.flightDir);
        else arrow.owner=nullptr;
        return;
    }
    const auto path=arrow.pos-previous;const double length=glm::length(path);
    if(length>1e-7) {
        const auto hit=geometry_.raycast(previous,path/length,length,true);
        if(hit.hit) {
            if(arrow.lodge) {
                const glm::dvec3 direction=arrow.noHit?
                    glm::normalize(glm::dvec3(arrow.flightDir.x*0.55,-1.0,arrow.flightDir.z*0.55)):arrow.flightDir;
                lodgeArrow(arrow,hit.point,hit.normal,direction);
            } else {
                SplatOptions options;options.stretch=arrow.flightDir;options.hasStretch=true;options.stretchAmt=0.7;
                paint(owner,hit.point+hit.normal*0.12,arrow.radius*(0.85+0.3*arrow.seed),arrow.seed,options,arrow.special);
                arrow.owner=nullptr;
            }
            return;
        }
    }
    if(arrow.lodge && !arrow.noHit) {
        arrow.trail+=speed*dt;
        if(arrow.trail>arrow.trailEvery) {
            arrow.trail=0;
            const auto ground=geometry_.raycast(arrow.pos,{0,-1,0},4,true);
            if(ground.hit) {
                const double radius=arrow.trailRadius*(0.8+owner.random()*0.4);
                const double seed=owner.random();
                paint(owner,ground.point+ground.normal*0.1,radius,seed,{},arrow.special);
            }
        }
    }
    if(arrow.age>arrow.life || arrow.pos.y<PlayerTuning().number("waterY")-1.2)arrow.owner=nullptr;
}
void CombatWorld::updateDevices(double dt) {
    constexpr double pi=3.14159265358979323846;
    auto nearestEnemy=[&](const CombatDevice& d,double range,bool requireLos,bool skipHidden=false) {
        PlayerSimulation* best=nullptr;double bestDistance=range;
        for(auto* e:actors_) {
            if(!e || e->team()==d.team || !e->state.alive || (skipHidden && e->state.squid && e->state.submerged)) continue;
            const auto p=e->state.pos+glm::dvec3(0,e->state.squid?0.3:0.8,0);const double distance=glm::length(p-d.pos);
            if(distance>=bestDistance || (requireLos && !geometry_.los(d.pos,p))) continue;
            best=e;bestDistance=distance;
        }
        return best;
    };
    auto scanWaddle=[&](const CombatDevice& d,double range) {
        PlayerSimulation* best=nullptr;double bestDistance=range;
        for(auto* e:actors_) {
            if(!e || e->team()==d.team || !e->state.alive) continue;
            const double distance=std::hypot(e->state.pos.x-d.pos.x,e->state.pos.z-d.pos.z);
            if(distance>=bestDistance || std::abs(e->state.pos.y-d.pos.y)>GetEquipment(EquipmentCategory::Sub,"waddle").number("senseUp")) continue;
            best=e;bestDistance=distance;
        }
        return best;
    };
    auto track=[&](PlayerSimulation& e,int team,double duration) {
        if(duration>e.state.track || e.state.trackTeam!=team) {e.state.track=duration;e.state.trackTeam=team;}
    };
    auto poison=[](PlayerSimulation& e,double duration) {e.state.poison=std::max(e.state.poison,duration);};
    auto stick=[&](CombatDevice& d,const RayHit& hit,const std::string& state,double timer) {
            d.pos=hit.point+hit.normal*0.005;d.normal=hit.normal;d.state=state;d.grounded=true;d.age=0;d.timer=timer;d.hp=GetEquipment(EquipmentCategory::Sub,d.kind).number("hp",1);
        if(d.kind=="curtain") {
            if(glm::length(d.N)<1e-5) { auto n=glm::dvec3(d.vel.x,0,d.vel.z);if(glm::length(n)>1e-5)d.N=glm::normalize(n); }
            d.halfW=GetEquipment(EquipmentCategory::Sub,d.kind).number("width")*0.5;d.R=GetEquipment(EquipmentCategory::Sub,d.kind).number("height");d.blocksActors=true;
            paint(*d.owner,hit.point+hit.normal*0.1,1.4,d.id*0.37,{},d.special);
        }
        if(d.kind=="sprinkler") {
            for(auto& old:devices_)if(&old!=&d && old.live && old.owner==d.owner && old.kind=="sprinkler" && old.state=="spray")old.live=false;
            d.pulse=0;d.spin=0;
        }
    };
    auto updateShaker=[&](CombatDevice& d,const EquipmentData& s) {
        constexpr double contactRadius=0.19;
        d.grounded=false;
        d.vel.y-=24*dt;
        const auto prev=d.pos;d.pos+=d.vel*dt;
        const auto path=d.pos-prev;const double distance=glm::length(path);
        bool floor=false;
        if(distance>1e-6) {
            const auto hit=geometry_.raycast(prev,path/distance,distance,true);
            if(hit.hit) {
                d.pos=hit.point+hit.normal*contactRadius;
                if(hit.normal.y>0.6) {
                    floor=true;const double vn=glm::dot(d.vel,hit.normal);
                    if(vn<-2.2)d.vel=(d.vel-hit.normal*vn)*0.45+hit.normal*(-vn*0.3);
                    else if(vn<0) {d.vel-=hit.normal*vn;const double friction=std::exp(-6*dt);d.vel.x*=friction;d.vel.z*=friction;}
                } else {
                    const double vn=glm::dot(d.vel,hit.normal);
                    if(vn<0)d.vel=(d.vel-hit.normal*vn*1.5)*0.62;
                    const glm::dvec3 forward(std::sin(d.heading),0,std::cos(d.heading));
                    const double dn=forward.x*hit.normal.x+forward.z*hit.normal.z;
                    if(dn<0 && std::abs(hit.normal.y)<0.6) {
                        const double x=forward.x-2*dn*hit.normal.x,z=forward.z-2*dn*hit.normal.z;
                        d.heading=std::atan2(x,z);
                    }
                }
            }
        }
        if(!floor) {
            const auto ground=geometry_.raycast(d.pos,{0,-1,0},contactRadius,true);
            if(ground.hit && ground.normal.y>0.6) {d.pos.y+=contactRadius-ground.dist;floor=true;const double vn=glm::dot(d.vel,ground.normal);if(vn<0){d.vel-=ground.normal*vn;const double friction=std::exp(-6*dt);d.vel.x*=friction;d.vel.z*=friction;}}
        }
        d.grounded=floor;
        if(floor && !d.held) {d.held=true;d.fuse=s.number("fuse");}
        const double horizontal=std::hypot(d.vel.x,d.vel.z);
        if(!floor && horizontal>1.5) {
            d.trail+=horizontal*dt;
            while(d.trail>=s.number("trailEvery")) {
                d.trail-=s.number("trailEvery");
                ShooterProjectile p;p.owner=d.owner;p.weaponId="shaker";p.kind="drop";p.pos=p.start=d.pos-glm::dvec3(0,0.1,0);
                p.vel={d.vel.x*0.2+(d.owner->random()-0.5)*0.5,-2.2-d.owner->random(),d.vel.z*0.2+(d.owner->random()-0.5)*0.5};
                p.life=1.2;p.straight=0;p.gravity=28;p.drag=0.8;p.radius=s.number("trailRadius")*(0.85+d.owner->random()*0.3);p.damage=0;p.damageFar=0;p.size=0.045;p.special=d.special;p.seed=d.owner->random();launch(std::move(p));
            }
        }
        if(d.held && d.fuse>=0) {d.fuse-=dt;if(d.fuse<=0) {d.fuse=-1;explodeDevice(d);return;}}
        else if(d.held && d.timer>0) {d.timer-=dt;if(d.timer<=0) {d.timer=-1;explodeDevice(d);return;}}
        if(d.age>6 && !d.held) {d.held=true;d.fuse=s.number("fuse");}
        if(d.pos.y<-3.4)d.live=false;
    };
    for(int phase=0;phase<2;++phase) for(auto& d:devices_) {
        if(d.special!=(phase==1)) continue;
        if(!d.live || !d.owner) continue;
        if(d.kind=="brolly")continue;
        const auto& s=GetEquipment(EquipmentCategory::Sub,d.kind);
        d.age+=dt;
        if(d.kind=="shaker" && d.state=="fly") {updateShaker(d,s);continue;}
        if(d.state=="fly") {
            if(d.kind=="scan" || d.kind=="mist") {
                if(d.age>=d.fuse) {d.state=d.kind=="scan"?"cloud":"mist";d.age=0;d.timer=s.number(d.kind=="scan"?"cloudTime":"mistTime");
                    if(d.kind=="mist" && d.target && d.target->state.alive) poison(*d.target,s.number("mistTime"));}
            }
            if(!d.live || d.state!="fly") continue;
            const auto prev=d.pos;const double speed=glm::length(d.vel);
            if(d.kind!="tracer") d.vel.y-=24*dt;
            d.pos+=d.vel*dt;
            PlayerSimulation* direct=nullptr;
            if(d.kind=="burst" || d.kind=="mist" || d.kind=="torpedo") for(auto* e:actors_) {
                if(!e || e->team()==d.team || !e->state.alive) continue;
                const bool torpedo=d.kind=="torpedo";
                const double radius=torpedo?e->hitRadius():0.38;
                const auto h=StageGeometry::segmentCapsuleDist(prev,d.pos,torpedo?e->visualPos():e->state.pos,
                    radius,torpedo?e->hitHeight():(e->state.squid?0.55:1.45));
                if(h.dist<(torpedo?radius+0.16:0.5)) {d.pos=prev+(d.pos-prev)*h.t;direct=e;break;}
            }
            if(direct) {
                if(d.kind=="burst") explodeDevice(d,direct);
                else if(d.kind=="mist") {d.target=direct;d.state="mist";d.age=0;d.timer=s.number("mistTime");poison(*direct,s.number("mistTime"));}
                else if(d.kind=="torpedo") explodeDevice(d,direct);
                continue;
            }
            if(d.kind=="torpedo" && blockShot(prev,d.pos,d.team,0)) {explodeDevice(d);continue;}
            const auto path=d.pos-prev;const double distance=glm::length(path);
            const auto hit=distance>1e-6?geometry_.raycast(prev,path/distance,distance,true):RayHit{};
            if(d.kind=="tracer") {
                auto segmentEnd=hit.hit?hit.point:d.pos;
                PlayerSimulation* directHit=nullptr;
                for(auto* e:actors_) {
                    if(!e || e->team()==d.team || !e->state.alive || std::find(d.tagged.begin(),d.tagged.end(),e)!=d.tagged.end()) continue;
                    const auto h=StageGeometry::segmentCapsuleDist(prev,segmentEnd,e->visualPos(),e->hitRadius(),e->hitHeight());
                    if(h.dist<e->hitRadius()+d.radius) {d.pos=prev+(segmentEnd-prev)*h.t;directHit=e;break;}
                }
                if(directHit) {
                    applyHit(*d.owner,*directHit,s.number("directDamage"),d.kind);
                    track(*directHit,d.team,s.number("directMark"));
                    d.tagged.push_back(directHit);d.live=false;continue;
                }
                if(blockShot(prev,segmentEnd,d.team,s.number("directDamage"))) {d.pos=segmentEnd;d.live=false;continue;}
                d.pos=segmentEnd;
                d.left-=glm::length(segmentEnd-prev);d.travel+=glm::length(segmentEnd-prev);
                const auto fromTrail=d.trailPoints.empty()?glm::dvec3(0):d.trailPoints.back().first-d.pos;
                if(d.trailPoints.empty() || glm::dot(fromTrail,fromTrail)>1e-7) d.trailPoints.emplace_back(d.pos,time_);
                else d.trailPoints.back().second=time_;
                while(d.trailPoints.size()>2 && time_-d.trailPoints[1].second>s.number("trailLife")) d.trailPoints.erase(d.trailPoints.begin());
                for(auto* e:actors_) {
                    if(!e || e->team()==d.team || !e->state.alive || std::find(d.tagged.begin(),d.tagged.end(),e)!=d.tagged.end()) continue;
                    const auto target=e->visualPos();
                    bool touched=false;
                    for(std::size_t i=1;i<d.trailPoints.size();++i) {
                        const auto& a=d.trailPoints[i-1];const auto& b=d.trailPoints[i];
                        if(time_-a.second>s.number("trailLife")) continue;
                        if(StageGeometry::segmentCapsuleDist(a.first,b.first,target,e->hitRadius(),e->hitHeight()).dist<e->hitRadius()+s.number("trailRadius")) {touched=true;break;}
                    }
                    if(touched) {applyHit(*d.owner,*e,s.number("trailDamage"),d.kind);track(*e,d.team,s.number("trailMark"));d.tagged.push_back(e);}
                }
                if(hit.hit) {
                    paint(*d.owner,hit.point+hit.normal*0.1,s.number("puddleRadius"),d.owner->random(),{},d.special);
                    auto direction=glm::normalize(d.vel);direction=glm::normalize(direction-2.0*glm::dot(direction,hit.normal)*hit.normal);
                    if(hit.normal.y>0.6) {
                        direction.y*=s.number("floorExit");direction=glm::normalize(direction);
                        const double into=glm::dot(direction,hit.normal);
                        if(into<0.04) direction=glm::normalize(direction+hit.normal*(0.04-into));
                    }
                    d.pos=hit.point+hit.normal*0.03;d.vel=direction*s.number("speed");
                    ++d.uses;if(d.uses>s.number("maxBounces"))d.live=false;
                }
                if(d.left<=0 || d.pos.y<PlayerTuning().number("waterY")-0.3)d.live=false;
                continue;
            }
            if(hit.hit) {
                d.pos=hit.point;
                if(d.kind=="sticky") {stick(d,hit,"stuck",s.number("fuse"));continue;}
                if(d.kind=="sprinkler") {stick(d,hit,"spray",-1);continue;}
                if(d.kind=="burst") {explodeDevice(d);continue;}
                if(d.kind=="scan") {d.pos+=hit.normal*0.3;d.state="cloud";d.age=0;d.timer=s.number("cloudTime");continue;}
                if(d.kind=="mist") {d.pos+=hit.normal*0.3;d.state="mist";d.age=0;d.timer=s.number("mistTime");continue;}
                if((d.kind=="seeker" || d.kind=="waddle") && hit.normal.y>0.6) {
                    d.pos=hit.point;d.grounded=true;d.age=0;d.heading=std::atan2(d.vel.x,d.vel.z);d.target=nullptr;d.stuck=0;
                    if(d.kind=="waddle") {
                        d.state="sense";d.timer=s.number("fuse");d.locked=false;
                        d.walkTime=d.travel=d.lostTime=d.progressTime=d.repath=0;d.path.clear();d.pathIndex=0;d.hopping=d.falling=false;
                        d.progressX=d.pos.x;d.progressZ=d.pos.z;
                    } else {
                        d.state="run";d.target=nearestEnemy(d,s.number("seekRange"),false);d.timer=s.number("life");
                    }
                    continue;
                }
        if(d.kind=="curtain" && hit.normal.y>0.6) {stick(d,hit,"curtain",-1);continue;}
                if(d.kind=="torpedo") {d.pos=hit.point+hit.normal*0.14;d.N=hit.normal;explodeDevice(d);continue;}
                if(d.kind=="shaker" && hit.normal.y>0.6) {d.grounded=true;d.pos=hit.point+hit.normal*0.19;d.held=true;if(d.fuse<0)d.fuse=s.number("fuse");}
                const double vn=glm::dot(d.vel,hit.normal);d.vel=(d.vel-hit.normal*(1.4*vn))*(hit.normal.y>0.6?0.45:0.6);d.pos+=hit.normal*0.12;
                if(d.kind=="bomb" && hit.normal.y>0.6 && d.fuse<0)d.fuse=s.number("fuse");
            }
            if(d.kind=="torpedo" && d.age>=s.number("armTime")) {
                auto* target=nearestEnemy(d,s.number("lockRange"),true,true);
                if(target) {d.target=target;d.locked=true;d.state="unfold";d.age=0;d.timer=s.number("unfoldTime");d.vel={0,0,0};d.C=d.pos;continue;}
            }
            if(d.kind=="bomb" && d.fuse>=0) {d.fuse-=dt;if(d.fuse<=0)explodeDevice(d);}
            if(d.kind=="torpedo" && d.age>s.number("flyLife"))explodeDevice(d);
            if(d.kind=="boomerang") {
                d.left-=speed*dt;
                if(d.age>=s.number("outTime") || d.left<=0) {d.state="hover";d.age=0;d.timer=s.number("hover");d.vel={0,0,0};}
            }
            if(d.pos.y<-3.4 || (d.timer>0 && d.age>d.timer && d.kind!="boomerang" && d.kind!="bomb" && d.kind!="shaker"))d.live=false;
        } else if(d.state=="stuck") {
            if(d.kind=="shaker") {if(d.age>=d.timer)explodeDevice(d);}
            else if(d.age>=d.timer)explodeDevice(d);
        } else if(d.state=="cloud") {
            const double radius=s.number("radius")*std::clamp(d.age/0.25,0.0,1.0);
            for(auto* e:actors_) if(e && e->team()!=d.team && e->state.alive && std::find(d.tagged.begin(),d.tagged.end(),e)==d.tagged.end() && glm::length(e->state.pos+glm::dvec3(0,0.7,0)-d.pos)<radius) {d.tagged.push_back(e);track(*e,d.team,s.number("trackTime"));}
            if(d.age>=d.timer)d.live=false;
        } else if(d.state=="mist") {
            const double radius=s.number("radius")*std::clamp(d.age/0.4,0.0,1.0);
            for(auto* e:actors_) if(e && e->team()!=d.team && e->state.alive && glm::length(e->state.pos+glm::dvec3(0,0.6,0)-d.pos)<radius)poison(*e,0.6);
            if(d.age>=d.timer)d.live=false;
        } else if(d.state=="curtain") {
            d.hp-=s.number("decay")*dt;if(d.hp<=0)d.live=false;
        } else if(d.state=="spray") {
            d.spin+=dt*(d.age<s.number("sprayFade")?9:4.5);d.pulse-=dt;
            if(d.pulse<=0) {
                d.pulse=s.number("pulse")*(d.age<s.number("sprayFade")?1:2);
                const auto n=d.normal;auto t1=glm::dvec3(1,0,0);if(std::abs(n.x)>0.9)t1={0,0,1};t1=glm::normalize(t1-n*glm::dot(t1,n));const auto t2=glm::cross(n,t1);const auto base=d.pos+n*0.2;
                for(int k=0;k<static_cast<int>(s.number("drops"));++k) {
                    const double a=d.spin*1.7+(k/s.number("drops"))*2*pi+(d.owner->random()-0.5)*0.4;
                    const double out=0.6+d.owner->random()*0.4,sp=3.2+d.owner->random()*2.6;
                    ShooterProjectile p;p.owner=d.owner;p.weaponId="sprinkler";p.kind="drop";p.pos=p.start=base;
                    p.vel=(t1*std::cos(a)+t2*std::sin(a)+n*0.75)*(out*sp);p.life=1.2;p.straight=0;p.gravity=22;p.drag=0.4;
                    p.radius=0.5+d.owner->random()*0.25;p.damage=s.number("dropDamage");p.damageFar=p.damage;p.size=0.08;
                    p.seed=d.id+a;p.special=d.special;launch(std::move(p));
                }
            }
        } else if(d.state=="mine") {
            if(d.fuse>=0) {d.fuse-=dt;if(d.fuse<=0)explodeDevice(d);continue;}
            if(d.age<s.number("armTime"))continue;
            for(auto* e:actors_) if(e && e->team()!=d.team && e->state.alive && glm::length(e->state.pos-d.pos)<s.number("triggerRadius")) {d.fuse=s.number("delay");d.age=0;break;}
        } else if(d.state=="beacon") {
            // Beacon lifetime is controlled by hit points and its two source-defined jump uses.
        } else if(d.state=="sense") {
            const double scanRadius=s.number("senseRadius")*std::min(1.0,d.age/0.3+0.35);
            PlayerSimulation* sensed=scanWaddle(d,scanRadius);
            if(sensed) {
                d.target=sensed;d.locked=true;d.state="wake";d.age=0;
                d.heading=std::atan2(sensed->state.pos.x-d.pos.x,sensed->state.pos.z-d.pos.z);
            } else if(d.age>=d.timer) explodeDevice(d);
        } else if(d.state=="wake") {
            if(d.target && d.target->state.alive) {
                const double wanted=std::atan2(d.target->state.pos.x-d.pos.x,d.target->state.pos.z-d.pos.z);
                const double delta=std::remainder(wanted-d.heading,2*pi),turn=std::clamp(delta,-14*dt,14*dt);d.heading+=turn;
            }
            if(d.age>=0.32) {d.state="walk";d.age=0;d.walkTime=0;d.travel=0;d.lostTime=0;d.trail=0;d.stuck=0;d.path.clear();d.pathIndex=0;d.repath=0;d.hopping=d.falling=false;d.progressTime=0;d.progressX=d.pos.x;d.progressZ=d.pos.z;}
        } else if(d.state=="run" || d.state=="walk") {
            if(d.kind=="waddle") {
                d.walkTime+=dt;
                if(!d.target || !d.target->state.alive) {
                    d.target=scanWaddle(d,d.senseRadius);
                    if(d.target) {d.lostTime=0;d.repath=0;d.path.clear();d.pathIndex=0;}
                    else if((d.lostTime+=dt)>0.5) {explodeDevice(d);continue;}
                } else d.lostTime=0;
                d.left=std::max(0.0,std::min(s.number("life")-d.walkTime,(s.number("maxTravel")-d.travel)/d.speed));

                bool reached=false;
                for(auto* e:actors_) {
                    if(!e || e->team()==d.team || !e->state.alive) continue;
                    const auto chest=e->visualPos()+glm::dvec3(0,e->state.squid?0.25:0.7,0);
                    if(glm::length(chest-(d.pos+glm::dvec3(0,0.3,0)))<s.number("triggerDist")) {reached=true;break;}
                }
                if(reached || d.walkTime>s.number("life") || d.travel>s.number("maxTravel")) {explodeDevice(d);continue;}

                double moved=0.0;
                if(d.hopping) {
                    const double x0=d.pos.x,z0=d.pos.z;
                    d.hopTime+=dt;
                    const double u=std::clamp(d.hopTime/d.hopDuration,0.0,1.0),p=u*u;
                    d.pos={d.hopStart.x+(d.hopEnd.x-d.hopStart.x)*p,
                           d.hopStart.y+(d.hopEnd.y-d.hopStart.y)*u+2*u*(1-u),
                           d.hopStart.z+(d.hopEnd.z-d.hopStart.z)*p};
                    moved=std::hypot(d.pos.x-x0,d.pos.z-z0);
                    if(u>=1.0) {d.hopping=false;d.repath=0;}
                } else if(d.falling) {
                    const double x0=d.pos.x,z0=d.pos.z,y0=d.pos.y;
                    d.vel.y-=24*dt;
                    const double horizontal=std::hypot(d.vel.x,d.vel.z);
                    if(horizontal>1e-4) {
                        const glm::dvec3 direction(d.vel.x/horizontal,0,d.vel.z/horizontal);
                        const auto wall=geometry_.raycast(d.pos+glm::dvec3(0,0.3,0),direction,horizontal*dt+0.15,true);
                        if(wall.hit && std::abs(wall.normal.y)<0.5) {d.vel.x=0;d.vel.z=0;}
                    }
                    d.pos+=d.vel*dt;
                    const double ground=geometry_.groundHeight(d.pos.x,d.pos.z,y0+0.05);
                    if(std::isfinite(ground) && d.pos.y<=ground) {d.pos.y=ground;d.vel={0,0,0};d.falling=false;d.repath=0;}
                    if(d.pos.y<-2.8) {d.live=false;continue;}
                    moved=std::hypot(d.pos.x-x0,d.pos.z-z0);
                } else if(d.target && d.target->state.alive) {
                    glm::dvec3 goal=d.target->state.pos;const NavNode* nextNode=nullptr;
                    if(nav_) {
                        if((d.repath-=dt)<=0) {
                            d.repath=0.35;d.path.clear();d.pathIndex=0;
                            const int start=nav_->nearest(d.pos,0.8),end=nav_->nearest(d.target->state.pos,1.5);
                            if(start>=0 && end>=0 && start!=end) {
                                auto path=nav_->path(start,end,d.team,2500,true);
                                if(path.size()>1) {d.path=std::move(path);d.pathIndex=1;}
                            }
                        }
                        const double horizontal=std::hypot(d.target->state.pos.x-d.pos.x,d.target->state.pos.z-d.pos.z);
                        const double vertical=d.target->state.pos.y-d.pos.y;
                        if(!(horizontal<2.2 && std::abs(vertical)<0.8) && !d.path.empty()) {
                            const auto& nodes=nav_->nodes();
                            while(d.pathIndex<d.path.size()) {
                                const int id=d.path[d.pathIndex];
                                if(id<0 || id>=static_cast<int>(nodes.size())) {d.path.clear();d.pathIndex=0;break;}
                                const auto& candidate=nodes[id];
                                if(std::hypot(candidate.x-d.pos.x,candidate.z-d.pos.z)<0.45 && std::abs(candidate.y-d.pos.y)<0.6) {++d.pathIndex;continue;}
                                nextNode=&candidate;goal={candidate.x,candidate.y,candidate.z};break;
                            }
                        }
                    }
                    const double turn=std::remainder(std::atan2(goal.x-d.pos.x,goal.z-d.pos.z)-d.heading,2*pi);
                    d.heading+=std::clamp(turn,-s.number("turnRate")*dt,s.number("turnRate")*dt);
                    const double stepSpeed=s.number("speed")*(std::abs(turn)>1.3?0.45:1.0);
                    if(nextNode && nextNode->y-d.pos.y>0.45 && std::hypot(nextNode->x-d.pos.x,nextNode->z-d.pos.z)<1.35) {
                        d.hopping=true;d.hopTime=dt;d.hopStart=d.pos;d.hopEnd={nextNode->x,nextNode->y,nextNode->z};++d.pathIndex;
                        const double u=std::clamp(d.hopTime/d.hopDuration,0.0,1.0),p=u*u;
                        d.pos={d.hopStart.x+(d.hopEnd.x-d.hopStart.x)*p,
                               d.hopStart.y+(d.hopEnd.y-d.hopStart.y)*u+2*u*(1-u),
                               d.hopStart.z+(d.hopEnd.z-d.hopStart.z)*p};
                        moved=std::hypot(d.pos.x-d.hopStart.x,d.pos.z-d.hopStart.z);
                    } else {
                        const double step=stepSpeed*dt;double mx=std::sin(d.heading)*step,mz=std::cos(d.heading)*step;
                        const auto forward=glm::dvec3(std::sin(d.heading),0,std::cos(d.heading));
                        const auto wall=geometry_.raycast(d.pos+glm::dvec3(0,0.42,0),forward,step+0.2,true);
                        if(wall.hit && std::abs(wall.normal.y)<0.5) {
                            const double into=mx*wall.normal.x+mz*wall.normal.z;
                            mx-=into*wall.normal.x;mz-=into*wall.normal.z;
                            const double slide=std::hypot(mx,mz);
                            if(slide>1e-5) {
                                const auto side=geometry_.raycast(d.pos+glm::dvec3(0,0.42,0),glm::dvec3(mx/slide,0,mz/slide),slide+0.2,true);
                                if(side.hit && std::abs(side.normal.y)<0.5) {mx=0;mz=0;}
                            }
                        }
                        const double x=d.pos.x+mx,z=d.pos.z+mz;const double ground=geometry_.groundHeight(x,z,d.pos.y+0.45);
                        moved=std::hypot(mx,mz);
                        if(moved>1e-6 && std::isfinite(ground)) {
                            if(ground<d.pos.y-0.4) {d.falling=true;d.vel={mx/dt,0.6,mz/dt};d.pos.x=x;d.pos.z=z;}
                            else d.pos={x,ground,z};
                        }
                    }
                }
                d.travel+=moved;
                d.progressTime+=dt;
                if(d.progressTime>1.4) {
                    if(std::hypot(d.pos.x-d.progressX,d.pos.z-d.progressZ)<0.4 && !d.hopping && !d.falling) {explodeDevice(d);continue;}
                    d.progressTime=0;d.progressX=d.pos.x;d.progressZ=d.pos.z;
                }
            } else {
                if(!d.target || !d.target->state.alive) {
                    auto* target=nearestEnemy(d,s.number("seekRange"),false);
                    if(target!=d.target)d.dash=false;
                    d.target=target;
                }
                double stepSpeed=s.number("speed");
                if(d.target) {
                    const auto diff=d.target->state.pos-d.pos;
                    const double dist=std::hypot(diff.x,diff.z),wanted=std::atan2(diff.x,diff.z);
                    const double delta=std::remainder(wanted-d.heading,2*pi);
                    if(!d.dash && dist<d.commitDist && std::abs(delta)<0.35)d.dash=true;
                    else if(d.dash && (std::abs(delta)>pi*0.5 || dist>d.commitDist+1.0))d.dash=false;
                    if(!d.dash) {
                        d.heading+=std::clamp(delta,-s.number("turnRate")*dt,s.number("turnRate")*dt);
                        if(std::hypot(d.target->state.vel.x,d.target->state.vel.z)<2.5)
                            stepSpeed*=1.0-d.creep*std::clamp((std::abs(delta)-0.7)/0.6,0.0,1.0);
                    }
                }
                const glm::dvec3 f(std::sin(d.heading),0,std::cos(d.heading));const double step=stepSpeed*dt;
                const auto wall=geometry_.raycast(d.pos+glm::dvec3(0,0.2,0),f,step+0.3,true);
                if(wall.hit && std::abs(wall.normal.y)<0.5) {
                    d.stuck+=dt;
                    const double side=f.x*wall.normal.z-f.z*wall.normal.x;
                    d.heading+=6*dt*(side>=0?1:-1);
                } else {
                    d.stuck=std::max(0.0,d.stuck-dt);const double x=d.pos.x+f.x*step,z=d.pos.z+f.z*step,y=geometry_.groundHeight(x,z,d.pos.y+0.6);
                    if(!std::isfinite(y)) {d.live=false;continue;}
                    d.pos={x,y,z};d.trail+=step;
                    if(d.trail>0.35) {d.trail=0;paintDevice(d,d.pos+glm::dvec3(0,0.12,0),s.number("trailRadius",0.6));}
                }
                if(d.target && glm::length(d.target->state.pos-d.pos)<s.number("triggerDist")) {explodeDevice(d);continue;}
                if(d.age>s.number("life") || d.stuck>0.6) explodeDevice(d);
            }
        } else if(d.state=="unfold") {
            if((!d.target || !d.target->state.alive))d.target=nearestEnemy(d,d.lockRange*1.4,true,true);
            if(d.target && d.target->state.alive) {const auto delta=d.target->state.pos+glm::dvec3(0,d.target->state.squid?0.3:0.8,0)-d.pos;if(glm::length(delta)>1e-5){const auto dir=glm::normalize(delta);d.N=glm::normalize(d.N*std::exp(-9*dt)+dir*(1-std::exp(-9*dt)));}}
            if(d.age>=d.timer) {d.state="launch";d.age=0;d.speed=s.number("launchSpeed0");d.vel=d.N;}
        } else if(d.state=="launch") {
            if(d.target && d.target->state.alive) {
                const auto delta=d.target->state.pos+glm::dvec3(0,d.target->state.squid?0.3:0.8,0)-d.pos;
                if(glm::length(delta)>1e-5) {
                    const auto desired=glm::normalize(delta);const double angle=std::acos(std::clamp(glm::dot(d.vel,desired),-1.0,1.0));
                    if(angle>1e-6) {auto axis=glm::cross(d.vel,desired);const double n=glm::length(axis);if(n>1e-6){axis/=n;const double turn=std::min(angle,s.number("turnRate")*dt);d.vel=glm::normalize(d.vel*std::cos(turn)+glm::cross(axis,d.vel)*std::sin(turn)+axis*glm::dot(axis,d.vel)*(1-std::cos(turn)));}}
                }
            }
            d.speed=std::min(s.number("launchSpeed"),d.speed+s.number("launchAccel")*dt);const auto prev=d.pos;d.pos+=d.vel*d.speed*dt;
            for(auto* e:actors_)if(e && e->team()!=d.team && e->state.alive && StageGeometry::segmentCapsuleDist(prev,d.pos,e->visualPos(),e->hitRadius(),e->hitHeight()).dist<e->hitRadius()+0.16){explodeDevice(d,e);break;}
            if(d.live && blockShot(prev,d.pos,d.team,0))explodeDevice(d);
            if(d.live && geometry_.raycast(prev,glm::normalize(d.pos-prev),glm::length(d.pos-prev),true).hit)explodeDevice(d);
            if(d.live && d.age>s.number("launchLife"))explodeDevice(d);
        } else if(d.state=="hover") {
            if(d.age>=d.timer){d.state="return";d.age=0;}
            else {
                const double interval=1/s.number("tickRate");d.pulse-=dt;
                if(d.pulse<=0){d.pulse=interval;for(auto* e:actors_)if(e && e->team()!=d.team && e->state.alive && glm::length(e->state.pos-d.pos)<s.number("hoverRadius") && geometry_.los(d.pos,e->state.pos+glm::dvec3(0,0.7,0)))applyHit(*d.owner,*e,s.number("tickDamage"),d.kind);damageDevices(d.pos,s.number("hoverRadius"),s.number("tickDamage"),d.team);}
                d.trail-=dt;if(d.trail<=0){d.trail=0.2;const auto floor=geometry_.raycast(d.pos,{0,-1,0},3.4,true);if(floor.hit)paintDevice(d,floor.point+glm::dvec3(0,0.12,0),s.number("hoverPaint"));}
            }
        } else if(d.state=="return") {
            const auto delta=d.owner->state.pos+glm::dvec3(0,0.75,0)-d.pos;const double distance=glm::length(delta);
            if(distance< s.number("orbitRadius")+0.35){d.state="orbit";d.age=0;d.heading=std::atan2(d.pos.z-d.owner->state.pos.z,d.pos.x-d.owner->state.pos.x);}
            else {d.vel=glm::normalize(delta)*std::min(s.number("returnSpeed"),4+d.age*34);d.pos+=d.vel*dt;if(d.age>3)explodeDevice(d);}
        } else if(d.state=="orbit") {
            d.heading+=s.number("orbitSpin")*dt;d.pos=d.owner->state.pos+glm::dvec3(std::cos(d.heading)*s.number("orbitRadius"),0.95,std::sin(d.heading)*s.number("orbitRadius"));
            if(d.age>=2.5)explodeDevice(d);
        }
    }
}
std::vector<CombatThreat> CombatWorld::threats() const {
    std::vector<CombatThreat> result;
    for(const auto& p:projectiles_) if(p.owner) {
        CombatThreat t;t.id=p.id;t.kind=p.kind;t.team=p.owner->team();t.pos=p.pos;t.vel=p.vel;t.radius=p.radius;t.timeToImpact=std::max(0.0,p.life-p.age);
        t.state="fly";t.aimY=0;t.speed=glm::length(p.vel);t.left=t.speed*t.timeToImpact;result.push_back(std::move(t));
    }
    for(const auto& d:devices_) if(d.live && d.owner) {
        CombatThreat t;t.id=d.id;t.kind=d.kind;t.team=d.team;t.pos=d.pos;t.vel=d.vel;t.radius=d.radius;t.timeToImpact=std::max(0.0,d.timer-d.age);
        t.ground=d.kind=="waddle"?true:d.grounded;
        t.locked=d.kind=="waddle"?(d.state=="wake" || d.state=="walk"):d.kind=="torpedo"?(d.state=="unfold" || d.state=="launch"):d.locked;
        t.shootable=d.shootable && !(d.kind=="waddle" && d.state=="fly");t.state=d.state;
        t.target=(d.kind=="waddle"?(d.state=="wake" || d.state=="walk"):d.kind=="torpedo"?(d.state=="unfold" || d.state=="launch"):true) && d.target && d.target->state.alive ? d.target : nullptr;
        t.aimY=d.aimY;
        if(d.kind=="waddle") {
            if(d.state=="walk" && d.falling) t.vel=d.vel;
            else if(d.state=="walk" && !d.hopping) t.vel={std::sin(d.heading)*d.speed,0,std::cos(d.heading)*d.speed};
            else if(d.state!="fly") t.vel={0,0,0};
            t.left=d.state=="walk"?d.left:GetEquipment(EquipmentCategory::Sub,"waddle").number("life");
        }
        t.senseRadius=d.senseRadius;t.lockRange=d.lockRange;t.trigger=d.trigger;t.speed=d.kind=="torpedo"?GetEquipment(EquipmentCategory::Sub,"torpedo").number("launchSpeed"):d.speed;
        if(d.kind=="torpedo" && d.state=="launch") t.vel=d.vel*d.speed;
        if(d.kind!="waddle")t.left=d.left;
        result.push_back(std::move(t));
    }
    return result;
}
std::vector<CombatShield> CombatWorld::shields() const {
    std::vector<CombatShield> result;
    for(auto* actor:actors_) {
        if(!actor || actor->mainId()!="brolly")continue;
        const auto& k=actor->runner_.canopy_;
        if(!k.id || k.state!=CanopyState::Ready || k.open<0.55 || k.grow<0.9 || !actor->state.alive || actor->state.squid)continue;
        CombatShield s;s.id=k.id;s.team=actor->team();s.owner=actor;s.held=true;s.live=true;s.blocksActors=false;
        s.N=heldBrollyNormal(*actor);s.C=heldBrollyCenter(*actor,s.N);s.R=0.8;s.hp=k.hp;s.hpMax=actor->weapon().number("canopyHp");
        result.push_back(std::move(s));
    }
    for(const auto& d:devices_)if(d.kind=="brolly" && d.live && d.owner) {
        CombatShield s;s.id=d.id;s.team=d.team;s.owner=d.owner;s.held=false;s.live=true;s.blocksActors=true;s.C=d.C;s.N=d.N;s.R=d.R;
        s.pos=d.pos;s.halfW=d.halfW;s.planeOff=d.planeOff;s.speed=d.speed;s.hp=d.hp;s.hpMax=d.hpMax;s.left=d.left;
        result.push_back(std::move(s));
    }
    return result;
}
std::size_t CombatWorld::attackCount(const PlayerSimulation& a) const {
    return std::count_if(projectiles_.begin(),projectiles_.end(),[&](const auto& p){return p.owner==&a;})+std::count_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.live && d.owner==&a;});
}
} // namespace restoration
