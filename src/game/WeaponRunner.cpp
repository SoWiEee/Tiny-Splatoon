#include "WeaponRunner.hpp"
#include "PlayerSimulation.hpp"
#include "ArsenalData.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>
namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;
double lerp(double a,double b,double t) {return a+(b-a)*t;}
double smooth(double a,double b,double x) {double t=std::clamp((x-a)/(b-a),0.0,1.0);return t*t*(3-2*t);}
double angleDifference(double a,double b) {return std::remainder(b-a,2.0*pi);}
double bladeYaw(const PlayerSimulation& actor) {
    if (!actor.input.hasAimPoint) return actor.input.aimYaw;
    const auto delta = actor.input.aimPoint - actor.state.pos;
    if (delta.x*delta.x + delta.z*delta.z < 4.0) return actor.input.aimYaw;
    const double target = std::atan2(delta.x, delta.z);
    return std::abs(angleDifference(actor.input.aimYaw, target)) < 0.35 ? target : actor.input.aimYaw;
}
}
void WeaponRunner::reset() {
    cooldown_=bloom_=firingT_=charge_=chargeT_=flickRecover_=rollT_=burstT_=burstPower_=streamCd_=dodgeT_=rollLock_=0;
    flick_=-1; sinceRoll_=9; charging_=rolling_=turret_=rollQueued_=aimingSub_=previousSub_=false;
    rollsUsed_=0; twinSide_=swipeSide_=1; subCharge_=subChargeBudget_=0;subAimYaw_=0;subWagTime_=9;subLookSign_=0;previousJump_=false;rollHits_.clear();
    bladeSwing_={};bladeHeld_=bladeCharging_=bladeFull_=false;bladeHoldTime_=bladeCharge_=bladeCooldown_=bladeQueued_=bladeLunge_=bladeLungeSpeed_=0;bladeLungeDirection_=glm::dvec3(0);
    bladeStoreTime_=0;bowRing_=bowRingEvents_=0;
    bladeFlashTime_=99.0;sinceHand_={99.0,99.0};
    mittsCharging_=mittsLeaping_=false;mittsCharge_=mittsChargeTime_=mittsCooldown_=0;mittsLandTime_=99;mittsLeapVelocity_=glm::dvec3(0);mittsSide_=1;mittsLeapEvents_=0;
    canopy_=BrollyCanopy{};
}
WeaponProfile WeaponRunner::profile() const {
    const auto& w=actor_.weapon(); WeaponProfile p; p.kind=w.kind;
    p.range=w.number("range",w.number("rangeFull",8)); p.projectileSpeed=w.number("projSpeed",0);
    p.charges=p.kind=="charger" || p.kind=="spinner" || p.kind=="bow" || p.kind=="blade" || p.kind=="mitts";
    p.melee=p.kind=="roller" || p.kind=="brush" || p.kind=="blade" || p.kind=="mitts";
    return p;
}
bool WeaponRunner::busy() const {
    return charging_ || flick_ >= 0 || burstT_ > 0 || dodgeT_ > 0 || bladeSwing_.active || mittsLeaping_;
}
CombatPose WeaponRunner::pose() const {
    CombatPose p; p.firing=firingT_>0 || charging_ || flick_>=0 || rolling_ || burstT_>0 || turret_;
    p.charging=charging_;p.rolling=rolling_;p.subAim=aimingSub_;p.charge=charge_;p.firingTime=firingT_;p.dodgeTime=dodgeT_;
    p.shakerCharging=actor_.state.alive&&!actor_.state.squid&&aimingSub_&&actor_.subId()=="shaker";
    p.shakerCharge=p.shakerCharging?subCharge_:0.0;
    if(actor_.mainId()=="blade") {p.charging=bladeCharging_;p.charge=bladeCharge_;p.firing=p.firing || bladeSwing_.active;p.actionYaw=bladeSwing_.yaw;}
    if(actor_.mainId()=="mitts") {p.charging=mittsCharging_;p.charge=mittsCharge_;p.leaping=mittsLeaping_;p.mittsLeapEvents=mittsLeapEvents_;p.firing=p.firing || mittsLeaping_ || mittsCharging_ || mittsLandTime_<0.35;p.guard=firingT_>0.1;}
    if(actor_.mainId()=="bow") p.bowRing=bowRing_;
    p.bowRingEvents=bowRingEvents_;
    p.burstTime=burstT_;p.streaming=burstT_>0;p.turret=turret_;p.hand=twinSide_>0?1:0;
    p.lockTime=rollLock_;
    p.guard=p.guard || (canopy_.state==CanopyState::Ready && canopy_.open>=0.55 && canopy_.grow>=0.9);
    p.firing=p.firing || canopy_.open>0;
    p.sinceHand=sinceHand_;p.bladeFlashTime=bladeFlashTime_;p.mittsLandTime=mittsLandTime_;
    p.canopyOpen=canopy_.open;p.canopyGrow=canopy_.grow;p.canopyHp=canopy_.hp;
    p.canopyHpMax=actor_.mainId()=="brolly"?actor_.weapon().number("canopyHp"):0.0;
    p.canopyShake=canopy_.shake;p.canopyFlash=canopy_.flash;p.canopyOpening=canopy_.opening;
    p.canopyRegrowK=canopy_.regrowK;
    p.canopyState=canopy_.state==CanopyState::Ready?"ready":canopy_.state==CanopyState::Broken?"broken":"launched";
    return p;
}

