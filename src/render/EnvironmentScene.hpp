#pragma once
#include "CharacterShader.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
namespace restoration {
class EnvironmentScene {
public:
    EnvironmentScene();
    ~EnvironmentScene();
    void Load(const std::filesystem::path& assets,const std::string& stage);
    int Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,
        const CharacterLightState& light,double time,const std::map<std::string,std::vector<float>>& theme,
        const std::function<double(double,double,double)>& waterHeight,int selection=0) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
