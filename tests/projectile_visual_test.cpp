#include "ProjectileVisuals.hpp"
#include "CombatWorld.hpp"
#include "PlayerSimulation.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

int main(int argc,char** argv) {
    try {
        using namespace restoration;
        StageData stage{};stage.bounds={-30,30,-30,30};stage.spawnPads={glm::dvec3(0),glm::dvec3(20,0,20)};
        StageGeometry geometry(stage);FacePaint paint(stage,geometry);
        PlayerSimulation a(stage,geometry,paint,0),b(stage,geometry,paint,1);
        std::ifstream input(argc>1?argv[1]:"assets/reference/projectiles/blob-fixtures.txt");
        int cases=0;if(!(input>>cases)||cases!=24)throw std::runtime_error("Missing source projectile draw fixtures");
        for(int sample=0;sample<cases;sample++) {
            ShooterProjectile p;int team=0,count=0;
            input>>p.kind>>p.pos.x>>p.pos.y>>p.pos.z>>p.vel.x>>p.vel.y>>p.vel.z>>p.start.x>>p.start.y>>p.start.z
                >>p.age>>p.life>>p.seed>>p.size>>p.delay>>p.look.size>>p.look.tail0>>p.look.tailK>>p.look.wobble>>p.look.frequency>>p.look.nose>>p.look.satellites>>team>>count;
            p.owner=team?&b:&a;const auto blobs=BuildProjectileBlobs({p});
            if(blobs.size()!=static_cast<std::size_t>(count))throw std::runtime_error("Source projectile satellite/delay count mismatch case "+std::to_string(sample));
            for(const auto& blob:blobs) {
                const float* fields[]={glm::value_ptr(blob.matrix),glm::value_ptr(blob.shape),glm::value_ptr(blob.color)};
                const int lengths[]={16,4,3};
                for(int group=0;group<3;group++)for(int i=0;i<lengths[group];i++) {
                    double expected;if(!(input>>expected)||std::abs(fields[group][i]-expected)>1e-5)
                        throw std::runtime_error("Source projectile Float32 instance mismatch case "+std::to_string(sample));
                }
            }
        }
        a.state.alive=true;
        // The same muzzle is used by presentation and physics, with source-style safety fallback.
        a.setMuzzleSource([](int){return glm::dvec3(0.2,1.1,.5);});
        if(glm::length(a.muzzle()-glm::dvec3(.2,1.1,.5))>1e-9)throw std::runtime_error("Rig muzzle was ignored");
        a.setMuzzleSource([](int){return glm::dvec3(900);});
        if(glm::length(a.muzzle()-a.state.pos)>2)throw std::runtime_error("Invalid rig muzzle escaped source fallback");
        std::cout<<cases<<" source Projectiles._draw fixtures passed: pop, teardrop, blast swell, satellites and delayed rounds; rig muzzle guards passed.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
