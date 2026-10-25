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
        std::ifstream input(argc>1?argv[1]:"assets/reference/projectiles/blob-fixtures.txt");
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
        std::cout<<cases<<" source Projectiles._draw fixtures and reusable output passed; shooter ink deterministic/filter/bounds and rig muzzle guards passed.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