glm::dvec3 WeaponRunner::muzzle(int side) const {
    // shortcut: use the live socket until CharacterPose exposes the source predictive aimReady blend.
    if(actor_.muzzleSource_) {
        const auto point=actor_.muzzleSource_(side);
        const auto center=actor_.state.pos+glm::dvec3(0,actor_.state.squid?0.4:1.05,0);
        if(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z)&&
           glm::length(point-center)<=std::sqrt(2.5)&&actor_.geometry_.los(center,point))return point;
    }
    auto m=actor_.state.pos+glm::dvec3(0,1.05,0)+actor_.aimDir()*0.3;
    if(side) {auto right=glm::dvec3(actor_.aimDir().z,0,-actor_.aimDir().x);if(glm::length(right)>1e-6) m+=glm::normalize(right)*(side>0?0.085:-0.085);}
    return m;
}
glm::dvec3 WeaponRunner::direction(glm::dvec3 from) const {
    auto dir=actor_.aimDir();
    if(actor_.input.hasAimPoint) {auto to=actor_.input.aimPoint-from;const double n=glm::length(to);if(n>=2 && glm::dot(to,dir)>=0) dir=to/n;}
    return dir;
}
glm::dvec3 WeaponRunner::scatter(glm::dvec3 dir,double degrees) {
    if(degrees<=0) return dir;
    double radius=degrees*pi/180*std::sqrt(actor_.random()),angle=actor_.random()*2*pi;
    auto side=glm::dvec3(-dir.z,0,dir.x);if(glm::length(side)<1e-4)side={1,0,0};side=glm::normalize(side);
    auto up=glm::cross(dir,side);
    return glm::normalize(dir+side*(std::cos(angle)*std::tan(radius))+up*(std::sin(angle)*std::tan(radius)*0.55));
}
glm::dvec3 WeaponRunner::ballistic(glm::dvec3 from,glm::dvec3 dir,double speed,double straight,double gravity,double drag,double range) const {
    if(!actor_.input.hasAimPoint) return dir;
    const auto to=actor_.input.aimPoint-from;const double hd=std::hypot(to.x,to.z),hdir=std::hypot(dir.x,dir.z);
    if(hd<1.5 || hd>range || !gravity || hdir<1e-4) return dir;
    auto endpoint=[&](double pitch) {
        double vh=std::cos(pitch)*speed,vy=std::sin(pitch)*speed,x=0,y=0,age=0;
        for(int i=0;i<90;++i) {const double px=x,py=y;age+=1.0/60;
            if(age>straight) {vy-=gravity/60;vh*=1-drag/60;vy*=1-drag/60;}
            x+=vh/60;y+=vy/60;
            if(x>=hd) return py+(y-py)*(hd-px)/std::max(1e-6,x-px);
            if(vh<0.5) break;
        }return -1000.0;
    };
    const double base=std::atan2(dir.y,hdir);double p0=base,e0=endpoint(p0)-to.y;
    if(std::abs(e0)<0.01)return dir;
    double p1=p0-std::atan2(e0,hd),e1=endpoint(p1)-to.y;
    for(int i=0;i<4 && std::abs(e1)>0.005;++i) {double diff=e1-e0;if(std::abs(diff)<1e-6)break;double p2=p1-e1*(p1-p0)/diff;p0=p1;e0=e1;p1=std::clamp(p2,-1.2,1.2);e1=endpoint(p1)-to.y;}
    if(std::abs(e1)>0.25 || std::abs(p1-base)>0.35)return dir;
    return {dir.x/hdir*std::cos(p1),std::sin(p1),dir.z/hdir*std::cos(p1)};
}
double WeaponRunner::spread() const {
    const auto& w=actor_.weapon();
    if(w.kind=="blaster")return actor_.state.grounded?w.number("spread",1.2):w.number("spreadAir",4);
    if(w.kind=="twins" && turret_) return w.number("turretSpread");
    double base=w.number(actor_.state.grounded?"spreadGround":"spreadAir",0);
    if(w.kind=="spinner")return base;
    return base*lerp(w.kind=="twins"?0.5:w.number("spreadFirst",0.45),1,bloom_);
}
bool WeaponRunner::spend(double ink) {
    if(actor_.state.ink<ink)return false;
    actor_.state.ink-=ink;actor_.lastFire_=0;return true;
}
void WeaponRunner::fireRound(double cone,const std::string& kind,double power,int side) {
    const auto& w=actor_.weapon();ShooterProjectile p;p.owner=&actor_;p.weaponId=actor_.mainId();p.pos=p.start=muzzle(side);
    p.radius=w.number("impactRadius");p.damage=w.number(kind=="blast"?"directDamage":"damage");p.damageFar=p.damage;p.kind=kind;
    double speed=w.number("projSpeed",0),range=w.number("range",0);
    p.straight=w.number("straightTime",0);p.trailEvery=w.number("trailEvery",0);p.trailRadius=w.number("trailRadius",0);
    if(w.kind=="spinner") {speed=lerp(w.number("speedMin"),w.number("speedMax"),power);p.straight=lerp(w.number("straightMin"),w.number("straightMax"),power);range=lerp(w.number("rangeMin"),w.number("rangeMax"),power);p.life=1.3;p.size=0.12;}
    if(w.kind=="twins") {if(!side){speed=w.number("turretProjSpeed");p.straight=w.number("turretStraight");range=w.number("turretRange");}p.size=0.13;}
    p.trail=-( (w.kind=="spinner"?2.2:w.kind=="twins"?2.4:2.5)-p.trailEvery );
    if(kind=="blast") {p.life=range/speed;p.straight=99;p.gravity=p.drag=0;p.size=0.26;p.trail=-1.5;p.trailEvery=2.2;p.trailRadius=0.45;}
    if(w.kind=="twins")p.look={side?.088:.086,side?.8:.9,side?1.3:1.5,side?.03:.025,side?28.0:30.0,side?.3:.35,2};
    auto dir=direction(p.pos);if(kind!="blast")dir=ballistic(p.pos,dir,speed,p.straight,p.gravity,p.drag,range);
    p.vel=scatter(dir,cone)*speed;p.seed=actor_.random();actor_.world_->launch(std::move(p));++actor_.shotsFired_;
    CharacterTrigger event{w.kind=="spinner"?"stream":"shoot"}; event.hand=w.kind=="twins"?(twinSide_>0?1:0):0;
    if(w.kind=="twins") sinceHand_[static_cast<std::size_t>(event.hand)]=0.0;
    actor_.triggerPose(event);
}
double WeaponRunner::moveSpeed() const {
    const auto& w=actor_.weapon();const double firing=w.number("moveSpeedFiring",6);
    if(w.kind=="brolly" && canopy_.open>0.2)return w.number("moveSpeedShield");
    if(w.kind=="bow" && charging_)return lerp(4.5,w.number("moveSpeedDrawing"),std::min(1.0,charge_*2.5));
    if(w.kind=="bow" && firingT_>0)return w.number("moveSpeedFiring");
    if(w.kind=="blade" && charging_)return w.number("moveSpeedCharging");
    if(w.kind=="mitts" && charging_)return w.number("moveSpeedCharging");
    if(w.kind=="blade") {
        if(bladeCharging_)return w.number("moveSpeedCharging");
        if(bladeSwing_.active && bladeSwing_.heavy && bladeSwing_.time<0.3)return firing*0.55;
        if(firingT_>0)return firing;
    }
    if(w.kind=="mitts") {
        if(mittsLeaping_)return std::max(std::hypot(mittsLeapVelocity_.x,mittsLeapVelocity_.z),4.2);
        if(mittsCharging_)return w.number("moveSpeedCharging");
        if(mittsLandTime_<0.25)return w.number("moveSpeedCharging")*2;
        return firingT_>0?firing:6;
    }
    if(w.kind=="brush")return rolling_?lerp(firing,w.number("brushSpeed"),smooth(0,0.35,rollT_)):firingT_>0?firing:6;
    if(w.kind=="bucket" && flick_>=0)return firing*0.7;
    if(w.kind=="spinner") {if(burstT_>0)return firing;if(charging_)return lerp(4.5,w.number("moveSpeedCharging"),std::min(1.0,charge_*2));return 6;}
    if(rolling_)return lerp(w.number("rollSpeed")*0.5,w.number("rollSpeed"),smooth(0,0.45,rollT_));
    if(flick_>=0 && w.kind=="roller")return lerp(firing,firing*0.45,std::clamp(flick_/w.number("flickWindup"),0.0,1.0));
    if(flickRecover_>0)return lerp(6,firing*0.6,flickRecover_/0.18);
    if(charging_)return lerp(4.2,firing,std::min(1.0,charge_*3));
    return firingT_>0?firing:6;
}
void WeaponRunner::tick(double dt,bool fire,bool pressed) {
    const bool sub=actor_.input.sub && !actor_.state.squid;
    const bool subReleased=previousSub_ && !sub;
    tick(dt,fire,pressed,sub,subReleased);
}
void WeaponRunner::tick(double dt,bool fire,bool pressed,bool sub,bool subReleased) {
    sinceHand_[0]+=dt;sinceHand_[1]+=dt;
    const auto& w=actor_.weapon();cooldown_-=dt;firingT_=std::max(0.0,firingT_-dt);flickRecover_=std::max(0.0,flickRecover_-dt);
    if(!fire)bloom_=std::max(0.0,bloom_-dt/w.number("bloomRecover",0.28));
    if(w.kind=="shooter" || w.kind=="blaster") shooter(dt,fire);
    else if(w.kind=="charger")charger(fire,dt);
    else if(w.kind=="roller" || w.kind=="brush")roller(fire,pressed,dt,w.kind=="brush");
    else if(w.kind=="bucket")bucket(fire,dt);
    else if(w.kind=="spinner")spinner(fire,dt);
    else if(w.kind=="twins")twins(fire,dt);
    else if(w.kind=="brolly")brolly(fire,dt);
    else if(w.kind=="bow")bow(fire,dt);
    else if(w.kind=="blade")blade(fire,pressed,dt);
    else if(w.kind=="mitts")mitts(fire,dt);
    if(sub && !aimingSub_) {
        aimingSub_=true;
        if(actor_.subId()=="shaker") {subCharge_=0;subChargeBudget_=0.1;subAimYaw_=actor_.input.aimYaw;subWagTime_=9;subLookSign_=0;}
    }
    const auto& subDef=actor_.subWeapon();
    const double previousSubCharge = subCharge_;
    if(sub && actor_.subId()=="shaker" && actor_.state.ink>=subDef.number("inkCost")) {
        const double move=std::clamp((std::hypot(actor_.state.vel.x,actor_.state.vel.z)-1.0)/5.0,0.0,1.0);
        double add=(dt/subDef.number("chargeTime"))*(1.0+subDef.number("moveBoost")*move);
        if(actor_.input.jump && !previousJump_)add+=subDef.number("jumpBoost");
        subWagTime_+=dt;
        const double aimDelta=angleDifference(subAimYaw_,actor_.input.aimYaw);subAimYaw_=actor_.input.aimYaw;
        if(dt>0 && std::abs(aimDelta)/dt>3.0) {
            const int sign=aimDelta>0?1:-1;
            if(sign!=subLookSign_) {
                if(subLookSign_!=0 && subWagTime_>0.05) {add+=subDef.number("shakeBoost");subWagTime_=0;}
                subLookSign_=sign;
            }
        }
        subChargeBudget_=std::min(0.3,subChargeBudget_+subDef.number("maxRate")*dt);
        add=std::min(add,subChargeBudget_);subChargeBudget_-=add;subCharge_=std::min(1.0,subCharge_+add);
    }
    if (actor_.subId() == "shaker") {
        const auto level = [](double charge) { return charge >= 1.0 ? 3 : charge >= 0.5 ? 2 : 1; };
        const int oldLevel = level(previousSubCharge), newLevel = level(subCharge_);
        if (aimingSub_ && newLevel > oldLevel) {
            WorldVisualEvent event;
            event.kind = "special_audio";
            event.source = newLevel >= 3 ? "shaker_clink_max" : "shaker_clink";
            event.pos = actor_.state.pos + glm::dvec3(0.0, 1.2, 0.0);
            event.team = actor_.team();
            event.charge = newLevel;
            event.sourceLocal = actor_.isLocal();
            actor_.world_->emitVisual(std::move(event));
        }
    }
    if(aimingSub_)actor_.fireFacing_=0.3;
    if(subReleased && aimingSub_ && !actor_.state.squid) {
        aimingSub_=false;
        if(actor_.world_->canDeploy(actor_,actor_.subId()) && spend(subDef.number("inkCost"))) {
            actor_.world_->deploy(actor_,actor_.subId(),false,subCharge_); actor_.triggerPose({"throw"});
        }
        subCharge_=subChargeBudget_=0;
    }
    if(!sub && !previousSub_)aimingSub_=false;
    previousSub_=sub;previousJump_=actor_.input.jump;
    actor_.firingT_=firingT_;
}

