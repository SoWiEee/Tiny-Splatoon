#include "CharacterData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace restoration {
namespace {
class Reader {
    std::vector<unsigned char> bytes_;
    std::size_t at_=0;
public:
    explicit Reader(const std::filesystem::path& path) {
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in) throw std::runtime_error("Cannot read character asset: "+path.string());
        const auto size=in.tellg();
        if(size<0 || size>256*1024*1024) throw std::runtime_error("Invalid character asset size");
        bytes_.resize(static_cast<std::size_t>(size));in.seekg(0);
        if(!in.read(reinterpret_cast<char*>(bytes_.data()),static_cast<std::streamsize>(bytes_.size()))) throw std::runtime_error("Truncated character asset");
    }
    void require(std::size_t count) const {
        if(count>bytes_.size()-at_) throw std::runtime_error("Truncated character asset record");
    }
    std::uint32_t u32() {
        require(4);std::uint32_t value=0;
        for(int i=0;i<4;++i)value|=std::uint32_t(bytes_[at_++])<<(i*8);
        return value;
    }
    int i32(){const auto bits=u32();std::int32_t value;std::memcpy(&value,&bits,4);return value;}
    double number(){
        require(8);std::uint64_t bits=0;
        for(int i=0;i<8;++i)bits|=std::uint64_t(bytes_[at_++])<<(i*8);
        double value;std::memcpy(&value,&bits,8);
        if(!std::isfinite(value))throw std::runtime_error("Non-finite character asset");
        return value;
    }
    float scalar(){const auto bits=u32();float value;std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Non-finite character vertex");return value;}
    std::uint32_t count(std::uint32_t limit){const auto value=u32();if(value>limit)throw std::runtime_error("Character asset count exceeds limit");return value;}
    bool flag(){const auto value=u32();if(value>1)throw std::runtime_error("Invalid character asset flag");return value!=0;}
    std::string string(){const auto length=count(4096);require(length);std::string value(reinterpret_cast<const char*>(bytes_.data()+at_),length);at_+=length;return value;}
    void magic(const char* expected){require(8);if(std::memcmp(bytes_.data()+at_,expected,8))throw std::runtime_error("Invalid character asset version");at_+=8;}
    glm::dvec3 vector(){const double x=number(),y=number(),z=number();return {x,y,z};}
    glm::dmat4 matrix(){glm::dmat4 out;for(int c=0;c<4;++c)for(int r=0;r<4;++r)out[c][r]=number();return out;}
    CharacterTransform transform(){
        CharacterTransform out;out.position=vector();
        const double x=number(),y=number(),z=number(),w=number();out.rotation={w,x,y,z};
        if(std::abs(glm::length(out.rotation)-1.0)>1e-5)throw std::runtime_error("Invalid character quaternion");
        out.scale=vector();out.visible=flag();out.matrixOverride=flag();if(out.matrixOverride)out.authoredMatrix=matrix();return out;
    }
    void end()const{if(at_!=bytes_.size())throw std::runtime_error("Unexpected character asset trailing data");}
};
bool meshName(const std::string& name){
    return name.size()==69 && name.substr(64)==".mesh" &&
        std::all_of(name.begin(),name.begin()+64,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
}

CharacterData::CharacterData(std::filesystem::path directory):directory_(std::move(directory)) {
    Reader in(directory_/"catalog.bin");in.magic("TSCHAR02");
    const auto count=in.count(128);
    if(count!=128)throw std::runtime_error("Incomplete source character style rigs");
    for(std::uint32_t i=0;i<count;++i){
        const int hair=in.count(7),hat=in.count(3),brows=in.count(3),key=(hair*4+hat)*4+brows;
        if(in.count(87)!=87)throw std::runtime_error("Character skeleton does not have 87 source bones");
        CharacterRig rig;
        for(int b=0;b<87;++b){
            auto& bone=rig.bones[b];bone.name=in.string();bone.parent=in.i32();
            if(bone.parent>=b || bone.parent<-1)throw std::runtime_error("Invalid character bone parent");
            bone.rest=in.vector();bone.inverseBind=in.matrix();
        }
        const auto strands=in.count(8);
        for(std::uint32_t s=0;s<strands;++s){CharacterHairStrand strand;strand.direction=in.vector();strand.length=in.number();strand.stiffness=in.number();strand.gravity=in.number();rig.hair.push_back(strand);}
        if(!rigs_.emplace(key,std::move(rig)).second)throw std::runtime_error("Duplicate character style rig");
    }
    const auto models=in.count(4096);
    for(std::uint32_t m=0;m<models;++m){
        CharacterModel model;model.id=in.string();const auto nodes=in.count(4096);
        for(std::uint32_t n=0;n<nodes;++n){
            CharacterNode node;node.parent=in.i32();node.bone=in.i32();
            if(node.parent>=static_cast<int>(n)||node.parent<-1||node.bone<-1||node.bone>=87)throw std::runtime_error("Invalid character model parent");
            node.binding=in.string();node.name=in.string();node.mesh=in.string();node.material=in.string();
            node.renderOrder=in.i32();
            if(!node.mesh.empty()&&!meshName(node.mesh))throw std::runtime_error("Invalid character mesh filename");
            node.local=in.transform();node.skinned=in.flag();node.bind=in.matrix();model.nodes.push_back(std::move(node));
        }
        const auto id=model.id;
        if(!models_.emplace(id,std::move(model)).second)throw std::runtime_error("Duplicate character model");
    }
    in.end();
}
const CharacterRig& CharacterData::rig(int hair,int hat,int brows)const{
    if(hair<0||hair>=8||hat<0||hat>=4||brows<0||brows>=4)throw std::invalid_argument("Unknown character style");
    return rigs_.at((hair*4+hat)*4+brows);
}
const CharacterModel& CharacterData::model(const std::string& id)const{
    const auto it=models_.find(id);if(it==models_.end())throw std::invalid_argument("Unknown character model: "+id);return it->second;
}
const CharacterMesh& CharacterData::mesh(const std::string& file)const{
    if(!meshName(file))throw std::invalid_argument("Unknown character mesh filename");
    const auto found=meshes_.find(file);if(found!=meshes_.end())return found->second;
    Reader in(directory_/file);in.magic("TSMESH01");CharacterMesh mesh;
    mesh.vertices=in.count(10000000);const auto indices=in.count(30000000),attributes=in.count(32);
    if(!mesh.vertices || !attributes || indices%3)throw std::runtime_error("Invalid character mesh topology");
    for(std::uint32_t a=0;a<attributes;++a){
        CharacterAttribute attribute;attribute.name=in.string();attribute.components=in.count(4);attribute.sourceType=in.string();attribute.sourceNormalized=in.flag();
        if(!attribute.components)throw std::runtime_error("Invalid character vertex attribute");
        const auto values=std::size_t(mesh.vertices)*attribute.components;in.require(values*4);attribute.values.reserve(values);
        for(std::size_t v=0;v<values;++v)attribute.values.push_back(in.scalar());
        for(const auto& previous:mesh.attributes)if(previous.name==attribute.name)throw std::runtime_error("Duplicate character attribute");
        mesh.attributes.push_back(std::move(attribute));
    }
    in.require(std::size_t(indices)*4);mesh.indices.reserve(indices);
    for(std::uint32_t i=0;i<indices;++i){const auto index=in.u32();if(index>=mesh.vertices)throw std::runtime_error("Character vertex index out of range");mesh.indices.push_back(index);}
    in.end();
    const auto position=std::find_if(mesh.attributes.begin(),mesh.attributes.end(),[](const auto& a){return a.name=="position"&&a.components==3;});
    if(position==mesh.attributes.end())throw std::runtime_error("Character mesh has no position attribute");
    return meshes_.emplace(file,std::move(mesh)).first->second;
}
}
