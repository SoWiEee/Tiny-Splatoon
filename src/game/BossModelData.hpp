#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace restoration {
struct BossModelVertex {
    glm::vec3 position{0}, normal{0};
    std::array<std::uint8_t,4> joints{};
    glm::vec4 weights{0}, material{0};
};
struct BossModelMesh {
    std::string name;
    std::vector<BossModelVertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct BossModelBone {
    std::string name;
    int parent=-1;
    glm::vec3 position{0};
};
struct BossModelSocket {
    std::string name;
    unsigned bone=0;
    glm::vec3 offset{0};
    float radius=0;
    bool weak=false,active=false;
};
struct BossModelAsset {
    std::string name;
    std::vector<BossModelBone> bones;
    std::vector<BossModelMesh> meshes;
    std::vector<BossModelSocket> sockets;
};
struct BossPoseLegData {
    glm::dvec3 hip{0},cox{0},knee{0},ankle{0},foot{0},out{0};
    double ld=0,l1=0,l2=0,side=0;
};
struct BossPoseArmData {
    glm::dvec3 shoulder{0},elbow{0},wrist{0},direction{0},up{0},pole{0},hinge{0};
    double upper=0,fore=0;
    bool crusher=false;
};
struct BossPoseChainData {
    std::string name,parent;
    std::vector<glm::dvec3> points;
    std::vector<std::string> bones;
    glm::dvec4 plane{0};
    double damp=0.98,stiff=0,gravity=1;
    bool hasPlane=false,flag=false,kelp=false;
};
struct BossPoseRigData {
    glm::dvec3 bodyPivot{0},containerPosition{0},nozzle{0},nozzleTip{0};
    glm::dmat4 containerMatrix{1};
    double containerPitch=0,halfLength=0,halfWidth=0,halfHeight=0;
    std::vector<BossPoseLegData> legs;
    std::vector<BossPoseArmData> arms;
    std::vector<BossPoseChainData> chains;
};
std::vector<BossModelAsset> LoadBossModels(const std::filesystem::path& path);
BossPoseRigData LoadBossPoseRig(const std::filesystem::path& path);
} // namespace restoration