void WeaponRunner::fireBrolly() {
    auto& a=actor_;const auto& w=a.weapon();
    if(!spend(w.number("inkPerShot"))) {cooldown_=std::max(cooldown_,0.2);return;}
    constexpr double pattern[11][2]={{0,0},{0.42,0.4},{0.42,1.9707963267948966},{0.42,3.541592653589793},{0.42,5.11238898038469},{0.9,0},{0.9,1.0471975511965976},{0.9,2.0943951023931953},{0.9,3.141592653589793},{0.9,4.1887902047863905},{0.9,5.235987755982989}};
    const auto m=muzzle();const auto base=direction(m);auto right=glm::cross(base,glm::dvec3(0,1,0));
    if(glm::length(right)<1e-6)right={1,0,0};else right=glm::normalize(right);
    const auto up=glm::normalize(glm::cross(right,base));
    const double cone=std::tan(w.number(a.state.grounded?"spreadDeg":"spreadAir")*pi/180.0),turn=a.random()*2*pi;
    const auto volley=a.world_->newVolley();
    for(int i=0;i<static_cast<int>(w.number("pellets")) && i<11;++i) {
        const double angle=pattern[i][1]+turn,jx=(a.random()-0.5)*0.18,jy=(a.random()-0.5)*0.18;
        const double ox=(pattern[i][0]*std::cos(angle)+jx)*cone,oy=(pattern[i][0]*std::sin(angle)+jy)*cone*0.72;
        const auto dir=glm::normalize(base+right*ox+up*oy);
        ShooterProjectile p;p.owner=&a;p.weaponId="brolly";p.kind="shot";p.pos=p.start=m;
        p.vel=dir*(w.number("projSpeed")*(0.985+a.random()*0.03));p.life=w.number("pelletLife");
        p.straight=w.number("straightTime");p.gravity=w.number("pelletGrav");p.drag=w.number("pelletDrag");
        p.radius=w.number("pelletPaint")*(0.85+a.random()*0.3);p.damage=w.number("pelletDamage");p.damageFar=w.number("pelletDamageFar");p.size=w.number("pelletSize");
        p.volley=volley;p.victimHitLimit=11;p.seed=a.random();a.world_->launch(std::move(p));
    }
    ++a.shotsFired_;firingT_=0.35;a.fireFacing_=0.5;cooldown_=w.number("fireInterval");a.triggerPose({"shoot"});
}

void WeaponRunner::brolly(bool fire,double dt) {
    const auto& w=actor_.weapon();auto& k=canopy_;
    k.flash=std::max(0.0,k.flash-dt*6.0);k.shake*=std::exp(-dt*12.0);
    if(!k.id)k.id=actor_.world_->allocateId();
    if(k.hp<=0 && k.state==CanopyState::Ready)k.hp=w.number("canopyHp");
    if(k.state!=CanopyState::Ready) {
        k.regrowTime=std::max(0.0,k.regrowTime-dt);
        k.regrowK=std::clamp(1.0-k.regrowTime/w.number("regrowTime"),0.0,1.0);
        if(k.regrowTime<=0) {k.state=CanopyState::Ready;k.hp=w.number("canopyHp");k.grow=0.001;}
    } else if(k.grow<1)k.grow=std::min(1.0,k.grow+dt/0.32);
    if(k.state==CanopyState::Ready && k.open<=0 && actor_.world_->time()-k.lastHit>w.number("canopyRegenDelay"))
        k.hp=std::min(w.number("canopyHp"),k.hp+w.number("canopyRegen")*dt);

    const bool canHold=actor_.state.alive && !actor_.state.squid && k.state==CanopyState::Ready && k.grow>0.6 && !k.lockRelease;
    if(fire&&!k.previousFire) {k.pressTime=0;k.shot=false;}
    if(fire)k.pressTime+=dt;else k.lockRelease=false;
    bool want=false;
    if(fire&&!k.lockRelease) {
        if(canHold) {
            if(!k.shot && cooldown_<=0 && k.open<0.05 && k.pressTime<=w.number("openDelay")+0.06) {fireBrolly();k.shot=true;}
            want=k.pressTime>=w.number("openDelay");
        } else if(cooldown_<=0)fireBrolly();
    }
    k.previousFire=fire;
    const double wasOpen=k.open;
    if(want)k.open=std::min(1.0,k.open+dt/w.number("openTime"));
    else k.open=std::max(0.0,k.open-dt/w.number("closeTime"));
    k.opening=k.open>wasOpen||(k.open>=1.0&&k.opening);
    if(k.open>0) {actor_.fireFacing_=0.5;actor_.lastFire_=0;firingT_=std::max(firingT_,0.2);}
    if(k.state!=CanopyState::Ready || !actor_.state.alive || actor_.state.squid)k.openTime=0;
    else if(k.open>=1 && want) {
        k.openTime+=dt;
        if(k.openTime>=w.number("launchHold")) {
            auto dir=actor_.aimDir();dir.y=0;if(glm::length(dir)<1e-6)dir={0,0,1};else dir=glm::normalize(dir);
            actor_.world_->launchCanopy(actor_,std::max(k.hp,w.number("canopyHp")*0.5),dir,actor_.state.smoothY+0.9);
            k.state=CanopyState::Launched;k.regrowTime=w.number("regrowTime");k.regrowK=0;k.grow=0;k.open=0;k.openTime=0;k.lockRelease=true;
        }
    } else k.openTime=0;
}

