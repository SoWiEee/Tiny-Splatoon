#include "ProjectileVisuals.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>

int main(int argc,char** argv) {
    try {
        using namespace restoration;
        StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        StageGeometry geometry(stage);FacePaint paint(stage,geometry);
        PlayerSimulation a(stage,geometry,paint,0),b(stage,geometry,paint,1);
        std::ifstream input(argc>1?argv[1]:"tests/fixtures/projectile-blob-fixtures.txt");
        int cases=0;if(!(input>>cases)||cases!=24)throw std::runtime_error("Missing source projectile draw fixtures");
        std::vector<ProjectileBlob> reused;reused.reserve(700);const auto reusedCapacity=reused.capacity();
        reused.push_back({});
        for(int sample=0;sample<cases;sample++) {
            ShooterProjectile p;int team=0,count=0;
            input>>p.kind>>p.pos.x>>p.pos.y>>p.pos.z>>p.vel.x>>p.vel.y>>p.vel.z>>p.start.x>>p.start.y>>p.start.z
                >>p.age>>p.life>>p.seed>>p.size>>p.delay>>p.look.size>>p.look.tail0>>p.look.tailK>>p.look.wobble>>p.look.frequency>>p.look.nose>>p.look.satellites>>team>>count;
            p.owner=team?&b:&a;const std::vector<ShooterProjectile> shots{p};
            const auto blobs=BuildProjectileBlobs(shots);BuildProjectileBlobs(shots,reused);
            if(reused.size()!=blobs.size()||reused.capacity()!=reusedCapacity)throw std::runtime_error("Projectile blob output reuse mismatch");
            if(blobs.size()!=static_cast<std::size_t>(count))throw std::runtime_error("Source projectile satellite/delay count mismatch case "+std::to_string(sample));
            for(std::size_t blobIndex=0;blobIndex<blobs.size();blobIndex++) {
                const auto& blob=blobs[blobIndex];const auto& reusedBlob=reused[blobIndex];
                const float* fields[]={glm::value_ptr(blob.matrix),glm::value_ptr(blob.shape),glm::value_ptr(blob.color)};
                const float* reusedFields[]={glm::value_ptr(reusedBlob.matrix),glm::value_ptr(reusedBlob.shape),glm::value_ptr(reusedBlob.color)};
                const int lengths[]={16,4,3};
                for(int group=0;group<3;group++)for(int i=0;i<lengths[group];i++) {
                    double expected;if(!(input>>expected)||std::abs(fields[group][i]-expected)>1e-5)
                        throw std::runtime_error("Source projectile Float32 instance mismatch case "+std::to_string(sample));
                    if(fields[group][i]!=reusedFields[group][i])throw std::runtime_error("Projectile blob return/reuse output differs");
                }
            }
        }
        ShooterProjectile inkShot;inkShot.owner=&a;inkShot.kind="shot";inkShot.weaponId="shooter";
        inkShot.pos={0,0,4};inkShot.start=glm::dvec3(0);inkShot.vel={0,0,20};inkShot.age=.15;inkShot.life=.6;inkShot.size=.15;inkShot.seed=.42;
        ShooterProjectile otherShot=inkShot;otherShot.weaponId="spinner";
        ShooterProjectile otherKind=inkShot;otherKind.kind="blast";
        const std::vector<ShooterProjectile> inkShots{inkShot,otherShot,otherKind};
        std::vector<ProjectileBlob> filtered;BuildProjectileBlobs(inkShots,filtered,false);
        if(filtered.size()!=1)throw std::runtime_error("Native shot/stream must bypass the source blob builder");
        std::vector<ProjectileBlob> inkA,inkB;AppendShooterInkBlobs(inkShots,inkA);AppendShooterInkBlobs(inkShots,inkB);
        if(inkA.size()!=2||inkA.size()!=inkB.size()||inkA.size()>4)throw std::runtime_error("Shooter ink blob count or filter mismatch");
        for(std::size_t blobIndex=0;blobIndex<inkA.size();blobIndex++) {
            const auto& blob=inkA[blobIndex];const auto& repeat=inkB[blobIndex];
            const float* fields[]={glm::value_ptr(blob.matrix),glm::value_ptr(blob.shape),glm::value_ptr(blob.color)};
            const float* repeated[]={glm::value_ptr(repeat.matrix),glm::value_ptr(repeat.shape),glm::value_ptr(repeat.color)};
            const int lengths[]={16,4,3};
            for(int group=0;group<3;group++)for(int i=0;i<lengths[group];i++)
                if(!std::isfinite(fields[group][i])||fields[group][i]!=repeated[group][i])
                    throw std::runtime_error("Shooter ink blob is non-finite or nondeterministic");
            const float sx=glm::length(glm::vec3(blob.matrix[0])),sy=glm::length(glm::vec3(blob.matrix[1])),sz=glm::length(glm::vec3(blob.matrix[2]));
            if(!(sx>0&&sy>0&&sz>=sx&&sz<0.7f))throw std::runtime_error("Shooter ink blob scale is outside its bounds");
        }
        const glm::vec3 primaryCenter(inkA[0].matrix[3]),dropCenter(inkA[1].matrix[3]);
        if(glm::length(primaryCenter-dropCenter)<=glm::length(glm::vec3(inkA[0].matrix[2]))+glm::length(glm::vec3(inkA[1].matrix[2])))
            throw std::runtime_error("Shooter secondary drop is not detached from the primary");
        if(glm::length(inkShots[0].pos-inkShot.pos)>0||glm::length(inkShots[0].vel-inkShot.vel)>0||
           glm::length(inkShots[0].start-inkShot.start)>0||inkShots[0].age!=inkShot.age||inkShots[0].life!=inkShot.life||inkShots[0].seed!=inkShot.seed)
            throw std::runtime_error("Shooter ink presentation mutated gameplay projectile state");
        auto early=inkShot,later=inkShot;early.age=.02;later.age=.12;
        std::vector<ProjectileBlob> birth,flight;AppendShooterInkBlobs({early},birth);AppendShooterInkBlobs({later},flight);
        if(birth.empty()||flight.empty()||birth[0].shape.y<=flight[0].shape.y||flight[0].shape.y<=0||
           glm::length(glm::vec3(birth[0].matrix[0]))<.08f||
           glm::length(glm::vec3(flight[0].matrix[0]))<=glm::length(glm::vec3(birth[0].matrix[0])))
            throw std::runtime_error("Launch impulse must decay while the main ink volume grows promptly");
        auto next=inkShot;next.age+=.001;std::vector<ProjectileBlob> nextFrame;AppendShooterInkBlobs({next},nextFrame);
        if(nextFrame.size()!=inkA.size()||glm::length(glm::vec3(nextFrame[1].matrix[3])-dropCenter)>.01f)
            throw std::runtime_error("Seeded secondary motion must remain smooth across adjacent frames");
        std::vector<ProjectileBlob> bounded(699);AppendShooterInkBlobs({inkShot},bounded);AppendShooterInkBlobs({inkShot},bounded);
        if(bounded.size()!=700)throw std::runtime_error("Shooter ink exceeded the shared instance bound");
        inkShot.pos.x=std::numeric_limits<double>::quiet_NaN();std::vector<ProjectileBlob> invalid;
        AppendShooterInkBlobs({inkShot},invalid);
        if(!invalid.empty())throw std::runtime_error("Invalid shooter position escaped the finite transform guard");
        a.state.alive=true;
        // The same muzzle is used by presentation and physics, with source-style safety fallback.
        a.setMuzzleSource([](int){return glm::dvec3(0.2,1.1,.5);});
        if(glm::length(a.muzzle()-glm::dvec3(.2,1.1,.5))>1e-9)throw std::runtime_error("Rig muzzle was ignored");
        a.setMuzzleSource([](int){return glm::dvec3(900);});
        if(glm::length(a.muzzle()-a.state.pos)>2)throw std::runtime_error("Invalid rig muzzle escaped source fallback");
        ProjectileFrame frame;
        CombatWorld snapshotWorld(stage,geometry,paint);
        ShooterProjectile ownedShot;ownedShot.kind="shot";ownedShot.weaponId="spinner";ownedShot.pos={12,12,12};ownedShot.start={11,12,12};
        ownedShot.vel={0,0,30};ownedShot.age=.2;ownedShot.life=1.3;ownedShot.seed=.7;ownedShot.look={.12,.8,1.1,.03,24,.2,2};
        ownedShot.straight=.23;ownedShot.gravity=35;ownedShot.drag=.7;
        {
            PlayerSimulation spinner(stage,geometry,paint,1,&snapshotWorld);spinner.setLoadout("spinner");spinner.state.alive=true;spinner.state.pos={4,4,6};
            spinner.setNetworkIdentity(41,9);spinner.setMuzzleSource([](int){return glm::dvec3(4,5,6);});
            CombatPose pose;pose.streaming=true;spinner.setNetworkGhostPose(pose,{});ownedShot.owner=&spinner;
            snapshotWorld.launch(ownedShot);
            PlayerSimulation charger(stage,geometry,paint,0,&snapshotWorld);charger.setLoadout("charger");charger.state.alive=true;
            charger.input.hasAimPoint=true;charger.input.aimPoint={0,1,10};charger.setMuzzleSource([](int){return glm::dvec3(0,1,0);});
            CombatPose charge;charge.charging=true;charge.charge=.5;charger.setNetworkGhostPose(charge,{});
            const auto posBefore=snapshotWorld.projectiles().front().pos;const auto ageBefore=snapshotWorld.projectiles().front().age;const auto timeBefore=snapshotWorld.time();
            frame=BuildProjectileFrame(snapshotWorld);
            if(frame.time!=timeBefore||frame.projectiles.size()!=1||frame.projectiles[0].team!=1||
               frame.projectiles[0].weaponId!="spinner"||frame.projectiles[0].look.satellites!=2||
               glm::length(frame.projectiles[0].pos-posBefore)>1e-9||frame.streamEmitters.size()!=1||frame.streamEmitters[0].id!=41||
               frame.streamEmitters[0].team!=1||glm::length(frame.streamEmitters[0].position-glm::dvec3(4,5,6))>1e-9||
               glm::length(frame.streamEmitters[0].velocity-ownedShot.vel)>1e-9||frame.streamEmitters[0].straight!=ownedShot.straight||
               frame.streamEmitters[0].gravity!=ownedShot.gravity||frame.streamEmitters[0].drag!=ownedShot.drag||
               frame.streamEmitters[0].radius!=.10||frame.chargerSights.size()!=1||
               glm::length(frame.chargerSights[0].direction-glm::dvec3(0,0,1))>1e-9||frame.chargerSights[0].team!=0)
                throw std::runtime_error("Projectile frame lost team, spinner or charger presentation values");
            if(snapshotWorld.projectiles().size()!=1||glm::length(snapshotWorld.projectiles().front().pos-posBefore)>1e-9||
               snapshotWorld.projectiles().front().age!=ageBefore||snapshotWorld.time()!=timeBefore||!spinner.state.alive||!charger.state.alive)
                throw std::runtime_error("Projectile frame construction changed gameplay state");
            snapshotWorld.tick(.01);spinner.setMuzzleSource([](int){return glm::dvec3(7,8,9);});
            if(glm::length(snapshotWorld.projectiles().front().pos-posBefore)<1e-9||snapshotWorld.projectiles().front().age==ageBefore||
               frame.projectiles[0].age!=ageBefore||glm::length(frame.projectiles[0].pos-posBefore)>1e-9||
               glm::length(frame.streamEmitters[0].position-glm::dvec3(4,5,6))>1e-9)
                throw std::runtime_error("Projectile frame changed with the source projectile or actor");
            snapshotWorld.cleanup(spinner);
        }
        if(!snapshotWorld.projectiles().empty()||frame.projectiles.size()!=1||frame.projectiles[0].team!=1||
           glm::length(frame.projectiles[0].pos-glm::dvec3(12,12,12))>1e-9||frame.streamEmitters.size()!=1||
           glm::length(frame.streamEmitters[0].position-glm::dvec3(4,5,6))>1e-9||frame.chargerSights.size()!=1)
            throw std::runtime_error("Projectile frame retained an actor pointer or changed after cleanup");
        std::cout<<cases<<" source Projectiles._draw fixtures and reusable output passed; shooter ink deterministic/filter/bounds, owned projectile snapshot, and rig muzzle guards passed.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
