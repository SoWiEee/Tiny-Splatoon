#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include "NavGraph.hpp"
#include "BossSimulation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace restoration {

namespace {
glm::dvec3 heldBrollyNormal(const PlayerSimulation& actor) {
    const double pitch=std::clamp(actor.input.aimPitch,-0.5,0.45),cp=std::cos(pitch);
    return {std::sin(actor.input.aimYaw)*cp,std::sin(pitch),std::cos(actor.input.aimYaw)*cp};
}
glm::dvec3 heldBrollyCenter(const PlayerSimulation& actor,const glm::dvec3& normal) {
    return actor.state.pos+glm::dvec3(0,actor.state.smoothY+0.9,0)+normal*0.62;
}
}

CombatWorld::CombatWorld(const StageData& stage,const StageGeometry& geometry,FacePaint& paint)
    :stage_(stage),geometry_(geometry),paint_(paint),specials_(*this) {}

void CombatWorld::emitVisual(WorldVisualEvent event) {
    if (visualEvents_.size() == 512) visualEvents_.erase(visualEvents_.begin());
    if(networkVisualForwarder_&&!networkApplying_)networkVisualForwarder_(event);
    visualEvents_.push_back(std::move(event));
}

void CombatWorld::queueRemoteVisual(WorldVisualEvent event) {
    if(visualEvents_.size()==512)visualEvents_.erase(visualEvents_.begin());
    visualEvents_.push_back(std::move(event));
}

void CombatWorld::updateRemoteWorld(std::uint32_t owner,std::vector<ShooterProjectile> projectiles,std::vector<CombatDevice> devices,std::vector<WorldObjectView> objects) {
    auto found=std::find_if(remoteWorlds_.begin(),remoteWorlds_.end(),[&](const auto& world){return world.owner==owner;});
    if(found==remoteWorlds_.end()){remoteWorlds_.push_back({owner,std::move(projectiles),std::move(devices),std::move(objects)});found=std::prev(remoteWorlds_.end());}
    else {found->projectiles=std::move(projectiles);found->devices=std::move(devices);found->objects=std::move(objects);}
    for(auto& item:found->projectiles){item.networkRemote=true;item.networkOwner=owner;}
    for(auto& item:found->devices){item.networkRemote=true;item.networkOwner=owner;}
}

const std::vector<ShooterProjectile>& CombatWorld::projectiles() const {
    if(remoteWorlds_.empty())return projectiles_;
    projectileView_=projectiles_;
    for(const auto& remote:remoteWorlds_)projectileView_.insert(projectileView_.end(),remote.projectiles.begin(),remote.projectiles.end());
    return projectileView_;
}

const std::vector<CombatDevice>& CombatWorld::devices() const {
    if(remoteWorlds_.empty())return devices_;
    deviceView_=devices_;
    for(const auto& remote:remoteWorlds_)deviceView_.insert(deviceView_.end(),remote.devices.begin(),remote.devices.end());
    return deviceView_;
}

std::vector<WorldObjectView> CombatWorld::visualObjects() const {
    auto result=specials_.visualObjects();
    for(const auto& remote:remoteWorlds_)result.insert(result.end(),remote.objects.begin(),remote.objects.end());
    return result;
}

std::vector<WorldVisualEvent> CombatWorld::takeVisualEvents() {
    std::vector<WorldVisualEvent> out;
    out.swap(visualEvents_);
    return out;
}

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
    for (auto& event : hudEvents_) {
        if (event.attacker == &actor) event.attacker = nullptr;
        if (event.victim == &actor) event.victim = nullptr;
    }
    if (ticking_) {
        for (auto*& registered : actors_) if (registered == &actor) registered = nullptr;
    } else {
        actors_.erase(std::remove(actors_.begin(),actors_.end(),&actor),actors_.end());
    }
}

