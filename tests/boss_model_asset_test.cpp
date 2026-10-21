#include "BossModelData.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

int main(int argc,char** argv){
    using namespace restoration;
    if(argc!=2) return 2;
    try{
        const auto models=LoadBossModels(argv[1]);
        const auto poseRig=LoadBossPoseRig(std::filesystem::path(argv[1]).parent_path()/"pose-rig.bin");
        const auto& hull=models.at(0);const auto& crab=models.at(1);
        if(hull.bones.size()!=100||hull.meshes.size()!=5||hull.sockets.size()!=10||hull.poseSockets.size()!=20)throw std::runtime_error("Source HULLBREAKER rig/model/socket set is incomplete");
        std::size_t triangles=0;glm::vec3 lo(1e9f),hi(-1e9f);bool hasUvSpan=false;
        for(const auto& mesh:hull.meshes){
            triangles+=mesh.indices.size()/3;
            glm::vec2 uvLo(1e9f),uvHi(-1e9f);
            for(const auto& vertex:mesh.vertices){lo=glm::min(lo,vertex.position);hi=glm::max(hi,vertex.position);uvLo=glm::min(uvLo,vertex.uv);uvHi=glm::max(uvHi,vertex.uv);}
            hasUvSpan|=uvHi.x-uvLo.x>.5f&&uvHi.y-uvLo.y>.5f;
        }
        if(triangles!=59010||hi.x-lo.x<7.0f||hi.y-lo.y<4.0f||hi.z-lo.z<10.0f||!hasUvSpan)throw std::runtime_error("Boss asset lost its source-scale geometry or material UVs");
        if(crab.name!="crablet"||crab.bones.size()<10||crab.meshes.size()!=1||crab.meshes.front().indices.size()/3!=1840)throw std::runtime_error("Source crablet model is incomplete");
        const std::array<const char*,10> names={"shellF","shellR","body","clawL","clawR","eyeL","eyeR","belly","crackL","crackR"};
        for(std::size_t i=0;i<names.size();++i)if(hull.sockets[i].name!=names[i])throw std::runtime_error("Boss hit socket order differs from source");
        const std::array<const char*,20> poseNames={"clawL","clawR","cannon","mouth","eyeL","eyeR","belly","shellTop","hatch","body","shellMid","shellRear","crackL","crackR","footL0","footL1","footL2","footR0","footR1","footR2"};
        for(std::size_t i=0;i<poseNames.size();++i)if(hull.poseSockets[i].name!=poseNames[i])throw std::runtime_error("Boss source pose socket order differs from buildParts");
        const auto& bodyPoseSocket=hull.poseSockets[9];
        if(hull.bones[bodyPoseSocket.bone].name!="head"||
            glm::length(bodyPoseSocket.offset-glm::vec3(0,-.05f,.75f))>1e-5f)
            throw std::runtime_error("Boss body pose socket must preserve BossModel's head-bone getSocket override");
        if(poseRig.legs.size()!=6||poseRig.arms.size()!=2||poseRig.chains.size()!=14||
            std::abs(poseRig.bodyPivot.x)>1e-6||std::abs(poseRig.bodyPivot.y-2.1)>1e-6||
            std::abs(poseRig.bodyPivot.z-.7)>1e-6||std::abs(poseRig.containerPosition.y-3.0)>1e-6||
            std::abs(poseRig.containerPosition.z+1.3)>1e-6||std::abs(poseRig.legs.front().hip.x-.92)>1e-6||
            std::abs(poseRig.legs.front().hip.y-1.75)>1e-6||std::abs(poseRig.legs.front().hip.z-1.7)>1e-6)
            throw std::runtime_error("Boss pose-rig stream order or source rig values changed");
        std::cout<<"HULLBREAKER "<<triangles<<" triangles / "<<hull.bones.size()<<" bones / "<<hull.sockets.size()<<" hit sockets / "<<hull.poseSockets.size()<<" pose sockets; crablet "<<crab.meshes.front().indices.size()/3<<" triangles\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
