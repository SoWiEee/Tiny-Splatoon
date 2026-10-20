#pragma once
#include "CharacterPose.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace restoration {
struct CharacterAttribute {
    std::string name, sourceType;
    std::uint32_t components=0;
    bool sourceNormalized=false;
    std::vector<float> values;
};
struct CharacterMesh {
    std::uint32_t vertices=0;
    std::vector<CharacterAttribute> attributes;
    std::vector<std::uint32_t> indices;
};
struct CharacterNode {
    int parent=-1, bone=-1, renderOrder=0;
    std::string binding,name,mesh,material;
    CharacterTransform local;
    bool skinned=false;
    glm::dmat4 bind{1.0};
};
struct CharacterModel {
    std::string id;
    std::vector<CharacterNode> nodes;
};

// Source geometry is shared by content hash; only selected meshes are loaded.
class CharacterData {
public:
    explicit CharacterData(std::filesystem::path directory="assets/reference/characters");
    const CharacterRig& rig(int hair,int hat,int brows) const;
    const CharacterModel& model(const std::string& id) const;
    const CharacterMesh& mesh(const std::string& file) const;
    std::size_t rigCount() const { return rigs_.size(); }
    const std::map<std::string,CharacterModel>& models() const { return models_; }
private:
    std::filesystem::path directory_;
    std::map<int,CharacterRig> rigs_;
    std::map<std::string,CharacterModel> models_;
    mutable std::map<std::string,CharacterMesh> meshes_;
};
}
