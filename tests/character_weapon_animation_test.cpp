#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../src/game/CharacterWeaponAnimation.hpp"
#include <cmath>
#include <iostream>

namespace {
using namespace restoration;
CharacterPartPose& add(std::vector<CharacterPartPose>& parts,const std::string& name) {
    parts.emplace_back();parts.back().name="weapon/right/"+name;return parts.back();
}
CharacterPartPose& find(std::vector<CharacterPartPose>& parts,const std::string& name) {
    const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.name=="weapon/right/"+name;});
    assert(it!=parts.end());return *it;
}
bool close(double a,double b,double eps=1e-9) { return std::abs(a-b)<=eps; }
}

int main() {
    for(const char* weapon:{"twins","spinner","roller","brush","bucket"}) {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon=weapon;in.dt=1.0/60.0;
        in.sinceShoot=0.01;in.firing=true;in.charge=1.0;
        animation.tick(in);
        assert(animation.trig==0.0); // These actual source models have no trigger part.
    }
    {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon="spinner";in.dt=1.0/60.0;
        in.sinceShoot=99.01666666666667;in.charge=0.2896454643296228;
        animation.tick(in);
        std::vector<CharacterPartPose> parts;add(parts,"spin");
        animation.apply(parts,"right");
        // actual Character.update weapon_spinner/frame 1 in character_pose_fixtures.json
        assert(close(find(parts,"spin").local.rotation.z,0.006890798026040803));
        assert(close(find(parts,"spin").local.rotation.w,0.9999762581694449));
    }
    {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon="bow";in.dt=1.0/60.0;in.t=0.5;in.charge=0.8;in.chargeFlash=0.1;
        in.runner=CharacterRunnerState{};in.runner->charging=true;in.runner->charge=0.8;
        in.kit=CharacterWeaponKitState{};in.kit->bowDraw=0.75;
        in.color={0.2,0.4,0.8};animation.tick(in);
        std::vector<CharacterPartPose> parts;
        auto& off=add(parts,"off");off.local.position={0.1,0.2,0.3};
        off.local.rotation=glm::angleAxis(0.5,glm::dvec3(0,1,0));
        for(const char* n:{"parts/limbU","parts/limbUInk","parts/limbL","parts/limbLInk","parts/stringU","parts/stringL","parts/arrows","parts/fletch","parts/heads","parts/heads/0","glow"}) add(parts,n);
        animation.apply(parts,"right");
        assert(close(animation.offPositionOffset.z,0.165));
        const auto expectedOff=glm::dvec3(0.1,0.2,0.3)+glm::angleAxis(0.5,glm::dvec3(0,1,0))*glm::dvec3(0,0,0.165);
        assert(glm::length(find(parts,"off").local.position-expectedOff)<1e-9);
        assert(close(find(parts,"parts/arrows").local.position.z,-0.165));
        assert(find(parts,"parts/arrows").local.visible);
        assert(close(find(parts,"parts/limbU").local.rotation.x,-std::sin(0.3*0.75/2.0)));
        assert(find(parts,"parts/heads/0").color.has_value());
        const double glow=0.4+(0.35+1.4*0.8+1.4-0.4)*(1.0-std::exp(-18.0/60.0));
        assert(close(find(parts,"parts/heads/0").emissiveIntensity,glow+0.5));
        assert(close(find(parts,"glow").uniforms.at("uCharge")[0],0.8));
    }
    {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon="mitts";in.dt=1.0/60.0;in.t=0.2;in.sinceShoot=0.2;in.hand=1;in.color={0.2,0.4,0.8};
        in.kit=CharacterWeaponKitState{};in.kit->charging=true;in.kit->charge=0.6;in.kit->cling=true;in.kit->hasSinceHand=true;in.kit->sinceHand={0.4,0.2};
        animation.tick(in);
        std::vector<CharacterPartPose> parts;add(parts,"parts/pad");add(parts,"parts/pad/0");
        animation.apply(parts,"right");
        assert(find(parts,"parts/pad").local.scale.x>1.0);
        assert(find(parts,"parts/pad").local.scale.y<find(parts,"parts/pad").local.scale.x);
        assert(find(parts,"parts/pad/0").uniforms.at("uGlow")[0]>0.06);
        assert(close(find(parts,"parts/pad/0").uniforms.at("uSoak")[0],0.76));
    }
    {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon="blade";in.dt=0.05;in.t=0.5;in.color={0.2,0.4,0.8};
        in.kit=CharacterWeaponKitState{};in.kit->charge=0.8;
        animation.tick(in);
        std::vector<CharacterPartPose> parts;add(parts,"off/5");add(parts,"off/6");
        animation.apply(parts,"right");
        const double expected=0.8*(1.0-std::exp(-30.0*0.05));
        assert(close(find(parts,"off/5").uniforms.at("uCharge")[0],expected));
        assert(find(parts,"off/6").local.visible);
        assert(find(parts,"off/5").color.has_value());
    }
    {
        CharacterWeaponAnimation animation;
        CharacterWeaponAnimationInput in;in.weapon="brolly";in.dt=1.0/60.0;in.t=0.5;in.sinceShoot=1.0;in.color={0.2,0.4,0.8};
        in.kit=CharacterWeaponKitState{};in.kit->open=0.4;in.kit->grow=0.8;in.kit->hp=0.5;in.kit->hpMax=1.0;in.kit->shake=0.1;in.kit->flash=0.2;
        animation.tick(in);
        std::vector<CharacterPartPose> parts;add(parts,"parts/g0");add(parts,"parts/g1");add(parts,"parts/runner");add(parts,"parts/trigger");
        add(parts,"parts/lamp/0");add(parts,"parts/g1/1");add(parts,"parts/g1/2");
        for(int i=0;i<8;++i) add(parts,"off/"+std::to_string(16+i));
        animation.apply(parts,"right");
        assert(find(parts,"parts/g0").local.matrixOverride);
        assert(close(find(parts,"parts/g0").local.authoredMatrix[3][1],0.066));
        assert(close(find(parts,"parts/g0").local.authoredMatrix[3][2],0.56));
        assert(find(parts,"parts/runner").local.position.z>0.35);
        assert(find(parts,"parts/g1/1").local.visible);
        assert(!find(parts,"parts/g1/2").local.visible);
        assert(find(parts,"off/16").local.matrixOverride&&find(parts,"off/16").local.visible);
        assert(find(parts,"parts/lamp/0").emissiveIntensity>1.4);
    }
    std::cout<<"character weapon animation kits passed\n";
}