void WeaponRunner::bow(bool fire,double dt) {
    const auto& w=actor_.weapon();
    if(fire && cooldown_<=0) {
        if(!charging_) {
            if(actor_.state.ink<w.number("inkFull")*w.number("inkMin"))return;
            charging_=true;charge_=chargeT_=0;bowRing_=0;
        }
        const double maxCharge=std::clamp(actor_.state.ink/w.number("inkFull"),0.0,1.0);
        chargeT_=std::min(1.0,chargeT_+dt/w.number("chargeTime"));
        const double before=charge_;
        charge_=std::min(maxCharge,chargeT_);actor_.fireFacing_=0.4;actor_.lastFire_=0;
        if(bowRing_<1 && before<w.number("ring1") && charge_>=w.number("ring1")) {bowRing_=1;++bowRingEvents_;}
        if(bowRing_<2 && before<0.999 && charge_>=0.999) {bowRing_=2;++bowRingEvents_;}
    } else if(charging_) {
        charging_=false;
        const double releasedCharge=charge_;
        if(spend(w.number("inkFull")*std::max(w.number("inkMin"),releasedCharge)))fireBow(releasedCharge);
        charge_=chargeT_=0;bowRing_=0;firingT_=0.35;cooldown_=w.number("cooldown");
    }
}

void WeaponRunner::fireBow(double charge) {
    actor_.triggerPose({"charge_release"});
    auto& a=actor_;const auto& w=a.weapon();
    double range=0,speed=0,damage=0,sideDamage=0,fan=0,fuse=0,burstRadius=0,burstDamage=0,burstEdge=0,burstPaint=0;
    bool lodge=false;int tier=0;
    if(charge>=0.999) {
        range=w.number("flightFull");speed=w.number("speedFull");damage=w.number("damageFull");sideDamage=w.number("sideFull");fan=w.number("fanFull");
        tier=2;lodge=true;fuse=w.number("fuseFull");burstRadius=w.number("burstRadius.1");burstDamage=w.number("burstDamage.1");
        burstEdge=w.number("burstEdge.1");burstPaint=w.number("burstPaint.1");
    } else if(charge>=w.number("ring1")) {
        const double u=(charge-w.number("ring1"))/(1.0-w.number("ring1"));
        range=lerp(w.number("flightRing.0"),w.number("flightRing.1"),u);speed=lerp(w.number("speedRing.0"),w.number("speedRing.1"),u);
        damage=lerp(w.number("damageRing.0"),w.number("damageRing.1"),u);sideDamage=damage*w.number("sideMul");fan=w.number("fanRing");
        tier=1;lodge=true;fuse=w.number("fuseRing");burstRadius=w.number("burstRadius.0");burstDamage=w.number("burstDamage.0");
        burstEdge=w.number("burstEdge.0");burstPaint=w.number("burstPaint.0");
    } else {
        const double u=std::clamp(charge/w.number("ring1"),0.0,1.0);
        range=lerp(w.number("flightTap.0"),w.number("flightTap.1"),u);speed=w.number("speedTap");
        damage=lerp(w.number("damageTap.0"),w.number("damageTap.1"),u);sideDamage=damage*w.number("sideMul");fan=w.number("fanTap");
    }
    const auto m=muzzle();auto dir=ballistic(m,direction(m),speed,w.number("straight"),w.number("grav"),0.0,range);
    const bool air=!a.state.grounded;
    glm::dvec3 axis{0,1,0};
    if(air) {axis=glm::cross(dir,glm::dvec3(0,1,0));if(glm::length(axis)<1e-4)axis={1,0,0};else axis=glm::normalize(axis);}
    const double fanRad=fan*pi/180.0;const auto volley=a.world_->newVolley();
    for(int i=-1;i<=1;++i) {
        auto arrowDir=dir;
        if(i)arrowDir=glm::normalize(dir*std::cos(i*fanRad)+glm::cross(axis,dir)*std::sin(i*fanRad)+axis*glm::dot(axis,dir)*(1-std::cos(i*fanRad)));
        ShooterProjectile p;p.owner=&a;p.weaponId="bow";p.kind="arrow";p.state="flying";p.pos=p.start=m;p.vel=arrowDir*speed;
        p.age=0;p.life=3;p.straight=w.number("straight");p.gravity=w.number("grav");p.drag=w.number("diveDrag");p.diveGravity=w.number("diveGrav");
        p.range=range;p.distance=0;p.damage=i?sideDamage:damage;p.damageFar=p.damage;p.radius=w.number("paintTap");p.size=0.11;
        p.tier=tier;p.lodge=lodge;p.fuse=fuse;p.burstRadius=burstRadius;p.burstDamage=burstDamage;p.burstEdge=burstEdge;p.burstPaint=burstPaint;
        p.burstInner=w.number("burstInner");p.stickPaint=w.number("paintStick");p.trailEvery=w.number("trailEvery");p.trailRadius=w.number("trailRadius");
        p.trail=-1.8;p.flightDir=arrowDir;p.volley=volley+static_cast<std::uint64_t>(i+1);p.centerArrow=i==0;p.seed=a.random();
        a.world_->launch(std::move(p));
    }
    ++a.shotsFired_;a.fireFacing_=0.4;
}

