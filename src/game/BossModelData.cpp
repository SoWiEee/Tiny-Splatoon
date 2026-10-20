#include "BossModelData.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace restoration {
namespace {
class Reader {
public:
    explicit Reader(const std::filesystem::path& path):file_(path,std::ios::binary) {
        if(!file_)throw std::runtime_error("Cannot open Boss model asset: "+path.string());
    }
    void bytes(void* data,std::streamsize size){if(!file_.read(static_cast<char*>(data),size))throw std::runtime_error("Truncated Boss model asset");}
    std::uint8_t u8(){char value;read(&value,1);return static_cast<std::uint8_t>(value);}
    std::uint16_t u16(){const auto a=u8(),b=u8();return static_cast<std::uint16_t>(a|(std::uint16_t(b)<<8));}
    std::int16_t i16(){return static_cast<std::int16_t>(u16());}
    std::uint32_t u32(){const auto a=u16(),b=u16();return std::uint32_t(a)|(std::uint32_t(b)<<16);}
    float f32(){const std::uint32_t bits=u32();float value;static_assert(sizeof(value)==sizeof(bits));std::memcpy(&value,&bits,sizeof(value));if(!std::isfinite(value))throw std::runtime_error("Non-finite Boss model value");return value;}
    std::string string(){const auto length=u8();if(!length||length>63)throw std::runtime_error("Invalid Boss model name length");std::string value(length,'\0');read(value.data(),length);return value;}
    bool done(){return file_.peek()==std::char_traits<char>::eof();}
private:
    void read(void* data,std::streamsize size){bytes(data,size);}
    std::ifstream file_;
};
constexpr std::uint32_t MaxBones=128,MaxMeshes=8,MaxVertices=200000,MaxIndices=1000000,MaxSockets=64;
}

std::vector<BossModelAsset> LoadBossModels(const std::filesystem::path& path){
    Reader r(path);
    char magic[4]{};r.bytes(magic,4);
    if(std::string(magic,4)!="IBOS"||r.u32()!=1)throw std::runtime_error("Invalid Boss model header");
    const auto modelCount=r.u32();if(modelCount!=2)throw std::runtime_error("Boss model asset must contain hullbreaker and crablet");
    std::vector<BossModelAsset> models;models.reserve(modelCount);
    for(std::uint32_t m=0;m<modelCount;++m){
        BossModelAsset model;model.name=r.string();
        const auto boneCount=r.u32();if(!boneCount||boneCount>MaxBones)throw std::runtime_error("Invalid Boss model bone count");
        model.bones.reserve(boneCount);
        for(std::uint32_t i=0;i<boneCount;++i){
            BossModelBone bone;bone.name=r.string();bone.parent=r.i16();bone.position={r.f32(),r.f32(),r.f32()};
            if((i==0&&bone.parent!=-1)||(i>0&&(bone.parent<0||bone.parent>=static_cast<int>(i))))throw std::runtime_error("Invalid Boss model bone parent");
            model.bones.push_back(std::move(bone));
        }
        const auto meshCount=r.u32();if(!meshCount||meshCount>MaxMeshes)throw std::runtime_error("Invalid Boss model mesh count");
        model.meshes.reserve(meshCount);
        for(std::uint32_t i=0;i<meshCount;++i){
            BossModelMesh mesh;mesh.name=r.string();
            const auto vertices=r.u32(),indices=r.u32();
            if(!vertices||vertices>MaxVertices||!indices||indices>MaxIndices||indices%3)throw std::runtime_error("Invalid Boss model mesh size");
            mesh.vertices.resize(vertices);
            for(auto& v:mesh.vertices){
                v.position={r.f32(),r.f32(),r.f32()};v.normal={r.f32(),r.f32(),r.f32()};
                for(auto& joint:v.joints){joint=r.u8();if(joint>=boneCount)throw std::runtime_error("Boss model references missing bone");}
                v.weights={r.f32(),r.f32(),r.f32(),r.f32()};v.material={r.f32(),r.f32(),r.f32(),r.f32()};
                const float weight=v.weights.x+v.weights.y+v.weights.z+v.weights.w;
                // The locked source emits zero normals for a small set of
                // steel/junk vertices; preserve those values instead of
                // rejecting valid exported geometry.
                if(v.weights.x<0||v.weights.y<0||v.weights.z<0||v.weights.w<0||std::abs(weight-1)>0.002f)
                    throw std::runtime_error("Invalid Boss model vertex attributes");
            }
            mesh.indices.resize(indices);for(auto& index:mesh.indices){index=r.u32();if(index>=vertices)throw std::runtime_error("Boss model index out of range");}
            model.meshes.push_back(std::move(mesh));
        }
        const auto socketCount=r.u32();if(socketCount>MaxSockets)throw std::runtime_error("Invalid Boss model socket count");
        model.sockets.reserve(socketCount);
        for(std::uint32_t i=0;i<socketCount;++i){
            BossModelSocket socket;socket.name=r.string();socket.bone=r.u16();socket.offset={r.f32(),r.f32(),r.f32()};socket.radius=r.f32();socket.weak=r.u8()!=0;socket.active=r.u8()!=0;
            if(socket.bone>=boneCount||socket.radius<=0||socket.radius>10)throw std::runtime_error("Invalid Boss model socket");
            model.sockets.push_back(std::move(socket));
        }
        models.push_back(std::move(model));
    }
    if(!r.done())throw std::runtime_error("Trailing bytes in Boss model asset");
    if(models[0].name!="hullbreaker"||models[0].bones.size()!=100||models[0].meshes.size()!=5||models[0].sockets.size()!=10||
       models[1].name!="crablet"||models[1].meshes.size()!=1||models[1].sockets.size()!=0)
        throw std::runtime_error("Incomplete source Boss model asset");
    return models;
}

