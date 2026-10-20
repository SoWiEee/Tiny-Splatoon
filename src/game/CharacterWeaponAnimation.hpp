#pragma once
#include "CharacterData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace restoration {
struct CharacterWeaponKitState {
    bool charging=false,cling=false,leaping=false,opening=false;
    bool hasSinceHand=false;
    double bowDraw=0.0,charge=0.0,landT=99.0,flashT=99.0;
    double open=0.0,grow=1.0,shake=0.0,hp=1.0,hpMax=1.0,flash=0.0,regrowK=0.0;
    std::array<double,2> sinceHand{99.0,99.0};
    std::string state="ready";
};

struct CharacterWeaponAnimationInput {
    std::string weapon;
    double t=0.0,dt=0.0,sinceShoot=99.0,sinceFlick=99.0,sinceRelease=99.0;
    double charge=0.0,chargeFlash=0.0,rolling=0.0,groundSpeed=0.0;
    bool full=false,firing=false,grounded=true,near=true;
    int hand=0;
    glm::dvec3 color{1.0};
    std::optional<CharacterRunnerState> runner;
    std::optional<CharacterWeaponKitState> kit;
    std::optional<glm::dquat> worldQuaternion;
};

// Per held instance, like animateWeapon's state stored on the source weapon object.
class CharacterWeaponAnimation {
public:
    double pump=0.0,trig=0.0;
    // Bow draw shifts the off group along its local +Z axis; the pose owner rotates this by inHand.rotation.
    glm::dvec3 offPositionOffset{0.0};
    void tick(const CharacterWeaponAnimationInput& input) {
        st_=input;st_.dt=std::clamp(input.dt,0.0,0.1);
        const auto& id=st_.weapon;const double dt=st_.dt,u=st_.sinceShoot;
        if(id=="bow") { tickBow(); return; }
        if(id=="mitts") { tickMitts(); return; }
        if(id=="blade") { tickBlade(); return; }
        if(id=="brolly") { tickBrolly(); return; }
        if(id=="roller"||id=="brush") {
            if(st_.rolling>0.3&&st_.grounded) drumW_=st_.groundSpeed/0.1;
            else drumW_*=std::exp(-dt*2.2);
            if(st_.sinceFlick>=0.15&&st_.sinceFlick-dt<0.15) drumW_+=34.0;
            drumA_+=drumW_*dt;
        }
        if(id=="blaster") {
            pump=u>=0.6?0.0:u<0.14?0.0:u<0.29?mj((u-0.14)/0.15):u<0.33?1.0:
                u<0.46?1.0-mj((u-0.33)/0.13):-0.1*std::sin(3.14159265358979323846*std::min(1.0,(u-0.46)/0.12));
        }
        if(id=="spinner") {
            const double wantSpin=u<0.12?38.0:st_.charge*30.0;
            spinW_=damp(spinW_,wantSpin,wantSpin>spinW_?6.0:1.6,dt);
            spinA_=std::fmod(spinA_+spinW_*dt,6.28318530717958647692);
        }
        if(!st_.near) return;
        if(id=="shooter"||id=="blaster"||id=="charger") {
            const bool want=id=="charger"?st_.charge>0.01:id=="blaster"?u<0.07:st_.firing&&u<0.16;
            trig=damp(trig,want?1.0:0.0,(want?1.0:0.0)>trig?45.0:22.0,dt);
        }
        if(id=="blaster") {
            const bool depressed=u<0.36;
            needle_=spring(0,depressed?-1.95:1.3,depressed?9.0:4.2,depressed?0.55:0.28);
            if(u<dt*1.5) ps_[3]-=7.0f;
            if(u>=0.33&&u-dt<0.33) ps_[3]+=4.5f;
            bulb_=std::clamp(spring(2,0.0,7.0,0.22),-0.3,0.3);
        } else if(id=="charger") {
            const double target=-0.03*std::clamp(st_.charge,0.0,1.0);
            bolt_=std::clamp(spring(4,target,target<ps_[4]?5.0:16.0,0.32),-0.034,0.004);
        } else if(id=="bucket") {
            if(st_.worldQuaternion) {
                const auto q=*st_.worldQuaternion;
                const auto axis=q*glm::dvec3(0.0,1.0,0.0);
                const auto from=glm::normalize(axis);const double scalar=from.y+1.0;
                const auto correction=scalar<1e-8?glm::dquat(0.0,0.0,-from.z,from.y):
                    glm::normalize(glm::dquat(scalar,-from.z,0.0,from.x));
                const auto local=glm::inverse(q)*correction*q;
                auto vector=glm::dvec3(local.x,local.y,local.z);const double length=glm::length(vector);
                double angle=2.0*std::atan2(length,local.w);
                if(angle>3.14159265358979323846) angle-=6.28318530717958647692;
                vector=length>1e-5?vector/length:glm::dvec3(1.0,0.0,0.0);
                angle=std::clamp(angle,-0.6,0.6);
                spring(6,vector.x*angle,2.2,0.16);spring(8,vector.z*angle,2.2,0.16);
            }
        }
    }