void WeaponRunner::blade(bool fire,bool pressed,double dt) {
    auto& a=actor_;const auto& w=a.weapon();
    bladeFlashTime_+=dt;
    bladeCooldown_=std::max(0.0,bladeCooldown_-dt);bladeQueued_=std::max(0.0,bladeQueued_-dt);
    // The source advances an existing swing before processing this frame's trigger edge.
    if(bladeSwing_.active) {
        const double before=bladeSwing_.time;bladeSwing_.time+=dt;
        if(!bladeSwing_.fired && bladeSwing_.time>=(bladeSwing_.heavy?0.05:0.028)) {
            bladeSwing_.fired=true;
            if(bladeSwing_.heavy)fireBladeWave();else fireBladeDrops();
        }
        if(bladeSwing_.heavy && !bladeSwing_.stroke && bladeSwing_.time>=0.075) {
            bladeSwing_.stroke=true;
            const auto f=glm::dvec3(std::sin(bladeSwing_.yaw),0,std::cos(bladeSwing_.yaw));
            for(double d:{0.75,1.35}) {
                const auto origin=a.state.pos+f*d+glm::dvec3(0,0.8,0);
                const auto hit=a.world_->geometry().raycast(origin,{0,-1,0},2.2,true);
                if(hit.hit) {SplatOptions o;o.stretch=f;o.hasStretch=true;o.stretchAmt=0.9;a.world_->paint(a,hit.point+hit.normal*0.08,w.number("strokeRadius"),a.random(),o);}
            }
        }
        if(bladeSwing_.time>=0.01 && before<= (bladeSwing_.heavy?0.16:0.12))bladeMelee();
        if(bladeSwing_.time>0.4)bladeSwing_.active=false;
    }
    if(bladeLunge_>0) {
        bladeLunge_=std::max(0.0,bladeLunge_-dt);
        if(bladeLunge_>0) {const double u=bladeLunge_/std::max(0.01,w.number("lungeTime")*0.5);
            const double speed=bladeLungeSpeed_*(0.55+0.45*std::min(1.0,u));a.state.vel.x=bladeLungeDirection_.x*speed;a.state.vel.z=bladeLungeDirection_.z*speed;}
    }
    const bool canAct=!a.state.squid && a.kidT_>=PlayerTuning().number("emergeDelay") && a.state.alive;
    if(!canAct) {
        if(bladeCharging_) {
            bladeStoreTime_+=dt;
            if(!a.input.fire || bladeStoreTime_>w.number("chargeStore")) {
                bladeCharging_=bladeFull_=false;bladeCharge_=0;bladeStoreTime_=0;
            }
        }
        bladeHeld_=bladeHeld_ && a.input.fire;
        return;
    }
    bladeStoreTime_=0;
    if(pressed) {
        if(bladeCooldown_<=0) {
            if(a.state.ink>=w.number("tapInk")) {
                a.state.ink-=w.number("tapInk");a.lastFire_=0;bladeSide_=-bladeSide_;
                bladeSwing_={};bladeSwing_.active=true;bladeSwing_.yaw=bladeYaw(a);bladeSwing_.pitch=a.input.aimPitch;
                bladeCooldown_=w.number("tapInterval");firingT_=0.4;a.fireFacing_=0.45;
                CharacterTrigger event{"blade_slash"};event.side=bladeSide_;event.amount=bladeSide_;a.triggerPose(event);
            } else bladeCooldown_=0.18;
        } else bladeQueued_=0.16;
        bladeHeld_=true;bladeHoldTime_=0;bladeCharging_=false;bladeCharge_=0;bladeFull_=false;
    }
    if(fire && bladeHeld_) {
        bladeHoldTime_+=dt;
        if(!bladeCharging_ && bladeHoldTime_>=w.number("chargeDelay") && !(bladeSwing_.active && bladeSwing_.heavy)) {
            bladeCharging_=true;bladeCharge_=0;bladeFull_=false;
        }
        if(bladeCharging_) {
            const double cap=a.state.ink>=w.number("heavyInk")?1.0:0.94;
            bladeCharge_=std::min(cap,bladeCharge_+dt/w.number("chargeTime"));a.fireFacing_=std::max(a.fireFacing_,0.35);a.lastFire_=0;firingT_=std::max(firingT_,0.3);
            bladeFull_=bladeCharge_>=1.0;
        }
    } else if(bladeHeld_ && !fire) {
        bladeHeld_=false;
        if(bladeCharging_) {
            const double charge=bladeCharge_;bladeCharging_=false;bladeCharge_=0;bladeFull_=false;
            if(charge>=1.0) {
                a.state.ink=std::max(0.0,a.state.ink-w.number("heavyInk"));a.lastFire_=0;
                bladeSwing_={};bladeSwing_.active=true;bladeSwing_.heavy=true;bladeSwing_.yaw=bladeYaw(a);bladeSwing_.pitch=a.input.aimPitch;
                bladeCooldown_=w.number("heavyInterval");firingT_=0.5;a.fireFacing_=0.6;bladeFlashTime_=0.0;
                a.triggerPose({"blade_heavy"});
                bladeLunge_=0;
                if(a.state.grounded) {
                    bool safe=true;const auto f=glm::dvec3(std::sin(bladeSwing_.yaw),0,std::cos(bladeSwing_.yaw));
                    for(double k:{0.5,1.0,1.4})if(!std::isfinite(a.world_->geometry().groundHeight(a.state.pos.x+f.x*w.number("lungeDist")*k,a.state.pos.z+f.z*w.number("lungeDist")*k,a.state.pos.y+0.6)))safe=false;
                    if(safe){bladeLunge_=w.number("lungeTime");bladeLungeDirection_=f;bladeLungeSpeed_=w.number("lungeDist")/(w.number("lungeTime")*0.75);}
                }
            } else if(charge>=0.3) {
                if(bladeCooldown_<=0.04) {bladeCooldown_=0;if(a.state.ink>=w.number("tapInk")){a.state.ink-=w.number("tapInk");a.lastFire_=0;bladeSide_=-bladeSide_;bladeSwing_={};bladeSwing_.active=true;bladeSwing_.yaw=bladeYaw(a);bladeSwing_.pitch=a.input.aimPitch;bladeCooldown_=w.number("tapInterval");CharacterTrigger event{"blade_slash"};event.side=bladeSide_;event.amount=bladeSide_;a.triggerPose(event);}}
                else bladeQueued_=0.16;
            }
        }
    }
    if(bladeQueued_>0 && bladeCooldown_<=0 && !bladeCharging_ && !bladeSwing_.active && a.state.ink>=w.number("tapInk")) {
        bladeQueued_=0;a.state.ink-=w.number("tapInk");a.lastFire_=0;bladeSide_=-bladeSide_;bladeSwing_={};bladeSwing_.active=true;bladeSwing_.yaw=bladeYaw(a);bladeSwing_.pitch=a.input.aimPitch;bladeCooldown_=w.number("tapInterval");
        CharacterTrigger event{"blade_slash"};event.side=bladeSide_;event.amount=bladeSide_;a.triggerPose(event);
    }
}

void WeaponRunner::fireBladeDrops() {
    auto& a=actor_;const auto& w=a.weapon();const double yaw=bladeSwing_.yaw,pitch=a.input.aimPitch;
    const bool steep=pitch<-0.55;const double up=steep?pitch*0.85:std::clamp(pitch,-0.4,0.45)*0.75+0.06;
    const auto volley=a.world_->newVolley();
    for(int i=0;i<static_cast<int>(w.number("tapDrops"));++i) {
        const double t=i/std::max(1.0,w.number("tapDrops")-1),off=bladeSide_*(t-0.5)*w.number("tapSpreadDeg")*pi/180.0;
        const double ang=yaw+off,ox=std::sin(ang),oz=std::cos(ang),mid=1-std::abs(t-0.5)*2;
        const double speed=w.number("tapSpeed")*(0.8+0.24*mid)*(0.95+a.random()*0.1)*(steep?0.45:1.0);
        ShooterProjectile p;p.owner=&a;p.weaponId="blade";p.kind="drop";p.life=1.2;p.straight=steep?0:w.number("tapStraight");p.delay=t*0.018;
        p.radius=w.number("tapDropRadius")*(0.85+a.random()*0.3);p.damage=w.number("tapDamageNear");p.damageFar=w.number("tapDamageFar");p.size=0.13;
        p.trail=-0.2;p.trailEvery=1.25;p.trailRadius=0.3;p.gravity=24;p.drag=0.5;p.volley=volley;p.seed=a.random();p.victimHitLimit=1;
        p.centerArrow=i==static_cast<int>(w.number("tapDrops"))/2;
        const double r=steep?0.55:0.8;p.pos=p.start=a.state.pos+glm::dvec3(ox*r,(steep?0.7:0.98)+(0.5-t)*0.12,oz*r);
        const double cu=std::cos(up+(a.random()-0.5)*0.05),tx=-oz*bladeSide_,tz=ox*bladeSide_;
        const double hx=ox+tx*0.12,hz=oz+tz*0.12,hl=std::hypot(hx,hz);
        p.vel={hx/hl*cu*speed,std::sin(up)*speed,hz/hl*cu*speed};a.world_->launch(std::move(p));
    }
    ++a.shotsFired_;
}

void WeaponRunner::fireBladeWave() {
    auto& a=actor_;const auto& w=a.weapon();const double pitch=std::abs(bladeSwing_.pitch)<0.3?bladeSwing_.pitch*0.4:std::clamp(bladeSwing_.pitch,-0.45,0.35),cp=std::cos(pitch),yaw=bladeSwing_.yaw;
    const glm::dvec3 dir(std::sin(yaw)*cp,std::sin(pitch),std::cos(yaw)*cp);
    ShooterProjectile p;p.owner=&a;p.weaponId="blade";p.kind="bladewave";p.pos=p.start=a.state.pos+glm::dvec3(std::sin(yaw)*0.55,w.number("waveHeight")*0.52,std::cos(yaw)*0.55);
    p.vel=dir*w.number("waveSpeed");p.damage=w.number("waveDamage");p.damageFar=w.number("waveDamageFar");p.range=w.number("waveRange");p.radius=w.number("waveWidth")*0.5;
    p.size=w.number("waveHeight");p.life=p.range/w.number("waveSpeed");p.trailEvery=w.number("wavePaintEvery");p.trailRadius=w.number("wavePaintRadius");p.trail=0.3;a.world_->launch(std::move(p));++a.shotsFired_;
}

