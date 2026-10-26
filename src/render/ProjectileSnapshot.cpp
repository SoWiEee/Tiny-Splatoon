#define GLM_ENABLE_EXPERIMENTAL
#include "ProjectileSnapshot.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace restoration {
ProjectileSnapshot SnapshotProjectile(const ShooterProjectile& p) {
    ProjectileSnapshot visual;
    visual.id=p.id;visual.pos=p.pos;visual.vel=p.vel;visual.start=p.start;visual.flightDir=p.flightDir;
    visual.hitPoint=p.hitPoint;visual.hitNormal=p.hitNormal;visual.age=p.age;visual.life=p.life;visual.delay=p.delay;
    visual.seed=p.seed;visual.distance=p.distance;visual.fuse=p.fuse;visual.size=p.size;visual.straight=p.straight;
    visual.gravity=p.gravity;visual.drag=p.drag;visual.team=p.owner?p.owner->team():-1;visual.tier=p.tier;
    visual.kind=p.kind;visual.weaponId=p.weaponId;visual.state=p.state;
    visual.look={p.look.size,p.look.tail0,p.look.tailK,p.look.wobble,p.look.frequency,p.look.nose,p.look.satellites};
    return visual;
}
ProjectileFrame BuildProjectileFrame(const CombatWorld& world) {
    ProjectileFrame frame;
    frame.time=world.time();
    const auto& shots=world.projectiles();
    frame.projectiles.reserve(shots.size());
    for(const auto& p:shots) {
        if(!p.owner)continue;
        frame.projectiles.push_back(SnapshotProjectile(p));
    }
    frame.streamEmitters.reserve(world.actors().size());
    frame.chargerSights.reserve(world.actors().size());
    for(const auto* actor:world.actors()) {
        if(!actor)continue;
        if(actor->state.alive&&!actor->state.squid&&actor->mainId()=="spinner"&&actor->combatPose().streaming) {
            const auto& weapon=actor->weapon();
            const ShooterProjectile* newest=nullptr;
            for(const auto& shot:shots)
                if(shot.owner==actor&&shot.weaponId=="spinner"&&shot.kind=="shot"&&(!newest||shot.age<newest->age))newest=&shot;
            InkStreamEmitter emitter;
            emitter.id=actor->networkNid()?actor->networkNid():static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(actor));
            emitter.active=true;emitter.team=actor->team();emitter.position=actor->muzzle();
            emitter.velocity=newest?newest->vel:actor->aimDirection()*weapon.number("speedMax",40);
            const double speed=glm::length(emitter.velocity);
            const double power=std::clamp((speed-weapon.number("speedMin",25))/std::max(1.0,weapon.number("speedMax",40)-weapon.number("speedMin",25)),0.0,1.0);
            emitter.straight=newest?newest->straight:.12;emitter.gravity=newest?newest->gravity:28;emitter.drag=newest?newest->drag:.8;
            emitter.life=std::clamp((weapon.number("rangeMin",10)+(weapon.number("rangeMax",21)-weapon.number("rangeMin",10))*power)/std::max(1.0,speed),.15,.7);
            emitter.seed=.37;emitter.radius=.10;frame.streamEmitters.push_back(emitter);
        }
        if(actor->state.alive&&actor->weapon().kind=="charger") {
            const auto& weapon=actor->weapon();
            const auto pose=actor->combatPose();
            if(!pose.charging)continue;
            const auto muzzle=actor->muzzle();
            auto direction=actor->input.aimPoint-muzzle;
            const auto fallback=glm::dvec3(std::sin(actor->input.aimYaw)*std::cos(actor->input.aimPitch),std::sin(actor->input.aimPitch),std::cos(actor->input.aimYaw)*std::cos(actor->input.aimPitch));
            direction=actor->input.hasAimPoint&&glm::length(direction)>=2&&glm::dot(direction,fallback)>=0?glm::normalize(direction):fallback;
            frame.chargerSights.push_back({muzzle,direction,weapon.number("rangeMin")+(weapon.number("rangeMax")-weapon.number("rangeMin"))*pose.charge,pose.charge,actor->team()});
        }
    }
    return frame;
}
}