    void apply(std::vector<CharacterPartPose>& parts,const std::string& side) const {
        const std::string prefix="weapon/"+side+"/";
        const auto part=[&](const std::string& key)->CharacterPartPose* {
            const auto found=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.name==prefix+key;});
            return found==parts.end()?nullptr:&*found;
        };
        const auto rotation=[&](const char* key,double angle,glm::dvec3 axis) {
            if(auto* p=part(key)) p->local.rotation=glm::angleAxis(angle,axis);
        };
        if(st_.weapon=="bow") { applyBow(parts,side,part); return; }
        if(st_.weapon=="mitts") { applyMitts(parts,side,part); return; }
        if(st_.weapon=="blade") { applyBlade(parts,side,part); return; }
        if(st_.weapon=="brolly") { applyBrolly(parts,side,part); return; }
        if(auto* drum=part("drum")) drum->local.rotation=glm::angleAxis(drumA_,glm::dvec3(1,0,0));
        if(st_.weapon=="spinner") rotation("spin",spinA_,{0,0,1});
        for(const char* key:{"body","ink","bodyFar","inkFar"}) if(auto* p=part(key))
            p->local.visible=(std::string(key).find("Far")==std::string::npos)?st_.near:!st_.near;
        const auto partPrefix=prefix+"parts/";
        for(auto& p:parts) if(p.name.rfind(partPrefix,0)==0&&p.name.find('/',partPrefix.size())==std::string::npos)
            p.local.visible=st_.near;
        if(!st_.near) return;
        rotation("parts/trigger",0.42*trig,{1,0,0});
        const double u=st_.sinceShoot,k=pulse(u,0.006,30.0);
        if(st_.weapon=="shooter") {
            if(auto* p=part("parts/bolt")) p->local.position.z-=0.0095*k;
            const double ck=pulse(u,0.01,18.0);
            if(auto* p=part("parts/can")) p->local.scale={1.0+0.07*ck,1.0+0.07*ck,1.0-0.035*ck};
        } else if(st_.weapon=="twins") {
            for(const char* key:{"parts/slide","parts/slideInk"}) if(auto* p=part(key))
                p->local.position.z-=0.013*pulse(u,0.005,26.0);
        } else if(st_.weapon=="blaster") {
            if(auto* p=part("parts/pump")) p->local.position.z-=0.036*pump;
            rotation("parts/needle",needle_+0.018*std::sin(st_.t*41.0)*(u>0.6?1.0:0.3),{0,0,1});
            if(auto* p=part("parts/bulb")) p->local.scale={1.0-0.35*bulb_,1.0-0.35*bulb_,1.0+0.9*bulb_};
        } else if(st_.weapon=="charger") {
            if(auto* p=part("parts/bolt")) p->local.position.z+=bolt_;
        } else if(st_.weapon=="bucket") {
            if(auto* p=part("parts/surface")) {
                const auto qx=glm::angleAxis(static_cast<double>(ps_[6]),glm::dvec3(1,0,0));
                const auto qz=glm::angleAxis(static_cast<double>(ps_[8]),glm::dvec3(0,0,1));
                p->local.rotation=qx*qz;
                const double drain=u<0.6?(u<0.16?mj(u/0.16):1.0-mj((u-0.16)/0.44)):0.0;
                p->local.position.y+=-0.034*drain+0.002*std::sin(st_.t*7.3);
                const double ripple=1.0+0.02*std::sin(st_.t*11.0+1.3)*(0.3+drain);
                p->local.scale={ripple*(1.0-0.1*drain),1.0,(2.0-ripple)*(1.0-0.1*drain)};
            }
            rotation("parts/lever",-0.4*(u<0.3?1.0-mj(std::max(0.0,u-0.18)/0.12):0.0),{1,0,0});
        }
    }