void WeaponRunner::bladeMelee() {
    auto& a=actor_;const auto& w=a.weapon();const double reach=w.number("reach"),half=(bladeSwing_.heavy?w.number("heavyArcDeg"):w.number("meleeArcDeg"))*pi/360.0;
    const double fx=std::sin(bladeSwing_.yaw),fz=std::cos(bladeSwing_.yaw),damage=bladeSwing_.heavy?w.number("heavyMelee"):w.number("tapMelee");
    const auto from=a.state.pos+glm::dvec3(0,1,0);
    for(auto* e:a.world_->actors()) {
        if(!e || e==&a || e->team()==a.team() || !e->state.alive || std::find(bladeSwing_.hits.begin(),bladeSwing_.hits.end(),e)!=bladeSwing_.hits.end())continue;
        const auto delta=e->state.pos-a.state.pos;const double d=std::hypot(delta.x,delta.z);if(d>reach+e->hitRadius())continue;
        const double ey=delta.y+e->state.smoothY,h=e->hitHeight();
        if(ey>(bladeSwing_.heavy?1.9:1.55) || ey+h<(bladeSwing_.heavy?-0.9:-0.35))continue;
        const double fwd=delta.x*fx+delta.z*fz,lat=std::abs(delta.x*fz-delta.z*fx),ang=std::atan2(lat,fwd);
        if(ang>half && !(d<e->hitRadius()+0.4 && fwd>-0.15))continue;
        const auto to=e->visualPos()+glm::dvec3(0,std::min(h*0.6,0.85),0);
        if(!a.world_->geometry().los(from,to))continue;
        bladeSwing_.hits.push_back(e);
        if(a.world_->blockShot(from,to,a.team(),damage))continue;
        a.world_->applyHit(a,*e,damage,"blade");
    }
}

void WeaponRunner::mitts(bool fire,double dt) {
    auto& a=actor_;const auto& w=a.weapon();mittsCooldown_-=dt;mittsLandTime_+=dt;
    if(mittsCharging_) {
        mittsChargeTime_+=dt;const double cap=std::clamp((a.state.ink-w.number("leapInkMin"))/(w.number("leapInkMax")-w.number("leapInkMin")),0.0,1.0);
        mittsCharge_=std::min(cap,mittsChargeTime_/w.number("leapChargeTime"));a.lastFire_=0;a.fireFacing_=0.5;
        if(!a.input.jump)releaseMittsLeap();
    }
    if(mittsLeaping_) {
        if(a.state.grounded) {
            mittsLeaping_=false;mittsLandTime_=0;a.state.invuln=std::max(a.state.invuln,w.number("landInvuln"));
            for(auto* e:a.world_->actors())if(e && e->team()!=a.team() && e->state.alive && glm::length(e->state.pos-a.state.pos)<=w.number("gloveRadius")+0.38 && std::abs(e->state.pos.y-a.state.pos.y)<1.6)a.world_->applyHit(a,*e,w.number("gloveDamage"),"mitts");
            a.world_->burst(a,a.state.pos+glm::dvec3(0,0.35,0),w.number("landRadius"),w.number("landDamageMax"),w.number("landDamageMin"),w.number("landPaint"),"mitts");
        } else {a.lastFire_=0;a.fireFacing_=0.3;}
    }
    if(!fire || mittsCharging_ || mittsLeaping_) { if(mittsCooldown_<0)mittsCooldown_=0;return; }
    firingT_=0.35;a.fireFacing_=0.5;
    for(int guard=0;mittsCooldown_<=0 && guard<2;++guard) {
        if(a.state.ink<w.number("inkPerPunch")){mittsCooldown_=w.number("punchInterval");break;}
        a.state.ink-=w.number("inkPerPunch");a.lastFire_=0;mittsSide_=1-mittsSide_;fireMittsFist(mittsSide_);mittsCooldown_+=w.number("punchInterval");
    }
}

void WeaponRunner::fireMittsFist(int hand) {
    sinceHand_[static_cast<std::size_t>(hand)]=0.0;
    CharacterTrigger event{"shoot"}; event.hand=hand; actor_.triggerPose(event);
    auto& a=actor_;const auto& w=a.weapon();const auto m=muzzle(hand?1:-1);auto dir=direction(m);
    if(glm::length(dir)<1e-8)dir={0,0,1};else dir=glm::normalize(dir);
    ShooterProjectile p;p.owner=&a;p.weaponId="mitts";p.kind="fist";p.pos=p.start=m;p.vel=dir*w.number("fistSpeed");p.life=w.number("fistRange")/w.number("fistSpeed");
    p.range=w.number("fistRange");p.radius=w.number("fistPaint");p.size=w.number("fistSize");p.damage=w.number("punchDamage");p.damageFar=w.number("splashMin");p.burstRadius=w.number("splashRadius");p.burstDamage=w.number("splashMax");p.burstEdge=w.number("splashMin");p.trail=-0.8;p.seed=a.random();
    a.world_->launch(std::move(p));++a.shotsFired_;
}

void WeaponRunner::releaseMittsLeap() {
    auto& a=actor_;const auto& w=a.weapon();const double charge=mittsCharge_;mittsCharging_=false;mittsCharge_=mittsChargeTime_=0;
    a.state.ink=std::max(0.0,a.state.ink-lerp(w.number("leapInkMin"),w.number("leapInkMax"),charge));a.lastFire_=0;
    const double yaw=a.input.aimYaw,angle=std::clamp(w.number("leapAngle")*pi/180.0+a.input.aimPitch*w.number("leapPitchK"),w.number("leapAngleMin")*pi/180.0,w.number("leapAngleMax")*pi/180.0);
    const double speed=std::sqrt(lerp(w.number("leapSpeedMin")*w.number("leapSpeedMin"),w.number("leapSpeedMax")*w.number("leapSpeedMax"),charge));
    const double vy=std::min(w.number("leapVyMax"),speed*std::sin(angle)),horizontal=speed*std::cos(angle);
    mittsLeapVelocity_={std::sin(yaw)*horizontal,vy,std::cos(yaw)*horizontal};a.state.vel=mittsLeapVelocity_;a.state.grounded=false;mittsLeaping_=true;mittsLandTime_=0;++mittsLeapEvents_;a.triggerPose({"jump"});
}

double WeaponRunner::damageTaken(double amount,const PlayerSimulation* attacker) const {
    if(actor_.mainId()!="mitts")return amount;
    const auto& w=actor_.weapon();
    if(mittsLeaping_ || mittsLandTime_<w.number("leapArmorGrace"))return amount*w.number("leapArmor");
    if(attacker && attacker!=&actor_ && firingT_>0.35-w.number("guardAfter")) {
        const auto delta=attacker->state.pos-actor_.state.pos;const double len=std::hypot(delta.x,delta.z);
        if(len>1e-5 && (std::sin(actor_.input.aimYaw)*delta.x+std::cos(actor_.input.aimYaw)*delta.z)/len>std::cos(w.number("guardArc")*pi/180.0))return amount*w.number("guardArmor");
    }
    return amount;
}

