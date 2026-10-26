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

void CombatWorld::launch(ShooterProjectile p) {
    if(!p.owner || !p.owner->state.alive) return;
    // shortcut: reuse the shot seed for cosmetic variation until the browser's separate visual RNG is ported.
    if(p.look.size==0) {
        if(p.kind=="blast")p.look={p.special?p.size*.77:.2,.5,.9,.085,17,.15,p.special?3:4};
        else if(p.kind=="slosh")p.look={p.size*.86,.7,1.5,.12,15,.1,p.size>=.2?2:1};
        else if(p.weaponId=="spinner")p.look={.08,.9,1.6,.025,30,.35,2};
        else if(p.weaponId=="brolly")p.look={.068,.55,1.1,.06,24,.2,1};
        else if(p.weaponId=="brush")p.look={.07+p.seed*.025,.4,1,.1,19,0,1};
        else if(p.weaponId=="blade")p.look={.044+p.seed*.04,.8,1.7,.07,22,.15,1};
        else if(p.kind=="drop")p.look={p.size,.4,1,.09,20,0,1};
        else p.look={p.special?p.size*.68:.1+p.seed*.012,.8,1.3,.035,26,.3,p.special?2:3};
    }
    if(!p.id)p.id=allocateId();
    (ticking_ ? pendingProjectiles_ : projectiles_).push_back(std::move(p));
}

void CombatWorld::tickBladeWave(ShooterProjectile& wave,double dt) {
    if(!wave.owner)return;
    auto& owner=*wave.owner;const auto previous=wave.pos;
    wave.age+=dt;const double step=glm::length(wave.vel)*dt;
    wave.pos+=wave.vel*dt;wave.distance+=step;
    if(blockCanopyShot(previous,wave.pos,owner.team(),wave.damage,&wave)
       || specials_.shotHit(previous,wave.pos,owner.team(),wave.damage,&owner)) {emitVisual({"blade_wave_stop",wave.pos,{0,1,0},0,owner.team(),0,.08,0,"blade",wave.id});wave.owner=nullptr;return;}
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
        emitVisual({"blade_wave_hit",wave.pos,-glm::normalize(wave.vel),.09,owner.team(),0,.08,0,"blade",wave.id});
    }
    if(blockShot(previous,wave.pos,owner.team(),damage)) {emitVisual({"blade_wave_stop",wave.pos,{0,1,0},0,owner.team(),0,.08,0,"blade",wave.id});wave.owner=nullptr;return;}
    const auto path=wave.pos-previous;const double length=glm::length(path);
    if(length>1e-8){const auto hit=geometry_.raycast(previous,path/length,length,true);if(hit.hit){emitVisual({"blade_wave_wall",hit.point,hit.normal,.1,owner.team(),0,.08,0,"blade",wave.id});wave.owner=nullptr;return;}}
    wave.trail+=step;
    if(wave.trail>=wave.trailEvery) {
        wave.trail=0;const auto ground=geometry_.raycast(wave.pos,{0,-1,0},wave.size*0.52+1.4,true);
        if(ground.hit) {SplatOptions options;options.stretch=f;options.hasStretch=true;options.stretchAmt=0.85;paint(owner,ground.point+ground.normal*0.08,wave.trailRadius,owner.random(),options,wave.special);}
    }
    if(wave.distance>=wave.range || wave.age>=wave.life || wave.pos.y<-3.4){
        if(wave.distance>=wave.range)emitVisual({"blade_wave_stop",wave.pos,{0,1,0},0,owner.team(),0,.08,0,"blade",wave.id});
        wave.owner=nullptr;
    }
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
    emitVisual({"bow_lodge",point,normal,.06,arrow.owner->team(),0,.08,0,"bow",arrow.id});
    SplatOptions options;options.stretch=arrow.flightDir;options.hasStretch=true;options.stretchAmt=0.5;
    paint(*arrow.owner,point+normal*0.08,arrow.stickPaint,arrow.seed,options,arrow.special);
}

void CombatWorld::burstArrow(ShooterProjectile& arrow) {
    auto& owner=*arrow.owner;
    const auto center=arrow.hitPoint+arrow.hitNormal*0.18;
    emitVisual({"bow_burst",center,arrow.hitNormal,arrow.burstRadius*(arrow.tier==2?.7:.55),owner.team(),double(arrow.tier),.08,0,"bow",arrow.id});
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
        emitVisual({"bow_impact",at,-arrow.flightDir,.07,owner.team(),1,.08,0,"bow",arrow.id});
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
                emitVisual({"bow_impact",hit.point,hit.normal,.07,owner.team(),0,.08,0,"bow",arrow.id});
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

} // namespace restoration
