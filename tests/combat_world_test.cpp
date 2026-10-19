#include "../restoration/PlayerSimulation.hpp"
#include "../restoration/ArsenalData.hpp"
#include "world_specials_source_fixtures.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

int main(){
    using namespace restoration;
    try{
        const StageData* stage=nullptr;for(const auto& candidate:ReferenceStages())if(candidate.id=="tidewater"&&candidate.mode=="turf")stage=&candidate;
        if(!stage)throw std::runtime_error("Missing source Turf stage");
        StageGeometry geometry(*stage);FacePaint paint(*stage,geometry);CombatWorld world(*stage,geometry,paint);
        auto a=std::make_unique<PlayerSimulation>(*stage,geometry,paint,0,&world);
        auto ally=std::make_unique<PlayerSimulation>(*stage,geometry,paint,0,&world);
        auto enemy=std::make_unique<PlayerSimulation>(*stage,geometry,paint,1,&world);
        a->state.invuln=ally->state.invuln=enemy->state.invuln=0;
        auto check=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
        world.applyHit(*a,*ally,36,"shooter");check(ally->state.hp==100,"Friendly fire changed ally HP");
        world.applyHit(*a,*enemy,36,"shooter");check(enemy->state.hp==64,"One shared-world hit applied more than once");
        glm::dvec3 point;bool found=false;
        for(int id=0;id<static_cast<int>(stage->faces.size())&&!found;++id){
            const auto& f=stage->faces[id];const auto& grid=paint.faceGrid(id);if(!f.turf||grid.offset<0)continue;
            for(int j=0;j<grid.rows&&!found;++j)for(int i=0;i<grid.columns&&!found;++i)if(!paint.dead()[grid.offset+j*grid.columns+i]){point=f.origin+f.u*((i+0.5)*grid.cellWidth)+f.v*((j+0.5)*grid.cellHeight)+f.normal*0.05;found=true;}
        }
        check(found,"Missing source paintable cell");
        const double first=world.paint(*a,point,0.45,0.5),charge=a->state.specialCharge;
        check(first>0&&std::abs(a->turfPainted()-first)<1e-9,"Paint credit was not applied once");
        const auto counts=paint.counts();
        check(world.paint(*a,point,0.45,0.5)==0&&a->state.specialCharge==charge,"Own-ink repaint added duplicate credit");
        const double transfer=world.paint(*enemy,point,0.45,0.5);
        check(transfer>0&&paint.counts()[0]<counts[0]&&paint.counts()[1]>0,"Enemy repaint did not transfer ownership");
        const double specialCharge=enemy->state.specialCharge;
        world.paint(*enemy,point+glm::dvec3(1,0,0),0.8,0.5,{},true);
        check(enemy->state.specialCharge==specialCharge,"Special paint recharged special");
        ShooterProjectile p;p.owner=a.get();p.pos=a->state.pos+glm::dvec3(0,1,0);p.vel={0,0,34};world.launch(p);
        world.deploy(*a,a->subId());check(world.attackCount(*a)>=2,"Shared world did not own projectile and device");
        world.applyHit(*enemy,*a,1000,"shooter");check(!a->state.alive&&world.attackCount(*a)==0,"Death did not clean the actor's attacks");
        a.reset();check(world.actors().size()==2,"Actor destruction left a registered pointer");
        world.clear();check(world.projectiles().empty()&&world.devices().empty(),"Restart did not clear attacks");
        auto fixture=[](std::string_view id)->const test::SpecialSourceFixture& {
            for(const auto& row:test::specialSourceFixtures)if(row.id==id)return row;
            throw std::runtime_error("Missing locked source special fixture");
        };
        auto slam=std::make_unique<PlayerSimulation>(*stage,geometry,paint,0,&world);
        auto slamVictim=std::make_unique<PlayerSimulation>(*stage,geometry,paint,1,&world);
        auto attacker=std::make_unique<PlayerSimulation>(*stage,geometry,paint,1,&world);
        slam->setLoadout("shooter","bomb","slam");
        slam->state.pos=stage->spawnPads[0]+glm::dvec3(0,1,0);slam->state.grounded=true;slam->state.invuln=0;slam->state.ink=12;
        slam->state.specialCharge=slam->weapon().number("specialCost",190.0);
        slamVictim->state.pos=slam->state.pos+glm::dvec3(0.8,0,0);slamVictim->state.invuln=0;
        attacker->state.pos=slam->state.pos+glm::dvec3(16,0,0);attacker->state.invuln=0;
        slam->input.fire=true;slam->input.special=true;
        constexpr double step=1.0/60.0;
        slam->tick(step);
        auto slamPose=slam->combatPose();
        check(slamPose.special&&slamPose.specialKind=="slam"&&!slamPose.specialThrown,
              "PlayerSimulation special input did not activate and expose Slam pose");
        check(slam->state.specialCharge==0&&slam->state.ink==100&&slam->shotsFired()==0,
              "Slam activation did not spend/refill and consume its activation frame");
        check(std::abs(slamPose.specialTime)<1e-12,"Slam source activation frame advanced the body timer");
        int bodyTicks=0;
        auto bodyStep=[&] { slam->tick(step);world.tick(step);++bodyTicks; };
        const double startY=slam->state.pos.y;
        bodyStep();
        check(slam->state.pos.y>startY&&slam->shotsFired()==0,"Slam body did not own movement and suppress the runner");
        check(std::abs(slam->combatPose().specialTime-step)<1e-12,"Slam body pose timer differs from the source step");
        const double hpBeforeArmor=slam->state.hp;
        world.applyHit(*attacker,*slam,40,"shooter");
        check(std::abs(slam->state.hp-(hpBeforeArmor-10.0))<1e-12,"Slam armor did not filter incoming damage before the main kit");
        slam->input.special=false;bodyStep();
        slam->input.special=true;bodyStep();
        check(slam->combatPose().special&&slam->state.specialCharge==0,
              "Repeated special input cancelled or restarted a running Slam");
        const int sourceBodyFrames=fixture("slam").frames;
        while(slam->combatPose().special&&bodyTicks<sourceBodyFrames)bodyStep();
        if(bodyTicks!=sourceBodyFrames||slam->combatPose().special||slamVictim->state.alive)
            std::cerr<<"Slam first difference: ticks="<<bodyTicks<<"/"<<sourceBodyFrames
                <<" active="<<slam->combatPose().special<<" time="<<slam->combatPose().specialTime
                <<" victimHp="<<slamVictim->state.hp<<" victimAlive="<<slamVictim->state.alive
                <<" actorPos="<<slam->state.pos.x<<","<<slam->state.pos.y<<","<<slam->state.pos.z
                <<" victimPos="<<slamVictim->state.pos.x<<","<<slamVictim->state.pos.y<<","<<slamVictim->state.pos.z<<"\n";
        check(bodyTicks==sourceBodyFrames&&!slam->combatPose().special&&!slamVictim->state.alive,
              "PlayerSimulation Slam body/impact diverged from the locked source frame and hit fixture");

        slam->input.special=false;slam->tick(step);
        slam->state.invuln=0;slam->state.specialCharge=slam->weapon().number("specialCost",190.0);
        slam->input.special=true;slam->tick(step);
        check(slam->combatPose().special,"Slam could not restart after its prior body ended");
        world.applyHit(*attacker,*slam,1000,"shooter");
        check(!slam->state.alive&&!slam->combatPose().special
              &&std::abs(slam->state.specialCharge-slam->weapon().number("specialCost",190.0)*0.5)<1e-12,
              "Death during an active special did not retain gauge and clean active state");

        auto bubbler=std::make_unique<PlayerSimulation>(*stage,geometry,paint,0,&world);
        bubbler->setLoadout("shooter","bomb","bubbler");
        bubbler->state.specialCharge=bubbler->weapon().number("specialCost",190.0);
        bubbler->state.invuln=0;bubbler->input.special=true;bubbler->tick(step);
        check(bubbler->combatPose().special&&bubbler->combatPose().specialKind=="bubbler"
              &&bubbler->state.shield>0,"Bubbler special input did not wire into the world-owned shield");
        const double chargeBeforeActivePaint=bubbler->state.specialCharge;
        const double activePaint=world.paint(*bubbler,point,0.65,0.5);
        check(activePaint>0&&bubbler->state.specialCharge==chargeBeforeActivePaint,
              "Ordinary paint during an active special recharged the special gauge");
        const double hpBeforeShield=bubbler->state.hp;
        world.applyHit(*attacker,*bubbler,36,"shooter");
        check(bubbler->state.hp==hpBeforeShield,"Bubbler did not filter damage through PlayerSimulation");
        auto inkProtected=std::make_unique<PlayerSimulation>(*stage,geometry,paint,0,&world);
        glm::dvec3 inkPoint;bool inkFloor=false;
        for(int id=0;id<static_cast<int>(stage->faces.size())&&!inkFloor;++id){
            const auto& f=stage->faces[id];const auto& grid=paint.faceGrid(id);
            if(!f.turf||f.normal.y<0.99||grid.offset<0||grid.columns<9||grid.rows<9)continue;
            if(paint.dead()[grid.offset+(grid.rows/2)*grid.columns+grid.columns/2])continue;
            inkPoint=f.origin+f.u*(f.width/2)+f.v*(f.height/2);inkFloor=true;
        }
        check(inkFloor,"Missing enemy-ink shield test floor");
        inkProtected->setLoadout("shooter","bomb","bubbler");
        inkProtected->spawnAt(inkPoint,0);inkProtected->state.invuln=0;
        inkProtected->state.specialCharge=inkProtected->weapon().number("specialCost",190.0);
        world.paint(*attacker,inkPoint+glm::dvec3(0,0.05,0),3.0,0.4,{},true);
        inkProtected->input.special=true;inkProtected->tick(step);
        check(inkProtected->combatPose().special,"Enemy-ink test did not activate Bubbler");
        for(int frame=0;frame<30;++frame){inkProtected->tick(step);world.tick(step);}
        check(inkProtected->state.groundTeam==2&&inkProtected->state.shield>0&&inkProtected->state.hp==100,
              "Bubbler failed to suppress source enemy-ink damage while shielded");
        world.clear();
        check(!bubbler->combatPose().special&&bubbler->state.shield==0,"World restart retained special pose or shield state");
        inkProtected->input.special=false;inkProtected->tick(step);
        check(inkProtected->state.groundTeam==2&&inkProtected->state.hp<100,
              "Enemy-ink damage did not resume after shield cleanup");
        std::cout<<"Shared actor damage, team filtering, paint credit, cleanup, and PlayerSimulation special lifecycle passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