void WeaponRunner::shooter(double,bool fire) {
    const auto& w=actor_.weapon();if(!fire){cooldown_=std::max(0.0,cooldown_);return;}
    firingT_=0.35;actor_.fireFacing_=0.5;
    for(int guard=0;cooldown_<=0 && guard<3;++guard) {
        if(!spend(w.number("inkPerShot"))){cooldown_+=w.number("fireInterval");break;}
        fireRound(spread(),w.kind=="blaster"?"blast":"shot");bloom_=std::min(1.0,bloom_+w.number("bloomPerShot",0.3));cooldown_+=w.number("fireInterval");
    }
}
void WeaponRunner::charger(bool fire,double dt) {
    const auto& w=actor_.weapon();
    if(fire && cooldown_<=0) {
        if(!charging_){if(actor_.state.ink<w.number("inkFull")*0.2)return;charging_=true;charge_=chargeT_=0;}
        chargeT_=std::min(1.0,chargeT_+dt/w.number("chargeTime"));
        charge_=std::min(std::clamp(actor_.state.ink/w.number("inkFull"),0.0,1.0),chargeT_<0.2?chargeT_*1.25:0.25+(chargeT_-0.2)*0.9375);actor_.fireFacing_=0.4;
    } else if(charging_) {
        charging_=false;const double c=std::max(0.12,charge_);actor_.state.ink=std::max(0.0,actor_.state.ink-w.number("inkFull")*c);actor_.lastFire_=0;
        const auto m=muzzle();const double damage=c>=0.999?w.number("damageMax"):lerp(w.number("damageMin"),w.number("damageMax")*0.62,c);
        actor_.world_->beam(actor_,m,direction(m),lerp(w.number("rangeMin"),w.number("rangeMax"),c),0.14,damage,w.number("lineRadius")*(0.8+c*0.4),false,"charger");
        ++actor_.shotsFired_;charge_=chargeT_=0;firingT_=0.35;cooldown_=0.28;actor_.triggerPose({"charge_release"});
    }
}
void WeaponRunner::bucket(bool fire,double dt) {
    const auto& w=actor_.weapon();
    if(flick_>=0){flick_+=dt;actor_.fireFacing_=0.45;if(flick_>=w.number("windup")){flick_=-1;fireSlosh();cooldown_=w.number("fireInterval")-w.number("windup");firingT_=0.35;}return;}
    if(fire && cooldown_<=0 && spend(w.number("inkPerShot")))flick_=0;
}
void WeaponRunner::spinner(bool fire,double dt) {
    const auto& w=actor_.weapon();
    if(burstT_>0){burstT_-=dt;streamCd_-=dt;firingT_=0.2;actor_.fireFacing_=0.5;actor_.lastFire_=0;
        for(int guard=0;streamCd_<=0 && guard<4;++guard){fireRound(spread(),"shot",burstPower_);streamCd_+=w.number("fireInterval");}
        if(burstT_<=0){burstT_=0;cooldown_=0.2;}return;}
    if(fire && cooldown_<=0){if(!charging_){if(actor_.state.ink<w.number("inkFull")*0.15)return;charging_=true;charge_=chargeT_=0;}
        chargeT_=std::min(1.0,chargeT_+dt/w.number("chargeTime"));charge_=std::min(std::clamp(actor_.state.ink/w.number("inkFull"),0.0,1.0),chargeT_);actor_.fireFacing_=0.4;
    }else if(charging_){charging_=false;double c=std::max(0.15,charge_);actor_.state.ink=std::max(0.0,actor_.state.ink-w.number("inkFull")*c);burstPower_=c;burstT_=lerp(w.number("burstMin"),w.number("burstMax"),c);streamCd_=charge_=chargeT_=0;}
}
bool WeaponRunner::tryDodge() {
    auto& a=actor_;const auto& w=a.weapon();
    if(w.kind=="mitts") {
        if((mittsCharging_ || mittsLeaping_) && a.input.jump)return true;
        if(!a.input.fire || !a.state.grounded || a.state.squid || a.state.ink<w.number("leapInkMin"))return false;
        mittsCharging_=true;mittsCharge_=mittsChargeTime_=0;a.jumpBuffer_=0;return true;
    }
    if(w.kind!="twins")return false;
    if(dodgeT_>0 && a.input.fire && rollLock_<=0 && rollsUsed_>0 && rollsUsed_<w.number("rollCharges")){rollQueued_=true;return true;}
    if(!a.input.fire || dodgeT_>0 || rollLock_>0 || !a.state.grounded || a.state.squid)return false;
    const double n=std::hypot(a.input.move.x,a.input.move.z);if(n<0.3 || !spend(w.number("rollInk")))return false;
    dodgeDir_={a.input.move.x/n,0,a.input.move.z/n};dodgeT_=w.number("rollTime");turret_=false;sinceRoll_=0;
    CharacterTrigger event{"dodge"}; const double cy=std::cos(a.state.yaw),sy=std::sin(a.state.yaw);
    event.direction={dodgeDir_.x*cy-dodgeDir_.z*sy,0,dodgeDir_.x*sy+dodgeDir_.z*cy};event.duration=w.number("rollTime");a.triggerPose(event);
    if(++rollsUsed_>=w.number("rollCharges")){rollLock_=w.number("rollLockout");rollsUsed_=0;}return true;
}
void WeaponRunner::cancelDodge() {
    dodgeT_=0;rollLock_=0;rollQueued_=false;rollsUsed_=0;turret_=false;dodgeDir_=glm::dvec3(0);
}
bool WeaponRunner::bodyOverride(double dt) {
    if(dodgeT_>0) {const auto& w=actor_.weapon();
        double u=1-dodgeT_/w.number("rollTime"),speed=w.number("rollDist")/(0.7*w.number("rollTime"))*(1-0.6*u);
        actor_.state.vel.x=dodgeDir_.x*speed;actor_.state.vel.z=dodgeDir_.z*speed;actor_.integrate(dt,false);return true;
    }
    if(mittsLeaping_) {actor_.state.vel.x=mittsLeapVelocity_.x;actor_.state.vel.z=mittsLeapVelocity_.z;actor_.integrate(dt,false);return true;}
    if(bladeLunge_>0) {actor_.state.vel.x=bladeLungeDirection_.x*bladeLungeSpeed_;actor_.state.vel.z=bladeLungeDirection_.z*bladeLungeSpeed_;actor_.integrate(dt,false);return true;}
    return false;
}
void WeaponRunner::twins(bool fire,double dt) {
    const auto& w=actor_.weapon();rollLock_=std::max(0.0,rollLock_-dt);sinceRoll_+=dt;
    if(rollLock_<=0 && sinceRoll_>w.number("rollReset") && dodgeT_<=0)rollsUsed_=0;
    if(turret_ && (!fire || std::hypot(actor_.input.move.x,actor_.input.move.z)>0.3))turret_=false;
    if(dodgeT_>0){dodgeT_-=dt;if(dodgeT_<=0){dodgeT_=0;turret_=fire;if(rollQueued_){rollQueued_=false;if(tryDodge())return;}}return;}
    if(!fire){cooldown_=std::max(0.0,cooldown_);return;}firingT_=0.35;actor_.fireFacing_=0.5;
    for(int guard=0;cooldown_<=0 && guard<3;++guard){if(!spend(w.number("inkPerShot"))){cooldown_+=w.number("fireInterval");break;}twinSide_=-twinSide_;fireRound(spread(),"shot",1,turret_?0:twinSide_);bloom_=std::min(1.0,bloom_+0.25);cooldown_+=w.number(turret_?"turretInterval":"fireInterval");}
}
void WeaponRunner::roller(bool fire,bool pressed,double dt,bool brush) {
    auto& a=actor_;const auto& w=a.weapon();
    if(flick_>=0){flick_+=dt;a.fireFacing_=0.4;if(flick_>=(brush?0.04:w.number("flickWindup"))){flick_=-1;if(brush){fireSwipe();cooldown_=w.number("swipeInterval");}else{fireFlick();cooldown_=w.number("flickInterval")-w.number("flickWindup");flickRecover_=0.18;}firingT_=0.25;}return;}
    const double hs=std::hypot(a.state.vel.x,a.state.vel.z);const bool moving=a.state.grounded && hs>2 && std::hypot(a.input.move.x,a.input.move.z)>0.3;
    if((pressed || (brush && fire && !moving)) && cooldown_<=0 && spend(w.number(brush?"swipeInk":"flickInk"))){flick_=0;if(brush){swipeSide_=-swipeSide_;rolling_=false;}return;}
    bool roll=fire && a.state.grounded && a.state.ink>(brush?0.3:0.5) && (brush?moving:cooldown_<=0.25);
    rollT_=roll?rollT_+dt:0;if(roll!=rolling_){rolling_=roll;if(roll){lastRollPos_=a.state.pos;if(brush)rollHits_.clear();}}
    if(!rolling_)return;a.lastFire_=0;
    const glm::dvec3 fwd(std::sin(a.state.yaw),0,std::cos(a.state.yaw)),right(fwd.z,0,-fwd.x);
    for(auto* e:a.world_->actors()) {
        if(e->team()==a.team() || !e->state.alive)continue;
        auto delta=e->state.pos-a.state.pos;double f=glm::dot(delta,fwd),lat=std::abs(glm::dot(delta,right));
        if(f>-0.2 && f<(brush?1.2:1.35) && lat<w.number(brush?"brushWidth":"rollWidth")/2+(brush?0.3:0.35) && std::abs(delta.y)<1.2 && (brush || hs>1)) {
            auto it=rollHits_.find(e);double last=it==rollHits_.end()?-9:it->second;
            if(a.world_->time()-last>(brush?w.number("brushHitCd"):0.5)){rollHits_[e]=a.world_->time();a.world_->applyHit(a,*e,w.number(brush?"brushDamage":"rollDamage"),a.mainId());}
        }
    }
    const double moved=glm::length(a.state.pos-lastRollPos_);if(moved<(brush?0.22:0.28))return;
    lastRollPos_=a.state.pos;a.state.ink=std::max(0.0,a.state.ink-w.number(brush?"brushInkPerMeter":"rollInkPerMeter")*moved);
    SplatOptions opt;opt.stretch=fwd;opt.hasStretch=true;opt.stretchAmt=brush?0.5:1;opt.kind=brush?SplatKind::Shot:SplatKind::Roll;
    if(brush)a.world_->paint(a,a.state.pos+fwd*0.65+glm::dvec3(0,0.35,0),w.number("brushWidth")*0.45,a.random(),opt);
    else for(int i=-1;i<=1;++i)a.world_->paint(a,a.state.pos+fwd*0.75+right*(i*w.number("rollWidth")*0.33)+glm::dvec3(0,0.35,0),0.62,a.random(),opt);
}
void WeaponRunner::fireFlick() {
    actor_.triggerPose({"flick"});
    auto& a=actor_;const auto& w=a.weapon();const auto fwd=glm::dvec3(std::sin(a.state.yaw),0,std::cos(a.state.yaw));const double up=std::clamp(a.input.aimPitch,-0.2,0.5)+0.32;
    for(int i=0;i<w.number("flickDrops");++i){double t=i/(w.number("flickDrops")-1)*2-1,angle=a.state.yaw+t*w.number("flickSpreadDeg")*pi/360+(a.random()-0.5)*0.05;
        double speed=w.number("flickSpeed")*(0.82+0.28*(1-std::abs(t))+a.random()*0.08);ShooterProjectile p;p.owner=&a;p.weaponId=a.mainId();p.kind="drop";p.life=1.4;p.straight=0;p.radius=0.85+a.random()*0.3;p.damage=w.number("flickDamageNear");p.damageFar=w.number("flickDamageFar");p.trail=0;p.trailEvery=1.8;p.trailRadius=0.45;p.gravity=26;p.drag=0.4;p.seed=a.random();p.pos=p.start=a.state.pos+fwd*0.6+glm::dvec3(0,1.3,0);
        double cu=std::cos(up+(a.random()-0.5)*0.12);p.vel={std::sin(angle)*cu*speed,std::sin(up)*speed,std::cos(angle)*cu*speed};a.world_->launch(std::move(p));}
    ++a.shotsFired_;
}
void WeaponRunner::fireSlosh() {
    actor_.triggerPose({"slosh"});
    auto& a=actor_;const auto& w=a.weapon();auto m=muzzle(),dir=direction(m);double hd=std::hypot(dir.x,dir.z);if(hd==0)hd=1;double pitch=std::atan2(dir.y,hd)+w.number("lob");
    if(a.input.hasAimPoint){auto to=a.input.aimPoint-m;double dist=std::hypot(to.x,to.z);
        if(dist>=1 && dist<=w.number("reach")){
            auto f=[&](double angle){double vh=std::cos(angle)*w.number("throwSpeed"),vy=std::sin(angle)*w.number("throwSpeed"),x=0,y=0;for(int i=0;i<180;++i){double px=x,py=y;vy-=w.number("gravity")/60;vh*=1-w.number("drag")/60;vy*=1-w.number("drag")/60;x+=vh/60;y+=vy/60;if(x>=dist)return py+(y-py)*(dist-px)/std::max(1e-6,x-px)-to.y;if(y<to.y-8)break;}return -1000.0;};
            double fp=f(-0.6);for(double p=-0.58;p<=1.2;p+=0.02){double v=f(p);if(fp<0 && v>=0){double lo=p-0.02,hi=p;for(int k=0;k<10;++k){double mid=(lo+hi)/2;if(f(mid)>=0)hi=mid;else lo=mid;}pitch=hi;break;}fp=v;}
        }
    }
    dir={dir.x/hd*std::cos(pitch),std::sin(pitch),dir.z/hd*std::cos(pitch)};auto volley=a.world_->newVolley();
    for(int i=0;i<w.number("blobs");++i){double speed=w.number("throwSpeed")*(1-i*0.075),side=(a.random()-0.5)*0.05,pit=pitch+(a.random()-0.5)*0.04,yaw=std::atan2(dir.x,dir.z)+side;ShooterProjectile p;p.owner=&a;p.weaponId=a.mainId();p.kind="slosh";p.life=2.2;p.straight=0;p.radius=w.number("impactRadius")*(1-i*0.08);p.damage=w.number("damage");p.belowFalloff=w.number("belowFalloff");p.belowFalloffMax=w.number("belowFalloffMax");p.size=0.22-i*0.02;p.trail=0;p.trailEvery=1.6;p.trailRadius=0.42;p.gravity=w.number("gravity");p.drag=w.number("drag");p.seed=a.random();p.volley=volley;p.pos=p.start=m-dir*(i*0.12);p.vel={std::sin(yaw)*std::cos(pit)*speed,std::sin(pit)*speed,std::cos(yaw)*std::cos(pit)*speed};a.world_->launch(std::move(p));}
    ++a.shotsFired_;
}
void WeaponRunner::fireSwipe() {
    CharacterTrigger event{"flick"}; event.side=swipeSide_;event.amount=swipeSide_; actor_.triggerPose(event);
    auto& a=actor_;const auto& w=a.weapon();double fx=std::sin(a.state.yaw),fz=std::cos(a.state.yaw),up=std::clamp(a.input.aimPitch,-0.3,0.3)*0.5+0.08,arc=w.number("swipeSpreadDeg")*pi/180*1.6;
    for(int i=0;i<w.number("swipeDrops");++i){double t=i/std::max(1.0,w.number("swipeDrops")-1),angle=a.state.yaw-swipeSide_*(t-0.5)*arc,ox=std::sin(angle),oz=std::cos(angle),tx=-oz*swipeSide_,tz=ox*swipeSide_;double speed=w.number("swipeSpeed")*(0.8+a.random()*0.3);
        ShooterProjectile p;p.owner=&a;p.weaponId=a.mainId();p.kind="drop";p.life=1;p.straight=0;p.radius=0.55+a.random()*0.2;p.damage=w.number("swipeDamageNear");p.damageFar=w.number("swipeDamageFar");p.size=0.1;p.trail=0;p.trailEvery=2.2;p.trailRadius=0.32;p.gravity=26;p.drag=0.6;p.seed=a.random();p.pos=p.start=a.state.pos+glm::dvec3(ox*0.85,0.45,oz*0.85);double hx=ox*0.8+tx*0.45+fx*0.25,hz=oz*0.8+tz*0.45+fz*0.25,hl=std::hypot(hx,hz),cu=std::cos(up+(a.random()-0.5)*0.08);p.vel={hx/hl*cu*speed,std::sin(up)*speed,hz/hl*cu*speed};a.world_->launch(std::move(p));}
    ++a.shotsFired_;
}
} // namespace restoration
