#include "../src/game/CharacterData.hpp"
#include "../src/render/CharacterRenderData.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    restoration::CharacterData data;
    restoration::CharacterStyle style;
    if(argc>8) {
        std::istringstream fields(argv[8]);std::string field;
        for(auto* index:{&style.hair,&style.skin,&style.outfit,&style.eyes,&style.hat,&style.brows}) {
            if(!std::getline(fields,field,',')) return 2;
            *index=std::stoi(field);
        }
    }
    const auto& rig=data.rig(style.hair,style.hat,style.brows);
    const std::string name=argc>1?argv[1]:"pose-oracle";
    const std::string weapon=argc>2?argv[2]:"shooter";
    restoration::CharacterPose pose(data,rig,name,weapon);
    if(argc>8) pose.setStyle(style);
    if(argc>10) pose.setSub(argv[10]);
    restoration::CharacterSceneInput scene;
    scene.inWorld=true;scene.lifeLevel=2;
    if(argc>9) {
        std::istringstream fields(argv[9]);std::string field;
        std::vector<double> values;
        while(std::getline(fields,field,',')) values.push_back(std::stod(field));
        if(values.size()!=2&&values.size()!=5) return 2;
        scene.isLocal=values[0]!=0.0;scene.lifeLevel=static_cast<int>(values[1]);
        if(values.size()==5) scene.cameraPosition=glm::dvec3(values[2],values[3],values[4]);
    }
    if(argc>3&&*argv[3]) pose.setDance(argv[3]);
    if(argc>4&&std::stod(argv[4])>0.0)
        pose.setHurt(std::stod(argv[4]),{0.028426039504420793,0.10461648408208657,1.0});
    const double slope=argc>5?std::stod(argv[5]):0.0;
    std::vector<std::string> partNames;
    if(argc>6) {
        std::istringstream names(argv[6]); std::string part;
        while(std::getline(names,part,'|')) if(!part.empty()) partNames.push_back(std::move(part));
    }
    if(argc>7) {
        std::istringstream color(argv[7]); std::string component; glm::dvec3 team;
        for(int i=0;i<3;++i) {
            if(!std::getline(color,component,',')) return 2;
            team[i]=std::stod(component);
        }
        pose.setColor(team);
    }
    std::cout<<std::setprecision(9);
    std::string line;
    while(std::getline(std::cin,line)) {
        if(line.empty()) continue;
        std::istringstream in(line);
        double dt,x,y,z,yaw,grounded,form,speed,mx,mz,firing,charge,aim,ink,special,lowInk,enemyInk,invuln,hpSet,hp,
            subAim,rolling,visible,vySet,vy,turnRateSet,turnRate,wallX,wallY,wallZ;
        unsigned eventCount=0;
        if(!(in>>dt>>x>>y>>z>>yaw>>grounded>>form>>speed>>mx>>mz>>firing>>charge>>aim>>ink>>special>>lowInk>>enemyInk>>invuln>>hpSet>>hp
            >>subAim>>rolling>>visible>>vySet>>vy>>turnRateSet>>turnRate>>wallX>>wallY>>wallZ>>eventCount)||eventCount>64) return 2;
        restoration::CharacterAnimState state;
        state.position={x,y,z}; state.yaw=yaw; state.grounded=grounded!=0;
        state.form=static_cast<restoration::CharacterForm>(static_cast<int>(form));
        state.speed=speed; state.localMove={mx,mz}; state.firing=firing!=0; state.charge=charge;
        state.aimPitch=aim; state.ink=ink; state.special=special; state.lowInk=lowInk!=0;
        state.inEnemyInk=enemyInk!=0; state.invuln=invuln!=0; state.subAim=subAim!=0; state.rolling=rolling!=0; state.visible=visible!=0;
        if(hpSet!=0) state.hp=hp;
        if(vySet!=0) state.vy=vy;
        if(turnRateSet!=0) state.turnRate=turnRate;
        state.wallNormal={wallX,wallY,wallZ};
        for(unsigned i=0;i<eventCount;++i) {
            restoration::CharacterTrigger event;
            double amountSet=0.0,amount=0.0,directionSet=0.0;
            if(!(in>>event.name>>amountSet>>amount>>directionSet>>event.direction.x>>event.direction.y>>event.direction.z>>event.hand>>event.side>>event.duration)) return 2;
            if(amountSet!=0.0) event.amount=amount;
            event.hasDirection=directionSet!=0.0;
            pose.trigger(event);
        }
        double hideSub=0.0;if(in>>hideSub) state.subPropHidden=hideSub!=0.0;
        scene.raycast=[=](glm::dvec3 origin,glm::dvec3 direction,double distance) {
            restoration::CharacterGroundHit hit;
            const double den=direction.y-slope*direction.x;
            const double t=std::abs(den)<1e-12?-1.0:(slope*origin.x-origin.y)/den;
            hit.hit=t>=0.0&&t<=distance; hit.distance=hit.hit?t:distance;
            if(hit.hit) { hit.point=origin+direction*t; hit.normal=glm::normalize(glm::dvec3(-slope,1.0,0.0)); }
            return hit;
        };
        const auto& frame=pose.update(dt,state,scene);
        bool first=true;
        auto emit=[&](double value) { if(!first) std::cout<<','; std::cout<<value; first=false; };
        auto emitTransform=[&](const restoration::CharacterTransform& transform) {
            emit(transform.position.x); emit(transform.position.y); emit(transform.position.z);
            emit(transform.rotation.x); emit(transform.rotation.y); emit(transform.rotation.z); emit(transform.rotation.w);
            emit(transform.scale.x); emit(transform.scale.y); emit(transform.scale.z); emit(transform.visible?1.0:0.0);
        };
        emitTransform(frame.root); emitTransform(frame.model); emitTransform(frame.kid); emitTransform(frame.squid);
        for(float value:frame.channels) emit(value);
        for(const auto& bone:frame.bones) {
            emit(bone.position.x); emit(bone.position.y); emit(bone.position.z);
            emit(bone.rotation.x); emit(bone.rotation.y); emit(bone.rotation.z); emit(bone.rotation.w);
            emit(bone.scale.x); emit(bone.scale.y); emit(bone.scale.z); emit(bone.visible?1.0:0.0);
        }
        emit(frame.material.look.x); emit(frame.material.look.y);
        emit(frame.material.gaze.x); emit(frame.material.gaze.y); emit(frame.material.gaze.z); emit(frame.material.gaze.w);
        emit(frame.material.lid.x); emit(frame.material.lid.y); emit(frame.material.lid.z); emit(frame.material.lid.w);
        for(const auto& matrix:frame.skinMatrices)
            for(int column=0;column<4;++column) for(int row=0;row<4;++row) emit(matrix[column][row]);
        const auto& m=frame.material;
        for(const auto& v:{m.team,m.shirt,m.shorts,m.shoe,m.sole,m.sock,m.strap,m.iris,m.iris2,m.flash,m.glow}) { emit(v.x); emit(v.y); emit(v.z); }
        for(const auto& v:{m.hurt,m.mouth,m.mouth2,m.lid,m.gaze}) { emit(v.x); emit(v.y); emit(v.z); emit(v.w); }
        emit(m.look.x); emit(m.look.y);
        emit(m.wig.x); emit(m.wig.y); emit(m.wig.z);
        emit(m.rimLight.x); emit(m.rimLight.y); emit(m.rimLight.z);
        emit(m.fillLight.x); emit(m.fillLight.y); emit(m.fillLight.z);
        emit(m.tankEmissive.x); emit(m.tankEmissive.y); emit(m.tankEmissive.z);
        emit(m.rim.x); emit(m.rim.y); emit(m.rim.z); emit(m.rim.w);
        emit(m.pattern); emit(m.hurtSeed); emit(m.time); emit(m.opacity); emit(m.freckle); emit(m.pupil); emit(m.coilEmissiveIntensity);
        emit(frame.muzzle.x); emit(frame.muzzle.y); emit(frame.muzzle.z);
        emit(frame.muzzleLeft.x); emit(frame.muzzleLeft.y); emit(frame.muzzleLeft.z);
        emit(frame.head.x); emit(frame.head.y); emit(frame.head.z);
        emit(static_cast<double>(frame.parts.size()));
        for(const auto& path:partNames) {
            const auto part=std::find_if(frame.parts.begin(),frame.parts.end(),[&](const auto& candidate){return candidate.name==path;});
            emit(part==frame.parts.end()?0.0:1.0);
            if(part==frame.parts.end()) {
                for(int i=0;i<28;++i) emit(std::numeric_limits<double>::quiet_NaN());
                continue;
            }
            emitTransform(part->local);
            emit(part->local.matrixOverride?1.0:0.0);
            for(int column=0;column<4;++column) for(int row=0;row<4;++row) emit(part->local.authoredMatrix[column][row]);
        }
        std::cout<<'\n';
    }
}