BossPoseRigData LoadBossPoseRig(const std::filesystem::path& path){
    Reader r(path);
    char magic[4]{};r.bytes(magic,4);
    if(std::string(magic,4)!="IBPR"||r.u32()!=1)throw std::runtime_error("Invalid Boss pose-rig header");
    const auto v3=[&]{
        const double x=r.f32();
        const double y=r.f32();
        const double z=r.f32();
        return glm::dvec3(x,y,z);
    };
    BossPoseRigData data;data.bodyPivot=v3();data.containerPosition=v3();
    data.containerPitch=r.f32();data.halfLength=r.f32();data.halfWidth=r.f32();data.halfHeight=r.f32();
    for(int c=0;c<4;++c)for(int row=0;row<4;++row)data.containerMatrix[c][row]=r.f32();
    data.nozzle=v3();data.nozzleTip=v3();
    const auto legCount=r.u32();if(legCount!=6)throw std::runtime_error("Boss pose rig requires six source legs");
    data.legs.reserve(legCount);
    for(std::uint32_t i=0;i<legCount;++i){BossPoseLegData leg;leg.hip=v3();leg.cox=v3();leg.knee=v3();leg.ankle=v3();leg.foot=v3();leg.out=v3();leg.ld=r.f32();leg.l1=r.f32();leg.l2=r.f32();leg.side=r.f32();data.legs.push_back(leg);}
    const auto armCount=r.u32();if(armCount!=2)throw std::runtime_error("Boss pose rig requires two source claws");
    data.arms.reserve(armCount);
    for(std::uint32_t i=0;i<armCount;++i){BossPoseArmData arm;arm.shoulder=v3();arm.elbow=v3();arm.wrist=v3();arm.direction=v3();arm.up=v3();arm.pole=v3();arm.hinge=v3();arm.upper=r.f32();arm.fore=r.f32();arm.crusher=r.u8()!=0;data.arms.push_back(arm);}
    const auto chainCount=r.u32();if(chainCount>32)throw std::runtime_error("Invalid Boss pose chain count");
    data.chains.reserve(chainCount);
    for(std::uint32_t i=0;i<chainCount;++i){
        BossPoseChainData chain;chain.name=r.string();chain.parent=r.string();
        const auto points=r.u16();if(points<2||points>16)throw std::runtime_error("Invalid Boss pose chain points");
        chain.points.reserve(points);for(std::uint16_t j=0;j<points;++j)chain.points.push_back(v3());
        const auto bones=r.u16();if(bones+1!=points)throw std::runtime_error("Invalid Boss pose chain bones");
        chain.bones.reserve(bones);for(std::uint16_t j=0;j<bones;++j)chain.bones.push_back(r.string());
        chain.hasPlane=r.u8()!=0;chain.plane={r.f32(),r.f32(),r.f32(),r.f32()};chain.damp=r.f32();chain.stiff=r.f32();chain.gravity=r.f32();chain.flag=r.u8()!=0;chain.kelp=r.u8()!=0;
        data.chains.push_back(std::move(chain));
    }
    if(!r.done())throw std::runtime_error("Trailing Boss pose-rig bytes");
    return data;
}
} // namespace restoration