void CombatWorld::clear() {
    specials_.clear();
    visualEvents_.clear();
    hudEvents_.clear();
    ++hudEventEpoch_;
    hudEventSerial_ = 0;
    volleyBossHits_.clear();
    remoteWorlds_.clear();
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
        const auto path=p.pos-prev;const double distance=glm::length(path);
        const bool nativeInk=(p.kind=="shot"&&p.weaponId=="shooter")||(p.kind=="slosh"&&p.weaponId=="bucket")||(p.kind=="drop"&&(p.weaponId=="roller"||p.weaponId=="brush"));
        const auto& impactSource=nativeInk?p.weaponId:p.kind;
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
            if(p.kind!="blast")emitVisual({"bulletImpact",at,(nativeInk?-path:path)/(distance>1e-9?distance:1.0),p.radius,owner.team(),0,.08,nativeInk?p.seed:0.0,impactSource,p.id});
            if(p.kind=="blast") {const auto& w=owner.weapon();burst(owner,at,w.number("splashRadius"),w.number("splashDamageMax"),w.number("splashDamageMin"),w.number("impactRadius"),p.weaponId,e,p.special);}
            done=true;break;
        }
        if(!done && distance>0) done=blockShot(prev,p.pos,owner.team(),p.damage);
        if(!done&&boss_&&owner.team()==0&&distance>0){
            const auto bossHit=boss_->segmentHit(prev,p.pos,p.size*.6);
            if(bossHit){
                const auto wall=geometry_.raycast(prev,path/distance,distance,true);
                if(!wall.hit||bossHit->distance<wall.dist){
                    const int target=bossHit->crablet>=0?bossHit->crablet:-1;
                    const int prior=p.volley?static_cast<int>(std::count_if(volleyBossHits_.begin(),volleyBossHits_.end(),
                        [&](const auto& v){return v.volley==p.volley&&v.target==target;})):0;
                    if(prior<p.victimHitLimit){
                        if(p.volley)volleyBossHits_.push_back({p.volley,target});
                        double amount=p.damage;
                        if(p.kind=="drop")amount+=(p.damageFar-amount)*std::clamp(glm::length(p.start-bossHit->point)/7.0,0.0,1.0);
                        boss_->hit(owner,amount,*bossHit,p.weaponId);
                    }
                    if(p.kind!="blast")emitVisual({"bulletImpact",bossHit->point,(nativeInk?-path:path)/(distance>1e-9?distance:1.0),p.radius,owner.team(),0,.08,nativeInk?p.seed:0.0,impactSource,p.id});
                    if(p.kind=="blast"){
                        const auto& w=owner.weapon();
                        burst(owner,bossHit->point,w.number("splashRadius"),w.number("splashDamageMax"),w.number("splashDamageMin"),w.number("impactRadius"),p.weaponId,nullptr,p.special,true);
                    }
                    done=true;
                }
            }
        }
        if(!done && distance>0) {
            const auto hit=geometry_.raycast(prev,path/distance,distance,true);
            if(hit.hit) {
                SplatOptions options;options.stretch=glm::normalize(p.vel);options.hasStretch=true;options.stretchAmt=0.7;
                paint(owner,hit.point+hit.normal*0.14,p.radius*(0.85+owner.random()*0.3),p.seed,options,p.special);
                emitVisual({"bulletImpact",hit.point,hit.normal,p.radius,owner.team(),0,.08,nativeInk?p.seed:0.0,impactSource,p.id});
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
    volleyBossHits_.erase(std::remove_if(volleyBossHits_.begin(),volleyBossHits_.end(),[&](const auto& h){return std::none_of(projectiles_.begin(),projectiles_.end(),[&](const auto& p){return p.volley==h.volley;});}),volleyBossHits_.end());
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
        if(networkLocalPeer_&&actor->networkOwner()!=networkLocalPeer_){
            const auto pose=actor->combatPose();
            if(!actor->state.alive||actor->state.squid||pose.canopyState!="ready"||pose.canopyOpen<0.55||pose.canopyGrow<0.9)continue;
            CombatShield shield;shield.team=actor->team();shield.owner=actor;shield.held=true;shield.live=true;shield.blocksActors=false;
            shield.id=0;shield.N=heldBrollyNormal(*actor);shield.C=heldBrollyCenter(*actor,shield.N);shield.R=0.8;shield.hp=pose.canopyHp;shield.hpMax=pose.canopyHpMax;
            result.push_back(std::move(shield));continue;
        }
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
    for(const auto& remote:remoteWorlds_)for(const auto& d:remote.devices)if(d.kind=="brolly"&&d.live&&d.owner){
        CombatShield s;s.id=d.id;s.team=d.team;s.owner=d.owner;s.held=false;s.live=true;s.blocksActors=true;s.C=d.C;s.N=d.N;s.R=d.R;
        s.pos=d.pos;s.halfW=d.halfW;s.planeOff=d.planeOff;s.speed=d.speed;s.hp=d.hp;s.hpMax=d.hpMax;s.left=d.left;result.push_back(std::move(s));
    }
    return result;
}

std::size_t CombatWorld::attackCount(const PlayerSimulation& a) const {
    return std::count_if(projectiles_.begin(),projectiles_.end(),[&](const auto& p){return p.owner==&a;})+std::count_if(devices_.begin(),devices_.end(),[&](const auto& d){return d.live && d.owner==&a;});
}

} // namespace restoration