private:
    void tickBow() {
        constexpr double drawLen=0.22;
        const double hand=std::max(0.0,st_.kit?st_.kit->bowDraw:0.0);
        const bool drawing=st_.runner?st_.runner->charging:st_.charge>0.01;
        const double charge=st_.runner?st_.runner->charge:st_.charge;
        const double rel=st_.sinceRelease;
        if(rel<bowLastRelease_-1e-4) { bowLoose_=true;bowStringVelocity_=0.0; }
        bowLastRelease_=rel;
        if(drawing) bowLoose_=false;
        if(bowLoose_) {
            constexpr double omega=6.28318530717958647692*15.0,zeta=0.3;
            const double wd=omega*std::sqrt(1.0-zeta*zeta),e=std::exp(-zeta*omega*st_.dt);
            const double c=std::cos(wd*st_.dt),sn=std::sin(wd*st_.dt),x0=bowString_,v0=bowStringVelocity_;
            const double b=(v0+zeta*omega*x0)/wd;
            bowString_=e*(x0*c+b*sn);
            bowStringVelocity_=e*((-zeta*omega*x0+wd*b)*c+(-zeta*omega*b-wd*x0)*sn);
            bowString_=std::max(-0.16,bowString_);
            if(rel>0.3&&hand<0.03&&std::abs(bowString_)<0.02) bowLoose_=false;
        } else { bowString_=hand;bowStringVelocity_=0.0; }
        const double ring=charge>=0.999?2.0:charge>=0.45?1.0:0.0;
        const double want=drawing?0.35+1.4*charge+(ring>=1.0?1.4:0.0)+(ring==2.0?2.2+0.9*std::sin(st_.t*30.0):0.0):0.35;
        bowGlow_+=(want-bowGlow_)*(1.0-std::exp(-st_.dt*18.0));
        const double full=ring==2.0?1.0:0.0;
        bowCoilCharge_=drawing?charge:0.0;bowCoilFull_=drawing?full:0.0;
        bowChargeFlash_=st_.chargeFlash;
        offPositionOffset=st_.hand==0?glm::dvec3(0.0,0.0,drawLen*hand):glm::dvec3(0.0);
    }

    void tickMitts() {
        if(!mittsInitialized_) { mittsState_.fill(0.0f);mittsState_[0]=1.0f;mittsState_[2]=1.0f;mittsLastPunch_=99.0;mittsInitialized_=true; }
        const int hand=std::clamp(st_.hand,0,1);
        const double since=st_.kit&&st_.kit->hasSinceHand?st_.kit->sinceHand[static_cast<std::size_t>(hand)]:st_.sinceShoot;
        if(since<mittsLastPunch_) mittsState_[1]-=7.5f;
        mittsLastPunch_=since;
        const auto* kit=st_.kit?&*st_.kit:nullptr;
        const double charge=kit&&kit->charging?kit->charge:0.0;
        const double cling=kit&&kit->cling&&hand==1?1.0:0.0;
        const double leap=kit&&kit->leaping?1.0:0.0;
        const double land=kit?pulse(kit->landT-0.01,0.02,7.0):0.0;
        const double wob=charge>0.0?0.022*charge*std::sin(st_.t*(22.0+14.0*charge)+hand*1.7):0.0;
        const double target=1.0+0.08*charge-0.3*cling+0.05*leap-0.28*land+wob;
        mittsSquash_=springArray(mittsState_,0,target,charge>0.0?6.0:9.0,0.3,st_.dt);
        mittsGlowTarget_=0.06+1.1*charge*charge+(charge>=0.999?0.35+0.25*std::sin(st_.t*18.0):0.0)
            +1.4*pulse(since-0.005,0.01,20.0)+0.8*land;
        mittsState_[4]=static_cast<float>(mittsState_[4]+(mittsGlowTarget_-mittsState_[4])*(1.0-std::exp(-st_.dt*30.0)));
        mittsSoak_=1.0-0.4*charge;mittsCling_=cling;
    }

    void tickBlade() {
        const auto* kit=st_.kit?&*st_.kit:nullptr;
        const double charge=kit?kit->charge:st_.charge;
        if(kit&&kit->flashT<0.02) bladeFlash_=1.0;
        if(!kit&&st_.chargeFlash>0.9) bladeFlash_=1.0;
        bladeFlash_=std::max(0.0,bladeFlash_-st_.dt*3.2);
        bladeCharge_=damp(bladeCharge_,charge,charge>bladeCharge_?30.0:9.0,st_.dt);
        const double full=charge>=0.999?1.0:0.0;
        bladeFull_=damp(bladeFull_,full,full>0.0?18.0:10.0,st_.dt);
    }

    void tickBrolly() {
        const auto* kit=st_.kit?&*st_.kit:nullptr;
        const double open=kit?kit->open:0.0;
        brollyOpen_=open;
        brollyEaseOpen_=open<=0.0?0.0:open>=1.0?1.0:(kit&&kit->opening?easeOutBack(open):smooth(open));
        brollyFlutter_=open>0.9?0.012*std::sin(st_.t*13.7):0.0;
        brollyKick_=kit?kit->shake:0.0;
        const double grow=kit?kit->grow:1.0;
        brollyGrow_=std::max(0.001,grow<1.0?easeOutBack(std::max(0.0,grow)):1.0);
        brollyNear_=st_.near||open>0.004||grow<0.999;
        if(brollyNear_) {
            const bool want=st_.sinceShoot<0.12||open>0.1;
            trig=damp(trig,want?1.0:0.0,want?40.0:20.0,st_.dt);
        }
        brollyHpFraction_=kit&&kit->hpMax>0.0?kit->hp/kit->hpMax:1.0;
        brollyFlash_=kit?kit->flash:0.0;brollyRegrow_=kit?kit->regrowK:0.0;
        brollyStateReady_=!kit||kit->state=="ready";
    }

    template<class PartFn>
    void applyBow(std::vector<CharacterPartPose>&,const std::string&,const PartFn& part) const {
        if(auto* off=part("off")) off->local.position+=off->local.rotation*offPositionOffset;
        const double phi=0.3*std::clamp(bowString_,-0.2,1.05);
        for(const char* key:{"parts/limbU","parts/limbUInk"}) if(auto* p=part(key)) p->local.rotation=glm::angleAxis(-phi,glm::dvec3(1,0,0));
        for(const char* key:{"parts/limbL","parts/limbLInk"}) if(auto* p=part(key)) p->local.rotation=glm::angleAxis(phi,glm::dvec3(1,0,0));
        const glm::dvec3 nock(0.0,0.0,-0.22*bowString_);
        const glm::dvec3 tipU=bowTip({0.0,0.382,0.0},{0.0,0.165,0.118},-phi);
        const glm::dvec3 tipL=bowTip({0.0,-0.407,0.0},{0.0,-0.19,0.118},phi);
        setBowString(part("parts/stringU"),nock,tipU,{0.0,1.0,0.0},0.382);
        setBowString(part("parts/stringL"),nock,tipL,{0.0,-1.0,0.0},0.407);
        const double show=st_.runner?((st_.runner->charging?1.0:0.0)):((st_.charge>0.01)?1.0:0.0);
        const double rel=st_.sinceRelease;
        const double visible=show>0.5?1.0:rel<0.3?0.0:std::min(1.0,(rel-0.3)/0.12);
        for(const char* key:{"parts/arrows","parts/fletch","parts/heads"}) if(auto* p=part(key)) {
            p->local.position=nock;p->local.scale=glm::dvec3(std::max(0.001,visible));p->local.visible=visible>0.01;
        }
        if(auto* heads=part("parts/heads/0")) {
            heads->color=st_.color*0.55;heads->emissive=st_.color;heads->emissiveIntensity=bowGlow_+5.0*bowChargeFlash_;
        }
        if(auto* coil=part("glow")) coil->uniforms={{"uCharge",{bowCoilCharge_}},{"uFull",{bowCoilFull_}},
            {"uFlash",{bowChargeFlash_}},{"uTime",{st_.t}}};
    }

    template<class PartFn>
    void applyMitts(std::vector<CharacterPartPose>& parts,const std::string& side,const PartFn& part) const {
        const bool near=st_.near;
        for(const char* key:{"body","ink","bodyFar","inkFar"}) if(auto* p=part(key))
            p->local.visible=(std::string(key).find("Far")==std::string::npos)?near:!near;
        const std::string prefix="weapon/"+side+"/parts/";
        for(auto& p:parts) if(p.name.rfind(prefix,0)==0&&p.name.find('/',prefix.size())==std::string::npos) p.local.visible=near;
        if(auto* pad=part("parts/pad")) {
            const double xy=1.0+(1.0-mittsSquash_)*0.55+0.1*(st_.kit&&st_.kit->charging?st_.kit->charge:0.0)+0.06*mittsCling_;
            pad->local.scale={xy,xy*(1.0-0.35*mittsCling_*0.5),std::max(0.55,mittsSquash_)};
            pad->local.visible=near;
        }
        if(auto* surface=part("parts/pad/0")) {
            surface->color=st_.color;
            surface->uniforms={{"uGlow",{static_cast<double>(mittsState_[4])}},{"uSoak",{mittsSoak_}},
                {"sheenColor",{st_.color.x*0.5+0.5,st_.color.y*0.5+0.5,st_.color.z*0.5+0.5}}};
        }
    }

    template<class PartFn>
    void applyBlade(std::vector<CharacterPartPose>&,const std::string&,const PartFn& part) const {
        const bool near=st_.near;
        for(const char* key:{"body","ink","bodyFar","inkFar"}) if(auto* p=part(key))
            p->local.visible=(std::string(key).find("Far")==std::string::npos)?near:!near;
        const bool on=bladeCharge_>0.01||bladeFull_>0.01||bladeFlash_>0.01;
        if(auto* edge=part("off/5")) {
            edge->local.visible=true;edge->color=st_.color;edge->emissive=st_.color;edge->emissiveIntensity=1.0;
            edge->uniforms={{"uCharge",{bladeCharge_}},{"uFull",{bladeFull_}},{"uFlash",{bladeFlash_}},{"uTime",{st_.t}}};
        }
        if(auto* aura=part("off/6")) {
            aura->local.visible=on&&near;
            if(on) aura->uniforms={{"uColor",{st_.color.x,st_.color.y,st_.color.z}},{"uCharge",{bladeCharge_}},
                {"uFull",{bladeFull_}},{"uFlash",{bladeFlash_}},{"uTime",{st_.t}}};
        }
    }

    template<class PartFn>
    void applyBrolly(std::vector<CharacterPartPose>& parts,const std::string& side,const PartFn& part) const {
        const bool near=brollyNear_;
        for(const char* key:{"body","ink","bodyFar","inkFar"}) if(auto* p=part(key))
            p->local.visible=(std::string(key).find("Far")==std::string::npos)?near:!near;
        const std::string prefix="weapon/"+side+"/parts/";
        for(auto& p:parts) if(p.name.rfind(prefix,0)==0&&p.name.find('/',prefix.size())==std::string::npos) p.local.visible=near;
        for(int k=0;k<8;++k) if(auto* p=part("off/"+std::to_string(16+k))) p->local.visible=near;
        for(int k=1;k<8;k+=2) {
            if(auto* mesh=part("parts/g"+std::to_string(k)+"/0")) mesh->color=st_.color;
            if(auto* mesh=part("off/"+std::to_string(7+k)+"/0")) mesh->color=st_.color;
        }
        if(!near) return;
        std::array<glm::dmat4,8> goreMatrices{};
        for(int k=0;k<8;++k) {
            const double e=std::max(0.0,brollyEaseOpen_),ec=std::min(1.0,e),a0=-1.05*(k&1?1.0:0.965);
            const double sx0=0.19*(k&1?1.0:1.08);
            const double a=a0*(1.0-e)-0.16*brollyKick_*(brollyOpen_>0.5?1.0:0.0)+brollyFlutter_*(k&1?1.0:-1.0);
            const double sx=(sx0+(1.0-sx0)*ec)*brollyGrow_;
            const double sl=(0.37+(1.0-0.37)*ec)*brollyGrow_;
            const glm::dmat4 gore=brollyGoreMatrix(k,a,sx,sl,sl);
            goreMatrices[static_cast<std::size_t>(k)]=gore;
            if(auto* p=part("parts/g"+std::to_string(k))) {
                p->local.matrixOverride=true;p->local.authoredMatrix=gore;p->local.visible=near&&((st_.kit?st_.kit->grow:1.0)>0.002);
            }
        }
        const double ec=std::clamp(brollyEaseOpen_,0.0,1.0);
        if(auto* runner=part("parts/runner")) runner->local.position.z=0.305+(0.47-0.305)*ec;
        if(auto* trigger=part("parts/trigger")) {
            trigger->local.rotation=glm::angleAxis(0.42*trig,glm::dvec3(1,0,0));trigger->local.visible=near;
        }
        if(auto* lamp=part("parts/lamp/0")) {
            const glm::dvec3 ok=srgb(0x3dff7a),mid=srgb(0xffc21a),low=srgb(0xff3b30),re=srgb(0x9fd8ff);
            if(brollyStateReady_) {
                lamp->emissive=brollyHpFraction_>0.5?mix(mid,ok,(brollyHpFraction_-0.5)*2.0):mix(low,mid,brollyHpFraction_*2.0);
                lamp->emissiveIntensity=(brollyHpFraction_<0.25?0.6+2.2*(std::sin(st_.t*22.0)>0.0?1.0:0.1):1.4)
                    +2.5*brollyFlash_+(brollyGrow_<1.0?3.0*(1.0-brollyGrow_):0.0);
            } else {
                lamp->emissive=re;lamp->emissiveIntensity=0.15+(std::sin(st_.t*(6.0+14.0*brollyRegrow_))>0.3?1.6*brollyRegrow_:0.0);
            }
        }
        const bool have=(st_.kit?st_.kit->grow:1.0)>0.5;
        const bool tear0=have&&brollyHpFraction_<0.66,tear1=have&&brollyHpFraction_<0.33;
        for(const char* key:{"parts/g1/1","parts/g4/1"}) if(auto* p=part(key)) p->local.visible=tear0;
        for(const char* key:{"parts/g6/1","parts/g2/1","parts/g1/2","parts/g7/1"}) if(auto* p=part(key)) p->local.visible=tear1;
        const bool showStruts=(st_.kit?st_.kit->grow:1.0)>0.05;
        const double rz=0.305+(0.47-0.305)*ec;
        for(int k=0;k<8;++k) if(auto* p=part("off/"+std::to_string(16+k))) {
            p->local.matrixOverride=true;p->local.authoredMatrix=brollyStrutMatrix(k,rz,goreMatrices[static_cast<std::size_t>(k)]);
            p->local.visible=near&&showStruts;
        }
    }

    static double smooth(double t) { t=std::clamp(t,0.0,1.0);return t*t*(3.0-2.0*t); }
    static double easeOutBack(double t) {
        constexpr double c1=1.9,c3=c1+1.0;
        return 1.0+c3*std::pow(t-1.0,3.0)+c1*std::pow(t-1.0,2.0);
    }
    static float springArray(std::array<float,8>& state,std::size_t i,double target,double hz,double zeta,double dt) {
        constexpr double tau=6.28318530717958647692;
        const double w=tau*hz,x0=static_cast<double>(state[i])-target,v0=state[i+1];
        const double wd=w*std::sqrt(1.0-zeta*zeta),e=std::exp(-zeta*w*dt),c=std::cos(wd*dt),sn=std::sin(wd*dt);
        const double b=(v0+zeta*w*x0)/wd;
        state[i]=static_cast<float>(e*(x0*c+b*sn)+target);
        state[i+1]=static_cast<float>(e*((-zeta*w*x0+wd*b)*c+(-zeta*w*b-wd*x0)*sn));
        return state[i];
    }
    static glm::dvec3 bowTip(glm::dvec3 tip,glm::dvec3 root,double th) {
        const double dy=tip.y-root.y,dz=tip.z-root.z,c=std::cos(th),s=std::sin(th);
        return {tip.x,root.y+dy*c-dz*s,root.z+dy*s+dz*c};
    }
    static glm::dquat fromTo(glm::dvec3 from,glm::dvec3 to) {
        from=glm::normalize(from);to=glm::normalize(to);
        const double r=glm::dot(from,to)+1.0;
        if(r<std::numeric_limits<double>::epsilon()) {
            if(std::abs(from.x)>std::abs(from.z)) return glm::normalize(glm::dquat(0.0,-from.y,from.x,0.0));
            return glm::normalize(glm::dquat(0.0,0.0,-from.z,from.y));
        }
        const auto c=glm::cross(from,to);
        return glm::normalize(glm::dquat(r,c.x,c.y,c.z));
    }
    static void setBowString(CharacterPartPose* part,glm::dvec3 nock,glm::dvec3 tip,glm::dvec3 rest,double length) {
        if(!part) return;
        auto delta=tip-nock;double len=glm::length(delta);if(len==0.0) len=1e-4;
        part->local.position=nock;part->local.rotation=fromTo(rest,delta/len);part->local.scale={1.0,len/length,1.0};
    }
    static glm::dmat4 translation(glm::dvec3 p) {
        glm::dmat4 out(1.0);out[3]=glm::dvec4(p,1.0);return out;
    }
    static glm::dmat4 rotateZ(double angle) {
        const double c=std::cos(angle),s=std::sin(angle);glm::dmat4 out(1.0);
        out[0][0]=c;out[0][1]=s;out[1][0]=-s;out[1][1]=c;return out;
    }
    static glm::dmat4 brollyFold(double a,double sx,double sy,double sz) {
        const double c=std::cos(a),s=std::sin(a);glm::dmat4 out(1.0);
        out[0][0]=sx;out[1][1]=c*sy;out[1][2]=s*sy;out[2][1]=-s*sz;out[2][2]=c*sz;return out;
    }
    static glm::dmat4 brollyGoreMatrix(int k,double a,double sx,double sy,double sz) {
        constexpr double tau=6.28318530717958647692;
        const double angle=-static_cast<double>(k)*tau/8.0;
        const double a0=-1.05*(k&1?1.0:0.965),sx0=0.19*(k&1?1.0:1.08);
        const auto r=rotateZ(angle),fold=brollyFold(a,sx,sy,sz),fold0=brollyFold(a0,sx0,0.37,0.37);
        return translation({0.0,0.066,0.56})*r*fold*glm::inverse(fold0)*rotateZ(-angle);
    }
    static glm::dvec3 brollyPoint(double th,double ps,double off) {
        constexpr double tau=6.28318530717958647692;
        const double half=52.0*tau/360.0,rim=0.7,radius=rim/std::sin(half),r=radius+off;
        return {r*std::sin(th)*std::sin(ps),r*std::sin(th)*std::cos(ps),-radius+r*std::cos(th)};
    }
    static glm::dmat4 brollyStrutMatrix(int k,double runnerZ,const glm::dmat4& gore) {
        constexpr double tau=6.28318530717958647692;
        const double gw=tau/16.0,angle=-static_cast<double>(k)*tau/8.0;
        const auto r=rotateZ(angle),fold0=brollyFold(-1.05*(k&1?1.0:0.965),0.19*(k&1?1.0:1.08),0.37,0.37);
        const auto ribA=(r*fold0)*glm::dvec4(brollyPoint((52.0*tau/360.0)*0.42,gw,-0.006-0.004),1.0);
        const auto dir4=r*glm::dvec4(std::sin(gw),std::cos(gw),0.0,0.0);
        const auto dir=glm::normalize(glm::dvec3(dir4));
        const glm::dvec3 sa=glm::dvec3(0.0,0.066,runnerZ)+dir*0.019;
        const glm::dvec3 sb=glm::dvec3(gore*ribA);
        const glm::dvec3 sy=sb-sa,za(0.0,0.0,1.0);
        glm::dvec3 sx=glm::cross(sy,za);
        if(glm::dot(sx,sx)<1e-10) sx={1.0,0.0,0.0}; else sx=glm::normalize(sx);
        sx*=0.0026;
        glm::dvec3 sz=glm::normalize(glm::cross(sx,sy))*0.0026;
        glm::dmat4 out(1.0);out[0]=glm::dvec4(sx,0.0);out[1]=glm::dvec4(sy,0.0);out[2]=glm::dvec4(sz,0.0);out[3]=glm::dvec4(sa,1.0);
        return out;
    }
    static glm::dvec3 mix(glm::dvec3 a,glm::dvec3 b,double t) { return a+(b-a)*t; }
    static glm::dvec3 srgb(std::uint32_t hex) {
        auto channel=[](double v) { v/=255.0;return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4); };
        return {channel((hex>>16)&0xff),channel((hex>>8)&0xff),channel(hex&0xff)};
    }
    static double mj(double t) { t=std::clamp(t,0.0,1.0);return t*t*t*(10.0+t*(6.0*t-15.0)); }
    static double pulse(double t,double attack,double decay) { return t<0.0?0.0:t<attack?t/attack:std::exp(-(t-attack)*decay); }
    static double damp(double a,double b,double rate,double dt) { return a+(b-a)*(1.0-std::exp(-rate*dt)); }
    double spring(std::size_t i,double target,double hz,double zeta) {
        const double w=6.28318530717958647692*hz,x0=ps_[i]-target,v0=ps_[i+1],dt=st_.dt;
        double x,v;
        if(zeta<0.999) {
            const double wd=w*std::sqrt(1.0-zeta*zeta),e=std::exp(-zeta*w*dt),c=std::cos(wd*dt),s=std::sin(wd*dt),b=(v0+zeta*w*x0)/wd;
            x=e*(x0*c+b*s);v=e*((-zeta*w*x0+wd*b)*c+(-zeta*w*b-wd*x0)*s);
        } else { const double e=std::exp(-w*dt),b=v0+w*x0;x=e*(x0+b*dt);v=e*(v0-w*b*dt); }
        ps_[i]=static_cast<float>(x+target);ps_[i+1]=static_cast<float>(v);return ps_[i];
    }
    CharacterWeaponAnimationInput st_;
    std::array<float,12> ps_{1.3f};
    std::array<float,8> mittsState_{};
    bool mittsInitialized_=false,bowLoose_=false,brollyStateReady_=true,brollyNear_=true;
    double bowString_=0.0,bowStringVelocity_=0.0,bowLastRelease_=99.0,bowGlow_=0.4,bowCoilCharge_=0.0,bowCoilFull_=0.0,bowChargeFlash_=0.0;
    double mittsLastPunch_=99.0,mittsSquash_=1.0,mittsGlowTarget_=0.0,mittsSoak_=1.0,mittsCling_=0.0;
    double bladeCharge_=0.0,bladeFlash_=0.0,bladeFull_=0.0;
    double brollyOpen_=0.0,brollyEaseOpen_=0.0,brollyFlutter_=0.0,brollyKick_=0.0,brollyGrow_=1.0;
    double brollyHpFraction_=1.0,brollyFlash_=0.0,brollyRegrow_=0.0;
    double drumW_=0.0,drumA_=0.0,spinW_=0.0,spinA_=0.0,needle_=0.0,bulb_=0.0,bolt_=0.0;
};
}
